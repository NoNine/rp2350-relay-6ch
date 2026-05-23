/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef RP2350_RELAY_6CH_INDICATOR_H_
#define RP2350_RELAY_6CH_INDICATOR_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum indicator_command_result {
	INDICATOR_COMMAND_ACCEPTED,
	INDICATOR_COMMAND_REJECTED,
	INDICATOR_COMMAND_BUSY,
};

enum indicator_rgb_pattern {
	INDICATOR_RGB_OFF,
	INDICATOR_RGB_BOOTING,
	INDICATOR_RGB_READY,
	INDICATOR_RGB_ACCEPTED,
	INDICATOR_RGB_RELAY_ACTIVE,
	INDICATOR_RGB_ATTENTION,
	INDICATOR_RGB_REBOOT_PENDING,
	INDICATOR_RGB_FAULT,
};

enum indicator_buzzer_pattern {
	INDICATOR_BUZZER_SILENT,
	INDICATOR_BUZZER_ACCEPTED,
	INDICATOR_BUZZER_REJECTED,
	INDICATOR_BUZZER_REBOOT_PENDING,
	INDICATOR_BUZZER_BOOT_READY,
};

struct indicator_test_snapshot {
	enum indicator_rgb_pattern rgb;
	enum indicator_buzzer_pattern buzzer;
	bool ready;
	bool degraded;
	bool fault;
	bool reboot_pending;
	uint8_t relay_state_mask;
	uint8_t pulse_mask;
};

void indicator_init(void);
void indicator_set_ready(bool ready);
void indicator_set_relay_state(uint8_t state_mask, uint8_t pulse_mask);
void indicator_record_command(enum indicator_command_result result);
void indicator_set_degraded(bool degraded);
void indicator_set_fault(bool fault);
void indicator_set_reboot_pending(bool pending);

#ifdef CONFIG_ZTEST
void indicator_test_reset(void);
void indicator_test_get_snapshot(struct indicator_test_snapshot *snapshot);
void indicator_test_force_render(void);
void indicator_test_advance(uint32_t ms);
#endif

#ifdef __cplusplus
}
#endif

#endif /* RP2350_RELAY_6CH_INDICATOR_H_ */
