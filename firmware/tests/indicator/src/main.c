/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <zephyr/drivers/display.h>

#include <rp2350_relay_6ch/indicator.h>

static struct indicator_test_snapshot snapshot(void)
{
	struct indicator_test_snapshot snap;

	indicator_test_force_render();
	indicator_test_get_snapshot(&snap);
	return snap;
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

	indicator_set_ready(true);
	snap = snapshot();

	zassert_equal(snap.rgb, INDICATOR_RGB_READY);
	zassert_true(snap.ready);
	if (IS_ENABLED(CONFIG_RP2350_RELAY_6CH_BUZZER_FEEDBACK)) {
		zassert_equal(snap.buzzer, INDICATOR_BUZZER_BOOT_READY);
	} else {
		zassert_equal(snap.buzzer, INDICATOR_BUZZER_SILENT);
	}
}

ZTEST(indicator, test_boot_ready_buzzer_is_long_when_enabled)
{
	struct indicator_test_snapshot snap;

	if (!IS_ENABLED(CONFIG_RP2350_RELAY_6CH_BUZZER_FEEDBACK)) {
		ztest_test_skip();
	}

	indicator_set_ready(true);
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

	indicator_set_ready(true);
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

	indicator_set_ready(true);
	snap = snapshot();
	zassert_equal(snap.buzzer, INDICATOR_BUZZER_SILENT);
}

ZTEST(indicator, test_reboot_pending_buzzer_finishes_before_reset_delay)
{
	struct indicator_test_snapshot snap;

	if (!IS_ENABLED(CONFIG_RP2350_RELAY_6CH_BUZZER_FEEDBACK)) {
		ztest_test_skip();
	}

	indicator_set_reboot_pending(true);
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

	indicator_set_ready(true);
	indicator_set_relay_state(BIT(0), 0U);
	snap = snapshot();

	zassert_equal(snap.rgb, INDICATOR_RGB_RELAY_ACTIVE);

	indicator_set_relay_state(0U, BIT(1));
	snap = snapshot();

	zassert_equal(snap.rgb, INDICATOR_RGB_RELAY_ACTIVE);
	zassert_equal(snap.pulse_mask, BIT(1));
}

ZTEST(indicator, test_accepted_command_transient)
{
	struct indicator_test_snapshot snap;

	indicator_set_ready(true);
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

	indicator_set_ready(true);
	indicator_record_command(INDICATOR_COMMAND_ACCEPTED);
	indicator_record_command(INDICATOR_COMMAND_REJECTED);
	snap = snapshot();

	zassert_equal(snap.rgb, INDICATOR_RGB_ATTENTION);
}

ZTEST(indicator, test_degraded_persists_until_cleared)
{
	struct indicator_test_snapshot snap;

	indicator_set_ready(true);
	indicator_set_degraded(true);
	snap = snapshot();

	zassert_equal(snap.rgb, INDICATOR_RGB_ATTENTION);
	zassert_true(snap.degraded);

	indicator_test_advance(1000U);
	indicator_set_degraded(false);
	snap = snapshot();

	zassert_equal(snap.rgb, INDICATOR_RGB_READY);
	zassert_false(snap.degraded);
}

ZTEST(indicator, test_reboot_pending_priority)
{
	struct indicator_test_snapshot snap;

	indicator_set_ready(true);
	indicator_record_command(INDICATOR_COMMAND_REJECTED);
	indicator_set_reboot_pending(true);
	snap = snapshot();

	zassert_equal(snap.rgb, INDICATOR_RGB_REBOOT_PENDING);
	zassert_true(snap.reboot_pending);
}

ZTEST(indicator, test_fault_highest_priority)
{
	struct indicator_test_snapshot snap;

	indicator_set_ready(true);
	indicator_set_relay_state(BIT(0), 0U);
	indicator_set_reboot_pending(true);
	indicator_set_fault(true);
	snap = snapshot();

	zassert_equal(snap.rgb, INDICATOR_RGB_FAULT);
	zassert_true(snap.fault);
}

ZTEST(indicator, test_buzzer_quiet_by_default)
{
	struct indicator_test_snapshot snap;

	indicator_record_command(INDICATOR_COMMAND_ACCEPTED);
	snap = snapshot();

	if (IS_ENABLED(CONFIG_RP2350_RELAY_6CH_BUZZER_FEEDBACK)) {
		zassert_equal(snap.buzzer, INDICATOR_BUZZER_ACCEPTED);
	} else {
		zassert_equal(snap.buzzer, INDICATOR_BUZZER_SILENT);
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
	zassert_equal(snap.buzzer, INDICATOR_BUZZER_ACCEPTED);

	indicator_record_command(INDICATOR_COMMAND_BUSY);
	snap = snapshot();
	zassert_equal(snap.buzzer, INDICATOR_BUZZER_REJECTED);

	indicator_set_reboot_pending(true);
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
	indicator_set_ready(true);
	snap = snapshot();

	zassert_equal(snap.display_state, INDICATOR_DISPLAY_READY);
	zassert_equal(snap.display_mode, INDICATOR_DISPLAY_MODE_READY);
	zassert_equal(snap.display_detail, INDICATOR_DISPLAY_DETAIL_OK);
	zassert_equal(snap.display_filled_mask, 0U);
	zassert_equal(snap.display_pulse_mask, 0U);
	zassert_equal(snap.display_post_write_count, 1U);
	zassert_true(snap.display_write_count > 0U);
	zassert_true(indicator_test_display_pixel_is_set(115U, 54U));
	zassert_false(indicator_test_display_pixel_is_set(125U, 54U));
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
	indicator_set_ready(true);
	(void)snapshot();

	zassert_true(indicator_test_display_pixel_is_set(3U, 3U));
	zassert_true(indicator_test_display_pixel_is_set(7U, 3U));
	zassert_true(indicator_test_display_pixel_is_set(11U, 3U));
	zassert_false(indicator_test_display_pixel_is_set(0U, 3U));
	zassert_false(indicator_test_display_pixel_is_set(3U, 1U));
}

ZTEST(indicator, test_display_status_band_aligns_to_inset_column)
{
	indicator_test_configure_display(true, true, 128U, 64U,
					 PIXEL_FORMAT_MONO01, false, false, false);
	indicator_test_reset();
	indicator_set_relay_state(BIT(0), 0U);
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
	indicator_set_ready(true);
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

ZTEST(indicator, test_display_rejects_blanking_failure)
{
	struct indicator_test_snapshot snap;

	indicator_test_configure_display(true, true, 128U, 64U,
					 PIXEL_FORMAT_MONO01, true, false, false);
	indicator_test_reset();
	snap = snapshot();

	zassert_equal(snap.display_state, INDICATOR_DISPLAY_FAILED);
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
	indicator_set_ready(true);
	snap = snapshot();
	zassert_equal(snap.display_state, INDICATOR_DISPLAY_FAILED);
	zassert_equal(snap.display_write_count, writes_before);

	indicator_test_set_display_render_failure(false);
	indicator_set_relay_state(BIT(0), 0U);
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
	indicator_set_ready(true);
	indicator_set_relay_state(BIT(0) | BIT(5), BIT(1));
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
	indicator_set_ready(true);
	indicator_set_relay_state(BIT(0), 0U);
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
	indicator_set_ready(true);
	indicator_set_relay_state(0U, BIT(1));
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
	indicator_test_configure_display(true, true, 128U, 64U,
					 PIXEL_FORMAT_MONO01, false, false, false);
	indicator_test_reset();
	indicator_set_relay_state(BIT(0), 0U);
	(void)snapshot();

	zassert_true(indicator_test_display_pixel_is_set(3U, 18U));
	zassert_false(indicator_test_display_pixel_is_set(11U, 33U));
	zassert_true(indicator_test_display_pixel_is_set(8U, 33U));
	zassert_true(indicator_test_display_pixel_is_set(12U, 31U));
}

ZTEST(indicator, test_display_draws_visible_pulse_mark)
{
	indicator_test_configure_display(true, true, 128U, 64U,
					 PIXEL_FORMAT_MONO01, false, false, false);
	indicator_test_reset();
	indicator_set_relay_state(0U, BIT(0));
	(void)snapshot();

	zassert_true(indicator_test_display_pixel_is_set(3U, 18U));
	zassert_false(indicator_test_display_pixel_is_set(14U, 22U));
	zassert_false(indicator_test_display_pixel_is_set(15U, 23U));
	zassert_false(indicator_test_display_pixel_is_set(17U, 22U));
	zassert_false(indicator_test_display_pixel_is_set(18U, 23U));
	zassert_true(indicator_test_display_pixel_is_set(11U, 27U));
	zassert_true(indicator_test_display_pixel_is_set(14U, 24U));
}

ZTEST(indicator, test_display_blinks_pulse_mark_without_clearing_cell)
{
	struct indicator_test_snapshot snap;
	uint16_t writes_before;

	indicator_test_configure_display(true, true, 128U, 64U,
					 PIXEL_FORMAT_MONO01, false, false, false);
	indicator_test_reset();
	indicator_set_relay_state(0U, BIT(0));
	snap = snapshot();
	writes_before = snap.display_write_count;

	zassert_equal(snap.display_mode, INDICATOR_DISPLAY_MODE_ACTIVE);
	zassert_equal(snap.display_detail, INDICATOR_DISPLAY_DETAIL_P1);
	zassert_true(indicator_test_display_pixel_is_set(3U, 18U));
	zassert_false(indicator_test_display_pixel_is_set(14U, 22U));
	zassert_false(indicator_test_display_pixel_is_set(17U, 22U));

	indicator_test_advance(500U);
	indicator_test_get_snapshot(&snap);

	zassert_true(snap.display_write_count > writes_before);
	zassert_equal(snap.display_mode, INDICATOR_DISPLAY_MODE_ACTIVE);
	zassert_equal(snap.display_detail, INDICATOR_DISPLAY_DETAIL_P1);
	zassert_true(indicator_test_display_pixel_is_set(3U, 18U));
	zassert_true(indicator_test_display_pixel_is_set(14U, 22U));
	zassert_true(indicator_test_display_pixel_is_set(17U, 22U));
}

ZTEST(indicator, test_display_multiple_pulses_render_p_star)
{
	struct indicator_test_snapshot snap;

	indicator_test_configure_display(true, true, 128U, 64U,
					 PIXEL_FORMAT_MONO01, false, false, false);
	indicator_test_reset();
	indicator_set_relay_state(0U, BIT(0) | BIT(3));
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
	indicator_set_ready(true);
	indicator_record_command(INDICATOR_COMMAND_REJECTED);
	snap = snapshot();
	zassert_equal(snap.display_mode, INDICATOR_DISPLAY_MODE_ATTN);
	zassert_equal(snap.display_detail, INDICATOR_DISPLAY_DETAIL_E_ARG);

	indicator_record_command(INDICATOR_COMMAND_BUSY);
	snap = snapshot();
	zassert_equal(snap.display_mode, INDICATOR_DISPLAY_MODE_ATTN);
	zassert_equal(snap.display_detail, INDICATOR_DISPLAY_DETAIL_E_BUSY);

	indicator_set_reboot_pending(true);
	snap = snapshot();
	zassert_equal(snap.display_mode, INDICATOR_DISPLAY_MODE_REBOOT);
	zassert_equal(snap.display_detail, INDICATOR_DISPLAY_DETAIL_HOLD);

	indicator_set_fault(true);
	snap = snapshot();
	zassert_equal(snap.display_mode, INDICATOR_DISPLAY_MODE_FAULT);
	zassert_equal(snap.display_detail, INDICATOR_DISPLAY_DETAIL_E_IO);
}
