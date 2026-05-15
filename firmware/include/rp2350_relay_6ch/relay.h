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

int relay_init(void);
int relay_count(void);
int relay_get(uint8_t channel, bool *on);
int relay_get_all(uint8_t *state_mask);
int relay_set(uint8_t channel, bool on);
int relay_set_all(uint8_t state_mask);
int relay_off_all(void);

#ifdef __cplusplus
}
#endif

#endif /* RP2350_RELAY_6CH_RELAY_H_ */
