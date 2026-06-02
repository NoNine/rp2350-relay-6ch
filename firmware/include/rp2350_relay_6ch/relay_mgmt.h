/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef RP2350_RELAY_6CH_RELAY_MGMT_H_
#define RP2350_RELAY_6CH_RELAY_MGMT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/mgmt/mcumgr/mgmt/mgmt_defines.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RP2350_RELAY_6CH_MGMT_GROUP_ID MGMT_GROUP_ID_PERUSER
#define RP2350_RELAY_6CH_MGMT_PROTOCOL_VERSION 7U
#define RP2350_RELAY_6CH_MGMT_COMMAND_MODEL_VERSION 2U
#define RP2350_RELAY_6CH_HARDWARE_NAME "Waveshare RP2350-Relay-6CH"

enum rp2350_relay_6ch_mgmt_cmd {
	RP2350_RELAY_6CH_MGMT_CMD_IDENTITY = 0x00,
	RP2350_RELAY_6CH_MGMT_CMD_CAPABILITIES = 0x01,
	RP2350_RELAY_6CH_MGMT_CMD_BUILD_INFO = 0x02,
	RP2350_RELAY_6CH_MGMT_CMD_GET = 0x10,
	RP2350_RELAY_6CH_MGMT_CMD_STATUS = 0x11,
	RP2350_RELAY_6CH_MGMT_CMD_HEALTH = 0x12,
	RP2350_RELAY_6CH_MGMT_CMD_TRANSPORT = 0x13,
	RP2350_RELAY_6CH_MGMT_CMD_SAFETY = 0x14,
	RP2350_RELAY_6CH_MGMT_CMD_WATCHDOG = 0x15,
	RP2350_RELAY_6CH_MGMT_CMD_SET = 0x20,
	RP2350_RELAY_6CH_MGMT_CMD_SET_ALL = 0x21,
	RP2350_RELAY_6CH_MGMT_CMD_PULSE = 0x22,
	RP2350_RELAY_6CH_MGMT_CMD_OFF_ALL = 0x23,
	RP2350_RELAY_6CH_MGMT_CMD_HEARTBEAT = 0x30,
	RP2350_RELAY_6CH_MGMT_CMD_REBOOT = 0x40,
	RP2350_RELAY_6CH_MGMT_CMD_EVENT = 0x7f,
	RP2350_RELAY_6CH_MGMT_CMD_COUNT = RP2350_RELAY_6CH_MGMT_CMD_REBOOT + 1,
};

enum rp2350_relay_6ch_mgmt_err {
	RP2350_RELAY_6CH_MGMT_ERR_OK = 0,
	RP2350_RELAY_6CH_MGMT_ERR_DECODE = 1,
	RP2350_RELAY_6CH_MGMT_ERR_INVALID_ARGUMENT = 2,
	RP2350_RELAY_6CH_MGMT_ERR_BUSY = 3,
	RP2350_RELAY_6CH_MGMT_ERR_RELAY_IO = 4,
	RP2350_RELAY_6CH_MGMT_ERR_REBOOT_UNAVAILABLE = 5,
	RP2350_RELAY_6CH_MGMT_ERR_REBOOT_FAILED = 6,
};

struct rp2350_relay_6ch_mgmt_counters {
	uint32_t received;
	uint32_t succeeded;
	uint32_t decode_errors;
	uint32_t invalid_args;
	uint32_t busy;
};

void relay_mgmt_get_counters(struct rp2350_relay_6ch_mgmt_counters *counters);
void relay_mgmt_reset_counters(void);
void relay_mgmt_publish_health(void);

#ifdef CONFIG_ZTEST
int relay_mgmt_test_handle(uint8_t command_id, bool write, const uint8_t *request,
			   size_t request_len, uint8_t *response, size_t response_size,
			   size_t *response_len);
uint32_t relay_mgmt_test_reboot_delay_ms(void);
void relay_mgmt_test_cancel_reboot(void);
void relay_mgmt_test_force_reboot_schedule_result(int result);
void relay_mgmt_test_run_reboot_work(void);
void relay_mgmt_test_force_reboot_return(bool enabled);
#endif

#ifdef __cplusplus
}
#endif

#endif /* RP2350_RELAY_6CH_RELAY_MGMT_H_ */
