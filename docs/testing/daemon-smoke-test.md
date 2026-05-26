# Daemon Smoke Test

This procedure validates the Linux-only `rp2350-relayd` and `rp2350-relayctl`
workflow. Keep relay loads disconnected during first bring-up and never leave a
relay energized at teardown.

## Setup

- Use a Linux operator host with the host wheel installed.
- Flash matching firmware before starting the daemon.
- Connect the board USB-C device connector directly to the Linux host.
- Identify the relay controller USB serial number or exact serial port.
- Stop other tools that may already own the serial device.

Set the daemon socket once:

```sh
export SOCKET="${XDG_RUNTIME_DIR}/rp2350-relay/bench-a.sock"
```

## Checks

1. Start the daemon in a terminal:

   ```sh
   rp2350-relayd --serial <usb-serial> --socket "$SOCKET" --wait-device
   ```

   For a bench-only exact-port check:

   ```sh
   rp2350-relayd --port /dev/ttyACM0 --socket "$SOCKET"
   ```

2. In another terminal, confirm daemon status:

   ```sh
   rp2350-relayctl --socket "$SOCKET" daemon-status
   ```

   Confirm the response includes `connected`, `selector_type`,
   `selector_value`, `current_port`, `socket_path`, `reconnect_attempts`,
   `last_error`, and `daemon_version`.

3. Query firmware identity and relay status through the daemon:

   ```sh
   rp2350-relayctl --socket "$SOCKET" info
   rp2350-relayctl --socket "$SOCKET" status
   ```

4. Pulse `CH1` and turn all relays off:

   ```sh
   rp2350-relayctl --socket "$SOCKET" pulse 1 100
   rp2350-relayctl --socket "$SOCKET" off-all
   ```

5. Confirm JSON output for scripts:

   ```sh
   rp2350-relayctl --socket "$SOCKET" --output json daemon-status
   rp2350-relayctl --socket "$SOCKET" --output json status
   ```

6. Stop the daemon with `Ctrl+C` or `systemctl --user stop rp2350-relayd` when
   running under the example user service. Confirm all relays are off after
   shutdown.

## Expected Results

- The daemon starts as the operator user without root.
- The daemon creates the socket parent directory and socket with user-only
  permissions.
- Short-lived `rp2350-relayctl` commands complete through the daemon socket.
- `daemon-status` succeeds while the daemon is running.
- The daemon owns the serial port while running.
- All relays are off after `off-all` and after clean daemon shutdown.
