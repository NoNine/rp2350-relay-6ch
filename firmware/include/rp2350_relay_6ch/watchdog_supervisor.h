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

#ifdef __cplusplus
}
#endif

#endif /* RP2350_RELAY_6CH_WATCHDOG_SUPERVISOR_H_ */
