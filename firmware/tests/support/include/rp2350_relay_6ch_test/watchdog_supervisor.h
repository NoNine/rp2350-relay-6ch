/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef RP2350_RELAY_6CH_TEST_WATCHDOG_SUPERVISOR_H_
#define RP2350_RELAY_6CH_TEST_WATCHDOG_SUPERVISOR_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void watchdog_supervisor_test_reset(void);
void watchdog_supervisor_test_set_device_ready(bool ready);
void watchdog_supervisor_test_set_setup_result(int result);
void watchdog_supervisor_test_set_feed_result(int result);
void watchdog_supervisor_test_set_last_reset_watchdog(bool last_reset_watchdog);
void watchdog_supervisor_test_run_once(void);

#ifdef __cplusplus
}
#endif

#endif /* RP2350_RELAY_6CH_TEST_WATCHDOG_SUPERVISOR_H_ */
