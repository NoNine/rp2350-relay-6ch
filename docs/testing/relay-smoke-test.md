# Relay Smoke Test

Use this procedure after flashing Phase 1 firmware to RP2350-Relay-6CH
hardware.

## Setup

- Power the board from the documented external DC input or a bench supply set
  to the expected input range.
- Keep relay contacts disconnected from hazardous loads during bring-up.
- Connect the Zephyr shell console for manual relay commands.
- Confirm the firmware logs relay initialization without GPIO errors.

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
7. Reset or power-cycle the board again.
8. Confirm all relay indicator LEDs and relay contacts are off after reset.

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
- `relay off` de-energizes all six relays.
- Reset and power-cycle both return all relays to off.
- No relay GPIO polarity differs from the documented active-high assumption.
