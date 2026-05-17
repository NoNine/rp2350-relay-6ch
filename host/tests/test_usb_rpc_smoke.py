import importlib.util
from pathlib import Path


USB_RPC_SMOKE = Path(__file__).resolve().parents[2] / "tools" / "usb_rpc_smoke.py"
SPEC = importlib.util.spec_from_file_location("usb_rpc_smoke", USB_RPC_SMOKE)
assert SPEC is not None
usb_rpc_smoke = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(usb_rpc_smoke)


def test_decode_indefinite_response_map() -> None:
    payload = (
        b"\xbf"
        b"\x65state\x00"
        b"\x67pulsing\x00"
        b"\x69uptime_ms\x1b\x00\x00\x00\x00\x00\x01\xe2\x40"
        b"\x6fusb_cdc_acm_smp\xf5"
        b"\xff"
    )

    assert usb_rpc_smoke.decode_map(payload) == {
        "state": 0,
        "pulsing": 0,
        "uptime_ms": 123456,
        "usb_cdc_acm_smp": True,
    }


def test_decode_nested_indefinite_error_map() -> None:
    payload = b"\xbf\x63err\xbf\x65group\x18\x40\x62rc\x02\xff\xff"

    assert usb_rpc_smoke.decode_map(payload) == {"err": {"group": 64, "rc": 2}}
