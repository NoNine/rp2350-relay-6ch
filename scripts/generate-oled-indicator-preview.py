#!/usr/bin/env python3
"""Generate OLED indicator UI preview PNG assets.

This is a deterministic documentation renderer. It mirrors the fixed OLED
layout implemented in firmware/src/indicator.c without invoking firmware or
dumping a device framebuffer.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
from tempfile import NamedTemporaryFile

try:
    from PIL import Image, ImageDraw
except ImportError as exc:  # pragma: no cover - exercised manually.
    raise SystemExit(
        "Pillow is required to generate the OLED preview. "
        "Use the Zephyr workspace Python environment."
    ) from exc


ROOT = Path(__file__).resolve().parents[1]
ASSET_DIR = ROOT / "docs/assets/discussions"

CANVAS_W = 1900
CANVAS_H = 1216
CARD_W = 584
CARD_H = 340
CARD_X = (46, 658, 1270)
CARD_Y = (106, 474, 842)
OLED_X_OFFSET = 38
OLED_Y_OFFSET = 60
OLED_SCALE = 4
OLED_OUTER_X_OFFSET = 28
OLED_OUTER_Y_OFFSET = 50
OLED_OUTER_W = 528
OLED_OUTER_H = 272

DISPLAY_WIDTH = 128
DISPLAY_HEIGHT = 64
DISPLAY_GLYPH_WIDTH = 5
DISPLAY_GLYPH_HEIGHT = 7
DISPLAY_TEXT_SPACING = 1
DISPLAY_UI_TOP_TEXT_Y = 3
DISPLAY_UI_TOP_RULE_Y = 13
DISPLAY_UI_CELL_X0 = 3
DISPLAY_UI_CELL_Y = 18
DISPLAY_UI_CELL_W = 17
DISPLAY_UI_CELL_H = 27
DISPLAY_UI_CELL_PITCH = 21
DISPLAY_UI_BOTTOM_RULE_Y = 50
DISPLAY_UI_STATUS_Y = 54
DISPLAY_UI_RULE_X0 = 3
DISPLAY_UI_RULE_X1 = 124
DISPLAY_UI_RULE_W = DISPLAY_UI_RULE_X1 - DISPLAY_UI_RULE_X0 + 1
DISPLAY_UI_TEXT_X0 = 3
DISPLAY_UI_TEXT_X1 = 124
DISPLAY_UI_PULSE_BAR_Y = 46
DISPLAY_UI_PULSE_BAR_H = 2
DISPLAY_UI_PULSE_BAR_W = DISPLAY_UI_CELL_W - 2
DISPLAY_UI_PULSE_BAR_MAX_RADIUS = (DISPLAY_UI_PULSE_BAR_W + 1) // 2

BG = (242, 243, 238)
CARD_BG = (225, 228, 222)
CARD_BORDER = (176, 181, 172)
BEZEL = (22, 27, 22)
OLED_OFF = (0, 0, 0)
OLED_ON = (232, 255, 234)
SHEET_TEXT = (39, 47, 42)

GLYPHS = {
    "0": (0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E),
    "1": (0x04, 0x0C, 0x14, 0x04, 0x04, 0x04, 0x1F),
    "2": (0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F),
    "3": (0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E),
    "4": (0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02),
    "5": (0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E),
    "6": (0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E),
    ":": (0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00),
    "*": (0x00, 0x15, 0x0E, 0x1F, 0x0E, 0x15, 0x00),
    "A": (0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11),
    "B": (0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E),
    "C": (0x0F, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0F),
    "D": (0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E),
    "E": (0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F),
    "F": (0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10),
    "G": (0x0F, 0x10, 0x10, 0x13, 0x11, 0x11, 0x0F),
    "H": (0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11),
    "I": (0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F),
    "J": (0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C),
    "K": (0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11),
    "L": (0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F),
    "M": (0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11),
    "N": (0x11, 0x19, 0x19, 0x15, 0x13, 0x13, 0x11),
    "O": (0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E),
    "P": (0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10),
    "Q": (0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D),
    "R": (0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11),
    "S": (0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E),
    "T": (0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04),
    "U": (0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E),
    "V": (0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04),
    "W": (0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A),
    "X": (0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11),
    "Y": (0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04),
    "Z": (0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F),
    "-": (0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00),
    "/": (0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10),
}


@dataclass(frozen=True)
class PreviewState:
    heading: str
    mode: str
    detail: str
    ready: bool
    state_mask: int = 0
    pulse_mask: int = 0
    error: bool = False
    timed_pulses: bool = False


@dataclass(frozen=True)
class PreviewAsset:
    name: str
    kind: str
    path: Path
    state: PreviewState | None = None
    pulse_now_ms: tuple[int, ...] = ()


STATES = (
    PreviewState("BOOT", "BOOT", "", False),
    PreviewState("READY", "READY", "OK", True),
    PreviewState("ACTIVE 1", "ACTIVE", "OK", True, state_mask=0b000001),
    PreviewState("ACTIVE 1/3/6", "ACTIVE", "OK", True, state_mask=0b100101),
    PreviewState("PULSE 2", "ACTIVE", "P2", True, pulse_mask=0b000010, timed_pulses=True),
    PreviewState("PULSE MULTI", "ACTIVE", "P*", True, pulse_mask=0b010010, timed_pulses=True),
    PreviewState("ATTN ARG", "ATTN", "E:ARG", True, error=True),
    PreviewState("REBOOT", "REBOOT", "HOLD", True),
    PreviewState("FAULT", "FAULT", "E:IO", False, error=True),
)


def state_by_heading(heading: str) -> PreviewState:
    for state in STATES:
        if state.heading == heading:
            return state
    raise ValueError(f"unknown preview state: {heading}")


DEFAULT_ASSETS = (
    PreviewAsset(
        "contact-sheet",
        "contact-sheet",
        ASSET_DIR / "oled-indicator-ui-generated-output-preview.png",
    ),
    PreviewAsset(
        "active-1",
        "solo",
        ASSET_DIR / "oled-indicator-ui-active-1.png",
        state_by_heading("ACTIVE 1"),
    ),
    PreviewAsset(
        "pulse-2",
        "solo",
        ASSET_DIR / "oled-indicator-ui-pulse-2.png",
        state_by_heading("PULSE 2"),
    ),
    PreviewAsset(
        "active-1-pulse-2-3",
        "solo",
        ASSET_DIR / "oled-indicator-ui-active-1-pulse-2-3.png",
        PreviewState(
            "ACTIVE 1 PULSE 2/3",
            "ACTIVE",
            "P*",
            True,
            state_mask=0b000001,
            pulse_mask=0b000110,
            timed_pulses=True,
        ),
        (0, 500, 0, 0, 0, 0),
    ),
)


def text_width(text: str, scale: int = 1) -> int:
    if not text:
        return 0
    return ((DISPLAY_GLYPH_WIDTH + DISPLAY_TEXT_SPACING) * len(text) - DISPLAY_TEXT_SPACING) * scale


def draw_text(draw: ImageDraw.ImageDraw, x: int, y: int, text: str, scale: int, color: tuple[int, int, int]) -> None:
    step = (DISPLAY_GLYPH_WIDTH + DISPLAY_TEXT_SPACING) * scale
    for idx, char in enumerate(text):
        if char == " ":
            continue
        glyph = GLYPHS.get(char)
        if glyph is None:
            raise ValueError(f"unsupported glyph: {char!r}")
        char_x = x + idx * step
        for row, bits in enumerate(glyph):
            for col in range(DISPLAY_GLYPH_WIDTH):
                if bits & (1 << ((DISPLAY_GLYPH_WIDTH - 1) - col)):
                    x0 = char_x + col * scale
                    y0 = y + row * scale
                    draw.rectangle((x0, y0, x0 + scale - 1, y0 + scale - 1), fill=color)


def draw_centered_text(
    draw: ImageDraw.ImageDraw,
    bounds: tuple[int, int, int, int],
    text: str,
    scale: int,
    color: tuple[int, int, int],
) -> None:
    x0, y0, x1, y1 = bounds
    x = x0 + ((x1 - x0 + 1) - text_width(text, scale)) // 2
    y = y0 + ((y1 - y0 + 1) - DISPLAY_GLYPH_HEIGHT * scale) // 2
    draw_text(draw, x, y, text, scale, color)


class Frame:
    def __init__(self) -> None:
        self.pixels = [[False for _ in range(DISPLAY_WIDTH)] for _ in range(DISPLAY_HEIGHT)]

    def set_pixel(self, x: int, y: int) -> None:
        if 0 <= x < DISPLAY_WIDTH and 0 <= y < DISPLAY_HEIGHT:
            self.pixels[y][x] = True

    def clear_pixel(self, x: int, y: int) -> None:
        if 0 <= x < DISPLAY_WIDTH and 0 <= y < DISPLAY_HEIGHT:
            self.pixels[y][x] = False

    def draw_hline(self, x: int, y: int, width: int) -> None:
        for col in range(width):
            self.set_pixel(x + col, y)

    def draw_vline(self, x: int, y: int, height: int) -> None:
        for row in range(height):
            self.set_pixel(x, y + row)

    def clear_vline(self, x: int, y: int, height: int) -> None:
        for row in range(height):
            self.clear_pixel(x, y + row)

    def draw_rect(self, x: int, y: int, width: int, height: int, filled: bool) -> None:
        if filled:
            for row in range(height):
                self.draw_hline(x, y + row, width)
            return
        self.draw_hline(x, y, width)
        self.draw_hline(x, y + height - 1, width)
        self.draw_vline(x, y, height)
        self.draw_vline(x + width - 1, y, height)

    def draw_char(self, x: int, y: int, char: str, clear: bool = False) -> None:
        glyph = GLYPHS.get(char)
        if glyph is None:
            raise ValueError(f"unsupported glyph: {char!r}")
        for row, bits in enumerate(glyph):
            for col in range(DISPLAY_GLYPH_WIDTH):
                if bits & (1 << ((DISPLAY_GLYPH_WIDTH - 1) - col)):
                    if clear:
                        self.clear_pixel(x + col, y + row)
                    else:
                        self.set_pixel(x + col, y + row)

    def draw_text(self, x: int, y: int, text: str) -> None:
        step = DISPLAY_GLYPH_WIDTH + DISPLAY_TEXT_SPACING
        for idx, char in enumerate(text):
            char_x = x + idx * step
            if char_x > DISPLAY_WIDTH - DISPLAY_GLYPH_WIDTH:
                break
            if char != " ":
                self.draw_char(char_x, y, char)

    def draw_pulse_mark(self, x: int, y: int, filled: bool) -> None:
        for offset in (0, 3):
            block_x = x + offset
            if filled:
                self.clear_vline(block_x, y, 2)
                self.clear_vline(block_x + 1, y, 2)
            else:
                self.draw_vline(block_x, y, 2)
                self.draw_vline(block_x + 1, y, 2)

    def draw_pulse_countdown(self, cell_x: int, duration_ms: int, end_ms: int, now_ms: int) -> None:
        if duration_ms == 0 or end_ms <= now_ms:
            return
        remaining_ms = min(end_ms - now_ms, duration_ms)
        radius = (remaining_ms * DISPLAY_UI_PULSE_BAR_MAX_RADIUS + duration_ms - 1) // duration_ms
        radius = max(radius, 1)
        center_x = cell_x + DISPLAY_UI_CELL_W // 2
        left = center_x - radius + 1
        width = radius * 2 - 1
        for row in range(DISPLAY_UI_PULSE_BAR_H):
            self.draw_hline(left, DISPLAY_UI_PULSE_BAR_Y + row, width)


def render_frame(state: PreviewState, pulse_now_ms: tuple[int, ...] = ()) -> Frame:
    frame = Frame()
    active_mask = state.state_mask | state.pulse_mask
    duration_ms = [0] * 6
    end_ms = [0] * 6
    if state.timed_pulses:
        for channel in range(6):
            if state.pulse_mask & (1 << channel):
                duration_ms[channel] = 1000
                end_ms[channel] = 1000

    frame.draw_text(DISPLAY_UI_TEXT_X0, DISPLAY_UI_TOP_TEXT_Y, "USB")
    if state.ready:
        frame.draw_text(DISPLAY_UI_TEXT_X0 + 26, DISPLAY_UI_TOP_TEXT_Y, "RDY")
    if active_mask:
        frame.draw_text(DISPLAY_UI_TEXT_X0 + 52, DISPLAY_UI_TOP_TEXT_Y, "ACT")
    if state.pulse_mask:
        frame.draw_text(DISPLAY_UI_TEXT_X0 + 78, DISPLAY_UI_TOP_TEXT_Y, "PLS")
    if state.error:
        frame.draw_text(DISPLAY_UI_TEXT_X0 + 108, DISPLAY_UI_TOP_TEXT_Y, "ERR")

    frame.draw_hline(DISPLAY_UI_RULE_X0, DISPLAY_UI_TOP_RULE_Y, DISPLAY_UI_RULE_W)
    frame.draw_hline(DISPLAY_UI_RULE_X0, DISPLAY_UI_BOTTOM_RULE_Y, DISPLAY_UI_RULE_W)

    for channel in range(6):
        cell_x = DISPLAY_UI_CELL_X0 + channel * DISPLAY_UI_CELL_PITCH
        bit = 1 << channel
        filled = bool(active_mask & bit)
        digit_x = cell_x + (DISPLAY_UI_CELL_W - DISPLAY_GLYPH_WIDTH) // 2
        digit_y = DISPLAY_UI_CELL_Y + (DISPLAY_UI_CELL_H - DISPLAY_GLYPH_HEIGHT) // 2

        frame.draw_rect(cell_x, DISPLAY_UI_CELL_Y, DISPLAY_UI_CELL_W, DISPLAY_UI_CELL_H, filled)
        frame.draw_char(digit_x, digit_y, str(channel + 1), clear=filled)
        if state.pulse_mask & bit:
            frame.draw_pulse_mark(cell_x + 11, DISPLAY_UI_CELL_Y + 4, filled)
            now_ms = pulse_now_ms[channel] if channel < len(pulse_now_ms) else 0
            frame.draw_pulse_countdown(cell_x, duration_ms[channel], end_ms[channel], now_ms)

    frame.draw_text(DISPLAY_UI_TEXT_X0, DISPLAY_UI_STATUS_Y, state.mode)
    if state.detail:
        detail_x = DISPLAY_UI_TEXT_X1 - text_width(state.detail) + 1
        frame.draw_text(detail_x, DISPLAY_UI_STATUS_Y, state.detail)

    return frame


def paste_frame(image: Image.Image, frame: Frame, x0: int, y0: int) -> None:
    draw = ImageDraw.Draw(image)
    draw.rectangle(
        (
            x0,
            y0,
            x0 + DISPLAY_WIDTH * OLED_SCALE - 1,
            y0 + DISPLAY_HEIGHT * OLED_SCALE - 1,
        ),
        fill=OLED_OFF,
    )
    for y, row in enumerate(frame.pixels):
        for x, enabled in enumerate(row):
            if enabled:
                px = x0 + x * OLED_SCALE
                py = y0 + y * OLED_SCALE
                draw.rectangle((px, py, px + OLED_SCALE - 1, py + OLED_SCALE - 1), fill=OLED_ON)


def draw_contact_sheet() -> Image.Image:
    image = Image.new("RGB", (CANVAS_W, CANVAS_H), BG)
    draw = ImageDraw.Draw(image)
    draw_centered_text(draw, (0, 22, CANVAS_W - 1, 82), "OLED INDICATOR GENERATED OUTPUT PREVIEW", 4, SHEET_TEXT)

    for idx, state in enumerate(STATES):
        col = idx % 3
        row = idx // 3
        card_x = CARD_X[col]
        card_y = CARD_Y[row]
        oled_x = card_x + OLED_X_OFFSET
        oled_y = card_y + OLED_Y_OFFSET
        outer_x = card_x + OLED_OUTER_X_OFFSET
        outer_y = card_y + OLED_OUTER_Y_OFFSET

        draw.rectangle((card_x, card_y, card_x + CARD_W - 1, card_y + CARD_H - 1), fill=CARD_BG)
        draw.rectangle((card_x, card_y, card_x + CARD_W - 1, card_y + CARD_H - 1), outline=CARD_BORDER, width=2)
        draw_centered_text(draw, (card_x, card_y + 12, card_x + CARD_W - 1, card_y + 46), state.heading, 3, SHEET_TEXT)
        draw.rectangle(
            (outer_x, outer_y, outer_x + OLED_OUTER_W - 1, outer_y + OLED_OUTER_H - 1),
            fill=BEZEL,
        )
        draw.rectangle(
            (outer_x + 4, outer_y + 4, outer_x + OLED_OUTER_W - 5, outer_y + OLED_OUTER_H - 5),
            outline=(56, 66, 57),
            width=2,
        )
        paste_frame(image, render_frame(state), oled_x, oled_y)

    return image


def draw_solo_display(state: PreviewState, pulse_now_ms: tuple[int, ...] = ()) -> Image.Image:
    image = Image.new("RGB", (DISPLAY_WIDTH * OLED_SCALE, DISPLAY_HEIGHT * OLED_SCALE), OLED_OFF)
    paste_frame(image, render_frame(state, pulse_now_ms), 0, 0)
    return image


def write_png(path: Path, image: Image.Image) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with NamedTemporaryFile("wb", suffix=".png", dir=path.parent, delete=False) as tmp:
        tmp_path = Path(tmp.name)
        image.save(tmp, format="PNG")
    tmp_path.replace(path)
    path.chmod(0o644)
    print(path.relative_to(ROOT))


def render_asset(asset: PreviewAsset) -> Image.Image:
    if asset.kind == "contact-sheet":
        return draw_contact_sheet()
    if asset.kind == "solo":
        if asset.state is None:
            raise ValueError(f"solo asset {asset.name!r} must declare a state")
        return draw_solo_display(asset.state, asset.pulse_now_ms)
    raise ValueError(f"unknown asset kind for {asset.name!r}: {asset.kind}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--asset",
        choices=tuple(asset.name for asset in DEFAULT_ASSETS),
        help="Generate one named preview asset.",
    )
    parser.add_argument(
        "--all",
        action="store_true",
        help="Generate all default preview assets. This is also the default.",
    )
    parser.add_argument(
        "--list-assets",
        action="store_true",
        help="List available default preview asset names and output paths.",
    )
    return parser.parse_args()


def selected_assets(args: argparse.Namespace) -> tuple[PreviewAsset, ...]:
    if args.asset:
        return tuple(asset for asset in DEFAULT_ASSETS if asset.name == args.asset)
    return DEFAULT_ASSETS


def main() -> None:
    args = parse_args()
    if args.list_assets:
        for asset in DEFAULT_ASSETS:
            print(f"{asset.name}: {asset.path.relative_to(ROOT)}")
        return

    for asset in selected_assets(args):
        write_png(asset.path, render_asset(asset))


if __name__ == "__main__":
    main()
