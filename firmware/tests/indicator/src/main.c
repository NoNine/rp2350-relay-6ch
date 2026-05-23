/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>

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
