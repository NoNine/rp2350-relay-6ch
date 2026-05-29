/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <rp2350_relay_6ch/indicator.h>

LOG_MODULE_REGISTER(rp2350_relay_indicator, LOG_LEVEL_INF);

#define RGB_NODE DT_ALIAS(led_strip)
#define BUZZER_NODE DT_PATH(zephyr_user)
#define DISPLAY_WIDTH 128U
#define DISPLAY_HEIGHT 64U
#define DISPLAY_POST_WIDTH 16U
#define DISPLAY_POST_HEIGHT 8U
#define DISPLAY_FRAME_SIZE ((DISPLAY_WIDTH * DISPLAY_HEIGHT) / 8U)
#define DISPLAY_GLYPH_WIDTH 5U
#define DISPLAY_GLYPH_HEIGHT 7U
#define DISPLAY_TEXT_SPACING 1U
#define DISPLAY_UI_TOP_TEXT_Y 3U
#define DISPLAY_UI_TOP_RULE_Y 13U
#define DISPLAY_UI_CELL_X0 3U
#define DISPLAY_UI_CELL_Y 18U
#define DISPLAY_UI_CELL_W 17U
#define DISPLAY_UI_CELL_H 27U
#define DISPLAY_UI_CELL_PITCH 21U
#define DISPLAY_UI_BOTTOM_RULE_Y 50U
#define DISPLAY_UI_STATUS_Y 54U
#define DISPLAY_UI_RULE_X0 3U
#define DISPLAY_UI_RULE_X1 124U
#define DISPLAY_UI_RULE_W (DISPLAY_UI_RULE_X1 - DISPLAY_UI_RULE_X0 + 1U)
#define DISPLAY_UI_TEXT_X0 3U
#define DISPLAY_UI_TEXT_X1 124U
#define DISPLAY_UI_PULSE_BAR_Y 46U
#define DISPLAY_UI_PULSE_BAR_H 2U
#define DISPLAY_UI_PULSE_BAR_W (DISPLAY_UI_CELL_W - 2U)
#define DISPLAY_UI_PULSE_BAR_MAX_RADIUS ((DISPLAY_UI_PULSE_BAR_W + 1U) / 2U)
#define DISPLAY_PULSE_CHANNELS 6U

#define COMMAND_TRANSIENT_MS 150U
#define ATTENTION_TRANSIENT_MS 600U
#define RENDER_INTERVAL_MS 100U
#define BEEP_ON_MS 60U
#define BOOT_READY_BEEP_ON_MS 300U
#define BEEP_OFF_MS 60U
#define BUZZER_PERIOD_NS PWM_USEC(1000)
#define RGB_BRIGHTNESS_NUMERATOR 1U
#define RGB_BRIGHTNESS_DENOMINATOR 5U

struct indicator_state {
	struct k_mutex lock;
	struct k_work_delayable work;
	bool initialized;
	bool hardware_enabled;
	bool ready;
	bool degraded;
	bool fault;
	bool reboot_pending;
	uint8_t relay_state_mask;
	uint8_t pulse_mask;
	uint32_t pulse_duration_ms[DISPLAY_PULSE_CHANNELS];
	int64_t pulse_end_ms[DISPLAY_PULSE_CHANNELS];
	int64_t accepted_until;
	int64_t attention_until;
	enum indicator_buzzer_pattern buzzer_pattern;
	uint8_t beeps_remaining;
	bool beep_on;
	uint32_t beep_on_ms;
	int64_t beep_next_ms;
	enum indicator_display_state display_state;
	enum indicator_display_mode display_mode;
	enum indicator_display_detail display_detail;
	enum indicator_display_detail attention_detail;
	uint8_t display_filled_mask;
	uint8_t display_pulse_mask;
	uint16_t display_post_write_count;
	uint16_t display_write_count;
	enum indicator_rgb_pattern last_rgb;
	enum indicator_buzzer_pattern last_buzzer;
};

static struct indicator_state state;

#if IS_ENABLED(CONFIG_RP2350_RELAY_6CH_INDICATORS) && DT_HAS_ALIAS(led_strip)
static const struct device *const rgb_led = DEVICE_DT_GET_OR_NULL(RGB_NODE);
#else
static const struct device *const rgb_led;
#endif

#if IS_ENABLED(CONFIG_RP2350_RELAY_6CH_INDICATORS) && DT_NODE_HAS_PROP(BUZZER_NODE, pwms)
static const struct pwm_dt_spec buzzer = PWM_DT_SPEC_GET(BUZZER_NODE);
#else
static const struct pwm_dt_spec buzzer = { .dev = NULL, .channel = 0U, .period = 0U };
#endif

#ifndef CONFIG_ZTEST
#if IS_ENABLED(CONFIG_RP2350_RELAY_6CH_DISPLAY) && DT_HAS_CHOSEN(zephyr_display)
static const struct device *const display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
#else
static const struct device *const display_dev;
#endif
#endif

#ifdef CONFIG_ZTEST
struct indicator_display_test_config {
	bool supported;
	bool ready;
	uint16_t width;
	uint16_t height;
	uint32_t pixel_formats;
	bool blanking_fails;
	bool orientation_fails;
	bool post_write_fails;
	bool render_write_fails;
	enum display_orientation orientation;
};

static struct indicator_display_test_config display_test_config;
static uint8_t display_test_frame[DISPLAY_FRAME_SIZE];
#endif

static bool rgb_available(void)
{
	return IS_ENABLED(CONFIG_RP2350_RELAY_6CH_INDICATORS) &&
	       rgb_led != NULL &&
	       device_is_ready(rgb_led);
}

static bool buzzer_available(void)
{
	return IS_ENABLED(CONFIG_RP2350_RELAY_6CH_INDICATORS) &&
	       IS_ENABLED(CONFIG_RP2350_RELAY_6CH_BUZZER_FEEDBACK) &&
	       buzzer.dev != NULL &&
	       pwm_is_ready_dt(&buzzer);
}

static bool buzzer_feedback_enabled(void)
{
	return IS_ENABLED(CONFIG_RP2350_RELAY_6CH_INDICATORS) &&
	       IS_ENABLED(CONFIG_RP2350_RELAY_6CH_BUZZER_FEEDBACK);
}

static bool display_configured(void)
{
#ifdef CONFIG_ZTEST
	return display_test_config.supported;
#else
	return IS_ENABLED(CONFIG_RP2350_RELAY_6CH_DISPLAY) && display_dev != NULL;
#endif
}

static bool display_ready(void)
{
#ifdef CONFIG_ZTEST
	return display_test_config.ready;
#else
	return display_configured() && device_is_ready(display_dev);
#endif
}

static int64_t indicator_now(void)
{
#ifdef CONFIG_ZTEST
	extern int64_t indicator_test_now_ms;

	if (indicator_test_now_ms >= 0) {
		return indicator_test_now_ms;
	}
#endif

	return k_uptime_get();
}

static enum indicator_rgb_pattern resolve_rgb_locked(int64_t now)
{
	if (!IS_ENABLED(CONFIG_RP2350_RELAY_6CH_INDICATORS) || !state.hardware_enabled) {
		return INDICATOR_RGB_OFF;
	}

	if (state.fault) {
		return INDICATOR_RGB_FAULT;
	}

	if (state.reboot_pending) {
		return INDICATOR_RGB_REBOOT_PENDING;
	}

	if (state.degraded || now < state.attention_until) {
		return INDICATOR_RGB_ATTENTION;
	}

	if (now < state.accepted_until) {
		return INDICATOR_RGB_ACCEPTED;
	}

	if ((state.relay_state_mask | state.pulse_mask) != 0U) {
		return INDICATOR_RGB_RELAY_ACTIVE;
	}

	if (state.ready) {
		return INDICATOR_RGB_READY;
	}

	return INDICATOR_RGB_BOOTING;
}

static enum indicator_display_mode display_mode_from_rgb(enum indicator_rgb_pattern pattern)
{
	switch (pattern) {
	case INDICATOR_RGB_FAULT:
		return INDICATOR_DISPLAY_MODE_FAULT;
	case INDICATOR_RGB_REBOOT_PENDING:
		return INDICATOR_DISPLAY_MODE_REBOOT;
	case INDICATOR_RGB_ATTENTION:
		return INDICATOR_DISPLAY_MODE_ATTN;
	case INDICATOR_RGB_RELAY_ACTIVE:
		return INDICATOR_DISPLAY_MODE_ACTIVE;
	case INDICATOR_RGB_READY:
	case INDICATOR_RGB_ACCEPTED:
		return INDICATOR_DISPLAY_MODE_READY;
	case INDICATOR_RGB_BOOTING:
		return INDICATOR_DISPLAY_MODE_BOOT;
	case INDICATOR_RGB_OFF:
	default:
		return INDICATOR_DISPLAY_MODE_OFF;
	}
}

static enum indicator_display_mode resolve_display_mode_locked(enum indicator_rgb_pattern pattern)
{
	if (pattern == INDICATOR_RGB_ACCEPTED &&
	    (state.relay_state_mask | state.pulse_mask) != 0U) {
		return INDICATOR_DISPLAY_MODE_ACTIVE;
	}

	return display_mode_from_rgb(pattern);
}

static enum indicator_display_detail display_detail_for_active_locked(void)
{
	uint8_t pulse_mask = state.pulse_mask;

	if (pulse_mask == 0U) {
		return INDICATOR_DISPLAY_DETAIL_OK;
	}

	if ((pulse_mask & (pulse_mask - 1U)) != 0U) {
		return INDICATOR_DISPLAY_DETAIL_P_STAR;
	}

	switch (find_lsb_set(pulse_mask)) {
	case 1:
		return INDICATOR_DISPLAY_DETAIL_P1;
	case 2:
		return INDICATOR_DISPLAY_DETAIL_P2;
	case 3:
		return INDICATOR_DISPLAY_DETAIL_P3;
	case 4:
		return INDICATOR_DISPLAY_DETAIL_P4;
	case 5:
		return INDICATOR_DISPLAY_DETAIL_P5;
	case 6:
		return INDICATOR_DISPLAY_DETAIL_P6;
	default:
		return INDICATOR_DISPLAY_DETAIL_P_STAR;
	}
}

static enum indicator_display_detail display_detail_locked(enum indicator_rgb_pattern pattern,
							   enum indicator_display_mode mode)
{
	if (pattern == INDICATOR_RGB_REBOOT_PENDING) {
		return INDICATOR_DISPLAY_DETAIL_HOLD;
	}

	if (pattern == INDICATOR_RGB_ATTENTION) {
		if (state.degraded) {
			return INDICATOR_DISPLAY_DETAIL_E_IO;
		}

		if (state.attention_detail != INDICATOR_DISPLAY_DETAIL_NONE) {
			return state.attention_detail;
		}

		return INDICATOR_DISPLAY_DETAIL_E_ARG;
	}

	if (mode == INDICATOR_DISPLAY_MODE_ACTIVE) {
		return display_detail_for_active_locked();
	}

	if (pattern == INDICATOR_RGB_READY || pattern == INDICATOR_RGB_ACCEPTED) {
		return INDICATOR_DISPLAY_DETAIL_OK;
	}

	if (pattern == INDICATOR_RGB_FAULT) {
		return INDICATOR_DISPLAY_DETAIL_E_IO;
	}

	return INDICATOR_DISPLAY_DETAIL_NONE;
}

static struct led_rgb rgb_dim(uint8_t red, uint8_t green, uint8_t blue)
{
	return (struct led_rgb){
		.r = (uint8_t)((red * RGB_BRIGHTNESS_NUMERATOR) /
			       RGB_BRIGHTNESS_DENOMINATOR),
		.g = (uint8_t)((green * RGB_BRIGHTNESS_NUMERATOR) /
			       RGB_BRIGHTNESS_DENOMINATOR),
		.b = (uint8_t)((blue * RGB_BRIGHTNESS_NUMERATOR) /
			       RGB_BRIGHTNESS_DENOMINATOR),
	};
}

static struct led_rgb rgb_color(enum indicator_rgb_pattern pattern, int64_t now)
{
	const bool blink_on = ((now / 250) % 2) == 0;
	const bool pulse_on = ((now / 500) % 2) == 0;

	switch (pattern) {
	case INDICATOR_RGB_BOOTING:
		return rgb_dim(12U, 12U, 12U);
	case INDICATOR_RGB_READY:
		return rgb_dim(0U, 32U, 0U);
	case INDICATOR_RGB_ACCEPTED:
		return rgb_dim(0U, 96U, 0U);
	case INDICATOR_RGB_RELAY_ACTIVE:
		return rgb_dim(0U, 32U, 64U);
	case INDICATOR_RGB_ATTENTION:
		return pulse_on ? rgb_dim(80U, 48U, 0U) : rgb_dim(10U, 6U, 0U);
	case INDICATOR_RGB_REBOOT_PENDING:
		return pulse_on ? rgb_dim(40U, 0U, 80U) : rgb_dim(0U, 0U, 60U);
	case INDICATOR_RGB_FAULT:
		return blink_on ? rgb_dim(96U, 0U, 0U) : rgb_dim(0U, 0U, 0U);
	case INDICATOR_RGB_OFF:
	default:
		return rgb_dim(0U, 0U, 0U);
	}
}

static void display_get_capabilities_local(struct display_capabilities *caps)
{
#ifdef CONFIG_ZTEST
	caps->x_resolution = display_test_config.width;
	caps->y_resolution = display_test_config.height;
	caps->supported_pixel_formats = display_test_config.pixel_formats;
	caps->current_pixel_format = PIXEL_FORMAT_MONO01;
	caps->current_orientation = DISPLAY_ORIENTATION_NORMAL;
#elif IS_ENABLED(CONFIG_RP2350_RELAY_6CH_DISPLAY)
	display_get_capabilities(display_dev, caps);
#else
	memset(caps, 0, sizeof(*caps));
#endif
}

static int display_blanking_off_local(void)
{
#ifdef CONFIG_ZTEST
	return display_test_config.blanking_fails ? -EIO : 0;
#elif IS_ENABLED(CONFIG_RP2350_RELAY_6CH_DISPLAY)
	return display_blanking_off(display_dev);
#else
	return -ENODEV;
#endif
}

static int display_set_orientation_local(enum display_orientation orientation)
{
#ifdef CONFIG_ZTEST
	display_test_config.orientation = orientation;
	return display_test_config.orientation_fails ? -EIO : 0;
#elif IS_ENABLED(CONFIG_RP2350_RELAY_6CH_DISPLAY)
	return display_set_orientation(display_dev, orientation);
#else
	ARG_UNUSED(orientation);

	return -ENODEV;
#endif
}

static int display_write_local(uint16_t x, uint16_t y,
			       const struct display_buffer_descriptor *desc,
			       const uint8_t *buf, bool post)
{
#ifdef CONFIG_ZTEST
	ARG_UNUSED(x);
	ARG_UNUSED(y);

	if ((post && display_test_config.post_write_fails) ||
	    (!post && display_test_config.render_write_fails)) {
		return -EIO;
	}

	if (!post && desc->buf_size == sizeof(display_test_frame)) {
		memcpy(display_test_frame, buf, sizeof(display_test_frame));
	}

	return 0;
#elif IS_ENABLED(CONFIG_RP2350_RELAY_6CH_DISPLAY)
	ARG_UNUSED(post);

	return display_write(display_dev, x, y, desc, buf);
#else
	ARG_UNUSED(x);
	ARG_UNUSED(y);
	ARG_UNUSED(desc);
	ARG_UNUSED(buf);
	ARG_UNUSED(post);

	return -ENODEV;
#endif
}

static bool display_capabilities_supported(const struct display_capabilities *caps)
{
	return caps->x_resolution == DISPLAY_WIDTH &&
	       caps->y_resolution == DISPLAY_HEIGHT &&
	       (caps->supported_pixel_formats & (PIXEL_FORMAT_MONO01 | PIXEL_FORMAT_MONO10)) != 0U;
}

static enum indicator_display_state display_post(void)
{
	struct display_capabilities caps;
	const uint8_t post_pattern[DISPLAY_POST_WIDTH] = {
		0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x81,
		0xff, 0x00, 0xaa, 0x55, 0x33, 0xcc, 0x0f, 0xf0,
	};
	const struct display_buffer_descriptor desc = {
		.buf_size = sizeof(post_pattern),
		.width = DISPLAY_POST_WIDTH,
		.height = DISPLAY_POST_HEIGHT,
		.pitch = DISPLAY_POST_WIDTH,
	};
	int ret;

	if (!display_configured()) {
		return INDICATOR_DISPLAY_UNSUPPORTED;
	}

	if (!display_ready()) {
		return INDICATOR_DISPLAY_NOT_DETECTED;
	}

	display_get_capabilities_local(&caps);
	if (!display_capabilities_supported(&caps)) {
		LOG_WRN("OLED display has unsupported geometry or pixel format");
		return INDICATOR_DISPLAY_FAILED;
	}

	ret = display_set_orientation_local(
		IS_ENABLED(CONFIG_RP2350_RELAY_6CH_DISPLAY_ROTATED_180) ?
			DISPLAY_ORIENTATION_ROTATED_180 :
			DISPLAY_ORIENTATION_NORMAL);
	if (ret < 0) {
		LOG_WRN("OLED display orientation failed: %d", ret);
		return INDICATOR_DISPLAY_FAILED;
	}

	ret = display_blanking_off_local();
	if (ret < 0) {
		LOG_WRN("OLED display blanking off failed: %d", ret);
		return INDICATOR_DISPLAY_FAILED;
	}

	ret = display_write_local(0U, 0U, &desc, post_pattern, true);
	if (ret < 0) {
		LOG_WRN("OLED display POST write failed: %d", ret);
		return INDICATOR_DISPLAY_FAILED;
	}
	state.display_post_write_count++;

	return INDICATOR_DISPLAY_READY;
}

static void log_display_detection(enum indicator_display_state display_state)
{
	switch (display_state) {
	case INDICATOR_DISPLAY_READY:
		LOG_INF("Optional OLED indicator detected");
		break;
	case INDICATOR_DISPLAY_NOT_DETECTED:
		LOG_INF("Optional OLED indicator not detected");
		break;
	case INDICATOR_DISPLAY_UNSUPPORTED:
		LOG_INF("Optional OLED indicator not configured");
		break;
	case INDICATOR_DISPLAY_FAILED:
	default:
		break;
	}
}

static void schedule_render(k_timeout_t timeout)
{
	if (IS_ENABLED(CONFIG_RP2350_RELAY_6CH_INDICATORS)) {
		(void)k_work_schedule(&state.work, timeout);
	}
}

static void schedule_render_now(void)
{
	if (IS_ENABLED(CONFIG_RP2350_RELAY_6CH_INDICATORS)) {
		(void)k_work_schedule(&state.work, K_NO_WAIT);
	}
}

static const char *display_mode_text(enum indicator_display_mode mode)
{
	switch (mode) {
	case INDICATOR_DISPLAY_MODE_BOOT:
		return "BOOT";
	case INDICATOR_DISPLAY_MODE_READY:
		return "READY";
	case INDICATOR_DISPLAY_MODE_ACTIVE:
		return "ACTIVE";
	case INDICATOR_DISPLAY_MODE_ATTN:
		return "ATTN";
	case INDICATOR_DISPLAY_MODE_FAULT:
		return "FAULT";
	case INDICATOR_DISPLAY_MODE_REBOOT:
		return "REBOOT";
	case INDICATOR_DISPLAY_MODE_OFF:
	default:
		return "";
	}
}

static const char *display_detail_text(enum indicator_display_detail detail)
{
	switch (detail) {
	case INDICATOR_DISPLAY_DETAIL_OK:
		return "OK";
	case INDICATOR_DISPLAY_DETAIL_P1:
		return "P1";
	case INDICATOR_DISPLAY_DETAIL_P2:
		return "P2";
	case INDICATOR_DISPLAY_DETAIL_P3:
		return "P3";
	case INDICATOR_DISPLAY_DETAIL_P4:
		return "P4";
	case INDICATOR_DISPLAY_DETAIL_P5:
		return "P5";
	case INDICATOR_DISPLAY_DETAIL_P6:
		return "P6";
	case INDICATOR_DISPLAY_DETAIL_P_STAR:
		return "P*";
	case INDICATOR_DISPLAY_DETAIL_E_ARG:
		return "E:ARG";
	case INDICATOR_DISPLAY_DETAIL_E_BUSY:
		return "E:BUSY";
	case INDICATOR_DISPLAY_DETAIL_E_IO:
		return "E:IO";
	case INDICATOR_DISPLAY_DETAIL_HOLD:
		return "HOLD";
	case INDICATOR_DISPLAY_DETAIL_NONE:
	default:
		return "";
	}
}

static void display_draw_hline(uint8_t *frame, uint8_t x, uint8_t y, uint8_t width)
{
	for (uint8_t col = 0U; col < width && (x + col) < DISPLAY_WIDTH; col++) {
		frame[((y / 8U) * DISPLAY_WIDTH) + x + col] |= BIT(y % 8U);
	}
}

static void display_draw_vline(uint8_t *frame, uint8_t x, uint8_t y, uint8_t height)
{
	for (uint8_t row = 0U; row < height && (y + row) < DISPLAY_HEIGHT; row++) {
		frame[(((y + row) / 8U) * DISPLAY_WIDTH) + x] |= BIT((y + row) % 8U);
	}
}

static void display_clear_vline(uint8_t *frame, uint8_t x, uint8_t y, uint8_t height)
{
	for (uint8_t row = 0U; row < height && (y + row) < DISPLAY_HEIGHT; row++) {
		frame[(((y + row) / 8U) * DISPLAY_WIDTH) + x] &= (uint8_t)~BIT((y + row) % 8U);
	}
}

static void display_draw_rect(uint8_t *frame, uint8_t x, uint8_t y,
			      uint8_t width, uint8_t height, bool filled)
{
	if (filled) {
		for (uint8_t row = 0U; row < height; row++) {
			display_draw_hline(frame, x, y + row, width);
		}
		return;
	}

	display_draw_hline(frame, x, y, width);
	display_draw_hline(frame, x, y + height - 1U, width);
	display_draw_vline(frame, x, y, height);
	display_draw_vline(frame, x + width - 1U, y, height);
}

static void display_draw_pulse_mark(uint8_t *frame, uint8_t x, uint8_t y, bool filled)
{
	static const uint8_t block_offsets[] = { 0U, 3U };

	for (uint8_t idx = 0U; idx < ARRAY_SIZE(block_offsets); idx++) {
		uint8_t block_x = x + block_offsets[idx];

		if (filled) {
			display_clear_vline(frame, block_x, y, 2U);
			display_clear_vline(frame, block_x + 1U, y, 2U);
		} else {
			display_draw_vline(frame, block_x, y, 2U);
			display_draw_vline(frame, block_x + 1U, y, 2U);
		}
	}
}

static void display_draw_pulse_countdown(uint8_t *frame, uint8_t cell_x,
					 uint32_t duration_ms, int64_t end_ms,
					 int64_t now)
{
	uint32_t remaining_ms;
	uint8_t radius;
	uint8_t center_x;
	uint8_t left;
	uint8_t width;

	if (duration_ms == 0U || end_ms <= now) {
		return;
	}

	remaining_ms = (uint32_t)(end_ms - now);
	if (remaining_ms > duration_ms) {
		remaining_ms = duration_ms;
	}

	radius = (uint8_t)(((uint64_t)remaining_ms * DISPLAY_UI_PULSE_BAR_MAX_RADIUS +
			    duration_ms - 1U) / duration_ms);
	if (radius == 0U) {
		radius = 1U;
	}

	center_x = cell_x + (DISPLAY_UI_CELL_W / 2U);
	left = center_x - radius + 1U;
	width = (uint8_t)((radius * 2U) - 1U);

	for (uint8_t row = 0U; row < DISPLAY_UI_PULSE_BAR_H; row++) {
		display_draw_hline(frame, left, DISPLAY_UI_PULSE_BAR_Y + row, width);
	}
}

static uint8_t glyph_bits(char c, uint8_t row)
{
	switch (c) {
	case '0': {
		static const uint8_t glyph[] = { 0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e };
		return glyph[row];
	}
	case '1': {
		static const uint8_t glyph[] = { 0x04, 0x0c, 0x14, 0x04, 0x04, 0x04, 0x1f };
		return glyph[row];
	}
	case '2': {
		static const uint8_t glyph[] = { 0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f };
		return glyph[row];
	}
	case '3': {
		static const uint8_t glyph[] = { 0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e };
		return glyph[row];
	}
	case '4': {
		static const uint8_t glyph[] = { 0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02 };
		return glyph[row];
	}
	case '5': {
		static const uint8_t glyph[] = { 0x1f, 0x10, 0x10, 0x1e, 0x01, 0x01, 0x1e };
		return glyph[row];
	}
	case '6': {
		static const uint8_t glyph[] = { 0x0e, 0x10, 0x10, 0x1e, 0x11, 0x11, 0x0e };
		return glyph[row];
	}
	case ':': {
		static const uint8_t glyph[] = { 0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00 };
		return glyph[row];
	}
	case '*': {
		static const uint8_t glyph[] = { 0x00, 0x15, 0x0e, 0x1f, 0x0e, 0x15, 0x00 };
		return glyph[row];
	}
	case 'A': {
		static const uint8_t glyph[] = { 0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 };
		return glyph[row];
	}
	case 'B': {
		static const uint8_t glyph[] = { 0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e };
		return glyph[row];
	}
	case 'C': {
		static const uint8_t glyph[] = { 0x0f, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0f };
		return glyph[row];
	}
	case 'D': {
		static const uint8_t glyph[] = { 0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e };
		return glyph[row];
	}
	case 'E': {
		static const uint8_t glyph[] = { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f };
		return glyph[row];
	}
	case 'F': {
		static const uint8_t glyph[] = { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10 };
		return glyph[row];
	}
	case 'G': {
		static const uint8_t glyph[] = { 0x0f, 0x10, 0x10, 0x13, 0x11, 0x11, 0x0f };
		return glyph[row];
	}
	case 'H': {
		static const uint8_t glyph[] = { 0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 };
		return glyph[row];
	}
	case 'I': {
		static const uint8_t glyph[] = { 0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1f };
		return glyph[row];
	}
	case 'J': {
		static const uint8_t glyph[] = { 0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0c };
		return glyph[row];
	}
	case 'K': {
		static const uint8_t glyph[] = { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 };
		return glyph[row];
	}
	case 'L': {
		static const uint8_t glyph[] = { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f };
		return glyph[row];
	}
	case 'M': {
		static const uint8_t glyph[] = { 0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11 };
		return glyph[row];
	}
	case 'N': {
		static const uint8_t glyph[] = { 0x11, 0x19, 0x19, 0x15, 0x13, 0x13, 0x11 };
		return glyph[row];
	}
	case 'O': {
		static const uint8_t glyph[] = { 0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e };
		return glyph[row];
	}
	case 'P': {
		static const uint8_t glyph[] = { 0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10 };
		return glyph[row];
	}
	case 'Q': {
		static const uint8_t glyph[] = { 0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d };
		return glyph[row];
	}
	case 'R': {
		static const uint8_t glyph[] = { 0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11 };
		return glyph[row];
	}
	case 'S': {
		static const uint8_t glyph[] = { 0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e };
		return glyph[row];
	}
	case 'T': {
		static const uint8_t glyph[] = { 0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 };
		return glyph[row];
	}
	case 'U': {
		static const uint8_t glyph[] = { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e };
		return glyph[row];
	}
	case 'V': {
		static const uint8_t glyph[] = { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04 };
		return glyph[row];
	}
	case 'W': {
		static const uint8_t glyph[] = { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a };
		return glyph[row];
	}
	case 'X': {
		static const uint8_t glyph[] = { 0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11 };
		return glyph[row];
	}
	case 'Y': {
		static const uint8_t glyph[] = { 0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04 };
		return glyph[row];
	}
	case 'Z': {
		static const uint8_t glyph[] = { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f };
		return glyph[row];
	}
	default:
		return 0U;
	}
}

static void display_set_pixel(uint8_t *frame, uint8_t x, uint8_t y)
{
	if (x < DISPLAY_WIDTH && y < DISPLAY_HEIGHT) {
		frame[((y / 8U) * DISPLAY_WIDTH) + x] |= BIT(y % 8U);
	}
}

static void display_clear_pixel(uint8_t *frame, uint8_t x, uint8_t y)
{
	if (x < DISPLAY_WIDTH && y < DISPLAY_HEIGHT) {
		frame[((y / 8U) * DISPLAY_WIDTH) + x] &= (uint8_t)~BIT(y % 8U);
	}
}

static void display_draw_char(uint8_t *frame, uint8_t x, uint8_t y, char c, bool clear)
{
	for (uint8_t row = 0U; row < DISPLAY_GLYPH_HEIGHT; row++) {
		uint8_t bits = glyph_bits(c, row);

		for (uint8_t col = 0U; col < DISPLAY_GLYPH_WIDTH; col++) {
			if ((bits & BIT((DISPLAY_GLYPH_WIDTH - 1U) - col)) != 0U) {
				if (clear) {
					display_clear_pixel(frame, x + col, y + row);
				} else {
					display_set_pixel(frame, x + col, y + row);
				}
			}
		}
	}
}

static uint8_t display_text_width(const char *text)
{
	uint8_t width = 0U;

	for (uint8_t idx = 0U; text[idx] != '\0'; idx++) {
		if (idx > 0U) {
			width += DISPLAY_TEXT_SPACING;
		}
		width += DISPLAY_GLYPH_WIDTH;
	}

	return width;
}

static void display_draw_text(uint8_t *frame, uint8_t x, uint8_t y, const char *text)
{
	uint8_t step = DISPLAY_GLYPH_WIDTH + DISPLAY_TEXT_SPACING;

	for (uint8_t idx = 0U; text[idx] != '\0' && x <= DISPLAY_WIDTH - DISPLAY_GLYPH_WIDTH; idx++) {
		if (text[idx] != ' ') {
			display_draw_char(frame, x, y, text[idx], false);
		}
		x += step;
	}
}

static void display_draw_annunciators(uint8_t *frame, bool ready, bool active,
				      bool pulsing, bool error)
{
	display_draw_text(frame, DISPLAY_UI_TEXT_X0, DISPLAY_UI_TOP_TEXT_Y, "USB");

	if (ready) {
		display_draw_text(frame, DISPLAY_UI_TEXT_X0 + 26U,
				  DISPLAY_UI_TOP_TEXT_Y, "RDY");
	}

	if (active) {
		display_draw_text(frame, DISPLAY_UI_TEXT_X0 + 52U,
				  DISPLAY_UI_TOP_TEXT_Y, "ACT");
	}

	if (pulsing) {
		display_draw_text(frame, DISPLAY_UI_TEXT_X0 + 78U,
				  DISPLAY_UI_TOP_TEXT_Y, "PLS");
	}

	if (error) {
		display_draw_text(frame, DISPLAY_UI_TEXT_X0 + 108U,
				  DISPLAY_UI_TOP_TEXT_Y, "ERR");
	}
}

static bool display_pulse_mark_visible(int64_t now)
{
	return ((now / 500) % 2) == 0;
}

static int display_render_frame(enum indicator_display_mode mode,
				enum indicator_display_detail detail,
				uint8_t state_mask, uint8_t pulse_mask,
				const uint32_t pulse_duration_ms[DISPLAY_PULSE_CHANNELS],
				const int64_t pulse_end_ms[DISPLAY_PULSE_CHANNELS],
				bool ready, bool error, int64_t now)
{
	static uint8_t frame[DISPLAY_FRAME_SIZE];
	struct display_buffer_descriptor desc = {
		.buf_size = sizeof(frame),
		.width = DISPLAY_WIDTH,
		.height = DISPLAY_HEIGHT,
		.pitch = DISPLAY_WIDTH,
	};
	const char *detail_text = display_detail_text(detail);
	const bool pulse_mark_visible = display_pulse_mark_visible(now);
	const uint8_t detail_width = display_text_width(detail_text);
	const uint8_t detail_x = detail_width <= DISPLAY_UI_RULE_W ?
				 DISPLAY_UI_TEXT_X1 - detail_width + 1U :
				 DISPLAY_UI_TEXT_X0;

	memset(frame, 0, sizeof(frame));
	display_draw_annunciators(frame, ready, state_mask != 0U,
				  pulse_mask != 0U, error);
	display_draw_hline(frame, DISPLAY_UI_RULE_X0, DISPLAY_UI_TOP_RULE_Y,
			   DISPLAY_UI_RULE_W);
	display_draw_hline(frame, DISPLAY_UI_RULE_X0, DISPLAY_UI_BOTTOM_RULE_Y,
			   DISPLAY_UI_RULE_W);

	for (uint8_t channel = 0U; channel < 6U; channel++) {
		uint8_t cell_x = DISPLAY_UI_CELL_X0 + (channel * DISPLAY_UI_CELL_PITCH);
		uint8_t bit = BIT(channel);
		bool filled = (state_mask & bit) != 0U || (pulse_mask & bit) != 0U;
		char label = (char)('1' + channel);
		uint8_t digit_x = cell_x + ((DISPLAY_UI_CELL_W - DISPLAY_GLYPH_WIDTH) / 2U);
		uint8_t digit_y = DISPLAY_UI_CELL_Y +
				  ((DISPLAY_UI_CELL_H - DISPLAY_GLYPH_HEIGHT) / 2U);

		display_draw_rect(frame, cell_x, DISPLAY_UI_CELL_Y,
				  DISPLAY_UI_CELL_W, DISPLAY_UI_CELL_H, filled);
		if (filled) {
			display_draw_char(frame, digit_x, digit_y, label, true);
		} else {
			display_draw_char(frame, digit_x, digit_y, label, false);
		}
		if (((pulse_mask & bit) != 0U) && pulse_mark_visible) {
			display_draw_pulse_mark(frame, cell_x + 11U, DISPLAY_UI_CELL_Y + 4U,
						filled);
		}
		if ((pulse_mask & bit) != 0U) {
			display_draw_pulse_countdown(frame, cell_x, pulse_duration_ms[channel],
						     pulse_end_ms[channel], now);
		}
	}

	display_draw_text(frame, DISPLAY_UI_TEXT_X0, DISPLAY_UI_STATUS_Y,
			  display_mode_text(mode));
	if (detail_text[0] != '\0') {
		display_draw_text(frame, detail_x, DISPLAY_UI_STATUS_Y, detail_text);
	}

	return display_write_local(0U, 0U, &desc, frame, false);
}

static bool display_can_render_locked(void)
{
	return state.display_state == INDICATOR_DISPLAY_READY;
}

static void set_buzzer_locked(enum indicator_buzzer_pattern pattern)
{
	if (!IS_ENABLED(CONFIG_RP2350_RELAY_6CH_BUZZER_FEEDBACK) || !state.hardware_enabled) {
		return;
	}

	switch (pattern) {
	case INDICATOR_BUZZER_ACCEPTED:
		state.beeps_remaining = 1U;
		state.beep_on_ms = BEEP_ON_MS;
		break;
	case INDICATOR_BUZZER_REJECTED:
		state.beeps_remaining = 2U;
		state.beep_on_ms = BEEP_ON_MS;
		break;
	case INDICATOR_BUZZER_REBOOT_PENDING:
		state.beeps_remaining = 3U;
		state.beep_on_ms = BEEP_ON_MS;
		break;
	case INDICATOR_BUZZER_BOOT_READY:
		state.beeps_remaining = 1U;
		state.beep_on_ms = BOOT_READY_BEEP_ON_MS;
		break;
	case INDICATOR_BUZZER_SILENT:
	default:
		state.beeps_remaining = 0U;
		state.beep_on_ms = BEEP_ON_MS;
		break;
	}

	state.buzzer_pattern = pattern;
	state.beep_on = false;
	state.beep_next_ms = indicator_now();
}

static void write_rgb(enum indicator_rgb_pattern pattern, int64_t now)
{
	struct led_rgb pixel = rgb_color(pattern, now);
	int ret;

	if (!rgb_available()) {
		return;
	}

	ret = led_strip_update_rgb(rgb_led, &pixel, 1U);
	if (ret < 0) {
		LOG_WRN_ONCE("RGB LED update failed: %d", ret);
	}
}

static void buzzer_off(void)
{
	int ret;

	if (!buzzer_available()) {
		return;
	}

	ret = pwm_set_pulse_dt(&buzzer, 0U);
	if (ret < 0) {
		LOG_WRN_ONCE("Buzzer PWM disable failed: %d", ret);
	}
}

static void render_buzzer_locked(int64_t now)
{
	int ret;

	if (!buzzer_feedback_enabled()) {
		state.last_buzzer = INDICATOR_BUZZER_SILENT;
		return;
	}

	if (state.beeps_remaining == 0U && !state.beep_on) {
		buzzer_off();
		state.buzzer_pattern = INDICATOR_BUZZER_SILENT;
		state.last_buzzer = INDICATOR_BUZZER_SILENT;
		return;
	}

	if (now < state.beep_next_ms) {
		state.last_buzzer = state.buzzer_pattern;
		return;
	}

	if (state.beep_on) {
		buzzer_off();
		state.beep_on = false;
		state.beep_next_ms = now + BEEP_OFF_MS;
		state.last_buzzer = state.buzzer_pattern;
		return;
	}

	if (state.beeps_remaining == 0U) {
		state.buzzer_pattern = INDICATOR_BUZZER_SILENT;
		state.last_buzzer = INDICATOR_BUZZER_SILENT;
		return;
	}

	ret = buzzer_available() ? pwm_set_dt(&buzzer, BUZZER_PERIOD_NS,
					      BUZZER_PERIOD_NS / 2U) : 0;
	if (ret < 0) {
		LOG_WRN_ONCE("Buzzer PWM update failed: %d", ret);
		state.beeps_remaining = 0U;
		state.buzzer_pattern = INDICATOR_BUZZER_SILENT;
		state.last_buzzer = INDICATOR_BUZZER_SILENT;
		return;
	}

	state.beep_on = true;
	state.beeps_remaining--;
	state.beep_next_ms = now + state.beep_on_ms;
	state.last_buzzer = state.buzzer_pattern;
}

static bool has_active_transient_locked(int64_t now)
{
	return now < state.accepted_until ||
	       now < state.attention_until ||
	       state.beeps_remaining != 0U ||
	       state.beep_on;
}

static void render(void)
{
	enum indicator_rgb_pattern pattern;
	enum indicator_display_mode display_mode;
	enum indicator_display_detail display_detail;
	uint8_t display_filled_mask;
	uint8_t display_pulse_mask;
	uint32_t pulse_duration_ms[DISPLAY_PULSE_CHANNELS];
	int64_t pulse_end_ms[DISPLAY_PULSE_CHANNELS];
	int64_t now = indicator_now();
	bool display_ready_state;
	bool display_error;
	bool write_display;
	bool reschedule;
	int display_ret = 0;

	k_mutex_lock(&state.lock, K_FOREVER);
	pattern = resolve_rgb_locked(now);
	state.last_rgb = pattern;
	display_mode = resolve_display_mode_locked(pattern);
	display_detail = display_detail_locked(pattern, display_mode);
	display_filled_mask = state.relay_state_mask | state.pulse_mask;
	display_pulse_mask = state.pulse_mask;
	memcpy(pulse_duration_ms, state.pulse_duration_ms, sizeof(pulse_duration_ms));
	memcpy(pulse_end_ms, state.pulse_end_ms, sizeof(pulse_end_ms));
	display_ready_state = state.ready;
	display_error = state.degraded || state.fault ||
			pattern == INDICATOR_RGB_ATTENTION;
	state.display_mode = display_mode;
	state.display_detail = display_detail;
	state.display_filled_mask = display_filled_mask;
	state.display_pulse_mask = display_pulse_mask;
	render_buzzer_locked(now);
	write_display = display_can_render_locked();
	reschedule = has_active_transient_locked(now) ||
		     state.pulse_mask != 0U ||
		     pattern == INDICATOR_RGB_ATTENTION ||
		     pattern == INDICATOR_RGB_REBOOT_PENDING ||
		     pattern == INDICATOR_RGB_FAULT;
	k_mutex_unlock(&state.lock);

	write_rgb(pattern, now);

	if (write_display) {
		display_ret = display_render_frame(display_mode, display_detail,
						   display_filled_mask,
						   display_pulse_mask,
						   pulse_duration_ms,
						   pulse_end_ms,
						   display_ready_state,
						   display_error, now);
		k_mutex_lock(&state.lock, K_FOREVER);
		if (display_ret < 0) {
			LOG_WRN("OLED display render failed: %d", display_ret);
			state.display_state = INDICATOR_DISPLAY_FAILED;
		} else {
			state.display_write_count++;
		}
		k_mutex_unlock(&state.lock);
	}

	if (reschedule) {
		schedule_render(K_MSEC(RENDER_INTERVAL_MS));
	}
}

static void render_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	render();
}

static void ensure_initialized(void)
{
	if (!state.initialized) {
		indicator_init();
	}
}

void indicator_init(void)
{
	enum indicator_display_state display_state;

	if (state.initialized) {
		return;
	}

	k_mutex_init(&state.lock);
	k_work_init_delayable(&state.work, render_work_handler);

	k_mutex_lock(&state.lock, K_FOREVER);
	state.initialized = true;
	state.display_post_write_count = 0U;
	state.display_state = display_post();
	display_state = state.display_state;
	state.hardware_enabled = rgb_available() || buzzer_available() ||
				 state.display_state == INDICATOR_DISPLAY_READY;
#ifdef CONFIG_ZTEST
	if (IS_ENABLED(CONFIG_RP2350_RELAY_6CH_INDICATORS)) {
		state.hardware_enabled = true;
	}
#endif
	state.ready = false;
	state.degraded = false;
	state.fault = false;
	state.reboot_pending = false;
	state.relay_state_mask = 0U;
	state.pulse_mask = 0U;
	memset(state.pulse_duration_ms, 0, sizeof(state.pulse_duration_ms));
	memset(state.pulse_end_ms, 0, sizeof(state.pulse_end_ms));
	state.accepted_until = 0;
	state.attention_until = 0;
	state.buzzer_pattern = INDICATOR_BUZZER_SILENT;
	state.beeps_remaining = 0U;
	state.beep_on = false;
	state.beep_on_ms = BEEP_ON_MS;
	state.beep_next_ms = 0;
	state.display_mode = state.hardware_enabled ? INDICATOR_DISPLAY_MODE_BOOT :
						      INDICATOR_DISPLAY_MODE_OFF;
	state.display_detail = INDICATOR_DISPLAY_DETAIL_NONE;
	state.attention_detail = INDICATOR_DISPLAY_DETAIL_NONE;
	state.display_filled_mask = 0U;
	state.display_pulse_mask = 0U;
	state.display_write_count = 0U;
	state.last_rgb = state.hardware_enabled ? INDICATOR_RGB_BOOTING : INDICATOR_RGB_OFF;
	state.last_buzzer = INDICATOR_BUZZER_SILENT;
	k_mutex_unlock(&state.lock);

	log_display_detection(display_state);

	if (!IS_ENABLED(CONFIG_RP2350_RELAY_6CH_INDICATORS)) {
		return;
	}

	if (!state.hardware_enabled) {
		LOG_WRN("Local status indicators enabled but no ready indicator devices found");
	}

	schedule_render_now();
}

void indicator_set_ready(bool ready)
{
	ensure_initialized();
	k_mutex_lock(&state.lock, K_FOREVER);
	if (ready && !state.ready) {
		set_buzzer_locked(INDICATOR_BUZZER_BOOT_READY);
	}
	state.ready = ready;
	k_mutex_unlock(&state.lock);
	schedule_render_now();
}

void indicator_set_relay_state(uint8_t state_mask, uint8_t pulse_mask)
{
	ensure_initialized();
	k_mutex_lock(&state.lock, K_FOREVER);
	state.relay_state_mask = state_mask;
	state.pulse_mask = pulse_mask;
	memset(state.pulse_duration_ms, 0, sizeof(state.pulse_duration_ms));
	memset(state.pulse_end_ms, 0, sizeof(state.pulse_end_ms));
	k_mutex_unlock(&state.lock);
	schedule_render_now();
}

void indicator_set_relay_timed_state(
	uint8_t state_mask, uint8_t pulse_mask,
	const struct indicator_pulse_timing pulse_timing[DISPLAY_PULSE_CHANNELS])
{
	const int64_t now = indicator_now();

	ensure_initialized();
	k_mutex_lock(&state.lock, K_FOREVER);
	state.relay_state_mask = state_mask;
	state.pulse_mask = pulse_mask;
	memset(state.pulse_duration_ms, 0, sizeof(state.pulse_duration_ms));
	memset(state.pulse_end_ms, 0, sizeof(state.pulse_end_ms));

	if (pulse_timing != NULL) {
		for (uint8_t channel = 0U; channel < DISPLAY_PULSE_CHANNELS; channel++) {
			uint8_t bit = BIT(channel);

			if ((pulse_mask & bit) == 0U ||
			    pulse_timing[channel].duration_ms == 0U ||
			    pulse_timing[channel].remaining_ms == 0U) {
				continue;
			}

			state.pulse_duration_ms[channel] = pulse_timing[channel].duration_ms;
			state.pulse_end_ms[channel] = now + pulse_timing[channel].remaining_ms;
		}
	}

	k_mutex_unlock(&state.lock);
	schedule_render_now();
}

void indicator_record_command(enum indicator_command_result result)
{
	const int64_t now = indicator_now();

	ensure_initialized();
	k_mutex_lock(&state.lock, K_FOREVER);
	switch (result) {
	case INDICATOR_COMMAND_ACCEPTED:
		state.accepted_until = now + COMMAND_TRANSIENT_MS;
		state.attention_detail = INDICATOR_DISPLAY_DETAIL_NONE;
		set_buzzer_locked(INDICATOR_BUZZER_ACCEPTED);
		break;
	case INDICATOR_COMMAND_BUSY:
		state.attention_until = now + ATTENTION_TRANSIENT_MS;
		state.attention_detail = INDICATOR_DISPLAY_DETAIL_E_BUSY;
		set_buzzer_locked(INDICATOR_BUZZER_REJECTED);
		break;
	case INDICATOR_COMMAND_REJECTED:
	default:
		state.attention_until = now + ATTENTION_TRANSIENT_MS;
		state.attention_detail = INDICATOR_DISPLAY_DETAIL_E_ARG;
		set_buzzer_locked(INDICATOR_BUZZER_REJECTED);
		break;
	}
	k_mutex_unlock(&state.lock);
	schedule_render_now();
}

void indicator_set_degraded(bool degraded)
{
	ensure_initialized();
	k_mutex_lock(&state.lock, K_FOREVER);
	state.degraded = degraded;
	k_mutex_unlock(&state.lock);
	schedule_render_now();
}

void indicator_set_fault(bool fault)
{
	ensure_initialized();
	k_mutex_lock(&state.lock, K_FOREVER);
	state.fault = fault;
	k_mutex_unlock(&state.lock);
	schedule_render_now();
}

void indicator_set_reboot_pending(bool pending)
{
	ensure_initialized();
	k_mutex_lock(&state.lock, K_FOREVER);
	state.reboot_pending = pending;
	if (pending) {
		set_buzzer_locked(INDICATOR_BUZZER_REBOOT_PENDING);
	}
	k_mutex_unlock(&state.lock);
	schedule_render_now();
}

#ifdef CONFIG_ZTEST
int64_t indicator_test_now_ms = -1;

void indicator_test_reset(void)
{
	if (state.initialized) {
		(void)k_work_cancel_delayable(&state.work);
		k_mutex_lock(&state.lock, K_FOREVER);
		state.initialized = false;
		k_mutex_unlock(&state.lock);
	}

	indicator_test_now_ms = 0;
	memset(display_test_frame, 0, sizeof(display_test_frame));
	display_test_config.orientation = DISPLAY_ORIENTATION_NORMAL;
	indicator_init();
	render();
}

void indicator_test_get_snapshot(struct indicator_test_snapshot *snapshot)
{
	if (snapshot == NULL) {
		return;
	}

	k_mutex_lock(&state.lock, K_FOREVER);
	snapshot->rgb = state.last_rgb;
	snapshot->buzzer = state.last_buzzer;
	snapshot->display_state = state.display_state;
	snapshot->display_mode = state.display_mode;
	snapshot->display_detail = state.display_detail;
	snapshot->ready = state.ready;
	snapshot->degraded = state.degraded;
	snapshot->fault = state.fault;
	snapshot->reboot_pending = state.reboot_pending;
	snapshot->relay_state_mask = state.relay_state_mask;
	snapshot->pulse_mask = state.pulse_mask;
	snapshot->display_filled_mask = state.display_filled_mask;
	snapshot->display_pulse_mask = state.display_pulse_mask;
	snapshot->display_post_write_count = state.display_post_write_count;
	snapshot->display_write_count = state.display_write_count;
	k_mutex_unlock(&state.lock);
}

void indicator_test_force_render(void)
{
	render();
}

void indicator_test_advance(uint32_t ms)
{
	if (indicator_test_now_ms < 0) {
		indicator_test_now_ms = k_uptime_get();
	}

	indicator_test_now_ms += ms;
	render();
}

bool indicator_test_display_pixel_is_set(uint8_t x, uint8_t y)
{
	if (x >= DISPLAY_WIDTH || y >= DISPLAY_HEIGHT) {
		return false;
	}

	return (display_test_frame[((y / 8U) * DISPLAY_WIDTH) + x] & BIT(y % 8U)) != 0U;
}

bool indicator_test_display_glyph_supported(char c)
{
	for (uint8_t row = 0U; row < 5U; row++) {
		if (glyph_bits(c, row) != 0U) {
			return true;
		}
	}

	return false;
}

void indicator_test_configure_display(bool supported, bool ready,
				      uint16_t width, uint16_t height,
				      uint32_t pixel_formats,
				      bool blanking_fails,
				      bool post_write_fails,
				      bool render_write_fails)
{
	display_test_config.supported = supported;
	display_test_config.ready = ready;
	display_test_config.width = width;
	display_test_config.height = height;
	display_test_config.pixel_formats = pixel_formats;
	display_test_config.blanking_fails = blanking_fails;
	display_test_config.orientation_fails = false;
	display_test_config.post_write_fails = post_write_fails;
	display_test_config.render_write_fails = render_write_fails;
	display_test_config.orientation = DISPLAY_ORIENTATION_NORMAL;
}

void indicator_test_set_display_render_failure(bool render_write_fails)
{
	display_test_config.render_write_fails = render_write_fails;
}

void indicator_test_set_display_orientation_failure(bool orientation_fails)
{
	display_test_config.orientation_fails = orientation_fails;
}

enum display_orientation indicator_test_display_orientation(void)
{
	return display_test_config.orientation;
}
#endif
