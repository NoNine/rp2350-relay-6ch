/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#ifdef CONFIG_HWINFO
#include <zephyr/drivers/hwinfo.h>
#endif

#include <rp2350_relay_6ch/health.h>
#include <rp2350_relay_6ch/watchdog_supervisor.h>

LOG_MODULE_REGISTER(rp2350_watchdog_supervisor, LOG_LEVEL_INF);

#define WATCHDOG_FATAL_REASONS                                                               \
	(HEALTH_REASON_RELAY_GPIO_INIT_FAILED | HEALTH_REASON_RELAY_IO_FAILED |              \
	 HEALTH_REASON_WATCHDOG_SUPERVISOR_FAILED)

struct watchdog_supervisor_model {
	struct k_mutex lock;
	bool initialized;
	bool started;
	bool enabled;
	bool healthy;
	uint32_t feeds;
	uint32_t feed_errors;
	bool last_reset_watchdog;
	int channel_id;
};

static struct watchdog_supervisor_model model;
static void watchdog_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(watchdog_work, watchdog_work_handler);

#if !defined(CONFIG_ZTEST) && DT_HAS_ALIAS(watchdog0)
static const struct device *const watchdog_dev = DEVICE_DT_GET(DT_ALIAS(watchdog0));
#elif !defined(CONFIG_ZTEST)
static const struct device *const watchdog_dev;
#endif

#ifdef CONFIG_ZTEST
static bool test_device_ready;
static int test_setup_result;
static int test_feed_result;
static bool test_last_reset_watchdog;
#endif

static void ensure_initialized(void)
{
	if (model.initialized) {
		return;
	}

	k_mutex_init(&model.lock);
	model.initialized = true;
	model.channel_id = -1;
}

static bool runtime_ready(void)
{
	struct health_snapshot health;

	health_snapshot(&health);
	return health.state != HEALTH_BOOTING &&
	       (health.reasons & HEALTH_REASON_RPC_NOT_READY) == 0U;
}

static bool feeding_allowed(const struct health_snapshot *health)
{
	return (health->reasons & WATCHDOG_FATAL_REASONS) == 0U;
}

static bool hardware_ready(void)
{
#ifdef CONFIG_ZTEST
	return test_device_ready;
#else
	return watchdog_dev != NULL && device_is_ready(watchdog_dev);
#endif
}

static bool read_last_reset_watchdog(void)
{
#ifdef CONFIG_ZTEST
	return test_last_reset_watchdog;
#elif defined(CONFIG_HWINFO)
	uint32_t cause = 0U;

	if (hwinfo_get_reset_cause(&cause) < 0) {
		return false;
	}

	return (cause & RESET_WATCHDOG) != 0U;
#else
	return false;
#endif
}

static int hardware_setup(void)
{
#ifdef CONFIG_ZTEST
	return test_setup_result;
#else
	struct wdt_timeout_cfg timeout = {
		.flags = WDT_FLAG_RESET_SOC,
		.window = {
			.min = 0U,
			.max = CONFIG_RP2350_RELAY_6CH_WATCHDOG_TIMEOUT_MS,
		},
	};
	int channel_id;
	int ret;

	channel_id = wdt_install_timeout(watchdog_dev, &timeout);
	if (channel_id < 0) {
		return channel_id;
	}

	ret = wdt_setup(watchdog_dev, WDT_OPT_PAUSE_HALTED_BY_DBG);
	if (ret < 0) {
		return ret;
	}

	model.channel_id = channel_id;
	return 0;
#endif
}

static int hardware_feed(void)
{
#ifdef CONFIG_ZTEST
	return test_feed_result;
#else
	return wdt_feed(watchdog_dev, model.channel_id);
#endif
}

static void record_supervisor_failure_locked(void)
{
	model.healthy = false;
	model.feed_errors++;
	health_record_watchdog_supervisor_failed();
}

void watchdog_supervisor_start(void)
{
	int ret;

	ensure_initialized();

	k_mutex_lock(&model.lock, K_FOREVER);
	model.last_reset_watchdog = read_last_reset_watchdog();
	if (model.started) {
		k_mutex_unlock(&model.lock);
		return;
	}

	if (!runtime_ready() || !hardware_ready()) {
		model.enabled = false;
		model.healthy = false;
		k_mutex_unlock(&model.lock);
		return;
	}

	ret = hardware_setup();
	if (ret < 0) {
		LOG_ERR("Watchdog setup failed: %d", ret);
		model.enabled = false;
		model.started = false;
		record_supervisor_failure_locked();
		k_mutex_unlock(&model.lock);
		return;
	}

	model.started = true;
	model.enabled = true;
	model.healthy = true;
	k_work_schedule(&watchdog_work,
			K_MSEC(CONFIG_RP2350_RELAY_6CH_WATCHDOG_FEED_INTERVAL_MS));
	k_mutex_unlock(&model.lock);
}

static void watchdog_work_handler(struct k_work *work)
{
	struct health_snapshot health;
	int ret;

	ARG_UNUSED(work);

	ensure_initialized();
	health_snapshot(&health);

	k_mutex_lock(&model.lock, K_FOREVER);
	if (!model.started || !model.enabled || !model.healthy) {
		k_mutex_unlock(&model.lock);
		return;
	}

	if (!feeding_allowed(&health)) {
		model.healthy = false;
		k_mutex_unlock(&model.lock);
		return;
	}

	ret = hardware_feed();
	if (ret < 0) {
		LOG_ERR("Watchdog feed failed: %d", ret);
		record_supervisor_failure_locked();
		k_mutex_unlock(&model.lock);
		return;
	}

	model.feeds++;
	k_work_schedule(&watchdog_work,
			K_MSEC(CONFIG_RP2350_RELAY_6CH_WATCHDOG_FEED_INTERVAL_MS));
	k_mutex_unlock(&model.lock);
}

void watchdog_supervisor_snapshot(struct watchdog_supervisor_snapshot *snapshot)
{
	if (snapshot == NULL) {
		return;
	}

	ensure_initialized();
	k_mutex_lock(&model.lock, K_FOREVER);
	snapshot->enabled = model.enabled;
	snapshot->healthy = model.healthy;
	snapshot->timeout_ms = CONFIG_RP2350_RELAY_6CH_WATCHDOG_TIMEOUT_MS;
	snapshot->feed_interval_ms = CONFIG_RP2350_RELAY_6CH_WATCHDOG_FEED_INTERVAL_MS;
	snapshot->feeds = model.feeds;
	snapshot->feed_errors = model.feed_errors;
	snapshot->last_reset_watchdog = model.last_reset_watchdog;
	k_mutex_unlock(&model.lock);
}

#ifdef CONFIG_ZTEST
void watchdog_supervisor_test_reset(void)
{
	ensure_initialized();
	(void)k_work_cancel_delayable(&watchdog_work);
	k_mutex_lock(&model.lock, K_FOREVER);
	model.started = false;
	model.enabled = false;
	model.healthy = false;
	model.feeds = 0U;
	model.feed_errors = 0U;
	model.last_reset_watchdog = false;
	model.channel_id = -1;
	test_device_ready = false;
	test_setup_result = 0;
	test_feed_result = 0;
	test_last_reset_watchdog = false;
	k_mutex_unlock(&model.lock);
}

void watchdog_supervisor_test_set_device_ready(bool ready)
{
	test_device_ready = ready;
}

void watchdog_supervisor_test_set_setup_result(int result)
{
	test_setup_result = result;
}

void watchdog_supervisor_test_set_feed_result(int result)
{
	test_feed_result = result;
}

void watchdog_supervisor_test_set_last_reset_watchdog(bool last_reset_watchdog)
{
	test_last_reset_watchdog = last_reset_watchdog;
}

void watchdog_supervisor_test_run_once(void)
{
	watchdog_work_handler(NULL);
}
#endif
