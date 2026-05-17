"""Protocol constants for the RP2350 relay management group."""

from __future__ import annotations

GROUP_RELAY = 64
PROTOCOL_VERSION = 1
HARDWARE_NAME = "Waveshare RP2350-Relay-6CH"

RELAY_COUNT = 6
RELAY_MASK = (1 << RELAY_COUNT) - 1
PULSE_MIN_MS = 10
PULSE_MAX_MS = 60000

OP_READ = 0
OP_READ_RSP = 1
OP_WRITE = 2
OP_WRITE_RSP = 3

CMD_INFO = 0
CMD_GET = 1
CMD_SET = 2
CMD_SET_ALL = 3
CMD_PULSE = 4
CMD_OFF_ALL = 5
CMD_STATUS = 6
CMD_REBOOT = 7

ERR_OK = 0
ERR_DECODE = 1
ERR_INVALID_ARGUMENT = 2
ERR_BUSY = 3
ERR_RELAY_IO = 4
ERR_REBOOT_UNAVAILABLE = 5

DEVICE_ERROR_NAMES = {
    ERR_DECODE: "decode",
    ERR_INVALID_ARGUMENT: "invalid_argument",
    ERR_BUSY: "busy",
    ERR_RELAY_IO: "relay_io",
    ERR_REBOOT_UNAVAILABLE: "reboot_unavailable",
}
