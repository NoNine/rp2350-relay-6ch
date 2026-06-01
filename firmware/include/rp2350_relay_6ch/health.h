/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef RP2350_RELAY_6CH_HEALTH_H_
#define RP2350_RELAY_6CH_HEALTH_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

enum health_state {
	HEALTH_BOOTING,
	HEALTH_NORMAL,
	HEALTH_RELAY_ACTIVE,
	HEALTH_DEGRADED,
	HEALTH_FAULT,
	HEALTH_RECOVERY_PENDING,
};

enum health_reason {
	HEALTH_REASON_NONE = 0,
	HEALTH_REASON_RELAY_GPIO_INIT_FAILED = BIT(0),
	HEALTH_REASON_RELAY_IO_FAILED = BIT(1),
	HEALTH_REASON_RPC_NOT_READY = BIT(2),
	HEALTH_REASON_COMM_OWNER_TIMEOUT = BIT(3),
	HEALTH_REASON_COMM_REBOOT_PENDING = BIT(4),
	HEALTH_REASON_INDICATOR_DEGRADED = BIT(5),
	HEALTH_REASON_HOST_REBOOT_PENDING = BIT(6),
	HEALTH_REASON_REBOOT_FAILED = BIT(7),
};

struct health_snapshot {
	enum health_state state;
	uint32_t reasons;
	enum health_reason primary_reason;
	uint8_t relay_state_mask;
	uint8_t pulse_mask;
	uint32_t transitions;
};

void health_init(void);
void health_set_relay_gpio_ready(bool ready);
void health_set_rpc_ready(bool ready);
void health_set_relay_state(uint8_t state_mask, uint8_t pulse_mask);
void health_set_comm_owner_timed_out(bool timed_out);
void health_set_comm_reboot_pending(bool pending);
void health_set_host_reboot_pending(bool pending);
void health_set_indicator_degraded(bool degraded);
void health_record_relay_gpio_init_failed(void);
void health_record_relay_io_error(void);
void health_record_reboot_failed(void);
void health_snapshot(struct health_snapshot *snapshot);
const char *health_state_name(enum health_state state);
const char *health_reason_name(enum health_reason reason);

#ifdef CONFIG_ZTEST
void health_test_reset(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* RP2350_RELAY_6CH_HEALTH_H_ */
