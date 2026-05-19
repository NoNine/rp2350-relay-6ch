# Test Procedures

Use these commands for development validation. Keep relay loads disconnected
during bring-up, and always confirm all relays are off after hardware tests.

## Host Tests

Run the host library and CLI tests with the repository wrapper:

```sh
scripts/test-host.sh
```

## Firmware Tests

Run the relay unit tests on `native_sim`:

```sh
west build -s firmware/tests/relay -b native_sim -d build/firmware-tests/relay
build/firmware-tests/relay/zephyr/zephyr.exe
```

Run the relay management tests on `native_sim`:

```sh
west build -s firmware/tests/relay_mgmt -b native_sim -d build/firmware-tests/relay-mgmt
build/firmware-tests/relay-mgmt/zephyr/zephyr.exe
```

## Hardware Smoke Tests

Run the hardware smoke-test wrapper:

```sh
scripts/smoke-hardware.sh
```

Run the CLI hardware smoke test:

```sh
rp2350-relay --port <serial-port> smoke
```

Use the assigned Windows serial port when hardware is attached to a separate
operator PC, for example `COM7`.

See [relay-smoke-test.md](relay-smoke-test.md) for manual relay checks and
[usb-rpc-smoke-test.md](usb-rpc-smoke-test.md) for USB RPC bring-up checks.
Phase verification reports live in `docs/testing/phase-*-verification.md`.
