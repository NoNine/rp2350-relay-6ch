/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef RP2350_RELAY_6CH_TEST_RELAY_MGMT_H_
#define RP2350_RELAY_6CH_TEST_RELAY_MGMT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int relay_mgmt_test_handle(uint8_t command_id, bool write, const uint8_t *request,
			   size_t request_len, uint8_t *response, size_t response_size,
			   size_t *response_len);
uint32_t relay_mgmt_test_reboot_delay_ms(void);
void relay_mgmt_test_cancel_reboot(void);
void relay_mgmt_test_force_reboot_schedule_result(int result);
void relay_mgmt_test_force_bootsel_schedule_result(int result);
void relay_mgmt_test_run_reboot_work(void);
void relay_mgmt_test_run_bootsel_work(void);
void relay_mgmt_test_force_reboot_return(bool enabled);
void relay_mgmt_test_force_bootsel_return(bool enabled);

#ifdef __cplusplus
}
#endif

#endif /* RP2350_RELAY_6CH_TEST_RELAY_MGMT_H_ */
