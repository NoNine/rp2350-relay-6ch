# Hardware watchdogs in MCU-based embedded systems

Date: 2026-06-01

Status: Discussion. This note records research and design reasoning for
hardware watchdog use in MCU-based embedded systems. It does not change the
authoritative PRD, implementation plan, phase scope, protocol, firmware
configuration, release artifacts, or verification status unless those documents
are updated explicitly.

## Summary

Watchdogs are recovery mechanisms for loss of forward progress. They are not
proof that application decisions are correct, and they do not replace explicit
safe-state design. In MCU systems, a good watchdog design usually combines a
hardware reset source with software liveness checks that decide whether the
hardware watchdog may be fed.

The most transferable pattern across RTOSes is central feed arbitration:
individual tasks or components report progress, then one supervisor feeds the
hardware watchdog only when the required health conditions are current. That
avoids the common failure mode where a periodic interrupt, idle hook, or
low-priority task keeps feeding the watchdog even though an important subsystem
is wedged.

For this relay controller, watchdogs should remain separate from the existing
communication-loss relay safety policy. Communication-loss timeout protects
relay outputs when host ownership disappears. A hardware watchdog protects
firmware forward progress. Both may cause a reset or all-off behavior, but they
answer different failure questions and should not be collapsed into one policy.

## Hardware watchdog configurations

### Single internal hardware watchdog

The simplest MCU design uses one on-chip watchdog timer. Firmware configures a
timeout, starts the watchdog, and periodically feeds it before the timeout
expires. If firmware stops feeding it, the MCU resets or enters a vendor-defined
timeout action.

This is common in small bare-metal and RTOS systems because it has low cost and
uses no extra board components. It catches dead loops, scheduler stalls,
interrupt lockups that prevent the feed path from running, and some classes of
memory corruption. It does not catch failures where the feed path remains alive
while the controlled output logic is wrong.

The key design choice is where feeding happens:

- Weak model: feed from the main loop, a timer callback, an idle hook, or an
  ISR with little or no health checking.
- Stronger model: feed from one supervisor after all required subsystems have
  reported recent progress.

### Independent watchdog

Many MCU families provide an independent watchdog clocked from a low-speed
internal oscillator or always-on domain. It can keep running when the main
clock tree is broken or when normal sleep/peripheral clocks are stopped. This
is stronger than a watchdog derived from the same clock domain as the failed
software path.

Independent watchdogs are useful for production recovery, but they are often
harder to stop once started. That is desirable in deployed systems and painful
in bring-up. Debug policy must be explicit: decide whether the watchdog pauses
under debug halt and whether production builds differ from bench builds.

### Window watchdog

A window watchdog detects feeds that are too late and, on supported hardware,
feeds that are too early. The early-feed check catches a class of runaway code
that loops rapidly through the feed path.

Window watchdogs work best when the healthy feed cadence is deterministic.
They are less forgiving in systems with long flash erase operations, blocking
I/O, variable radio stacks, or long critical sections unless those operations
are explicitly budgeted. RIOT's watchdog interface documents both normal and
window modes, with a `[min_time, max_time]` feed window for window operation:
<https://api.riot-os.org/group__drivers__periph__wdt.html>

### Multi-stage watchdog

Some platforms support staged timeout actions. A first-stage interrupt can
capture fault state, switch outputs to a safe state, or request a controlled
panic. A second stage performs a hard reset if the first stage cannot recover
or itself stalls.

ESP-IDF's interrupt watchdog is a concrete example of this style: it can enter
panic handling on timeout, and if panic handling cannot run, a second-stage
timeout hard-resets the chip:
<https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/wdts.html>

The first-stage handler must stay extremely small. It should not depend on
heap allocation, complex logging, blocking I/O, USB, filesystems, or normal
thread scheduling. For relay control, a first-stage action would only be
interesting if it can force outputs off more reliably than the normal reset
path.

### External supervisor watchdog

An external watchdog or supervisor IC is independent of the MCU silicon, clock
tree, and some internal failure modes. It may monitor a GPIO heartbeat, supply
voltage, reset pin behavior, or power rails. Some parts support windowed input,
manual reset, delayed reset release, and reset-reason pins.

External supervisors are useful when the MCU's internal watchdog is not
independent enough, when power quality is a known risk, or when a safety case
needs an independent reset source. They also add hardware cost, PCB routing,
bring-up complexity, reset timing interactions, and serviceability questions.
For this relay controller, an external supervisor would be a hardware revision
topic, not a firmware-only improvement.

### How many watchdogs are used

Common MCU patterns:

- One internal hardware watchdog: typical for small products and many v1
  embedded applications.
- One hardware watchdog plus a software or task watchdog: common in RTOS
  systems where several tasks must prove liveness before the hardware watchdog
  is fed.
- Independent watchdog plus window watchdog: seen on MCU families that expose
  both long-recovery and tighter timing supervision.
- Boot watchdog plus application watchdog: bootloader keeps a short watchdog
  during startup or update, then application owns a longer runtime watchdog.
- Internal watchdog plus external supervisor: used when board-level
  independence or power supervision matters.
- Per-core or per-domain watchdogs: used on multi-core MCUs and SoCs, where
  each CPU or subsystem must prove progress independently.

More watchdogs are not automatically safer. Each extra watchdog needs a clear
fault model, reset target, feed owner, timeout budget, and evidence that it
does not mask another watchdog or create boot loops.

## RTOS and framework patterns

### Bare-metal systems

Bare-metal firmware often feeds the watchdog in the main loop. This can be
acceptable when the main loop is the only scheduler and every important action
passes through it. It becomes weak when interrupt-driven subsystems can fail
while the main loop continues to run.

A stronger bare-metal pattern is a heartbeat bitmap or counter set. Each
critical module updates its own heartbeat after doing real work. The main loop
feeds the watchdog only when all required heartbeats have changed within their
deadlines.

### Zephyr

Zephyr has a generic hardware watchdog driver API:
<https://docs.zephyrproject.org/latest/hardware/peripherals/watchdog.html>

Zephyr also has a task watchdog service for supervising multiple threads. Its
documentation explicitly notes that a single hardware watchdog may be
insufficient in an RTOS with multiple parallel tasks, and that an existing
hardware watchdog can be used as a fallback:
<https://docs.zephyrproject.org/latest/services/task_wdt/index.html>

Zephyr's watchdog API hierarchy is:

```text
Application health/supervisor policy
        |
        v
Task watchdog service, optional
zephyr/task_wdt/task_wdt.h
        |
        v
Hardware watchdog public driver API
zephyr/drivers/watchdog.h
        |
        v
Watchdog driver backend API
wdt_driver_api implemented by SoC/device drivers
        |
        v
Hardware watchdog peripheral or external watchdog
```

| Layer | Zephyr API | Scope | Main job |
| --- | --- | --- | --- |
| Application policy | Product code above Zephyr watchdog APIs | Product or system health | Decide whether watchdog feeding is justified. |
| Task watchdog | `task_wdt_*` | Global software service with logical channels | Supervise multiple tasks or checkpoints through a kernel timer. |
| Hardware watchdog public API | `wdt_*` | One watchdog device instance | Configure, start, feed, and disable hardware watchdog timeouts. |
| Driver backend | `wdt_driver_api` callbacks | SoC or external watchdog driver | Bind public watchdog calls to hardware-specific operations. |
| Hardware | SoC watchdog or external supervisor | Physical reset mechanism | Reset the CPU, SoC, or board when feeding stops. |

The hardware watchdog API is device-oriented. Applications include
`zephyr/drivers/watchdog.h`, install timeout channels with
`wdt_install_timeout()`, start the device with `wdt_setup()`, feed an installed
channel with `wdt_feed()`, and disable the instance with `wdt_disable()` where
the driver permits disabling. Zephyr exposes setup options such as
`WDT_OPT_PAUSE_IN_SLEEP` and `WDT_OPT_PAUSE_HALTED_BY_DBG`, and timeout flags
such as `WDT_FLAG_RESET_NONE`, `WDT_FLAG_RESET_CPU_CORE`, and
`WDT_FLAG_RESET_SOC`.

The task watchdog API is a higher-level software service. Applications include
`zephyr/task_wdt/task_wdt.h`, initialize it with `task_wdt_init()`, add logical
channels with `task_wdt_add()`, feed them with `task_wdt_feed()`, and remove
them with `task_wdt_delete()`. `task_wdt_suspend()` and `task_wdt_resume()`
support low-power transitions. Passing a hardware watchdog device to
`task_wdt_init()` lets the task watchdog use that hardware watchdog as a
fallback if the scheduler or task watchdog service stops making progress.

The driver backend is not normally application code. Watchdog drivers implement
setup, disable, install-timeout, and feed callbacks behind the public `wdt_*`
functions. That separation is useful for portability, but it does not create a
product health policy.

The useful Zephyr pattern is layered supervision:

- hardware watchdog is the final reset mechanism;
- task watchdog channels represent thread or component liveness;
- the number of channels is fixed by configuration;
- hardware fallback covers scheduler or task-watchdog failure.

The application health model still sits above both Zephyr watchdog APIs. For
this relay controller, workers or checkpoints should report progress to a
central supervisor; they should not feed the hardware watchdog directly. The
supervisor can feed the task watchdog or hardware watchdog only when required
domains, such as relay GPIO, RPC readiness, communication-owner policy, and
boot/update state, are fresh enough. Zephyr supplies the feed mechanisms, not
the decision that feeding is safe.

For the RP2350 relay controller, local Zephyr support already exposes
`watchdog0 = &wdt0` in the Waveshare board DTS. The local RP2350 watchdog
driver accepts one feed channel and rejects callbacks. It supports SOC or CPU
core reset selection through Zephyr watchdog flags, with RP2350 using the
non-RP2040 watchdog tick path. The project does not currently enable watchdog
Kconfig in `firmware/prj.conf`.

### FreeRTOS ecosystem

Upstream FreeRTOS is a kernel, not a complete board-support framework. It does
not provide one universal watchdog driver API across MCUs. Watchdog ownership
is usually implemented with vendor HAL code, SDK code, or a product-specific
monitor task.

Typical FreeRTOS designs:

- monitor task receives liveness reports from critical tasks, then feeds the
  MCU watchdog;
- idle hook feeds or reports idle-task progress, mainly to detect CPU
  starvation;
- software timers check deadlines, but avoid doing heavyweight work in timer
  callbacks;
- task notifications, event groups, or counters represent per-task progress.

ESP-IDF is a useful FreeRTOS-based reference because it documents multiple
watchdogs as first-class system services. It has an interrupt watchdog for
blocked ISRs and critical sections, a task watchdog for tasks that run too long
without yielding, and a low-power or RTC watchdog that can cover boot-time
progress:
<https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/wdts.html>

The ESP-IDF lesson is that "watchdog" can mean several different fault
detectors: interrupt latency, task starvation, boot progress, and final reset.
A product should name which detector it is adding instead of simply saying it
"has a watchdog."

### Eclipse ThreadX / Azure RTOS

ThreadX is a compact RTOS kernel with threads, timers, queues, event flags,
semaphores, mutexes, and application timers. Its main repository describes it
as a deeply embedded RTOS and notes that it is commonly integrated through
semiconductor SDKs:
<https://github.com/eclipse-threadx/threadx>

Like FreeRTOS, ThreadX does not define one universal MCU watchdog peripheral
API. Watchdog control is usually provided by the vendor SDK or BSP, while
ThreadX timers and threads provide the software supervision structure.

Typical ThreadX watchdog architecture:

- one high-priority monitor thread evaluates component heartbeats;
- application timers or periodic thread sleeps establish check cadence;
- vendor HAL starts and feeds the actual MCU watchdog;
- failure information is kept in retained RAM, backup registers, or
  platform-specific reset-reason storage before allowing reset.

The ThreadX lesson is portability boundary discipline: keep task liveness and
policy in application code, and keep the hardware feed operation in a small
BSP/HAL adapter. That makes the watchdog policy testable without needing the
hardware watchdog to be active in every unit test.

### NuttX

NuttX models watchdogs through its character driver framework. The documented
design has a generic upper-half driver and platform-specific lower-half
drivers. Watchdog instances appear as device files such as `/dev/watchdog0` and
`/dev/watchdog1` when the chip supports multiple watchdogs:
<https://nuttx.apache.org/docs/latest/components/drivers/character/timers/watchdog.html>

The application-level interface uses `ioctl` commands such as start, stop, get
status, set timeout, capture callback, and keepalive. NuttX also documents an
auto-monitor option with several possible feed sources, including capture
callback, timer callback, worker callback, and idle callback.

The NuttX lesson is interface regularity. Treating watchdogs as named devices
makes multi-watchdog systems explicit and lets applications ask for status and
time-left data where supported. The warning is also clear: automatic feed
sources are convenient, but they must match the failure being detected.

### RT-Thread

RT-Thread exposes watchdog hardware through its I/O device framework. The
application finds the watchdog device, initializes it, controls timeout/feed
operations, and closes it through device APIs:
<https://www.rt-thread.io/document/site/programming-manual/device/watchdog/watchdog/>

The RT-Thread documentation says productized embedded systems generally need a
watchdog for automatic reset under abnormal conditions, and describes common
feeding from idle hook or key functions. For robust products, that basic feed
path should be upgraded into a health-gated feed path rather than a blind idle
feed.

The RT-Thread lesson is that a watchdog can fit naturally into a generic device
model. That makes hardware access uniform, but it does not by itself define
the liveness policy. Application code still needs to decide which components
must be healthy before feeding.

### RIOT OS

RIOT exposes a peripheral watchdog API with normal mode, window mode, optional
early warning callback, auto-start, and optional automatic kick helpers:
<https://api.riot-os.org/group__drivers__periph__wdt.html>

The RIOT documentation is useful because it describes both the strength and
weakness of automatic watchdog threads. It calls automatic kicking
non-invasive but weak, because it may only detect starvation of lower-priority
threads and can produce false triggers under high load.

The RIOT lesson is to be honest about detection coverage. A watchdog feed
thread proves that the feed thread ran; it does not prove that protocol,
storage, relay control, or update confirmation are healthy unless those
conditions gate the feed.

### CMSIS-RTOS2 and vendor SDKs

CMSIS-RTOS2 standardizes RTOS concepts such as threads, timers, mutexes, and
event flags across compatible kernels, but watchdog peripherals are usually
handled by vendor HAL, CMSIS-Driver extensions, or chip-specific headers. This
is common in STM32Cube, NXP MCUXpresso, Renesas FSP, and other vendor SDK
environments.

The practical model is the same as ThreadX and FreeRTOS:

- RTOS primitives implement liveness tracking;
- vendor HAL configures and feeds the watchdog;
- BSP code owns reset reason and debug-halt details;
- application policy decides when feeding is allowed.

## Watchdog programming model

### Configure deliberately

A watchdog configuration should answer:

- Which watchdog instance is used?
- What clock domain drives it?
- Can it be stopped after start?
- Does it pause in sleep or debug halt?
- Does it support window mode?
- Does it support first-stage interrupt or callback?
- What reset scope does it trigger: CPU core, subsystem, whole SoC, or board?
- How is reset reason retained across reboot?

The timeout must be longer than the worst credible healthy latency, including
flash erase/write, bootloader handoff, firmware update operations, USB reset,
critical sections, disabled interrupts, low-power wake time, and host-visible
shutdown paths. A watchdog that is too short becomes a random reset generator.
A watchdog that is too long becomes poor recovery.

### Feed only after health checks

The feed path should be centralized. Components report progress; they do not
feed the hardware watchdog directly. The supervisor feeds only when required
conditions are current.

Typical health inputs:

- scheduler or monitor thread is running;
- relay or actuator GPIO layer initialized and not reporting I/O errors;
- communication service is accepting commands or intentionally offline;
- update/boot state can be read;
- long operations have explicitly extended or suspended their deadline within
  policy;
- each critical worker has advanced its heartbeat counter since the last
  check.

Avoid feed paths that prove only timer interrupt execution. Feeding from a
timer ISR can keep the system alive when application threads are dead. Feeding
from idle hook alone can miss failures where idle still runs but important
work has stopped.

### Capture reset reason

A watchdog reset is much more useful when firmware can report it after reboot.
Useful state may include:

- reset reason: watchdog, software reboot, power-on, brownout, debug reset;
- boot count and consecutive watchdog-reset count;
- last completed boot phase;
- last supervisor failure reason;
- coarse uptime at failure;
- firmware image identity.

Retention depends on the MCU: reset controller registers, backup domain,
retained RAM, watchdog scratch registers, or bootloader-managed state. The
state must be small and robust against torn writes.

### Avoid boot loops

Watchdogs can turn one bug into an infinite reboot loop. Boot-loop mitigation
usually needs one or more of:

- safe outputs before starting complex services;
- staged boot checkpoints;
- delayed watchdog enable until minimum safe initialization is complete;
- bootloader rollback or image confirmation policy;
- retained consecutive-reset counters;
- fallback mode that keeps dangerous outputs off and exposes diagnostics.

For this project, MCUboot health confirmation and relay default-off behavior
are the natural places to integrate boot-loop handling if watchdogs are later
promoted into implementation scope.

### Test the watchdog path

Watchdog testing should include deliberate non-feed scenarios, not only a build
that enables the watchdog:

- main loop or monitor task stops feeding;
- one required worker stops reporting progress while the feed thread still
  runs;
- long flash/update operation stays within the configured budget;
- debug halt behavior is understood;
- reset reason after watchdog reset is visible;
- relay outputs return off after watchdog reset;
- repeated watchdog resets do not confirm a bad firmware image.

Hardware-in-loop checks are usually required because simulator and unit-test
environments do not exercise the real reset source.

## Application to the RP2350 relay controller

The current product scope already prioritizes deterministic local relay
control, communication-loss safety, and future MCUboot confirmation. Watchdog
research should support those goals without expanding v1 scope by accident.

Current repo facts:

- The PRD lists watchdog and health checks for safe upgrade confirmation where
  supported.
- Existing communication-loss policies force all relays off under selected
  host-ownership loss conditions.
- The Waveshare board definition aliases `watchdog0` to `wdt0` and marks the
  RP2350 watchdog device okay.
- The local Zephyr RP2350 watchdog driver supports one watchdog feed channel.
- Firmware config does not currently enable watchdog support.

Recommended future direction, only if explicitly promoted:

- Start with the RP2350 hardware watchdog as the final reset mechanism.
- Add one central firmware watchdog supervisor rather than letting unrelated
  modules feed the watchdog directly.
- Use task-level liveness channels only when the firmware has multiple
  long-running components that need independent supervision.
- Feed only after relay GPIO, RPC service, boot/update state, and required
  workers are healthy.
- Keep communication-loss timeout separate from watchdog liveness.
- Report watchdog reset reason and counters through existing status-style
  surfaces only after the protocol impact is scoped.
- Do not persist relay-on state across watchdog reset.

The likely first implementation would be modest: hardware watchdog enabled in
a product profile, central feed work item or thread, retained reset reason if
RP2350 support is reliable, and tests proving default-off after watchdog reset.
External supervisor hardware, dual-core supervision, persistent fault history,
and broad telemetry should remain separate future discussions.

## Research takeaways

- A watchdog is a recovery mechanism, not a health model.
- One well-owned hardware watchdog plus health-gated feeding is usually better
  than several poorly scoped watchdogs.
- RTOS task watchdogs are useful when they supervise actual task progress and
  feed a hardware fallback.
- Window watchdogs and staged watchdogs add coverage but also add timing
  obligations.
- External supervisors improve independence but are hardware design features,
  not firmware-only changes.
- The feed owner, timeout budget, reset reason, safe-state behavior, and test
  method should be designed together.
- For a relay controller, watchdog reset must preserve the core safety rule:
  all relays default off on boot, reset, firmware crash, and firmware restart.

## References

- Zephyr watchdog peripheral API:
  <https://docs.zephyrproject.org/latest/hardware/peripherals/watchdog.html>
- Zephyr task watchdog:
  <https://docs.zephyrproject.org/latest/services/task_wdt/index.html>
- ESP-IDF watchdogs:
  <https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/wdts.html>
- Eclipse ThreadX:
  <https://github.com/eclipse-threadx/threadx>
- NuttX watchdog timer drivers:
  <https://nuttx.apache.org/docs/latest/components/drivers/character/timers/watchdog.html>
- RT-Thread watchdog device:
  <https://www.rt-thread.io/document/site/programming-manual/device/watchdog/watchdog/>
- RIOT watchdog peripheral API:
  <https://api.riot-os.org/group__drivers__periph__wdt.html>
- RP2350 datasheet:
  <https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf>
