/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>

#include <rp2350_relay_6ch/health.h>
#include <rp2350_relay_6ch/watchdog_supervisor.h>

static struct watchdog_supervisor_snapshot snapshot(void)
{
	struct watchdog_supervisor_snapshot snap;

	watchdog_supervisor_snapshot(&snap);
	return snap;
}

static void watchdog_before(void *fixture)
{
	ARG_UNUSED(fixture);

	health_test_reset();
	watchdog_supervisor_test_reset();
}

ZTEST_SUITE(watchdog_supervisor, NULL, NULL, watchdog_before, NULL, NULL);

ZTEST(watchdog_supervisor, test_start_before_readiness_leaves_watchdog_disabled)
{
	struct watchdog_supervisor_snapshot snap;

	watchdog_supervisor_test_set_device_ready(true);
	watchdog_supervisor_start();
	snap = snapshot();

	zassert_false(snap.enabled);
	zassert_false(snap.healthy);
	zassert_equal(snap.timeout_ms, CONFIG_RP2350_RELAY_6CH_WATCHDOG_TIMEOUT_MS);
	zassert_equal(snap.feed_interval_ms,
		      CONFIG_RP2350_RELAY_6CH_WATCHDOG_FEED_INTERVAL_MS);
}

ZTEST(watchdog_supervisor, test_start_after_readiness_enables_watchdog)
{
	struct watchdog_supervisor_snapshot snap;

	watchdog_supervisor_test_set_device_ready(true);
	health_set_relay_gpio_ready(true);
	health_set_rpc_ready(true);
	watchdog_supervisor_start();
	snap = snapshot();

	zassert_true(snap.enabled);
	zassert_true(snap.healthy);
	zassert_equal(snap.feeds, 0U);
	zassert_equal(snap.feed_errors, 0U);
}

ZTEST(watchdog_supervisor, test_feed_allows_normal_relay_active_degraded_and_reboot_pending)
{
	struct watchdog_supervisor_snapshot snap;

	watchdog_supervisor_test_set_device_ready(true);
	health_set_relay_gpio_ready(true);
	health_set_rpc_ready(true);
	watchdog_supervisor_start();

	watchdog_supervisor_test_run_once();
	health_set_relay_state(BIT(0), 0U);
	watchdog_supervisor_test_run_once();
	health_set_comm_owner_timed_out(true);
	watchdog_supervisor_test_run_once();
	health_set_indicator_degraded(true);
	watchdog_supervisor_test_run_once();
	health_set_host_reboot_pending(true);
	watchdog_supervisor_test_run_once();
	health_set_comm_reboot_pending(true);
	watchdog_supervisor_test_run_once();

	snap = snapshot();
	zassert_true(snap.enabled);
	zassert_true(snap.healthy);
	zassert_equal(snap.feeds, 6U);
	zassert_equal(snap.feed_errors, 0U);
}

ZTEST(watchdog_supervisor, test_fatal_health_gate_stops_feeding)
{
	struct watchdog_supervisor_snapshot snap;

	watchdog_supervisor_test_set_device_ready(true);
	health_set_relay_gpio_ready(true);
	health_set_rpc_ready(true);
	watchdog_supervisor_start();
	watchdog_supervisor_test_run_once();
	health_record_relay_io_error();
	watchdog_supervisor_test_run_once();
	snap = snapshot();

	zassert_true(snap.enabled);
	zassert_false(snap.healthy);
	zassert_equal(snap.feeds, 1U);
	zassert_equal(snap.feed_errors, 0U);
}

ZTEST(watchdog_supervisor, test_setup_failure_records_supervisor_fault)
{
	struct watchdog_supervisor_snapshot snap;
	struct health_snapshot health;

	watchdog_supervisor_test_set_device_ready(true);
	watchdog_supervisor_test_set_setup_result(-EIO);
	health_set_relay_gpio_ready(true);
	health_set_rpc_ready(true);
	watchdog_supervisor_start();
	snap = snapshot();
	health_snapshot(&health);

	zassert_false(snap.enabled);
	zassert_false(snap.healthy);
	zassert_equal(snap.feed_errors, 1U);
	zassert_equal(health.state, HEALTH_FAULT);
	zassert_true((health.reasons & HEALTH_REASON_WATCHDOG_SUPERVISOR_FAILED) != 0U);
}

ZTEST(watchdog_supervisor, test_feed_failure_records_supervisor_fault)
{
	struct watchdog_supervisor_snapshot snap;
	struct health_snapshot health;

	watchdog_supervisor_test_set_device_ready(true);
	watchdog_supervisor_test_set_feed_result(-EIO);
	health_set_relay_gpio_ready(true);
	health_set_rpc_ready(true);
	watchdog_supervisor_start();
	watchdog_supervisor_test_run_once();
	snap = snapshot();
	health_snapshot(&health);

	zassert_true(snap.enabled);
	zassert_false(snap.healthy);
	zassert_equal(snap.feeds, 0U);
	zassert_equal(snap.feed_errors, 1U);
	zassert_equal(health.state, HEALTH_FAULT);
	zassert_true((health.reasons & HEALTH_REASON_WATCHDOG_SUPERVISOR_FAILED) != 0U);
}
