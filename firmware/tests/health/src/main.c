/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>

#include <rp2350_relay_6ch/health.h>

static struct health_snapshot snapshot(void)
{
	struct health_snapshot snap;

	health_snapshot(&snap);
	return snap;
}

static void health_before(void *fixture)
{
	ARG_UNUSED(fixture);

	health_test_reset();
}

ZTEST_SUITE(health, NULL, NULL, health_before, NULL, NULL);

ZTEST(health, test_initial_snapshot_is_booting)
{
	struct health_snapshot snap = snapshot();

	zassert_equal(snap.state, HEALTH_BOOTING);
	zassert_equal(snap.reasons, HEALTH_REASON_RPC_NOT_READY);
	zassert_equal(snap.primary_reason, HEALTH_REASON_RPC_NOT_READY);
	zassert_equal(snap.relay_state_mask, 0U);
	zassert_equal(snap.pulse_mask, 0U);
	zassert_equal(snap.transitions, 0U);
	zassert_equal(strcmp(health_state_name(snap.state), "booting"), 0);
	zassert_equal(strcmp(health_reason_name(snap.primary_reason), "rpc_not_ready"), 0);
}

ZTEST(health, test_ready_with_all_relays_off_becomes_normal)
{
	struct health_snapshot snap;

	health_set_relay_gpio_ready(true);
	health_set_rpc_ready(true);
	snap = snapshot();

	zassert_equal(snap.state, HEALTH_NORMAL);
	zassert_equal(snap.reasons, HEALTH_REASON_NONE);
	zassert_equal(snap.primary_reason, HEALTH_REASON_NONE);
	zassert_equal(strcmp(health_state_name(snap.state), "normal"), 0);
	zassert_equal(strcmp(health_reason_name(snap.primary_reason), "none"), 0);
}

ZTEST(health, test_relay_state_or_pulse_mask_becomes_relay_active)
{
	struct health_snapshot snap;

	health_set_relay_gpio_ready(true);
	health_set_rpc_ready(true);
	health_set_relay_state(BIT(0), 0U);
	snap = snapshot();

	zassert_equal(snap.state, HEALTH_RELAY_ACTIVE);
	zassert_equal(snap.relay_state_mask, BIT(0));
	zassert_equal(snap.pulse_mask, 0U);

	health_set_relay_state(0U, BIT(2));
	snap = snapshot();

	zassert_equal(snap.state, HEALTH_RELAY_ACTIVE);
	zassert_equal(snap.relay_state_mask, 0U);
	zassert_equal(snap.pulse_mask, BIT(2));
}

ZTEST(health, test_fault_reasons_are_latched)
{
	struct health_snapshot snap;

	health_set_relay_gpio_ready(true);
	health_set_rpc_ready(true);
	health_record_relay_gpio_init_failed();
	health_set_relay_gpio_ready(true);
	snap = snapshot();

	zassert_equal(snap.state, HEALTH_FAULT);
	zassert_true((snap.reasons & HEALTH_REASON_RELAY_GPIO_INIT_FAILED) != 0U);
	zassert_equal(snap.primary_reason, HEALTH_REASON_RELAY_GPIO_INIT_FAILED);

	health_record_relay_io_error();
	snap = snapshot();

	zassert_equal(snap.state, HEALTH_FAULT);
	zassert_true((snap.reasons & HEALTH_REASON_RELAY_IO_FAILED) != 0U);
	zassert_equal(snap.primary_reason, HEALTH_REASON_RELAY_GPIO_INIT_FAILED);
}

ZTEST(health, test_comm_timeout_degrades_and_recovery_clears_it)
{
	struct health_snapshot snap;

	health_set_relay_gpio_ready(true);
	health_set_rpc_ready(true);
	health_set_comm_owner_timed_out(true);
	snap = snapshot();

	zassert_equal(snap.state, HEALTH_DEGRADED);
	zassert_equal(snap.primary_reason, HEALTH_REASON_COMM_OWNER_TIMEOUT);

	health_set_comm_owner_timed_out(false);
	snap = snapshot();

	zassert_equal(snap.state, HEALTH_NORMAL);
	zassert_equal(snap.primary_reason, HEALTH_REASON_NONE);
}

ZTEST(health, test_recovery_pending_and_primary_reason_priority)
{
	struct health_snapshot snap;

	health_set_relay_gpio_ready(true);
	health_set_rpc_ready(true);
	health_set_host_reboot_pending(true);
	health_set_comm_reboot_pending(true);
	snap = snapshot();

	zassert_equal(snap.state, HEALTH_RECOVERY_PENDING);
	zassert_equal(snap.primary_reason, HEALTH_REASON_COMM_REBOOT_PENDING);
	zassert_equal(strcmp(health_state_name(snap.state), "recovery_pending"), 0);
	zassert_equal(strcmp(health_reason_name(snap.primary_reason), "comm_reboot_pending"),
		      0);

	health_set_comm_reboot_pending(false);
	snap = snapshot();

	zassert_equal(snap.state, HEALTH_RECOVERY_PENDING);
	zassert_equal(snap.primary_reason, HEALTH_REASON_HOST_REBOOT_PENDING);
}

ZTEST(health, test_reboot_failed_is_fault_and_clears_pending_reasons)
{
	struct health_snapshot snap;

	health_set_relay_gpio_ready(true);
	health_set_rpc_ready(true);
	health_set_host_reboot_pending(true);
	health_set_comm_reboot_pending(true);
	health_record_reboot_failed();
	snap = snapshot();

	zassert_equal(snap.state, HEALTH_FAULT);
	zassert_true((snap.reasons & HEALTH_REASON_REBOOT_FAILED) != 0U);
	zassert_false((snap.reasons & HEALTH_REASON_HOST_REBOOT_PENDING) != 0U);
	zassert_false((snap.reasons & HEALTH_REASON_COMM_REBOOT_PENDING) != 0U);
	zassert_equal(snap.primary_reason, HEALTH_REASON_REBOOT_FAILED);
	zassert_equal(strcmp(health_reason_name(snap.primary_reason), "reboot_failed"), 0);

	health_set_host_reboot_pending(false);
	health_set_comm_reboot_pending(false);
	snap = snapshot();

	zassert_equal(snap.state, HEALTH_FAULT);
	zassert_true((snap.reasons & HEALTH_REASON_REBOOT_FAILED) != 0U);
	zassert_equal(snap.primary_reason, HEALTH_REASON_REBOOT_FAILED);
}

ZTEST(health, test_indicator_degraded_is_advisory_degraded)
{
	struct health_snapshot snap;

	health_set_relay_gpio_ready(true);
	health_set_rpc_ready(true);
	health_set_indicator_degraded(true);
	snap = snapshot();

	zassert_equal(snap.state, HEALTH_DEGRADED);
	zassert_equal(snap.primary_reason, HEALTH_REASON_INDICATOR_DEGRADED);

	health_set_relay_state(BIT(1), 0U);
	snap = snapshot();

	zassert_equal(snap.state, HEALTH_DEGRADED);
	zassert_equal(snap.relay_state_mask, BIT(1));
}

ZTEST(health, test_transition_counter_changes_only_on_state_change)
{
	struct health_snapshot snap;

	health_set_relay_gpio_ready(true);
	health_set_rpc_ready(true);
	snap = snapshot();
	zassert_equal(snap.state, HEALTH_NORMAL);
	zassert_equal(snap.transitions, 1U);

	health_set_relay_state(0U, 0U);
	snap = snapshot();
	zassert_equal(snap.state, HEALTH_NORMAL);
	zassert_equal(snap.transitions, 1U);

	health_set_relay_state(BIT(0), 0U);
	snap = snapshot();
	zassert_equal(snap.state, HEALTH_RELAY_ACTIVE);
	zassert_equal(snap.transitions, 2U);
}
