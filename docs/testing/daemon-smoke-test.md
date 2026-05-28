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

For the production instance path, create or update
`~/.config/rp2350-relay/config.toml`:

```toml
[instances.bench-a]
serial = "<usb-serial>"
socket = "${XDG_RUNTIME_DIR}/rp2350-relay/bench-a.sock"
wait_device = true
```

## Checks

1. Start the daemon in a terminal:

   ```sh
   rp2350-relayd --instance bench-a
   ```

   For a bench-only exact-port check:

   ```sh
   SOCKET="${XDG_RUNTIME_DIR}/rp2350-relay/bench-a.sock"
   rp2350-relayd --port /dev/ttyACM0 --socket "$SOCKET"
   ```

2. In another terminal, confirm daemon status:

   ```sh
   rp2350-relayctl --instance bench-a daemon-status
   ```

   Confirm the response includes `connected`, `selector_type`,
   `selector_value`, `current_port`, `socket_path`, `reconnect_attempts`,
   `last_error`, and `daemon_version`.

3. Query firmware identity and relay status through the daemon:

   ```sh
   rp2350-relayctl --instance bench-a info
   rp2350-relayctl --instance bench-a status
   ```

4. Pulse `CH1` and turn all relays off:

   ```sh
   rp2350-relayctl --instance bench-a pulse 1 100
   rp2350-relayctl --instance bench-a off-all
   ```

5. Confirm JSON output for scripts:

   ```sh
   rp2350-relayctl --instance bench-a --output json daemon-status
   rp2350-relayctl --instance bench-a --output json status
   ```

6. Install and check the systemd user unit:

   ```sh
   rp2350-relayctl systemd install
   systemctl --user daemon-reload
   rp2350-relayctl systemd doctor --instance bench-a
   systemctl --user enable --now rp2350-relayd@bench-a
   rp2350-relayctl --instance bench-a daemon-status
   ```

7. For PC-boot startup before operator login, enable lingering once for the
   operator account:

   ```sh
   sudo loginctl enable-linger "$USER"
   systemctl --user enable --now rp2350-relayd@bench-a
   ```

8. Stop the foreground daemon with `Ctrl+C`, or stop the service with
   `systemctl --user stop rp2350-relayd@bench-a` when keeping autostart enabled.
   Use `systemctl --user disable --now rp2350-relayd@bench-a` only when removing
   autostart. Confirm all relays are off after shutdown.

## Expected Results

- The daemon starts as the operator user without root.
- The daemon creates the socket parent directory and socket with user-only
  permissions.
- Short-lived `rp2350-relayctl` commands complete through the daemon socket.
- `daemon-status` succeeds while the daemon is running.
- The daemon owns the serial port while running.
- The enabled systemd user service restarts on future user-session starts, and
  starts on PC boot when linger is enabled for the operator user.
- All relays are off after `off-all` and after clean daemon shutdown.

## Troubleshooting

If `rp2350-relayctl --instance bench-a daemon-status` reports
`connected: false` and `last_error` includes permission denied for the serial
device, inspect the device node and user session:

```sh
ls -l /dev/ttyACM0
getfacl /dev/ttyACM0 2>/dev/null || true
groups
systemctl --user show-environment | grep -E 'USER|XDG_RUNTIME_DIR|PATH' || true
```

Use the actual serial device path if it is not `/dev/ttyACM0`.

For a temporary manual-test workaround, grant access to the current device node
and restart the user service:

```sh
sudo chmod a+rw /dev/ttyACM0
systemctl --user restart rp2350-relayd@bench-a
sleep 2
rp2350-relayctl --instance bench-a daemon-status
```

The `chmod` workaround is reset when the board is unplugged, reconnected, or the
system reboots. For the permanent fix, ensure the operator user is in the
serial-port group, usually `dialout`, then fully log out and back in before
restarting the user service. If the user was added to `dialout` after the
current login session started, the user systemd manager may still have the old
group set until the session is restarted.

To force a fresh user manager session after stopping the daemon, run:

```sh
systemctl --user stop rp2350-relayd@bench-a
loginctl terminate-user "$USER"
```

`loginctl terminate-user "$USER"` logs out the current user and terminates that
user's running sessions. Save shell work first, then log back in and restart
the daemon service.

### Systemd logs and journal permissions

For a systemd-managed daemon, read recent logs with:

```sh
journalctl --user-unit=rp2350-relayd@bench-a.service -n 50 --no-pager
```

Follow live logs with:

```sh
journalctl --user-unit=rp2350-relayd@bench-a.service -f
```

If the command reports `No journal files were opened due to insufficient
permissions`, check the system-wide journal for the operator user's systemd
manager instead:

```sh
sudo journalctl -u user@$(id -u).service --grep=rp2350_relay_6ch -n 100 --no-pager
sudo journalctl -u user@$(id -u).service --grep="heartbeat failed" -n 100 --no-pager
```

If the service uses a different operator account, run `id -u <operator-user>`
and replace `$(id -u)` with that numeric user ID.

When logs appear empty, first confirm the instance is active and that
`daemon-status` is reaching the expected socket:

```sh
systemctl --user status rp2350-relayd@bench-a.service
systemctl --user show rp2350-relayd@bench-a.service \
  -p ActiveState -p SubState -p MainPID -p ExecMainStatus
rp2350-relayctl --instance bench-a daemon-status
```

Successful heartbeat checks are intentionally silent. A heartbeat failure is
logged only after the daemon was connected and an idle heartbeat command fails.
If the daemon is already disconnected, use `daemon-status` `last_error` and
reconnect log entries instead of expecting repeated heartbeat-failure logs.

For permanent non-`sudo` journal access on systems that restrict user journal
files, add the operator user to the journal-reading group, then fully log out
and back in:

```sh
sudo usermod -aG systemd-journal "$USER"
```
