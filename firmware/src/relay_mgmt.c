/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/mgmt/mcumgr/mgmt/handlers.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/mgmt/mcumgr/smp/smp.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>
#include <zephyr/version.h>

#ifdef CONFIG_REBOOT
#include <zephyr/sys/reboot.h>
#endif

#include <mgmt/mcumgr/util/zcbor_bulk.h>
#include <zcbor_decode.h>
#include <zcbor_encode.h>

#include <rp2350_relay_6ch/relay.h>
#include <rp2350_relay_6ch/relay_mgmt.h>

LOG_MODULE_REGISTER(rp2350_relay_mgmt, LOG_LEVEL_INF);

enum relay_mgmt_counter {
	RELAY_MGMT_COUNTER_RECEIVED,
	RELAY_MGMT_COUNTER_SUCCEEDED,
	RELAY_MGMT_COUNTER_DECODE_ERRORS,
	RELAY_MGMT_COUNTER_INVALID_ARGS,
	RELAY_MGMT_COUNTER_BUSY,
	RELAY_MGMT_COUNTER_COUNT,
};

static atomic_t counters[RELAY_MGMT_COUNTER_COUNT];

static void counter_inc(enum relay_mgmt_counter counter)
{
	atomic_inc(&counters[counter]);
}

static bool encode_relay_error(zcbor_state_t *zse,
			       enum rp2350_relay_6ch_mgmt_err error)
{
	return smp_add_cmd_err(zse, RP2350_RELAY_6CH_MGMT_GROUP_ID, error);
}

static int error_response(zcbor_state_t *zse,
			  enum rp2350_relay_6ch_mgmt_err error)
{
	switch (error) {
	case RP2350_RELAY_6CH_MGMT_ERR_DECODE:
		counter_inc(RELAY_MGMT_COUNTER_DECODE_ERRORS);
		break;
	case RP2350_RELAY_6CH_MGMT_ERR_INVALID_ARGUMENT:
		counter_inc(RELAY_MGMT_COUNTER_INVALID_ARGS);
		break;
	case RP2350_RELAY_6CH_MGMT_ERR_BUSY:
		counter_inc(RELAY_MGMT_COUNTER_BUSY);
		break;
	default:
		break;
	}

	return encode_relay_error(zse, error) ? MGMT_ERR_EOK : MGMT_ERR_EMSGSIZE;
}

static enum rp2350_relay_6ch_mgmt_err relay_ret_to_error(int ret)
{
	switch (ret) {
	case 0:
		return RP2350_RELAY_6CH_MGMT_ERR_OK;
	case -EINVAL:
		return RP2350_RELAY_6CH_MGMT_ERR_INVALID_ARGUMENT;
	case -EBUSY:
		return RP2350_RELAY_6CH_MGMT_ERR_BUSY;
	default:
		return RP2350_RELAY_6CH_MGMT_ERR_RELAY_IO;
	}
}

static bool encode_state(zcbor_state_t *zse)
{
	uint8_t state_mask;
	uint8_t pulse_mask = 0U;
	int ret = relay_get_all(&state_mask);

	if (ret < 0) {
		return false;
	}

	for (uint8_t channel = 0U; channel < RP2350_RELAY_6CH_CHANNEL_COUNT; channel++) {
		bool pulsing = false;

		ret = relay_is_pulsing(channel, &pulsing);
		if (ret < 0) {
			return false;
		}

		if (pulsing) {
			pulse_mask |= BIT(channel);
		}
	}

	return zcbor_tstr_put_lit(zse, "state") &&
	       zcbor_uint32_put(zse, state_mask) &&
	       zcbor_tstr_put_lit(zse, "pulsing") &&
	       zcbor_uint32_put(zse, pulse_mask);
}

static int encode_state_or_error(zcbor_state_t *zse)
{
	if (!encode_state(zse)) {
		return error_response(zse, RP2350_RELAY_6CH_MGMT_ERR_RELAY_IO);
	}

	counter_inc(RELAY_MGMT_COUNTER_SUCCEEDED);
	return MGMT_ERR_EOK;
}

static bool decode_request(zcbor_state_t *zsd, struct zcbor_map_decode_key_val *fields,
			   size_t field_count)
{
	size_t decoded;

	return zcbor_map_decode_bulk(zsd, fields, field_count, &decoded) == 0;
}

static bool field_found(struct zcbor_map_decode_key_val *fields, size_t field_count,
			const char *key)
{
	return zcbor_map_decode_bulk_key_found(fields, field_count, key);
}

static bool validate_request_map(zcbor_state_t *zsd)
{
	uint32_t unused;
	struct zcbor_map_decode_key_val fields[] = {
		ZCBOR_MAP_DECODE_KEY_DECODER("_unused", zcbor_uint32_decode, &unused),
	};

	return decode_request(zsd, fields, ARRAY_SIZE(fields));
}

static int info_handler(struct smp_streamer *ctxt)
{
	zcbor_state_t *zsd = ctxt->reader->zs;
	zcbor_state_t *zse = ctxt->writer->zs;
	bool ok;

	counter_inc(RELAY_MGMT_COUNTER_RECEIVED);

	if (!validate_request_map(zsd)) {
		return error_response(zse, RP2350_RELAY_6CH_MGMT_ERR_DECODE);
	}

	ok = zcbor_tstr_put_lit(zse, "protocol_version") &&
	     zcbor_uint32_put(zse, RP2350_RELAY_6CH_MGMT_PROTOCOL_VERSION) &&
	     zcbor_tstr_put_lit(zse, "firmware_version") &&
	     zcbor_tstr_put_lit(zse, KERNEL_VERSION_STRING) &&
	     zcbor_tstr_put_lit(zse, "hardware") &&
	     zcbor_tstr_put_lit(zse, RP2350_RELAY_6CH_HARDWARE_NAME) &&
	     zcbor_tstr_put_lit(zse, "relay_count") &&
	     zcbor_uint32_put(zse, RP2350_RELAY_6CH_CHANNEL_COUNT) &&
	     zcbor_tstr_put_lit(zse, "pulse_min_ms") &&
	     zcbor_uint32_put(zse, RP2350_RELAY_6CH_PULSE_MIN_MS) &&
	     zcbor_tstr_put_lit(zse, "pulse_max_ms") &&
	     zcbor_uint32_put(zse, RP2350_RELAY_6CH_PULSE_MAX_MS) &&
	     zcbor_tstr_put_lit(zse, "capabilities") &&
	     zcbor_uint32_put(zse, BIT(0) | BIT(1) | BIT(2) | BIT(3) | BIT(4));

	if (!ok) {
		return MGMT_ERR_EMSGSIZE;
	}

	counter_inc(RELAY_MGMT_COUNTER_SUCCEEDED);
	return MGMT_ERR_EOK;
}

static int get_handler(struct smp_streamer *ctxt)
{
	zcbor_state_t *zsd = ctxt->reader->zs;
	zcbor_state_t *zse = ctxt->writer->zs;
	uint32_t channel;
	bool channel_state;
	bool pulsing;
	struct zcbor_map_decode_key_val fields[] = {
		ZCBOR_MAP_DECODE_KEY_DECODER("channel", zcbor_uint32_decode, &channel),
	};
	bool ok;
	int ret;

	counter_inc(RELAY_MGMT_COUNTER_RECEIVED);

	if (!decode_request(zsd, fields, ARRAY_SIZE(fields))) {
		return error_response(zse, RP2350_RELAY_6CH_MGMT_ERR_DECODE);
	}

	if (!field_found(fields, ARRAY_SIZE(fields), "channel")) {
		return encode_state_or_error(zse);
	}

	if (channel >= RP2350_RELAY_6CH_CHANNEL_COUNT) {
		return error_response(zse, RP2350_RELAY_6CH_MGMT_ERR_INVALID_ARGUMENT);
	}

	ret = relay_get((uint8_t)channel, &channel_state);
	if (ret < 0) {
		return error_response(zse, relay_ret_to_error(ret));
	}

	ret = relay_is_pulsing((uint8_t)channel, &pulsing);
	if (ret < 0) {
		return error_response(zse, relay_ret_to_error(ret));
	}

	ok = zcbor_tstr_put_lit(zse, "channel") &&
	     zcbor_uint32_put(zse, channel) &&
	     zcbor_tstr_put_lit(zse, "on") &&
	     zcbor_bool_put(zse, channel_state) &&
	     zcbor_tstr_put_lit(zse, "pulsing") &&
	     zcbor_bool_put(zse, pulsing);
	if (!ok) {
		return MGMT_ERR_EMSGSIZE;
	}

	counter_inc(RELAY_MGMT_COUNTER_SUCCEEDED);
	return MGMT_ERR_EOK;
}

static int set_handler(struct smp_streamer *ctxt)
{
	zcbor_state_t *zsd = ctxt->reader->zs;
	zcbor_state_t *zse = ctxt->writer->zs;
	uint32_t channel;
	bool on;
	struct zcbor_map_decode_key_val fields[] = {
		ZCBOR_MAP_DECODE_KEY_DECODER("channel", zcbor_uint32_decode, &channel),
		ZCBOR_MAP_DECODE_KEY_DECODER("on", zcbor_bool_decode, &on),
	};
	int ret;

	counter_inc(RELAY_MGMT_COUNTER_RECEIVED);

	if (!decode_request(zsd, fields, ARRAY_SIZE(fields))) {
		return error_response(zse, RP2350_RELAY_6CH_MGMT_ERR_DECODE);
	}

	if (!field_found(fields, ARRAY_SIZE(fields), "channel") ||
	    !field_found(fields, ARRAY_SIZE(fields), "on") ||
	    channel >= RP2350_RELAY_6CH_CHANNEL_COUNT) {
		return error_response(zse, RP2350_RELAY_6CH_MGMT_ERR_INVALID_ARGUMENT);
	}

	ret = relay_set((uint8_t)channel, on);
	if (ret < 0) {
		return error_response(zse, relay_ret_to_error(ret));
	}

	return encode_state_or_error(zse);
}

static int set_all_handler(struct smp_streamer *ctxt)
{
	zcbor_state_t *zsd = ctxt->reader->zs;
	zcbor_state_t *zse = ctxt->writer->zs;
	uint32_t state_mask;
	struct zcbor_map_decode_key_val fields[] = {
		ZCBOR_MAP_DECODE_KEY_DECODER("state", zcbor_uint32_decode, &state_mask),
	};
	int ret;

	counter_inc(RELAY_MGMT_COUNTER_RECEIVED);

	if (!decode_request(zsd, fields, ARRAY_SIZE(fields))) {
		return error_response(zse, RP2350_RELAY_6CH_MGMT_ERR_DECODE);
	}

	if (!field_found(fields, ARRAY_SIZE(fields), "state") ||
	    (state_mask & ~BIT_MASK(RP2350_RELAY_6CH_CHANNEL_COUNT)) != 0U) {
		return error_response(zse, RP2350_RELAY_6CH_MGMT_ERR_INVALID_ARGUMENT);
	}

	ret = relay_set_all((uint8_t)state_mask);
	if (ret < 0) {
		return error_response(zse, relay_ret_to_error(ret));
	}

	return encode_state_or_error(zse);
}

static int pulse_handler(struct smp_streamer *ctxt)
{
	zcbor_state_t *zsd = ctxt->reader->zs;
	zcbor_state_t *zse = ctxt->writer->zs;
	uint32_t channel;
	uint32_t duration_ms;
	struct zcbor_map_decode_key_val fields[] = {
		ZCBOR_MAP_DECODE_KEY_DECODER("channel", zcbor_uint32_decode, &channel),
		ZCBOR_MAP_DECODE_KEY_DECODER("duration_ms", zcbor_uint32_decode,
					     &duration_ms),
	};
	int ret;

	counter_inc(RELAY_MGMT_COUNTER_RECEIVED);

	if (!decode_request(zsd, fields, ARRAY_SIZE(fields))) {
		return error_response(zse, RP2350_RELAY_6CH_MGMT_ERR_DECODE);
	}

	if (!field_found(fields, ARRAY_SIZE(fields), "channel") ||
	    !field_found(fields, ARRAY_SIZE(fields), "duration_ms") ||
	    channel >= RP2350_RELAY_6CH_CHANNEL_COUNT ||
	    duration_ms < RP2350_RELAY_6CH_PULSE_MIN_MS ||
	    duration_ms > RP2350_RELAY_6CH_PULSE_MAX_MS) {
		return error_response(zse, RP2350_RELAY_6CH_MGMT_ERR_INVALID_ARGUMENT);
	}

	ret = relay_pulse((uint8_t)channel, duration_ms);
	if (ret < 0) {
		return error_response(zse, relay_ret_to_error(ret));
	}

	return encode_state_or_error(zse);
}

static int off_all_handler(struct smp_streamer *ctxt)
{
	zcbor_state_t *zsd = ctxt->reader->zs;
	zcbor_state_t *zse = ctxt->writer->zs;
	int ret;

	counter_inc(RELAY_MGMT_COUNTER_RECEIVED);

	if (!validate_request_map(zsd)) {
		return error_response(zse, RP2350_RELAY_6CH_MGMT_ERR_DECODE);
	}

	ret = relay_off_all();
	if (ret < 0) {
		return error_response(zse, relay_ret_to_error(ret));
	}

	return encode_state_or_error(zse);
}

static int status_handler(struct smp_streamer *ctxt)
{
	zcbor_state_t *zsd = ctxt->reader->zs;
	zcbor_state_t *zse = ctxt->writer->zs;
	bool ok;

	counter_inc(RELAY_MGMT_COUNTER_RECEIVED);

	if (!validate_request_map(zsd)) {
		return error_response(zse, RP2350_RELAY_6CH_MGMT_ERR_DECODE);
	}

	ok = encode_state(zse) &&
	     zcbor_tstr_put_lit(zse, "uptime_ms") &&
	     zcbor_uint64_put(zse, k_uptime_get()) &&
	     zcbor_tstr_put_lit(zse, "received") &&
	     zcbor_uint32_put(zse, (uint32_t)atomic_get(&counters[RELAY_MGMT_COUNTER_RECEIVED])) &&
	     zcbor_tstr_put_lit(zse, "succeeded") &&
	     zcbor_uint32_put(zse, (uint32_t)atomic_get(&counters[RELAY_MGMT_COUNTER_SUCCEEDED])) &&
	     zcbor_tstr_put_lit(zse, "decode_errors") &&
	     zcbor_uint32_put(zse,
			      (uint32_t)atomic_get(&counters[RELAY_MGMT_COUNTER_DECODE_ERRORS])) &&
	     zcbor_tstr_put_lit(zse, "invalid_args") &&
	     zcbor_uint32_put(zse,
			      (uint32_t)atomic_get(&counters[RELAY_MGMT_COUNTER_INVALID_ARGS])) &&
	     zcbor_tstr_put_lit(zse, "busy") &&
	     zcbor_uint32_put(zse, (uint32_t)atomic_get(&counters[RELAY_MGMT_COUNTER_BUSY]));

	if (!ok) {
		return MGMT_ERR_EMSGSIZE;
	}

	counter_inc(RELAY_MGMT_COUNTER_SUCCEEDED);
	return MGMT_ERR_EOK;
}

#ifdef CONFIG_REBOOT
static void reboot_work_handler(struct k_work *work);

static K_WORK_DELAYABLE_DEFINE(reboot_work, reboot_work_handler);

static void reboot_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	(void)relay_off_all();
	sys_reboot(SYS_REBOOT_COLD);
}
#endif

static int reboot_handler(struct smp_streamer *ctxt)
{
	zcbor_state_t *zsd = ctxt->reader->zs;
	zcbor_state_t *zse = ctxt->writer->zs;
	bool ok;

	counter_inc(RELAY_MGMT_COUNTER_RECEIVED);

	if (!validate_request_map(zsd)) {
		return error_response(zse, RP2350_RELAY_6CH_MGMT_ERR_DECODE);
	}

#ifdef CONFIG_REBOOT
	(void)k_work_schedule(&reboot_work, K_MSEC(100));
	ok = zcbor_tstr_put_lit(zse, "ok") && zcbor_bool_put(zse, true);
#else
	ok = encode_relay_error(zse, RP2350_RELAY_6CH_MGMT_ERR_REBOOT_UNAVAILABLE);
#endif

	if (!ok) {
		return MGMT_ERR_EMSGSIZE;
	}

#ifdef CONFIG_REBOOT
	counter_inc(RELAY_MGMT_COUNTER_SUCCEEDED);
#endif
	return MGMT_ERR_EOK;
}

static const struct mgmt_handler relay_mgmt_handlers[] = {
	[RP2350_RELAY_6CH_MGMT_CMD_INFO] = {
		.mh_read = info_handler,
		.mh_write = NULL,
	},
	[RP2350_RELAY_6CH_MGMT_CMD_GET] = {
		.mh_read = get_handler,
		.mh_write = NULL,
	},
	[RP2350_RELAY_6CH_MGMT_CMD_SET] = {
		.mh_read = NULL,
		.mh_write = set_handler,
	},
	[RP2350_RELAY_6CH_MGMT_CMD_SET_ALL] = {
		.mh_read = NULL,
		.mh_write = set_all_handler,
	},
	[RP2350_RELAY_6CH_MGMT_CMD_PULSE] = {
		.mh_read = NULL,
		.mh_write = pulse_handler,
	},
	[RP2350_RELAY_6CH_MGMT_CMD_OFF_ALL] = {
		.mh_read = NULL,
		.mh_write = off_all_handler,
	},
	[RP2350_RELAY_6CH_MGMT_CMD_STATUS] = {
		.mh_read = status_handler,
		.mh_write = NULL,
	},
	[RP2350_RELAY_6CH_MGMT_CMD_REBOOT] = {
		.mh_read = NULL,
		.mh_write = reboot_handler,
	},
};

#ifdef CONFIG_MCUMGR_SMP_SUPPORT_ORIGINAL_PROTOCOL
static int relay_mgmt_translate_error_code(uint16_t error)
{
	switch (error) {
	case RP2350_RELAY_6CH_MGMT_ERR_OK:
		return MGMT_ERR_EOK;
	case RP2350_RELAY_6CH_MGMT_ERR_DECODE:
	case RP2350_RELAY_6CH_MGMT_ERR_INVALID_ARGUMENT:
		return MGMT_ERR_EINVAL;
	case RP2350_RELAY_6CH_MGMT_ERR_BUSY:
		return MGMT_ERR_EBUSY;
	case RP2350_RELAY_6CH_MGMT_ERR_REBOOT_UNAVAILABLE:
		return MGMT_ERR_ENOTSUP;
	default:
		return MGMT_ERR_EUNKNOWN;
	}
}
#endif

static struct mgmt_group relay_mgmt_group = {
	.mg_handlers = relay_mgmt_handlers,
	.mg_handlers_count = ARRAY_SIZE(relay_mgmt_handlers),
	.mg_group_id = RP2350_RELAY_6CH_MGMT_GROUP_ID,
#ifdef CONFIG_MCUMGR_SMP_SUPPORT_ORIGINAL_PROTOCOL
	.mg_translate_error = relay_mgmt_translate_error_code,
#endif
#ifdef CONFIG_MCUMGR_GRP_ENUM_DETAILS_NAME
	.mg_group_name = "rp2350 relay mgmt",
#endif
};

static void relay_mgmt_register_group(void)
{
	mgmt_register_group(&relay_mgmt_group);
}

MCUMGR_HANDLER_DEFINE(rp2350_relay_mgmt, relay_mgmt_register_group);

void relay_mgmt_get_counters(struct rp2350_relay_6ch_mgmt_counters *out)
{
	if (out == NULL) {
		return;
	}

	out->received = (uint32_t)atomic_get(&counters[RELAY_MGMT_COUNTER_RECEIVED]);
	out->succeeded = (uint32_t)atomic_get(&counters[RELAY_MGMT_COUNTER_SUCCEEDED]);
	out->decode_errors = (uint32_t)atomic_get(&counters[RELAY_MGMT_COUNTER_DECODE_ERRORS]);
	out->invalid_args = (uint32_t)atomic_get(&counters[RELAY_MGMT_COUNTER_INVALID_ARGS]);
	out->busy = (uint32_t)atomic_get(&counters[RELAY_MGMT_COUNTER_BUSY]);
}

void relay_mgmt_reset_counters(void)
{
	for (size_t i = 0U; i < ARRAY_SIZE(counters); i++) {
		atomic_set(&counters[i], 0);
	}
}

#ifdef CONFIG_ZTEST
int relay_mgmt_test_handle(uint8_t command_id, bool write, const uint8_t *request,
			   size_t request_len, uint8_t *response, size_t response_size,
			   size_t *response_len)
{
	struct cbor_nb_reader reader = { 0 };
	struct cbor_nb_writer writer = { 0 };
	struct smp_streamer ctxt = {
		.reader = &reader,
		.writer = &writer,
	};
	const struct mgmt_handler *handler;
	mgmt_handler_fn handler_fn;
	int ret;

	if (request == NULL || response == NULL || response_len == NULL ||
	    command_id >= ARRAY_SIZE(relay_mgmt_handlers)) {
		return -EINVAL;
	}

	handler = &relay_mgmt_handlers[command_id];
	handler_fn = write ? handler->mh_write : handler->mh_read;
	if (handler_fn == NULL) {
		return -ENOTSUP;
	}

	zcbor_new_decode_state(reader.zs, ARRAY_SIZE(reader.zs), request, request_len, 1,
			       NULL, 0);
	zcbor_new_encode_state(writer.zs, ARRAY_SIZE(writer.zs), response, response_size,
			       0);

	if (!zcbor_map_start_encode(ctxt.writer->zs,
				    CONFIG_MCUMGR_SMP_CBOR_MAX_MAIN_MAP_ENTRIES)) {
		return MGMT_ERR_EMSGSIZE;
	}

	ret = handler_fn(&ctxt);
	if (ret != MGMT_ERR_EOK) {
		return ret;
	}

	if (!zcbor_map_end_encode(ctxt.writer->zs,
				  CONFIG_MCUMGR_SMP_CBOR_MAX_MAIN_MAP_ENTRIES)) {
		return MGMT_ERR_EMSGSIZE;
	}

	*response_len = (size_t)(ctxt.writer->zs[0].payload - response);
	return 0;
}
#endif
