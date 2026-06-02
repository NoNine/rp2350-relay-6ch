/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef RP2350_RELAY_6CH_TEST_INDICATOR_H_
#define RP2350_RELAY_6CH_TEST_INDICATOR_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/drivers/display.h>

#include <rp2350_relay_6ch/indicator.h>

#ifdef __cplusplus
extern "C" {
#endif

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
	bool buzzer_on;
	uint8_t beeps_remaining;
};

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

#ifdef __cplusplus
}
#endif

#endif /* RP2350_RELAY_6CH_TEST_INDICATOR_H_ */
