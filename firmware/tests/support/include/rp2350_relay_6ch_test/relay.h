/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef RP2350_RELAY_6CH_TEST_RELAY_H_
#define RP2350_RELAY_6CH_TEST_RELAY_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool relay_comm_loss_test_reboot_scheduled(void);
uint32_t relay_comm_loss_test_reboot_remaining_ms(void);
bool relay_comm_loss_test_reboot_pending_indication_scheduled(void);
void relay_comm_loss_test_force_reboot_schedule_result(int result);
void relay_comm_loss_test_run_reboot_work(void);
void relay_comm_loss_test_force_reboot_return(bool enabled);
void relay_test_force_next_off_all_result(int result);

#ifdef __cplusplus
}
#endif

#endif /* RP2350_RELAY_6CH_TEST_RELAY_H_ */
