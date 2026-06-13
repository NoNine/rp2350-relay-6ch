/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/drivers/misc/pio_rpi_pico/pio_rpi_pico.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/dt-bindings/led/led.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "hardware/pio.h"

LOG_MODULE_REGISTER(waveshare_ws2812_pio, CONFIG_LED_STRIP_LOG_LEVEL);

#define DT_DRV_COMPAT rp2350_relay_6ch_waveshare_ws2812_pio

#define WAVESHARE_WS2812_GPIO 36U
#define WAVESHARE_WS2812_PIO_GPIO_BASE 16U
#define WAVESHARE_WS2812_CHAIN_LENGTH 1U
#define WAVESHARE_WS2812_NUM_COLORS 3U

struct waveshare_ws2812_config {
	const struct device *piodev;
	const struct pinctrl_dev_config *const pcfg;
	uint32_t frequency;
	uint16_t reset_delay;
	uint32_t cycles_per_bit;
	const uint8_t *color_mapping;
};

struct waveshare_ws2812_data {
	uint32_t sm;
	struct k_timer reset_on_complete_timer;
};

#define CYCLES_PER_BIT(node)                                                                       \
	(DT_PROP_BY_IDX(node, bit_waveform, 0) + DT_PROP_BY_IDX(node, bit_waveform, 1) +           \
	 DT_PROP_BY_IDX(node, bit_waveform, 2))

static uint32_t waveshare_ws2812_map_color(const struct waveshare_ws2812_config *config,
					   const struct led_rgb *pixel)
{
	uint32_t color = 0U;

	for (size_t i = 0U; i < WAVESHARE_WS2812_NUM_COLORS; i++) {
		switch (config->color_mapping[i]) {
		case LED_COLOR_ID_RED:
			color |= (uint32_t)pixel->r << (8U * (2U - i));
			break;
		case LED_COLOR_ID_GREEN:
			color |= (uint32_t)pixel->g << (8U * (2U - i));
			break;
		case LED_COLOR_ID_BLUE:
			color |= (uint32_t)pixel->b << (8U * (2U - i));
			break;
		default:
			break;
		}
	}

	return color << 8U;
}

static int waveshare_ws2812_update_rgb(const struct device *dev, struct led_rgb *pixels,
				       size_t num_pixels)
{
	const struct waveshare_ws2812_config *config = dev->config;
	struct waveshare_ws2812_data *data = dev->data;
	PIO pio = pio_rpi_pico_get_pio(config->piodev);

	if (num_pixels != WAVESHARE_WS2812_CHAIN_LENGTH) {
		return -EINVAL;
	}

	k_timer_status_sync(&data->reset_on_complete_timer);
	pio_sm_put_blocking(pio, data->sm, waveshare_ws2812_map_color(config, &pixels[0]));
	k_timer_start(&data->reset_on_complete_timer, K_USEC(config->reset_delay), K_NO_WAIT);

	return 0;
}

static size_t waveshare_ws2812_length(const struct device *dev)
{
	ARG_UNUSED(dev);

	return WAVESHARE_WS2812_CHAIN_LENGTH;
}

static DEVICE_API(led_strip, waveshare_ws2812_api) = {
	.update_rgb = waveshare_ws2812_update_rgb,
	.length = waveshare_ws2812_length,
};

static int waveshare_ws2812_init(const struct device *dev)
{
	const struct waveshare_ws2812_config *config = dev->config;
	struct waveshare_ws2812_data *data = dev->data;
	const float clkdiv =
		sys_clock_hw_cycles_per_sec() / (config->cycles_per_bit * config->frequency);
	pio_sm_config sm_config = pio_get_default_sm_config();
	uint offset;
	PIO pio;
	int ret;
	int sm;

	if (!device_is_ready(config->piodev)) {
		LOG_ERR("%s: PIO device not ready", dev->name);
		return -ENODEV;
	}

	pio = pio_rpi_pico_get_pio(config->piodev);

	ret = pio_set_gpio_base(pio, WAVESHARE_WS2812_PIO_GPIO_BASE);
	if (ret < 0) {
		LOG_ERR("%s: failed to set PIO GPIO base: %d", dev->name, ret);
		return ret;
	}

	static const uint16_t instructions[] = {
		(0x6021 | (((DT_INST_PROP_BY_IDX(0, bit_waveform, 2) - 1U) & 0xFU) << 8U)),
		(0x1023 | (((DT_INST_PROP_BY_IDX(0, bit_waveform, 0) - 1U) & 0xFU) << 8U)),
		(0x1000 | (((DT_INST_PROP_BY_IDX(0, bit_waveform, 1) - 1U) & 0xFU) << 8U)),
		(0x0000 | (((DT_INST_PROP_BY_IDX(0, bit_waveform, 1) - 1U) & 0xFU) << 8U)),
	};
	static const struct pio_program program = {
		.instructions = instructions,
		.length = ARRAY_SIZE(instructions),
		.origin = -1,
	};

	offset = pio_add_program(pio, &program);

	ret = pinctrl_apply_state(config->pcfg, PINCTRL_STATE_DEFAULT);
	if (ret < 0) {
		return ret;
	}

	sm = pio_claim_unused_sm(pio, false);
	if (sm < 0) {
		return -EINVAL;
	}

	sm_config_set_sideset(&sm_config, 1U, false, false);
	sm_config_set_sideset_pins(&sm_config, WAVESHARE_WS2812_GPIO);
	sm_config_set_out_shift(&sm_config, false, true, 24U);
	sm_config_set_fifo_join(&sm_config, PIO_FIFO_JOIN_TX);
	sm_config_set_clkdiv(&sm_config, clkdiv);
	pio_sm_set_consecutive_pindirs(pio, sm, WAVESHARE_WS2812_GPIO, 1U, true);
	pio_sm_init(pio, sm, offset, &sm_config);
	pio_sm_set_enabled(pio, sm, true);

	data->sm = sm;
	k_timer_init(&data->reset_on_complete_timer, NULL, NULL);

	return 0;
}

#define WAVESHARE_WS2812_CHILD_INIT(node)                                                          \
	BUILD_ASSERT(DT_PROP(node, chain_length) == WAVESHARE_WS2812_CHAIN_LENGTH,                 \
		     "Waveshare onboard WS2812 driver supports exactly one LED");                  \
	BUILD_ASSERT(DT_PROP_LEN(node, color_mapping) == WAVESHARE_WS2812_NUM_COLORS,              \
		     "Waveshare onboard WS2812 driver supports RGB color mapping only");           \
	BUILD_ASSERT(DT_SAME_NODE(DT_GPIO_CTLR_BY_IDX(node, gpios, 0), DT_NODELABEL(gpio0_hi)),    \
		     "Waveshare onboard WS2812 must use gpio0_hi");                               \
	BUILD_ASSERT(DT_GPIO_PIN_BY_IDX(node, gpios, 0) == 4U,                                     \
		     "Waveshare onboard WS2812 must use GP36");                                   \
                                                                                                   \
	PINCTRL_DT_DEFINE(DT_PARENT(node));                                                        \
                                                                                                   \
	static const uint8_t waveshare_ws2812_##node##_color_mapping[] = DT_PROP(node, color_mapping); \
	static struct waveshare_ws2812_data waveshare_ws2812_##node##_data;                        \
	static const struct waveshare_ws2812_config waveshare_ws2812_##node##_config = {           \
		.piodev = DEVICE_DT_GET(DT_PARENT(DT_PARENT(node))),                               \
		.pcfg = PINCTRL_DT_DEV_CONFIG_GET(DT_PARENT(node)),                                \
		.frequency = DT_PROP(node, frequency),                                             \
		.reset_delay = DT_PROP(node, reset_delay),                                         \
		.cycles_per_bit = CYCLES_PER_BIT(DT_PARENT(node)),                                 \
		.color_mapping = waveshare_ws2812_##node##_color_mapping,                          \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_DEFINE(node, &waveshare_ws2812_init, NULL, &waveshare_ws2812_##node##_data,      \
			 &waveshare_ws2812_##node##_config, POST_KERNEL,                           \
			 CONFIG_LED_STRIP_INIT_PRIORITY, &waveshare_ws2812_api);

DT_INST_FOREACH_CHILD_STATUS_OKAY(0, WAVESHARE_WS2812_CHILD_INIT)
