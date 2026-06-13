/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <zephyr/drivers/display.h>

#include <rp2350_relay_6ch/health.h>
#include <rp2350_relay_6ch/indicator.h>
#include <rp2350_relay_6ch_test/health.h>
#include <rp2350_relay_6ch_test/indicator.h>

#define TEST_DISPLAY_CELL_X0 3U
#define TEST_DISPLAY_CELL_PITCH 21U
#define TEST_DISPLAY_RELAY_CELLS 6U

static struct indicator_test_snapshot snapshot(void)
{
	struct indicator_test_snapshot snap;

	indicator_test_force_render();
	indicator_test_get_snapshot(&snap);
	return snap;
}

static void publish_health(enum health_state state, uint32_t reasons,
			   enum health_reason primary_reason, uint8_t relay_mask,
			   uint8_t pulse_mask)
{
	const struct health_snapshot health = {
		.state = state,
		.reasons = reasons,
		.primary_reason = primary_reason,
		.relay_state_mask = relay_mask,
		.pulse_mask = pulse_mask,
	};

	indicator_set_health_snapshot(&health);
}

static uint8_t display_cell_x_for_channel(uint8_t channel)
{
	uint8_t cell = channel;

	if (IS_ENABLED(CONFIG_RP2350_RELAY_6CH_DISPLAY_REVERSED_RELAY_CELLS)) {
		cell = (TEST_DISPLAY_RELAY_CELLS - 1U) - channel;
	}

	return TEST_DISPLAY_CELL_X0 + (cell * TEST_DISPLAY_CELL_PITCH);
}

static void indicator_before(void *fixture)
{
	ARG_UNUSED(fixture);

	indicator_test_configure_display(false, false, 0U, 0U, 0U,
					 false, false, false);
	indicator_test_reset();
}

ZTEST_SUITE(indicator, NULL, NULL, indicator_before, NULL, NULL);

ZTEST(indicator, test_booting_to_ready)
{
	struct indicator_test_snapshot snap = snapshot();

	zassert_equal(snap.rgb, INDICATOR_RGB_BOOTING);

	publish_health(HEALTH_NORMAL, HEALTH_REASON_NONE, HEALTH_REASON_NONE, 0U, 0U);
	snap = snapshot();

	zassert_equal(snap.rgb, INDICATOR_RGB_READY);
	zassert_true(snap.ready);
	if (IS_ENABLED(CONFIG_RP2350_RELAY_6CH_BUZZER_FEEDBACK)) {
		zassert_equal(snap.buzzer, INDICATOR_BUZZER_BOOT_READY);
	} else {
		zassert_equal(snap.buzzer, INDICATOR_BUZZER_SILENT);
	}
}

ZTEST(indicator, test_health_snapshots_drive_stable_states)
{
	struct indicator_test_snapshot snap;

	publish_health(HEALTH_NORMAL, HEALTH_REASON_NONE, HEALTH_REASON_NONE, 0U, 0U);
	snap = snapshot();
	zassert_equal(snap.rgb, INDICATOR_RGB_READY);
	zassert_true(snap.ready);

	publish_health(HEALTH_RELAY_ACTIVE, HEALTH_REASON_NONE, HEALTH_REASON_NONE,
		       BIT(1), 0U);
	snap = snapshot();
	zassert_equal(snap.rgb, INDICATOR_RGB_RELAY_ACTIVE);
	zassert_equal(snap.relay_state_mask, BIT(1));

	publish_health(HEALTH_DEGRADED, HEALTH_REASON_COMM_OWNER_TIMEOUT,
		       HEALTH_REASON_COMM_OWNER_TIMEOUT, 0U, 0U);
	snap = snapshot();
	zassert_equal(snap.rgb, INDICATOR_RGB_ATTENTION);
	zassert_true(snap.degraded);
	zassert_true(snap.owner_lost);

	publish_health(HEALTH_FAULT, HEALTH_REASON_RELAY_IO_FAILED,
		       HEALTH_REASON_RELAY_IO_FAILED, 0U, 0U);
	snap = snapshot();
	zassert_equal(snap.rgb, INDICATOR_RGB_FAULT);
	zassert_true(snap.fault);

	publish_health(HEALTH_RECOVERY_PENDING, HEALTH_REASON_HOST_REBOOT_PENDING,
		       HEALTH_REASON_HOST_REBOOT_PENDING, 0U, 0U);
	snap = snapshot();
	zassert_equal(snap.rgb, INDICATOR_RGB_REBOOT_PENDING);
	zassert_true(snap.reboot_pending);
}

ZTEST(indicator, test_publish_health_snapshot_uses_current_health_model)
{
	struct indicator_test_snapshot snap;

	health_test_reset();
	health_set_relay_gpio_ready(true);
	health_set_rpc_ready(true);
	health_set_host_reboot_pending(true);
	indicator_publish_health_snapshot();
	indicator_test_force_render();
	indicator_test_get_snapshot(&snap);

	zassert_true(snap.reboot_pending);
	zassert_equal(snap.rgb, INDICATOR_RGB_REBOOT_PENDING);
	zassert_equal(snap.display_mode, INDICATOR_DISPLAY_MODE_REBOOT);
	zassert_equal(snap.display_detail, INDICATOR_DISPLAY_DETAIL_HOLD);
}

ZTEST(indicator, test_fault_snapshot_from_boot_does_not_mark_ready)
{
	struct indicator_test_snapshot snap;

	publish_health(HEALTH_FAULT, HEALTH_REASON_RELAY_GPIO_INIT_FAILED,
		       HEALTH_REASON_RELAY_GPIO_INIT_FAILED, 0U, 0U);
	snap = snapshot();

	zassert_equal(snap.rgb, INDICATOR_RGB_FAULT);
	zassert_false(snap.ready);
	zassert_true(snap.fault);
	zassert_not_equal(snap.buzzer, INDICATOR_BUZZER_BOOT_READY);
}

ZTEST(indicator, test_command_transients_do_not_override_degraded_fault_or_recovery)
{
	struct indicator_test_snapshot snap;

	publish_health(HEALTH_DEGRADED, HEALTH_REASON_COMM_OWNER_TIMEOUT,
		       HEALTH_REASON_COMM_OWNER_TIMEOUT, 0U, 0U);
	indicator_record_command(INDICATOR_COMMAND_ACCEPTED);
	snap = snapshot();
	zassert_equal(snap.rgb, INDICATOR_RGB_ATTENTION);

	publish_health(HEALTH_FAULT, HEALTH_REASON_RELAY_IO_FAILED,
		       HEALTH_REASON_RELAY_IO_FAILED, 0U, 0U);
	indicator_record_command(INDICATOR_COMMAND_ACCEPTED);
	snap = snapshot();
	zassert_equal(snap.rgb, INDICATOR_RGB_FAULT);

	publish_health(HEALTH_RECOVERY_PENDING, HEALTH_REASON_HOST_REBOOT_PENDING,
		       HEALTH_REASON_HOST_REBOOT_PENDING, 0U, 0U);
	indicator_record_command(INDICATOR_COMMAND_REJECTED);
	snap = snapshot();
	zassert_equal(snap.rgb, INDICATOR_RGB_REBOOT_PENDING);
}

ZTEST(indicator, test_boot_ready_buzzer_is_long_when_enabled)
{
	struct indicator_test_snapshot snap;

	if (!IS_ENABLED(CONFIG_RP2350_RELAY_6CH_BUZZER_FEEDBACK)) {
		ztest_test_skip();
	}

	publish_health(HEALTH_NORMAL, HEALTH_REASON_NONE, HEALTH_REASON_NONE, 0U, 0U);
	snap = snapshot();
	zassert_equal(snap.buzzer, INDICATOR_BUZZER_BOOT_READY);

	indicator_test_advance(61U);
	indicator_test_get_snapshot(&snap);
	zassert_equal(snap.buzzer, INDICATOR_BUZZER_BOOT_READY);

	indicator_test_advance(240U);
	indicator_test_get_snapshot(&snap);
	zassert_equal(snap.buzzer, INDICATOR_BUZZER_BOOT_READY);

	indicator_test_advance(61U);
	indicator_test_get_snapshot(&snap);
	zassert_equal(snap.buzzer, INDICATOR_BUZZER_SILENT);
}

ZTEST(indicator, test_boot_ready_buzzer_does_not_repeat_when_already_ready)
{
	struct indicator_test_snapshot snap;

	publish_health(HEALTH_NORMAL, HEALTH_REASON_NONE, HEALTH_REASON_NONE, 0U, 0U);
	snap = snapshot();
	if (IS_ENABLED(CONFIG_RP2350_RELAY_6CH_BUZZER_FEEDBACK)) {
		zassert_equal(snap.buzzer, INDICATOR_BUZZER_BOOT_READY);
	} else {
		zassert_equal(snap.buzzer, INDICATOR_BUZZER_SILENT);
	}

	indicator_test_advance(301U);
	indicator_test_advance(61U);
	indicator_test_get_snapshot(&snap);
	zassert_equal(snap.buzzer, INDICATOR_BUZZER_SILENT);

	publish_health(HEALTH_NORMAL, HEALTH_REASON_NONE, HEALTH_REASON_NONE, 0U, 0U);
	snap = snapshot();
	zassert_equal(snap.buzzer, INDICATOR_BUZZER_SILENT);
}

ZTEST(indicator, test_reboot_pending_buzzer_finishes_before_reset_delay)
{
	struct indicator_test_snapshot snap;

	if (!IS_ENABLED(CONFIG_RP2350_RELAY_6CH_BUZZER_FEEDBACK)) {
		ztest_test_skip();
	}

	publish_health(HEALTH_RECOVERY_PENDING, HEALTH_REASON_HOST_REBOOT_PENDING,
		       HEALTH_REASON_HOST_REBOOT_PENDING, 0U, 0U);
	snap = snapshot();
	zassert_equal(snap.buzzer, INDICATOR_BUZZER_REBOOT_PENDING);

	indicator_test_advance(61U);
	indicator_test_get_snapshot(&snap);
	zassert_equal(snap.buzzer, INDICATOR_BUZZER_REBOOT_PENDING);

	indicator_test_advance(60U);
	indicator_test_get_snapshot(&snap);
	zassert_equal(snap.buzzer, INDICATOR_BUZZER_REBOOT_PENDING);

	indicator_test_advance(61U);
	indicator_test_get_snapshot(&snap);
	zassert_equal(snap.buzzer, INDICATOR_BUZZER_REBOOT_PENDING);

	indicator_test_advance(60U);
	indicator_test_get_snapshot(&snap);
	zassert_equal(snap.buzzer, INDICATOR_BUZZER_REBOOT_PENDING);

	indicator_test_advance(61U);
	indicator_test_get_snapshot(&snap);
	zassert_equal(snap.buzzer, INDICATOR_BUZZER_REBOOT_PENDING);

	indicator_test_advance(60U);
	indicator_test_get_snapshot(&snap);
	zassert_equal(snap.buzzer, INDICATOR_BUZZER_SILENT);

	indicator_test_advance(436U);
	indicator_test_get_snapshot(&snap);
	zassert_equal(snap.buzzer, INDICATOR_BUZZER_SILENT);
}

ZTEST(indicator, test_relay_active_overrides_ready)
{
	struct indicator_test_snapshot snap;

	publish_health(HEALTH_NORMAL, HEALTH_REASON_NONE, HEALTH_REASON_NONE, 0U, 0U);
	publish_health(HEALTH_RELAY_ACTIVE, HEALTH_REASON_NONE, HEALTH_REASON_NONE,
		       BIT(0), 0U);
	snap = snapshot();

	zassert_equal(snap.rgb, INDICATOR_RGB_RELAY_ACTIVE);

	publish_health(HEALTH_RELAY_ACTIVE, HEALTH_REASON_NONE, HEALTH_REASON_NONE,
		       0U, BIT(1));
	snap = snapshot();

	zassert_equal(snap.rgb, INDICATOR_RGB_RELAY_ACTIVE);
	zassert_equal(snap.pulse_mask, BIT(1));
}

ZTEST(indicator, test_accepted_command_transient)
{
	struct indicator_test_snapshot snap;

	publish_health(HEALTH_NORMAL, HEALTH_REASON_NONE, HEALTH_REASON_NONE, 0U, 0U);
	indicator_record_command(INDICATOR_COMMAND_ACCEPTED);
	snap = snapshot();

	zassert_equal(snap.rgb, INDICATOR_RGB_ACCEPTED);

	indicator_test_advance(151U);
	indicator_test_get_snapshot(&snap);

	zassert_equal(snap.rgb, INDICATOR_RGB_READY);
}

ZTEST(indicator, test_attention_overrides_accepted)
{
	struct indicator_test_snapshot snap;

	publish_health(HEALTH_NORMAL, HEALTH_REASON_NONE, HEALTH_REASON_NONE, 0U, 0U);
	indicator_record_command(INDICATOR_COMMAND_ACCEPTED);
	indicator_record_command(INDICATOR_COMMAND_REJECTED);
	snap = snapshot();

	zassert_equal(snap.rgb, INDICATOR_RGB_ATTENTION);
}

ZTEST(indicator, test_degraded_persists_until_cleared)
{
	struct indicator_test_snapshot snap;

	publish_health(HEALTH_NORMAL, HEALTH_REASON_NONE, HEALTH_REASON_NONE, 0U, 0U);
	publish_health(HEALTH_DEGRADED, HEALTH_REASON_INDICATOR_DEGRADED,
		       HEALTH_REASON_INDICATOR_DEGRADED, 0U, 0U);
	snap = snapshot();

	zassert_equal(snap.rgb, INDICATOR_RGB_ATTENTION);
	zassert_true(snap.degraded);

	indicator_test_advance(1000U);
	publish_health(HEALTH_NORMAL, HEALTH_REASON_NONE, HEALTH_REASON_NONE, 0U, 0U);
	snap = snapshot();

	zassert_equal(snap.rgb, INDICATOR_RGB_READY);
	zassert_false(snap.degraded);
}

ZTEST(indicator, test_owner_lost_persists_until_cleared)
{
	struct indicator_test_snapshot snap;

	publish_health(HEALTH_NORMAL, HEALTH_REASON_NONE, HEALTH_REASON_NONE, 0U, 0U);
	publish_health(HEALTH_DEGRADED, HEALTH_REASON_COMM_OWNER_TIMEOUT,
		       HEALTH_REASON_COMM_OWNER_TIMEOUT, 0U, 0U);
	snap = snapshot();

	zassert_equal(snap.rgb, INDICATOR_RGB_ATTENTION);
	zassert_true(snap.owner_lost);
	zassert_true(snap.degraded);

	indicator_test_advance(1000U);
	publish_health(HEALTH_NORMAL, HEALTH_REASON_NONE, HEALTH_REASON_NONE, 0U, 0U);
	snap = snapshot();

	zassert_equal(snap.rgb, INDICATOR_RGB_READY);
	zassert_false(snap.owner_lost);
}

ZTEST(indicator, test_owner_lost_buzzer_is_three_fault_pulses_when_enabled)
{
	struct indicator_test_snapshot snap;

	if (!IS_ENABLED(CONFIG_RP2350_RELAY_6CH_BUZZER_FEEDBACK)) {
		ztest_test_skip();
	}

	publish_health(HEALTH_DEGRADED, HEALTH_REASON_COMM_OWNER_TIMEOUT,
		       HEALTH_REASON_COMM_OWNER_TIMEOUT, 0U, 0U);
	snap = snapshot();
	zassert_equal(snap.buzzer, INDICATOR_BUZZER_OWNER_LOST);
	zassert_true(snap.buzzer_on);
	zassert_equal(snap.beeps_remaining, 2U);

	indicator_test_advance(251U);
	indicator_test_get_snapshot(&snap);
	zassert_equal(snap.buzzer, INDICATOR_BUZZER_OWNER_LOST);
	zassert_false(snap.buzzer_on);
	zassert_equal(snap.beeps_remaining, 2U);

	indicator_test_advance(250U);
	indicator_test_get_snapshot(&snap);
	zassert_equal(snap.buzzer, INDICATOR_BUZZER_OWNER_LOST);
	zassert_true(snap.buzzer_on);
	zassert_equal(snap.beeps_remaining, 1U);

	indicator_test_advance(251U);
	indicator_test_get_snapshot(&snap);
	zassert_equal(snap.buzzer, INDICATOR_BUZZER_OWNER_LOST);
	zassert_false(snap.buzzer_on);
	zassert_equal(snap.beeps_remaining, 1U);

	indicator_test_advance(250U);
	indicator_test_get_snapshot(&snap);
	zassert_equal(snap.buzzer, INDICATOR_BUZZER_OWNER_LOST);
	zassert_true(snap.buzzer_on);
	zassert_equal(snap.beeps_remaining, 0U);

	indicator_test_advance(251U);
	indicator_test_get_snapshot(&snap);
	zassert_equal(snap.buzzer, INDICATOR_BUZZER_OWNER_LOST);
	zassert_false(snap.buzzer_on);
	zassert_equal(snap.beeps_remaining, 0U);

	indicator_test_advance(250U);
	indicator_test_get_snapshot(&snap);
	zassert_equal(snap.buzzer, INDICATOR_BUZZER_SILENT);
	zassert_false(snap.buzzer_on);
	zassert_equal(snap.beeps_remaining, 0U);
}

ZTEST(indicator, test_owner_lost_buzzer_does_not_repeat_while_latched)
{
	struct indicator_test_snapshot snap;

	if (!IS_ENABLED(CONFIG_RP2350_RELAY_6CH_BUZZER_FEEDBACK)) {
		ztest_test_skip();
	}

	publish_health(HEALTH_DEGRADED, HEALTH_REASON_COMM_OWNER_TIMEOUT,
		       HEALTH_REASON_COMM_OWNER_TIMEOUT, 0U, 0U);
	snap = snapshot();
	zassert_equal(snap.buzzer, INDICATOR_BUZZER_OWNER_LOST);

	indicator_test_advance(251U);
	indicator_test_advance(250U);
	indicator_test_advance(251U);
	indicator_test_advance(250U);
	indicator_test_advance(251U);
	indicator_test_advance(250U);
	indicator_test_get_snapshot(&snap);
	zassert_equal(snap.buzzer, INDICATOR_BUZZER_SILENT);

	publish_health(HEALTH_DEGRADED, HEALTH_REASON_COMM_OWNER_TIMEOUT,
		       HEALTH_REASON_COMM_OWNER_TIMEOUT, 0U, 0U);
	snap = snapshot();
	zassert_equal(snap.buzzer, INDICATOR_BUZZER_SILENT);

	publish_health(HEALTH_NORMAL, HEALTH_REASON_NONE, HEALTH_REASON_NONE, 0U, 0U);
	publish_health(HEALTH_DEGRADED, HEALTH_REASON_COMM_OWNER_TIMEOUT,
		       HEALTH_REASON_COMM_OWNER_TIMEOUT, 0U, 0U);
	snap = snapshot();
	zassert_equal(snap.buzzer, INDICATOR_BUZZER_OWNER_LOST);
}

ZTEST(indicator, test_reboot_pending_priority)
{
	struct indicator_test_snapshot snap;

	publish_health(HEALTH_NORMAL, HEALTH_REASON_NONE, HEALTH_REASON_NONE, 0U, 0U);
	indicator_record_command(INDICATOR_COMMAND_REJECTED);
	publish_health(HEALTH_RECOVERY_PENDING, HEALTH_REASON_HOST_REBOOT_PENDING,
		       HEALTH_REASON_HOST_REBOOT_PENDING, 0U, 0U);
	snap = snapshot();

	zassert_equal(snap.rgb, INDICATOR_RGB_REBOOT_PENDING);
	zassert_true(snap.reboot_pending);
}

ZTEST(indicator, test_fault_highest_priority)
{
	struct indicator_test_snapshot snap;

	publish_health(HEALTH_NORMAL, HEALTH_REASON_NONE, HEALTH_REASON_NONE, 0U, 0U);
	publish_health(HEALTH_RELAY_ACTIVE, HEALTH_REASON_NONE, HEALTH_REASON_NONE,
		       BIT(0), 0U);
	publish_health(HEALTH_RECOVERY_PENDING, HEALTH_REASON_HOST_REBOOT_PENDING,
		       HEALTH_REASON_HOST_REBOOT_PENDING, 0U, 0U);
	publish_health(HEALTH_FAULT, HEALTH_REASON_RELAY_IO_FAILED,
		       HEALTH_REASON_RELAY_IO_FAILED, 0U, 0U);
	snap = snapshot();

	zassert_equal(snap.rgb, INDICATOR_RGB_FAULT);
	zassert_true(snap.fault);
}

ZTEST(indicator, test_accepted_command_buzzer_is_silent_by_default)
{
	struct indicator_test_snapshot snap;

	indicator_record_command(INDICATOR_COMMAND_ACCEPTED);
	snap = snapshot();

	if (IS_ENABLED(CONFIG_RP2350_RELAY_6CH_BUZZER_ACCEPTED_FEEDBACK)) {
		zassert_equal(snap.buzzer, INDICATOR_BUZZER_ACCEPTED);
		zassert_true(snap.buzzer_on);
	} else {
		zassert_equal(snap.buzzer, INDICATOR_BUZZER_SILENT);
		zassert_false(snap.buzzer_on);
	}
}

ZTEST(indicator, test_buzzer_feedback_mapping_when_enabled)
{
	struct indicator_test_snapshot snap;

	if (!IS_ENABLED(CONFIG_RP2350_RELAY_6CH_BUZZER_FEEDBACK)) {
		ztest_test_skip();
	}

	indicator_record_command(INDICATOR_COMMAND_ACCEPTED);
	snap = snapshot();
	if (IS_ENABLED(CONFIG_RP2350_RELAY_6CH_BUZZER_ACCEPTED_FEEDBACK)) {
		zassert_equal(snap.buzzer, INDICATOR_BUZZER_ACCEPTED);
	} else {
		zassert_equal(snap.buzzer, INDICATOR_BUZZER_SILENT);
	}

	indicator_record_command(INDICATOR_COMMAND_BUSY);
	snap = snapshot();
	zassert_equal(snap.buzzer, INDICATOR_BUZZER_REJECTED);

	publish_health(HEALTH_DEGRADED, HEALTH_REASON_COMM_OWNER_TIMEOUT,
		       HEALTH_REASON_COMM_OWNER_TIMEOUT, 0U, 0U);
	snap = snapshot();
	zassert_equal(snap.buzzer, INDICATOR_BUZZER_OWNER_LOST);

	publish_health(HEALTH_RECOVERY_PENDING, HEALTH_REASON_HOST_REBOOT_PENDING,
		       HEALTH_REASON_HOST_REBOOT_PENDING, 0U, 0U);
	snap = snapshot();
	zassert_equal(snap.buzzer, INDICATOR_BUZZER_REBOOT_PENDING);
}

ZTEST(indicator, test_display_unsupported_without_configured_device)
{
	struct indicator_test_snapshot snap = snapshot();

	zassert_equal(snap.display_state, INDICATOR_DISPLAY_UNSUPPORTED);
	zassert_equal(snap.display_mode, INDICATOR_DISPLAY_MODE_BOOT);
	zassert_equal(snap.display_write_count, 0U);
}

ZTEST(indicator, test_display_not_detected_is_not_fault)
{
	struct indicator_test_snapshot snap;

	indicator_test_configure_display(true, false, 128U, 64U,
					 PIXEL_FORMAT_MONO01, false, false, false);
	indicator_test_reset();
	snap = snapshot();

	zassert_equal(snap.display_state, INDICATOR_DISPLAY_NOT_DETECTED);
	zassert_equal(snap.rgb, INDICATOR_RGB_BOOTING);
	zassert_false(snap.degraded);
	zassert_false(snap.fault);
	zassert_equal(snap.display_write_count, 0U);
}

ZTEST(indicator, test_display_post_success_and_ready_render)
{
	struct indicator_test_snapshot snap;

	indicator_test_configure_display(true, true, 128U, 64U,
					 PIXEL_FORMAT_MONO01, false, false, false);
	indicator_test_reset();
	publish_health(HEALTH_NORMAL, HEALTH_REASON_NONE, HEALTH_REASON_NONE, 0U, 0U);
	snap = snapshot();

	zassert_equal(snap.display_state, INDICATOR_DISPLAY_READY);
	zassert_equal(snap.display_mode, INDICATOR_DISPLAY_MODE_READY);
	zassert_equal(snap.display_detail, INDICATOR_DISPLAY_DETAIL_OK);
	zassert_equal(snap.display_filled_mask, 0U);
	zassert_equal(snap.display_pulse_mask, 0U);
	zassert_equal(snap.display_clear_write_count, 1U);
	zassert_equal(snap.display_post_write_count, 1U);
	zassert_true(snap.display_write_count > 0U);
	zassert_true(indicator_test_display_pixel_is_set(115U, 54U));
	zassert_false(indicator_test_display_pixel_is_set(125U, 54U));
}

ZTEST(indicator, test_display_post_clears_retained_reset_pixels_before_render)
{
	struct indicator_test_snapshot snap;

	indicator_test_configure_display(true, true, 128U, 64U,
					 PIXEL_FORMAT_MONO01, false, false, false);
	indicator_test_seed_display_frame(0xff);
	indicator_test_reset();
	snap = snapshot();

	zassert_equal(snap.display_state, INDICATOR_DISPLAY_READY);
	zassert_equal(snap.display_clear_write_count, 1U);
	zassert_equal(snap.display_post_write_count, 1U);
	zassert_true(snap.display_write_count > 0U);
	zassert_false(indicator_test_display_pixel_is_set(127U, 63U));
}

ZTEST(indicator, test_shutdown_outputs_clears_local_indicators)
{
	struct indicator_test_snapshot snap;

	indicator_test_configure_display(true, true, 128U, 64U,
					 PIXEL_FORMAT_MONO01, false, false, false);
	indicator_test_reset();
	indicator_test_seed_display_frame(0xff);
	indicator_record_command(INDICATOR_COMMAND_REJECTED);
	indicator_test_force_render();

	indicator_shutdown_outputs();
	indicator_test_get_snapshot(&snap);

	zassert_equal(snap.rgb, INDICATOR_RGB_OFF);
	zassert_equal(snap.buzzer, INDICATOR_BUZZER_SILENT);
	zassert_false(snap.buzzer_on);
	zassert_equal(snap.beeps_remaining, 0U);
	zassert_equal(snap.display_mode, INDICATOR_DISPLAY_MODE_OFF);
	zassert_equal(snap.display_clear_write_count, 2U);
	zassert_true(snap.display_blanking_on);
	zassert_false(indicator_test_display_pixel_is_set(0U, 0U));
	zassert_false(indicator_test_display_pixel_is_set(127U, 63U));
}

ZTEST(indicator, test_shutdown_outputs_tolerates_display_failures)
{
	struct indicator_test_snapshot snap;

	indicator_test_configure_display(true, true, 128U, 64U,
					 PIXEL_FORMAT_MONO01, false, false, false);
	indicator_test_reset();
	indicator_test_set_display_clear_failure(true);
	indicator_test_set_display_blanking_on_failure(true);

	indicator_shutdown_outputs();
	indicator_test_get_snapshot(&snap);

	zassert_equal(snap.rgb, INDICATOR_RGB_OFF);
	zassert_equal(snap.buzzer, INDICATOR_BUZZER_SILENT);
	zassert_equal(snap.display_mode, INDICATOR_DISPLAY_MODE_OFF);
}

ZTEST(indicator, test_display_post_sets_configured_orientation)
{
	indicator_test_configure_display(true, true, 128U, 64U,
					 PIXEL_FORMAT_MONO01, false, false, false);
	indicator_test_reset();

	if (IS_ENABLED(CONFIG_RP2350_RELAY_6CH_DISPLAY_ROTATED_180)) {
		zassert_equal(indicator_test_display_orientation(),
			      DISPLAY_ORIENTATION_ROTATED_180);
	} else {
		zassert_equal(indicator_test_display_orientation(),
			      DISPLAY_ORIENTATION_NORMAL);
	}
}

ZTEST(indicator, test_display_glyphs_cover_fixed_label_vocabulary)
{
	const char required[] = "0123456:*ABCDEFGHIJKLMNOPQRSTUVWXYZ";

	for (size_t idx = 0U; idx < ARRAY_SIZE(required) - 1U; idx++) {
		zassert_true(indicator_test_display_glyph_supported(required[idx]),
			     "missing OLED glyph for %c", required[idx]);
	}
}

ZTEST(indicator, test_display_draws_static_usb_annunciator)
{
	indicator_test_configure_display(true, true, 128U, 64U,
					 PIXEL_FORMAT_MONO01, false, false, false);
	indicator_test_reset();
	publish_health(HEALTH_NORMAL, HEALTH_REASON_NONE, HEALTH_REASON_NONE, 0U, 0U);
	(void)snapshot();

	zassert_true(indicator_test_display_pixel_is_set(3U, 3U));
	zassert_true(indicator_test_display_pixel_is_set(7U, 3U));
	zassert_true(indicator_test_display_pixel_is_set(11U, 3U));
	zassert_false(indicator_test_display_pixel_is_set(0U, 3U));
	zassert_false(indicator_test_display_pixel_is_set(3U, 1U));
}

ZTEST(indicator, test_display_draws_active_annunciator_for_pulse_only_state)
{
	struct indicator_test_snapshot snap;

	indicator_test_configure_display(true, true, 128U, 64U,
					 PIXEL_FORMAT_MONO01, false, false, false);
	indicator_test_reset();
	publish_health(HEALTH_RELAY_ACTIVE, HEALTH_REASON_NONE, HEALTH_REASON_NONE,
		       0U, BIT(1));
	snap = snapshot();

	zassert_equal(snap.display_mode, INDICATOR_DISPLAY_MODE_ACTIVE);
	zassert_equal(snap.display_detail, INDICATOR_DISPLAY_DETAIL_P2);
	zassert_true(indicator_test_display_pixel_is_set(56U, 3U));
	zassert_true(indicator_test_display_pixel_is_set(62U, 3U));
	zassert_true(indicator_test_display_pixel_is_set(68U, 3U));
}

ZTEST(indicator, test_display_status_band_aligns_to_inset_column)
{
	indicator_test_configure_display(true, true, 128U, 64U,
					 PIXEL_FORMAT_MONO01, false, false, false);
	indicator_test_reset();
	publish_health(HEALTH_RELAY_ACTIVE, HEALTH_REASON_NONE, HEALTH_REASON_NONE,
		       BIT(0), 0U);
	(void)snapshot();

	zassert_true(indicator_test_display_pixel_is_set(4U, 54U));
	zassert_false(indicator_test_display_pixel_is_set(0U, 54U));
	zassert_true(indicator_test_display_pixel_is_set(116U, 54U));
	zassert_true(indicator_test_display_pixel_is_set(124U, 60U));
	zassert_false(indicator_test_display_pixel_is_set(125U, 60U));
}

ZTEST(indicator, test_display_draws_floorplan_rules_and_cell_origins)
{
	indicator_test_configure_display(true, true, 128U, 64U,
					 PIXEL_FORMAT_MONO01, false, false, false);
	indicator_test_reset();
	publish_health(HEALTH_NORMAL, HEALTH_REASON_NONE, HEALTH_REASON_NONE, 0U, 0U);
	(void)snapshot();

	zassert_true(indicator_test_display_pixel_is_set(3U, 13U));
	zassert_true(indicator_test_display_pixel_is_set(124U, 13U));
	zassert_false(indicator_test_display_pixel_is_set(2U, 13U));
	zassert_true(indicator_test_display_pixel_is_set(3U, 50U));
	zassert_true(indicator_test_display_pixel_is_set(124U, 50U));
	zassert_false(indicator_test_display_pixel_is_set(125U, 50U));

	zassert_true(indicator_test_display_pixel_is_set(3U, 18U));
	zassert_true(indicator_test_display_pixel_is_set(24U, 18U));
	zassert_true(indicator_test_display_pixel_is_set(45U, 18U));
	zassert_true(indicator_test_display_pixel_is_set(66U, 18U));
	zassert_true(indicator_test_display_pixel_is_set(87U, 18U));
	zassert_true(indicator_test_display_pixel_is_set(108U, 18U));

	zassert_true(indicator_test_display_pixel_is_set(19U, 44U));
	zassert_true(indicator_test_display_pixel_is_set(124U, 44U));
}

ZTEST(indicator, test_display_rejects_bad_geometry)
{
	struct indicator_test_snapshot snap;

	indicator_test_configure_display(true, true, 128U, 32U,
					 PIXEL_FORMAT_MONO01, false, false, false);
	indicator_test_reset();
	snap = snapshot();

	zassert_equal(snap.display_state, INDICATOR_DISPLAY_FAILED);
	zassert_equal(snap.display_post_write_count, 0U);
	zassert_equal(snap.display_write_count, 0U);
	zassert_false(snap.degraded);
	zassert_false(snap.fault);
}

ZTEST(indicator, test_display_rejects_blanking_on_failure)
{
	struct indicator_test_snapshot snap;

	indicator_test_configure_display(true, true, 128U, 64U,
					 PIXEL_FORMAT_MONO01, false, false, false);
	indicator_test_set_display_blanking_on_failure(true);
	indicator_test_reset();
	snap = snapshot();

	zassert_equal(snap.display_state, INDICATOR_DISPLAY_FAILED);
	zassert_equal(snap.display_clear_write_count, 0U);
	zassert_equal(snap.display_post_write_count, 0U);
	zassert_equal(snap.display_write_count, 0U);
}

ZTEST(indicator, test_display_rejects_blanking_failure)
{
	struct indicator_test_snapshot snap;

	indicator_test_configure_display(true, true, 128U, 64U,
					 PIXEL_FORMAT_MONO01, true, false, false);
	indicator_test_reset();
	snap = snapshot();

	zassert_equal(snap.display_state, INDICATOR_DISPLAY_FAILED);
	zassert_equal(snap.display_clear_write_count, 1U);
	zassert_equal(snap.display_post_write_count, 0U);
	zassert_equal(snap.display_write_count, 0U);
}

ZTEST(indicator, test_display_rejects_orientation_failure)
{
	struct indicator_test_snapshot snap;

	indicator_test_configure_display(true, true, 128U, 64U,
					 PIXEL_FORMAT_MONO01, false, false, false);
	indicator_test_set_display_orientation_failure(true);
	indicator_test_reset();
	snap = snapshot();

	zassert_equal(snap.display_state, INDICATOR_DISPLAY_FAILED);
	zassert_equal(snap.display_clear_write_count, 0U);
	zassert_equal(snap.display_post_write_count, 0U);
	zassert_equal(snap.display_write_count, 0U);
}

ZTEST(indicator, test_display_rejects_clear_write_failure)
{
	struct indicator_test_snapshot snap;

	indicator_test_configure_display(true, true, 128U, 64U,
					 PIXEL_FORMAT_MONO01, false, false, false);
	indicator_test_set_display_clear_failure(true);
	indicator_test_reset();
	snap = snapshot();

	zassert_equal(snap.display_state, INDICATOR_DISPLAY_FAILED);
	zassert_equal(snap.display_clear_write_count, 0U);
	zassert_equal(snap.display_post_write_count, 0U);
	zassert_equal(snap.display_write_count, 0U);
}

ZTEST(indicator, test_display_rejects_post_write_failure)
{
	struct indicator_test_snapshot snap;

	indicator_test_configure_display(true, true, 128U, 64U,
					 PIXEL_FORMAT_MONO01, false, true, false);
	indicator_test_reset();
	snap = snapshot();

	zassert_equal(snap.display_state, INDICATOR_DISPLAY_FAILED);
	zassert_equal(snap.display_clear_write_count, 1U);
	zassert_equal(snap.display_post_write_count, 0U);
	zassert_equal(snap.display_write_count, 0U);
}

ZTEST(indicator, test_display_render_failure_fails_until_reboot)
{
	struct indicator_test_snapshot snap;
	uint16_t writes_before;

	indicator_test_configure_display(true, true, 128U, 64U,
					 PIXEL_FORMAT_MONO01, false, false, false);
	indicator_test_reset();
	snap = snapshot();
	zassert_equal(snap.display_state, INDICATOR_DISPLAY_READY);
	writes_before = snap.display_write_count;

	indicator_test_set_display_render_failure(true);
	publish_health(HEALTH_NORMAL, HEALTH_REASON_NONE, HEALTH_REASON_NONE, 0U, 0U);
	snap = snapshot();
	zassert_equal(snap.display_state, INDICATOR_DISPLAY_FAILED);
	zassert_equal(snap.display_write_count, writes_before);

	indicator_test_set_display_render_failure(false);
	publish_health(HEALTH_RELAY_ACTIVE, HEALTH_REASON_NONE, HEALTH_REASON_NONE,
		       BIT(0), 0U);
	snap = snapshot();
	zassert_equal(snap.display_state, INDICATOR_DISPLAY_FAILED);
	zassert_equal(snap.display_write_count, writes_before);
}

ZTEST(indicator, test_display_relay_cells_and_pulse_detail)
{
	struct indicator_test_snapshot snap;

	indicator_test_configure_display(true, true, 128U, 64U,
					 PIXEL_FORMAT_MONO01, false, false, false);
	indicator_test_reset();
	publish_health(HEALTH_NORMAL, HEALTH_REASON_NONE, HEALTH_REASON_NONE, 0U, 0U);
	publish_health(HEALTH_RELAY_ACTIVE, HEALTH_REASON_NONE, HEALTH_REASON_NONE,
		       BIT(0) | BIT(5), BIT(1));
	snap = snapshot();

	zassert_equal(snap.display_state, INDICATOR_DISPLAY_READY);
	zassert_equal(snap.display_mode, INDICATOR_DISPLAY_MODE_ACTIVE);
	zassert_equal(snap.display_detail, INDICATOR_DISPLAY_DETAIL_P2);
	zassert_equal(snap.display_filled_mask, BIT(0) | BIT(1) | BIT(5));
	zassert_equal(snap.display_pulse_mask, BIT(1));
}

ZTEST(indicator, test_display_active_overrides_accepted_command_transient)
{
	struct indicator_test_snapshot snap;

	indicator_test_configure_display(true, true, 128U, 64U,
					 PIXEL_FORMAT_MONO01, false, false, false);
	indicator_test_reset();
	publish_health(HEALTH_NORMAL, HEALTH_REASON_NONE, HEALTH_REASON_NONE, 0U, 0U);
	publish_health(HEALTH_RELAY_ACTIVE, HEALTH_REASON_NONE, HEALTH_REASON_NONE,
		       BIT(0), 0U);
	indicator_record_command(INDICATOR_COMMAND_ACCEPTED);
	snap = snapshot();

	zassert_equal(snap.rgb, INDICATOR_RGB_ACCEPTED);
	zassert_equal(snap.display_mode, INDICATOR_DISPLAY_MODE_ACTIVE);
	zassert_equal(snap.display_detail, INDICATOR_DISPLAY_DETAIL_OK);
	zassert_equal(snap.display_filled_mask, BIT(0));
	zassert_equal(snap.display_pulse_mask, 0U);
}

ZTEST(indicator, test_display_pulse_detail_overrides_accepted_command_transient)
{
	struct indicator_test_snapshot snap;

	indicator_test_configure_display(true, true, 128U, 64U,
					 PIXEL_FORMAT_MONO01, false, false, false);
	indicator_test_reset();
	publish_health(HEALTH_NORMAL, HEALTH_REASON_NONE, HEALTH_REASON_NONE, 0U, 0U);
	publish_health(HEALTH_RELAY_ACTIVE, HEALTH_REASON_NONE, HEALTH_REASON_NONE,
		       0U, BIT(1));
	indicator_record_command(INDICATOR_COMMAND_ACCEPTED);
	snap = snapshot();

	zassert_equal(snap.rgb, INDICATOR_RGB_ACCEPTED);
	zassert_equal(snap.display_mode, INDICATOR_DISPLAY_MODE_ACTIVE);
	zassert_equal(snap.display_detail, INDICATOR_DISPLAY_DETAIL_P2);
	zassert_equal(snap.display_filled_mask, BIT(1));
	zassert_equal(snap.display_pulse_mask, BIT(1));
}

ZTEST(indicator, test_display_reverses_labels_in_filled_relay_cells)
{
	const uint8_t cell_x = display_cell_x_for_channel(0U);

	indicator_test_configure_display(true, true, 128U, 64U,
					 PIXEL_FORMAT_MONO01, false, false, false);
	indicator_test_reset();
	publish_health(HEALTH_RELAY_ACTIVE, HEALTH_REASON_NONE, HEALTH_REASON_NONE,
		       BIT(0), 0U);
	(void)snapshot();

	zassert_true(indicator_test_display_pixel_is_set(cell_x, 18U));
	zassert_false(indicator_test_display_pixel_is_set(cell_x + 8U, 33U));
	zassert_true(indicator_test_display_pixel_is_set(cell_x + 5U, 33U));
	zassert_true(indicator_test_display_pixel_is_set(cell_x + 9U, 31U));
}

ZTEST(indicator, test_display_optional_reversed_relay_cells_follow_hardware_order)
{
	if (!IS_ENABLED(CONFIG_RP2350_RELAY_6CH_DISPLAY_REVERSED_RELAY_CELLS)) {
		return;
	}

	indicator_test_configure_display(true, true, 128U, 64U,
					 PIXEL_FORMAT_MONO01, false, false, false);
	indicator_test_reset();
	publish_health(HEALTH_RELAY_ACTIVE, HEALTH_REASON_NONE, HEALTH_REASON_NONE,
		       BIT(5), 0U);
	(void)snapshot();

	zassert_true(indicator_test_display_pixel_is_set(4U, 19U));
	zassert_false(indicator_test_display_pixel_is_set(109U, 19U));

	indicator_test_reset();
	publish_health(HEALTH_RELAY_ACTIVE, HEALTH_REASON_NONE, HEALTH_REASON_NONE,
		       BIT(0), 0U);
	(void)snapshot();

	zassert_false(indicator_test_display_pixel_is_set(4U, 19U));
	zassert_true(indicator_test_display_pixel_is_set(109U, 19U));
}

ZTEST(indicator, test_display_draws_visible_pulse_mark)
{
	const uint8_t cell_x = display_cell_x_for_channel(0U);

	indicator_test_configure_display(true, true, 128U, 64U,
					 PIXEL_FORMAT_MONO01, false, false, false);
	indicator_test_reset();
	publish_health(HEALTH_RELAY_ACTIVE, HEALTH_REASON_NONE, HEALTH_REASON_NONE,
		       0U, BIT(0));
	(void)snapshot();

	zassert_true(indicator_test_display_pixel_is_set(cell_x, 18U));
	zassert_false(indicator_test_display_pixel_is_set(cell_x + 11U, 22U));
	zassert_false(indicator_test_display_pixel_is_set(cell_x + 12U, 23U));
	zassert_false(indicator_test_display_pixel_is_set(cell_x + 14U, 22U));
	zassert_false(indicator_test_display_pixel_is_set(cell_x + 15U, 23U));
	zassert_true(indicator_test_display_pixel_is_set(cell_x + 8U, 27U));
	zassert_true(indicator_test_display_pixel_is_set(cell_x + 11U, 24U));
}

ZTEST(indicator, test_display_blinks_pulse_mark_without_clearing_cell)
{
	const uint8_t cell_x = display_cell_x_for_channel(0U);
	struct indicator_test_snapshot snap;
	uint16_t writes_before;

	indicator_test_configure_display(true, true, 128U, 64U,
					 PIXEL_FORMAT_MONO01, false, false, false);
	indicator_test_reset();
	publish_health(HEALTH_RELAY_ACTIVE, HEALTH_REASON_NONE, HEALTH_REASON_NONE,
		       0U, BIT(0));
	snap = snapshot();
	writes_before = snap.display_write_count;

	zassert_equal(snap.display_mode, INDICATOR_DISPLAY_MODE_ACTIVE);
	zassert_equal(snap.display_detail, INDICATOR_DISPLAY_DETAIL_P1);
	zassert_true(indicator_test_display_pixel_is_set(cell_x, 18U));
	zassert_false(indicator_test_display_pixel_is_set(cell_x + 11U, 22U));
	zassert_false(indicator_test_display_pixel_is_set(cell_x + 14U, 22U));

	indicator_test_advance(500U);
	indicator_test_get_snapshot(&snap);

	zassert_true(snap.display_write_count > writes_before);
	zassert_equal(snap.display_mode, INDICATOR_DISPLAY_MODE_ACTIVE);
	zassert_equal(snap.display_detail, INDICATOR_DISPLAY_DETAIL_P1);
	zassert_true(indicator_test_display_pixel_is_set(cell_x, 18U));
	zassert_true(indicator_test_display_pixel_is_set(cell_x + 11U, 22U));
	zassert_true(indicator_test_display_pixel_is_set(cell_x + 14U, 22U));
}

ZTEST(indicator, test_display_draws_full_pulse_countdown_lane)
{
	const uint8_t cell_x = display_cell_x_for_channel(0U);
	struct indicator_pulse_timing timing[6] = {
		[0] = { .duration_ms = 1000U, .remaining_ms = 1000U },
	};

	indicator_test_configure_display(true, true, 128U, 64U,
					 PIXEL_FORMAT_MONO01, false, false, false);
	indicator_test_reset();
	publish_health(HEALTH_RELAY_ACTIVE, HEALTH_REASON_NONE, HEALTH_REASON_NONE,
		       0U, BIT(0));
	indicator_set_relay_timed_state(0U, BIT(0), timing);
	(void)snapshot();

	zassert_false(indicator_test_display_pixel_is_set(cell_x, 45U));
	zassert_true(indicator_test_display_pixel_is_set(cell_x + 1U, 46U));
	zassert_true(indicator_test_display_pixel_is_set(cell_x + 8U, 46U));
	zassert_true(indicator_test_display_pixel_is_set(cell_x + 15U, 47U));
	zassert_false(indicator_test_display_pixel_is_set(cell_x + 16U, 46U));
	zassert_false(indicator_test_display_pixel_is_set(cell_x + 8U, 48U));
}

ZTEST(indicator, test_display_timing_metadata_does_not_change_health_masks)
{
	struct indicator_pulse_timing timing[6] = {
		[0] = { .duration_ms = 1000U, .remaining_ms = 1000U },
	};
	struct indicator_test_snapshot snap;

	indicator_test_configure_display(true, true, 128U, 64U,
					 PIXEL_FORMAT_MONO01, false, false, false);
	indicator_test_reset();
	publish_health(HEALTH_NORMAL, HEALTH_REASON_NONE, HEALTH_REASON_NONE,
		       0U, 0U);
	indicator_set_relay_timed_state(BIT(0), BIT(0), timing);
	snap = snapshot();

	zassert_equal(snap.rgb, INDICATOR_RGB_READY);
	zassert_equal(snap.display_mode, INDICATOR_DISPLAY_MODE_READY);
	zassert_equal(snap.relay_state_mask, 0U);
	zassert_equal(snap.pulse_mask, 0U);
	zassert_equal(snap.display_filled_mask, 0U);
	zassert_equal(snap.display_pulse_mask, 0U);
	zassert_false(indicator_test_display_pixel_is_set(
		display_cell_x_for_channel(0U) + 1U, 46U));
}

ZTEST(indicator, test_display_center_drains_pulse_countdown_lane)
{
	const uint8_t cell_x = display_cell_x_for_channel(0U);
	struct indicator_pulse_timing timing[6] = {
		[0] = { .duration_ms = 1000U, .remaining_ms = 1000U },
	};

	indicator_test_configure_display(true, true, 128U, 64U,
					 PIXEL_FORMAT_MONO01, false, false, false);
	indicator_test_reset();
	publish_health(HEALTH_RELAY_ACTIVE, HEALTH_REASON_NONE, HEALTH_REASON_NONE,
		       0U, BIT(0));
	indicator_set_relay_timed_state(0U, BIT(0), timing);
	(void)snapshot();

	indicator_test_advance(500U);

	zassert_false(indicator_test_display_pixel_is_set(cell_x + 1U, 46U));
	zassert_true(indicator_test_display_pixel_is_set(cell_x + 5U, 46U));
	zassert_true(indicator_test_display_pixel_is_set(cell_x + 8U, 46U));
	zassert_true(indicator_test_display_pixel_is_set(cell_x + 11U, 47U));
	zassert_false(indicator_test_display_pixel_is_set(cell_x + 15U, 47U));
}

ZTEST(indicator, test_display_removes_elapsed_pulse_countdown_lane)
{
	const uint8_t cell_x = display_cell_x_for_channel(0U);
	struct indicator_pulse_timing timing[6] = {
		[0] = { .duration_ms = 1000U, .remaining_ms = 1000U },
	};

	indicator_test_configure_display(true, true, 128U, 64U,
					 PIXEL_FORMAT_MONO01, false, false, false);
	indicator_test_reset();
	publish_health(HEALTH_RELAY_ACTIVE, HEALTH_REASON_NONE, HEALTH_REASON_NONE,
		       0U, BIT(0));
	indicator_set_relay_timed_state(0U, BIT(0), timing);
	(void)snapshot();

	indicator_test_advance(1000U);

	zassert_false(indicator_test_display_pixel_is_set(cell_x + 1U, 46U));
	zassert_false(indicator_test_display_pixel_is_set(cell_x + 8U, 46U));
	zassert_false(indicator_test_display_pixel_is_set(cell_x + 15U, 47U));
	zassert_true(indicator_test_display_pixel_is_set(cell_x, 18U));
}

ZTEST(indicator, test_display_draws_countdown_for_each_pulsing_cell)
{
	const uint8_t ch1_cell_x = display_cell_x_for_channel(0U);
	const uint8_t ch2_cell_x = display_cell_x_for_channel(1U);
	const uint8_t ch4_cell_x = display_cell_x_for_channel(3U);
	struct indicator_pulse_timing timing[6] = {
		[0] = { .duration_ms = 1000U, .remaining_ms = 1000U },
		[3] = { .duration_ms = 2000U, .remaining_ms = 1000U },
	};

	indicator_test_configure_display(true, true, 128U, 64U,
					 PIXEL_FORMAT_MONO01, false, false, false);
	indicator_test_reset();
	publish_health(HEALTH_RELAY_ACTIVE, HEALTH_REASON_NONE, HEALTH_REASON_NONE,
		       0U, BIT(0) | BIT(3));
	indicator_set_relay_timed_state(0U, BIT(0) | BIT(3), timing);
	(void)snapshot();

	zassert_true(indicator_test_display_pixel_is_set(ch1_cell_x + 1U, 46U));
	zassert_true(indicator_test_display_pixel_is_set(ch1_cell_x + 8U, 47U));
	zassert_false(indicator_test_display_pixel_is_set(ch4_cell_x + 1U, 46U));
	zassert_true(indicator_test_display_pixel_is_set(ch4_cell_x + 5U, 46U));
	zassert_true(indicator_test_display_pixel_is_set(ch4_cell_x + 11U, 47U));
	zassert_false(indicator_test_display_pixel_is_set(ch4_cell_x + 16U, 47U));
	zassert_false(indicator_test_display_pixel_is_set(ch2_cell_x + 1U, 46U));
}

ZTEST(indicator, test_display_multiple_pulses_render_p_star)
{
	struct indicator_test_snapshot snap;

	indicator_test_configure_display(true, true, 128U, 64U,
					 PIXEL_FORMAT_MONO01, false, false, false);
	indicator_test_reset();
	publish_health(HEALTH_RELAY_ACTIVE, HEALTH_REASON_NONE, HEALTH_REASON_NONE,
		       0U, BIT(0) | BIT(3));
	snap = snapshot();

	zassert_equal(snap.display_mode, INDICATOR_DISPLAY_MODE_ACTIVE);
	zassert_equal(snap.display_detail, INDICATOR_DISPLAY_DETAIL_P_STAR);
	zassert_equal(snap.display_filled_mask, BIT(0) | BIT(3));
}

ZTEST(indicator, test_display_attention_reboot_and_fault_priority)
{
	struct indicator_test_snapshot snap;

	indicator_test_configure_display(true, true, 128U, 64U,
					 PIXEL_FORMAT_MONO01, false, false, false);
	indicator_test_reset();
	publish_health(HEALTH_NORMAL, HEALTH_REASON_NONE, HEALTH_REASON_NONE, 0U, 0U);
	indicator_record_command(INDICATOR_COMMAND_REJECTED);
	snap = snapshot();
	zassert_equal(snap.display_mode, INDICATOR_DISPLAY_MODE_ATTN);
	zassert_equal(snap.display_detail, INDICATOR_DISPLAY_DETAIL_E_ARG);

	indicator_record_command(INDICATOR_COMMAND_BUSY);
	snap = snapshot();
	zassert_equal(snap.display_mode, INDICATOR_DISPLAY_MODE_ATTN);
	zassert_equal(snap.display_detail, INDICATOR_DISPLAY_DETAIL_E_BUSY);

	publish_health(HEALTH_DEGRADED, HEALTH_REASON_COMM_OWNER_TIMEOUT,
		       HEALTH_REASON_COMM_OWNER_TIMEOUT, 0U, 0U);
	snap = snapshot();
	zassert_equal(snap.display_mode, INDICATOR_DISPLAY_MODE_ATTN);
	zassert_equal(snap.display_detail, INDICATOR_DISPLAY_DETAIL_OWNER);

	publish_health(HEALTH_RECOVERY_PENDING, HEALTH_REASON_HOST_REBOOT_PENDING,
		       HEALTH_REASON_HOST_REBOOT_PENDING, 0U, 0U);
	snap = snapshot();
	zassert_equal(snap.display_mode, INDICATOR_DISPLAY_MODE_REBOOT);
	zassert_equal(snap.display_detail, INDICATOR_DISPLAY_DETAIL_HOLD);

	publish_health(HEALTH_FAULT, HEALTH_REASON_RELAY_IO_FAILED,
		       HEALTH_REASON_RELAY_IO_FAILED, 0U, 0U);
	snap = snapshot();
	zassert_equal(snap.display_mode, INDICATOR_DISPLAY_MODE_FAULT);
	zassert_equal(snap.display_detail, INDICATOR_DISPLAY_DETAIL_E_IO);
}

ZTEST(indicator, test_display_health_snapshot_preserves_owner_timeout_detail)
{
	struct indicator_test_snapshot snap;

	indicator_test_configure_display(true, true, 128U, 64U,
					 PIXEL_FORMAT_MONO01, false, false, false);
	indicator_test_reset();

	publish_health(HEALTH_DEGRADED, HEALTH_REASON_INDICATOR_DEGRADED,
		       HEALTH_REASON_INDICATOR_DEGRADED, 0U, 0U);
	snap = snapshot();
	zassert_equal(snap.display_mode, INDICATOR_DISPLAY_MODE_ATTN);
	zassert_true(snap.degraded);
	zassert_false(snap.owner_lost);
	zassert_equal(snap.display_detail, INDICATOR_DISPLAY_DETAIL_E_IO);

	publish_health(HEALTH_DEGRADED, HEALTH_REASON_COMM_OWNER_TIMEOUT,
		       HEALTH_REASON_COMM_OWNER_TIMEOUT, 0U, 0U);
	snap = snapshot();
	zassert_equal(snap.display_mode, INDICATOR_DISPLAY_MODE_ATTN);
	zassert_true(snap.degraded);
	zassert_true(snap.owner_lost);
	zassert_equal(snap.display_detail, INDICATOR_DISPLAY_DETAIL_OWNER);
}
