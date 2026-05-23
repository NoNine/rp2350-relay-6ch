# Raspberry Pi Pico 2 DIY Targets

Raspberry Pi Pico 2 and Pico 2 W are supported DIY targets for reusing the
six-channel relay firmware, host library, and CLI with custom relay hardware.
The Waveshare RP2350-Relay-6CH remains the ready-made hardware target.

## Supported Boards

Use Zephyr's upstream Raspberry Pi Pico 2 board targets:

| Board | Zephyr target |
| --- | --- |
| Raspberry Pi Pico 2 | `rpi_pico2/rp2350a/m33` |
| Raspberry Pi Pico 2 W | `rpi_pico2/rp2350a/m33/w` |

The firmware protocol remains fixed at six relay channels. DIY overlays must
define exactly six relay nodes named `ch1` through `ch6`.

## Relay Overlay Contract

Pico DIY builds require an explicit relay overlay. The overlay must add a
top-level `/relays` node compatible with `gpio-leds`:

```dts
/ {
	relays {
		compatible = "gpio-leds";

		ch1 {
			gpios = <&gpio0 2 GPIO_ACTIVE_HIGH>;
		};

		ch2 {
			gpios = <&gpio0 3 GPIO_ACTIVE_HIGH>;
		};

		ch3 {
			gpios = <&gpio0 4 GPIO_ACTIVE_HIGH>;
		};

		ch4 {
			gpios = <&gpio0 5 GPIO_ACTIVE_HIGH>;
		};

		ch5 {
			gpios = <&gpio0 6 GPIO_ACTIVE_HIGH>;
		};

		ch6 {
			gpios = <&gpio0 7 GPIO_ACTIVE_HIGH>;
		};
	};
};
```

The repository includes this GP2 through GP7 mapping as an example at
`firmware/boards/raspberrypi/rpi_pico2/pico2w-relay-dev.overlay`. It is not a
required wiring convention; choose GPIOs that match the relay board or carrier.

Use `GPIO_ACTIVE_LOW` only when the external relay interface is proven
active-low. The host CLI still uses one-based channel numbers: `1` is `CH1`
and `6` is `CH6`.

## Pico 2 W Indicator Development Fixture

Pico 2 W may be used as the Phase 7 development fixture for the planned RGB
LED and buzzer status-indicator feature. This fixture mapping provides
devicetree hardware definitions only. It does not imply runtime indicator
behavior.

The current relay example overlay keeps `CH1` through `CH6` on GP2 through
GP7. RGB LED and buzzer fixture wiring must use separate GPIOs and must avoid
the Pico 2 W Wi-Fi pins GP23, GP24, GP25, and GP29. Keep UART0 GP0 and GP1
available for debug console use.

Do not copy the Waveshare board's GPIO23 buzzer assignment to a Pico 2 W
fixture. Choose a non-Wi-Fi PWM-capable GPIO for the passive buzzer.

The repository's Pico 2 W relay development overlay assigns concrete indicator
fixture pins:

| Function | Pico 2 W GPIO |
| --- | --- |
| RGB LED data | `GP8` via PIO1 |
| Buzzer PWM output | `GP9` via PWM slice 4B |

## Build And Flash

Build Pico 2 with an explicit relay overlay:

```sh
TARGET=pico2 RELAY_OVERLAY=firmware/boards/raspberrypi/rpi_pico2/pico2w-relay-dev.overlay \
  scripts/build-firmware.sh
```

Build Pico 2 W with an explicit relay overlay:

```sh
TARGET=pico2w RELAY_OVERLAY=firmware/boards/raspberrypi/rpi_pico2/pico2w-relay-dev.overlay \
  scripts/build-firmware.sh
```

The wrapper writes Pico builds to `build/firmware-pico2` or
`build/firmware-pico2w` by default. Flash the selected build directory:

```sh
west flash -d build/firmware-pico2
west flash -d build/firmware-pico2w
```

## Hardware Safety

- Keep hazardous relay loads disconnected during bring-up.
- Confirm relay module input voltage, drive polarity, current draw, and ground
  requirements before connecting Pico GPIOs.
- Use an external driver board for relay coils; do not drive relay coils
  directly from Pico GPIOs.
- Share Pico `GND` with the relay input-side ground unless the relay hardware
  explicitly provides isolated logic inputs and documents a different wiring
  model.
- Confirm every channel defaults off after boot, reset, smoke tests, and
  teardown.
- Run `rp2350-relay --port <serial-port> off-all` before disconnecting relay
  hardware or ending a failed test.

## Smoke Test

After flashing, run the same host CLI smoke test used for Waveshare hardware:

```sh
rp2350-relay --port <serial-port> smoke
```

On Linux, the serial port is typically similar to `/dev/ttyACM0`. On Windows,
use the assigned `COM` port, for example `COM7`.
