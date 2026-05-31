/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef RP2350_RELAY_6CH_INDICATOR_H_
#define RP2350_RELAY_6CH_INDICATOR_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef CONFIG_ZTEST
#include <zephyr/drivers/display.h>
#endif

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
	INDICATOR_BUZZER_OWNER_LOST,
	INDICATOR_BUZZER_REBOOT_PENDING,
	INDICATOR_BUZZER_BOOT_READY,
};

enum indicator_display_state {
	INDICATOR_DISPLAY_UNSUPPORTED,
	INDICATOR_DISPLAY_NOT_DETECTED,
	INDICATOR_DISPLAY_READY,
	INDICATOR_DISPLAY_FAILED,
};

enum indicator_display_mode {
	INDICATOR_DISPLAY_MODE_OFF,
	INDICATOR_DISPLAY_MODE_BOOT,
	INDICATOR_DISPLAY_MODE_READY,
	INDICATOR_DISPLAY_MODE_ACTIVE,
	INDICATOR_DISPLAY_MODE_ATTN,
	INDICATOR_DISPLAY_MODE_FAULT,
	INDICATOR_DISPLAY_MODE_REBOOT,
};

enum indicator_display_detail {
	INDICATOR_DISPLAY_DETAIL_NONE,
	INDICATOR_DISPLAY_DETAIL_OK,
	INDICATOR_DISPLAY_DETAIL_P1,
	INDICATOR_DISPLAY_DETAIL_P2,
	INDICATOR_DISPLAY_DETAIL_P3,
	INDICATOR_DISPLAY_DETAIL_P4,
	INDICATOR_DISPLAY_DETAIL_P5,
	INDICATOR_DISPLAY_DETAIL_P6,
	INDICATOR_DISPLAY_DETAIL_P_STAR,
	INDICATOR_DISPLAY_DETAIL_E_ARG,
	INDICATOR_DISPLAY_DETAIL_E_BUSY,
	INDICATOR_DISPLAY_DETAIL_E_IO,
	INDICATOR_DISPLAY_DETAIL_OWNER,
	INDICATOR_DISPLAY_DETAIL_HOLD,
};

struct indicator_pulse_timing {
	uint32_t duration_ms;
	uint32_t remaining_ms;
};

struct indicator_test_snapshot {
	enum indicator_rgb_pattern rgb;
	enum indicator_buzzer_pattern buzzer;
	enum indicator_display_state display_state;
	enum indicator_display_mode display_mode;
	enum indicator_display_detail display_detail;
	bool ready;
	bool degraded;
	bool owner_lost;
	bool fault;
	bool reboot_pending;
	uint8_t relay_state_mask;
	uint8_t pulse_mask;
	uint8_t display_filled_mask;
	uint8_t display_pulse_mask;
	uint16_t display_post_write_count;
	uint16_t display_write_count;
};

void indicator_init(void);
void indicator_set_ready(bool ready);
void indicator_set_relay_state(uint8_t state_mask, uint8_t pulse_mask);
void indicator_set_relay_timed_state(
	uint8_t state_mask, uint8_t pulse_mask,
	const struct indicator_pulse_timing pulse_timing[6]);
void indicator_record_command(enum indicator_command_result result);
void indicator_set_degraded(bool degraded);
void indicator_set_owner_lost(bool owner_lost);
void indicator_set_fault(bool fault);
void indicator_set_host_reboot_pending(bool pending);
void indicator_set_comm_loss_reboot_pending(bool pending);
void indicator_set_reboot_pending(bool pending);

#ifdef CONFIG_ZTEST
void indicator_test_reset(void);
void indicator_test_get_snapshot(struct indicator_test_snapshot *snapshot);
void indicator_test_force_render(void);
void indicator_test_advance(uint32_t ms);
bool indicator_test_display_pixel_is_set(uint8_t x, uint8_t y);
bool indicator_test_display_glyph_supported(char c);
void indicator_test_configure_display(bool supported, bool ready,
				      uint16_t width, uint16_t height,
				      uint32_t pixel_formats,
				      bool blanking_fails,
				      bool post_write_fails,
				      bool render_write_fails);
void indicator_test_set_display_render_failure(bool render_write_fails);
void indicator_test_set_display_orientation_failure(bool orientation_fails);
enum display_orientation indicator_test_display_orientation(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* RP2350_RELAY_6CH_INDICATOR_H_ */
