# Relay Smoke Test

Use this procedure after flashing Phase 2 firmware to RP2350-Relay-6CH
hardware.

## Setup

- Power the board from the documented external DC input or a bench supply set
  to the expected input range.
- Keep relay contacts disconnected from hazardous loads during bring-up.
- Connect a 3.3 V USB-UART adapter to the UART0 shell on the Pico-compatible
  header:
  - adapter RX to board `TXD0` / GPIO0
  - adapter TX to board `RXD0` / GPIO1
  - adapter GND to board MCU-side `GND`, not isolated relay/RS485 `SGND`
- Open the adapter serial port at 115200 8N1 with no flow control, then press
  Enter to reach the Zephyr shell prompt.
- Confirm the firmware logs relay initialization without GPIO errors.
- Leave the USB-C device port available for flashing and future host-control
  protocol work. Do not depend on USB CDC for the relay shell.

## Procedure

1. Reset or power-cycle the board.
2. Confirm all relay indicator LEDs and relay contacts are off.
3. For each channel from `1` through `6`, run:

   ```sh
   relay set <channel> on
   relay get <channel>
   relay set <channel> off
   relay get <channel>
   ```

4. Confirm only the selected relay switches on, then returns off.
5. Run:

   ```sh
   relay all on
   relay get
   relay off
   relay get
   ```

6. Confirm all relays switch on together, then all relays return off.
7. For each channel from `1` through `6`, run:

   ```sh
   relay pulse <channel> 100
   relay get <channel>
   ```

8. Confirm only the selected relay switches on briefly and returns off.
9. Run `relay get` and confirm all relays are off.
10. Reset or power-cycle the board again.
11. Confirm all relay indicator LEDs and relay contacts are off after reset.

## Teardown

Always run:

```sh
relay off
relay get
```

before disconnecting the board or ending a failed test. No test should leave a
relay energized.

## Pass Criteria

- `CH1` through `CH6` each switch independently.
- `relay pulse <channel> 100` briefly energizes only the selected relay and
  returns it off.
- `relay off` de-energizes all six relays.
- Reset and power-cycle both return all relays to off.
- No relay GPIO polarity differs from the documented active-high assumption.
