/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef RP2350_RELAY_6CH_RELAY_H_
#define RP2350_RELAY_6CH_RELAY_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RP2350_RELAY_6CH_CHANNEL_COUNT 6U
#define RP2350_RELAY_6CH_PULSE_MIN_MS 10U
#define RP2350_RELAY_6CH_PULSE_MAX_MS 60000U

int relay_init(void);
int relay_count(void);
int relay_get(uint8_t channel, bool *on);
int relay_get_all(uint8_t *state_mask);
int relay_is_pulsing(uint8_t channel, bool *pulsing);
int relay_set(uint8_t channel, bool on);
int relay_set_all(uint8_t state_mask);
int relay_off_all(void);
int relay_pulse(uint8_t channel, uint32_t duration_ms);
void relay_comm_loss_renew(void);
const char *relay_comm_loss_policy(void);
uint32_t relay_comm_loss_timeout_ms(void);
bool relay_comm_loss_reboot_on_timeout(void);
uint32_t relay_comm_loss_reboot_delay_ms(void);

#ifdef __cplusplus
}
#endif

#endif /* RP2350_RELAY_6CH_RELAY_H_ */
