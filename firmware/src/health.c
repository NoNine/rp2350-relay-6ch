/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <rp2350_relay_6ch/health.h>

struct health_model {
	struct k_mutex lock;
	bool initialized;
	bool relay_gpio_ready;
	bool rpc_ready;
	bool ready_seen;
	uint32_t reasons;
	struct health_snapshot snapshot;
};

static struct health_model model;

static enum health_reason primary_reason(uint32_t reasons)
{
	static const enum health_reason priority[] = {
		HEALTH_REASON_COMM_REBOOT_PENDING,
		HEALTH_REASON_HOST_REBOOT_PENDING,
		HEALTH_REASON_RELAY_GPIO_INIT_FAILED,
		HEALTH_REASON_RELAY_IO_FAILED,
		HEALTH_REASON_COMM_OWNER_TIMEOUT,
		HEALTH_REASON_RPC_NOT_READY,
		HEALTH_REASON_INDICATOR_DEGRADED,
	};

	for (size_t i = 0U; i < ARRAY_SIZE(priority); i++) {
		if ((reasons & priority[i]) != 0U) {
			return priority[i];
		}
	}

	return HEALTH_REASON_NONE;
}

static enum health_state derive_state_locked(uint32_t reasons)
{
	if ((reasons & (HEALTH_REASON_COMM_REBOOT_PENDING |
			HEALTH_REASON_HOST_REBOOT_PENDING)) != 0U) {
		return HEALTH_RECOVERY_PENDING;
	}

	if ((reasons & (HEALTH_REASON_RELAY_GPIO_INIT_FAILED |
			HEALTH_REASON_RELAY_IO_FAILED)) != 0U) {
		return HEALTH_FAULT;
	}

	if ((reasons & (HEALTH_REASON_COMM_OWNER_TIMEOUT |
			HEALTH_REASON_INDICATOR_DEGRADED)) != 0U) {
		return HEALTH_DEGRADED;
	}

	if ((reasons & HEALTH_REASON_RPC_NOT_READY) != 0U && model.ready_seen) {
		return HEALTH_DEGRADED;
	}

	if (!model.relay_gpio_ready || !model.rpc_ready) {
		return HEALTH_BOOTING;
	}

	if ((model.snapshot.relay_state_mask | model.snapshot.pulse_mask) != 0U) {
		return HEALTH_RELAY_ACTIVE;
	}

	return HEALTH_NORMAL;
}

static void recompute_locked(void)
{
	uint32_t reasons = model.reasons;
	enum health_state previous = model.snapshot.state;

	if (!model.rpc_ready) {
		reasons |= HEALTH_REASON_RPC_NOT_READY;
	} else {
		reasons &= (uint32_t)~HEALTH_REASON_RPC_NOT_READY;
	}

	if (model.relay_gpio_ready && model.rpc_ready) {
		model.ready_seen = true;
	}

	model.snapshot.reasons = reasons;
	model.snapshot.primary_reason = primary_reason(reasons);
	model.snapshot.state = derive_state_locked(reasons);

	if (model.snapshot.state != previous) {
		model.snapshot.transitions++;
	}
}

static void reset_locked(void)
{
	model.relay_gpio_ready = false;
	model.rpc_ready = false;
	model.ready_seen = false;
	model.reasons = 0U;
	memset(&model.snapshot, 0, sizeof(model.snapshot));
	model.snapshot.state = HEALTH_BOOTING;
	recompute_locked();
	model.snapshot.transitions = 0U;
}

void health_init(void)
{
	if (!model.initialized) {
		k_mutex_init(&model.lock);
		model.initialized = true;
	}

	k_mutex_lock(&model.lock, K_FOREVER);
	reset_locked();
	k_mutex_unlock(&model.lock);
}

static void ensure_initialized(void)
{
	if (!model.initialized) {
		health_init();
	}
}

void health_set_relay_gpio_ready(bool ready)
{
	ensure_initialized();
	k_mutex_lock(&model.lock, K_FOREVER);
	model.relay_gpio_ready = ready;
	recompute_locked();
	k_mutex_unlock(&model.lock);
}

void health_set_rpc_ready(bool ready)
{
	ensure_initialized();
	k_mutex_lock(&model.lock, K_FOREVER);
	model.rpc_ready = ready;
	recompute_locked();
	k_mutex_unlock(&model.lock);
}

void health_set_relay_state(uint8_t state_mask, uint8_t pulse_mask)
{
	ensure_initialized();
	k_mutex_lock(&model.lock, K_FOREVER);
	model.snapshot.relay_state_mask = state_mask;
	model.snapshot.pulse_mask = pulse_mask;
	recompute_locked();
	k_mutex_unlock(&model.lock);
}

void health_set_comm_owner_timed_out(bool timed_out)
{
	ensure_initialized();
	k_mutex_lock(&model.lock, K_FOREVER);
	if (timed_out) {
		model.reasons |= HEALTH_REASON_COMM_OWNER_TIMEOUT;
	} else {
		model.reasons &= (uint32_t)~HEALTH_REASON_COMM_OWNER_TIMEOUT;
	}
	recompute_locked();
	k_mutex_unlock(&model.lock);
}

void health_set_comm_reboot_pending(bool pending)
{
	ensure_initialized();
	k_mutex_lock(&model.lock, K_FOREVER);
	if (pending) {
		model.reasons |= HEALTH_REASON_COMM_REBOOT_PENDING;
	} else {
		model.reasons &= (uint32_t)~HEALTH_REASON_COMM_REBOOT_PENDING;
	}
	recompute_locked();
	k_mutex_unlock(&model.lock);
}

void health_set_host_reboot_pending(bool pending)
{
	ensure_initialized();
	k_mutex_lock(&model.lock, K_FOREVER);
	if (pending) {
		model.reasons |= HEALTH_REASON_HOST_REBOOT_PENDING;
	} else {
		model.reasons &= (uint32_t)~HEALTH_REASON_HOST_REBOOT_PENDING;
	}
	recompute_locked();
	k_mutex_unlock(&model.lock);
}

void health_set_indicator_degraded(bool degraded)
{
	ensure_initialized();
	k_mutex_lock(&model.lock, K_FOREVER);
	if (degraded) {
		model.reasons |= HEALTH_REASON_INDICATOR_DEGRADED;
	} else {
		model.reasons &= (uint32_t)~HEALTH_REASON_INDICATOR_DEGRADED;
	}
	recompute_locked();
	k_mutex_unlock(&model.lock);
}

void health_record_relay_gpio_init_failed(void)
{
	ensure_initialized();
	k_mutex_lock(&model.lock, K_FOREVER);
	model.reasons |= HEALTH_REASON_RELAY_GPIO_INIT_FAILED;
	recompute_locked();
	k_mutex_unlock(&model.lock);
}

void health_record_relay_io_error(void)
{
	ensure_initialized();
	k_mutex_lock(&model.lock, K_FOREVER);
	model.reasons |= HEALTH_REASON_RELAY_IO_FAILED;
	recompute_locked();
	k_mutex_unlock(&model.lock);
}

void health_snapshot(struct health_snapshot *snapshot)
{
	if (snapshot == NULL) {
		return;
	}

	ensure_initialized();
	k_mutex_lock(&model.lock, K_FOREVER);
	*snapshot = model.snapshot;
	k_mutex_unlock(&model.lock);
}

const char *health_state_name(enum health_state state)
{
	switch (state) {
	case HEALTH_BOOTING:
		return "booting";
	case HEALTH_NORMAL:
		return "normal";
	case HEALTH_RELAY_ACTIVE:
		return "relay_active";
	case HEALTH_DEGRADED:
		return "degraded";
	case HEALTH_FAULT:
		return "fault";
	case HEALTH_RECOVERY_PENDING:
		return "recovery_pending";
	default:
		return "unknown";
	}
}

const char *health_reason_name(enum health_reason reason)
{
	switch (reason) {
	case HEALTH_REASON_NONE:
		return "none";
	case HEALTH_REASON_RELAY_GPIO_INIT_FAILED:
		return "relay_gpio_init_failed";
	case HEALTH_REASON_RELAY_IO_FAILED:
		return "relay_io_failed";
	case HEALTH_REASON_RPC_NOT_READY:
		return "rpc_not_ready";
	case HEALTH_REASON_COMM_OWNER_TIMEOUT:
		return "comm_owner_timeout";
	case HEALTH_REASON_COMM_REBOOT_PENDING:
		return "comm_reboot_pending";
	case HEALTH_REASON_INDICATOR_DEGRADED:
		return "indicator_degraded";
	case HEALTH_REASON_HOST_REBOOT_PENDING:
		return "host_reboot_pending";
	default:
		return "unknown";
	}
}

#ifdef CONFIG_ZTEST
void health_test_reset(void)
{
	health_init();
}
#endif
