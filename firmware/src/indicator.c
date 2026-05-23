/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <rp2350_relay_6ch/indicator.h>

LOG_MODULE_REGISTER(rp2350_relay_indicator, LOG_LEVEL_INF);

#define RGB_NODE DT_ALIAS(led_strip)
#define BUZZER_NODE DT_PATH(zephyr_user)

#define COMMAND_TRANSIENT_MS 150U
#define ATTENTION_TRANSIENT_MS 600U
#define RENDER_INTERVAL_MS 100U
#define BEEP_ON_MS 60U
#define BEEP_OFF_MS 60U
#define BUZZER_PERIOD_NS PWM_USEC(1000)

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
	int64_t accepted_until;
	int64_t attention_until;
	enum indicator_buzzer_pattern buzzer_pattern;
	uint8_t beeps_remaining;
	bool beep_on;
	int64_t beep_next_ms;
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

static struct led_rgb rgb_color(enum indicator_rgb_pattern pattern, int64_t now)
{
	const bool blink_on = ((now / 250) % 2) == 0;
	const bool pulse_on = ((now / 500) % 2) == 0;

	switch (pattern) {
	case INDICATOR_RGB_BOOTING:
		return (struct led_rgb){ .r = 12U, .g = 12U, .b = 12U };
	case INDICATOR_RGB_READY:
		return (struct led_rgb){ .r = 0U, .g = 32U, .b = 0U };
	case INDICATOR_RGB_ACCEPTED:
		return (struct led_rgb){ .r = 0U, .g = 96U, .b = 0U };
	case INDICATOR_RGB_RELAY_ACTIVE:
		return (struct led_rgb){ .r = 0U, .g = 32U, .b = 64U };
	case INDICATOR_RGB_ATTENTION:
		return pulse_on ? (struct led_rgb){ .r = 80U, .g = 48U, .b = 0U } :
				  (struct led_rgb){ .r = 10U, .g = 6U, .b = 0U };
	case INDICATOR_RGB_REBOOT_PENDING:
		return pulse_on ? (struct led_rgb){ .r = 40U, .g = 0U, .b = 80U } :
				  (struct led_rgb){ .r = 0U, .g = 0U, .b = 60U };
	case INDICATOR_RGB_FAULT:
		return blink_on ? (struct led_rgb){ .r = 96U, .g = 0U, .b = 0U } :
				  (struct led_rgb){ .r = 0U, .g = 0U, .b = 0U };
	case INDICATOR_RGB_OFF:
	default:
		return (struct led_rgb){ .r = 0U, .g = 0U, .b = 0U };
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

static void set_buzzer_locked(enum indicator_buzzer_pattern pattern)
{
	if (!IS_ENABLED(CONFIG_RP2350_RELAY_6CH_BUZZER_FEEDBACK) || !state.hardware_enabled) {
		return;
	}

	switch (pattern) {
	case INDICATOR_BUZZER_ACCEPTED:
		state.beeps_remaining = 1U;
		break;
	case INDICATOR_BUZZER_REJECTED:
		state.beeps_remaining = 2U;
		break;
	case INDICATOR_BUZZER_REBOOT_PENDING:
		state.beeps_remaining = 3U;
		break;
	case INDICATOR_BUZZER_SILENT:
	default:
		state.beeps_remaining = 0U;
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
	state.beep_next_ms = now + BEEP_ON_MS;
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
	int64_t now = indicator_now();
	bool reschedule;

	k_mutex_lock(&state.lock, K_FOREVER);
	pattern = resolve_rgb_locked(now);
	state.last_rgb = pattern;
	render_buzzer_locked(now);
	reschedule = has_active_transient_locked(now) ||
		     pattern == INDICATOR_RGB_ATTENTION ||
		     pattern == INDICATOR_RGB_REBOOT_PENDING ||
		     pattern == INDICATOR_RGB_FAULT;
	k_mutex_unlock(&state.lock);

	write_rgb(pattern, now);

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
	if (state.initialized) {
		return;
	}

	k_mutex_init(&state.lock);
	k_work_init_delayable(&state.work, render_work_handler);

	k_mutex_lock(&state.lock, K_FOREVER);
	state.initialized = true;
	state.hardware_enabled = rgb_available() || buzzer_available();
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
	state.accepted_until = 0;
	state.attention_until = 0;
	state.buzzer_pattern = INDICATOR_BUZZER_SILENT;
	state.beeps_remaining = 0U;
	state.beep_on = false;
	state.beep_next_ms = 0;
	state.last_rgb = state.hardware_enabled ? INDICATOR_RGB_BOOTING : INDICATOR_RGB_OFF;
	state.last_buzzer = INDICATOR_BUZZER_SILENT;
	k_mutex_unlock(&state.lock);

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
		set_buzzer_locked(INDICATOR_BUZZER_ACCEPTED);
		break;
	case INDICATOR_COMMAND_BUSY:
	case INDICATOR_COMMAND_REJECTED:
	default:
		state.attention_until = now + ATTENTION_TRANSIENT_MS;
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
	snapshot->ready = state.ready;
	snapshot->degraded = state.degraded;
	snapshot->fault = state.fault;
	snapshot->reboot_pending = state.reboot_pending;
	snapshot->relay_state_mask = state.relay_state_mask;
	snapshot->pulse_mask = state.pulse_mask;
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
#endif
