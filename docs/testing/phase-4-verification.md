# Phase 4 Verification

Date: 2026-05-17
Hardware: Waveshare RP2350-Relay-6CH
Firmware commit: 41bcb7c
Interfaces: USB CDC ACM SMP on `COM31`
Result: PASS

## Commands Run

- `scripts/smoke-hardware.sh`
- `python tools/usb_rpc_smoke.py --port $env:PORT info`
- `python tools/usb_rpc_smoke.py --port $env:PORT status`
- `python tools/usb_rpc_smoke.py --port $env:PORT get`
- `python tools/usb_rpc_smoke.py --port $env:PORT set 0 on`
- `python tools/usb_rpc_smoke.py --port $env:PORT get --channel 0`
- `python tools/usb_rpc_smoke.py --port $env:PORT set 0 off`
- `python tools/usb_rpc_smoke.py --port $env:PORT set-all 0x21`
- `python tools/usb_rpc_smoke.py --port $env:PORT off-all`
- `python tools/usb_rpc_smoke.py --port $env:PORT pulse 0 100`
- `python tools/usb_rpc_smoke.py --port $env:PORT get --channel 0`
- `python tools/usb_rpc_smoke.py --port $env:PORT invalid-channel`
- `python tools/usb_rpc_smoke.py --port $env:PORT invalid-pulse`
- `python tools/usb_rpc_smoke.py --port $env:PORT status`
- `python tools/usb_rpc_smoke.py --port $env:PORT off-all`

## Results

- `scripts/smoke-hardware.sh` printed the Phase 4 manual hardware checklist.
- The USB CDC ACM serial device was used from the operator PC as `COM31`.
- USB RPC `info`, `status`, `get`, `set`, `set-all`, `pulse`, and `off-all`
  commands completed successfully.
- Invalid USB RPC channel and pulse-duration requests returned structured
  errors without blocking follow-up `status`.
- The operator reported PASS for the Phase 4 USB RPC manual smoke test.
- Final teardown ran `off-all`.

## Notes

- Hardware commands were run from the operator PC with `$env:PORT` set to
  `COM31`.
- No hazardous relay loads were reported connected during the USB RPC smoke
  test.
- No relay was reported left energized after teardown.
