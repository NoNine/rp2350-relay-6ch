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
#define RP2350_RELAY_6CH_MGMT_PROTOCOL_VERSION 1U
#define RP2350_RELAY_6CH_HARDWARE_NAME "Waveshare RP2350-Relay-6CH"

enum rp2350_relay_6ch_mgmt_cmd {
	RP2350_RELAY_6CH_MGMT_CMD_INFO = 0,
	RP2350_RELAY_6CH_MGMT_CMD_GET = 1,
	RP2350_RELAY_6CH_MGMT_CMD_SET = 2,
	RP2350_RELAY_6CH_MGMT_CMD_SET_ALL = 3,
	RP2350_RELAY_6CH_MGMT_CMD_PULSE = 4,
	RP2350_RELAY_6CH_MGMT_CMD_OFF_ALL = 5,
	RP2350_RELAY_6CH_MGMT_CMD_STATUS = 6,
	RP2350_RELAY_6CH_MGMT_CMD_REBOOT = 7,
	RP2350_RELAY_6CH_MGMT_CMD_COUNT,
};

enum rp2350_relay_6ch_mgmt_err {
	RP2350_RELAY_6CH_MGMT_ERR_OK = 0,
	RP2350_RELAY_6CH_MGMT_ERR_DECODE = 1,
	RP2350_RELAY_6CH_MGMT_ERR_INVALID_ARGUMENT = 2,
	RP2350_RELAY_6CH_MGMT_ERR_BUSY = 3,
	RP2350_RELAY_6CH_MGMT_ERR_RELAY_IO = 4,
	RP2350_RELAY_6CH_MGMT_ERR_REBOOT_UNAVAILABLE = 5,
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

#ifdef CONFIG_ZTEST
int relay_mgmt_test_handle(uint8_t command_id, bool write, const uint8_t *request,
			   size_t request_len, uint8_t *response, size_t response_size,
			   size_t *response_len);
#endif

#ifdef __cplusplus
}
#endif

#endif /* RP2350_RELAY_6CH_RELAY_MGMT_H_ */
