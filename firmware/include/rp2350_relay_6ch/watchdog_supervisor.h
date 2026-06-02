/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef RP2350_RELAY_6CH_WATCHDOG_SUPERVISOR_H_
#define RP2350_RELAY_6CH_WATCHDOG_SUPERVISOR_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct watchdog_supervisor_snapshot {
	bool enabled;
	bool healthy;
	uint32_t timeout_ms;
	uint32_t feed_interval_ms;
	uint32_t feeds;
	uint32_t feed_errors;
	bool last_reset_watchdog;
};

void watchdog_supervisor_start(void);
void watchdog_supervisor_snapshot(struct watchdog_supervisor_snapshot *snapshot);

#ifdef CONFIG_ZTEST
void watchdog_supervisor_test_reset(void);
void watchdog_supervisor_test_set_device_ready(bool ready);
void watchdog_supervisor_test_set_setup_result(int result);
void watchdog_supervisor_test_set_feed_result(int result);
void watchdog_supervisor_test_set_last_reset_watchdog(bool last_reset_watchdog);
void watchdog_supervisor_test_run_once(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* RP2350_RELAY_6CH_WATCHDOG_SUPERVISOR_H_ */
