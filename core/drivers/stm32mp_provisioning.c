// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2022-2024, STMicroelectronics
 *
 * This driver is intended for development support only. It allows
 * to provision BSEC/BSEC3 shadow cells with data read from OP-TEE OS
 * DTB but without any insurance that the OP-TEE driver are initialized
 * after this provisioning sequence. Therefore the driver initialization
 * prints a warning trace or panics upon CFG_INSECURE value.
 */

#include <arm.h>
#include <config.h>
#include <drivers/stm32_bsec.h>
#include <dt-bindings/soc/stm32mp-provisioning.h>
#include <keep.h>
#include <kernel/boot.h>
#include <kernel/delay.h>
#include <kernel/dt.h>
#include <kernel/dt_driver.h>
#include <kernel/panic.h>
#include <kernel/pm.h>
#include <libfdt.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stm32_util.h>
#include <sys/queue.h>
#include <trace.h>

/*
 * struct shadow_otp - OTP shadow memory cells provisioning
 * @otp_id: OTP base offset identifier
 * @otp_lock: OTP locking mask
 * @otp_len_word: Number of cells of @value
 * @value: Arrays of the BSEC words to load in shadow memory
 * @link: Reference in list
 */
struct shadow_otp {
	uint32_t otp_id;
	uint32_t otp_lock;
	size_t otp_len_word;
	uint32_t *value;
	SLIST_ENTRY(shadow_otp) link;
};

SLIST_HEAD(shadow_otp_head, shadow_otp);
static struct shadow_otp_head shadow_otp_head =
	SLIST_HEAD_INITIALIZER(&shadow_otp_head);

static void provision(struct shadow_otp *shadow_otp, size_t index)
{
	uint32_t provis_value = shadow_otp->value[index];
	uint32_t provis_lock = shadow_otp->otp_lock;
	uint32_t otp_id = shadow_otp->otp_id + index;
	uint32_t otp_val = 0;
	bool otp_lock = false;

	if (stm32_bsec_read_sw_lock(otp_id, &otp_lock))
		panic();
	if (stm32_bsec_shadow_read_otp(&otp_val, otp_id))
		panic();

	DMSG("OTP %"PRIu32": provisioning value %#"PRIx32", lock %#"PRIx32
	     " / shadow value %#"PRIx32", lock %#"PRIx32,
	     otp_id, provis_value, provis_lock, otp_val, otp_lock);

	if (otp_val != (otp_val | provis_value)) {
		if (otp_lock) {
			EMSG("Override the OTP %"PRIu32": shadow write lock",
			     otp_id);
			panic();
		}

		IMSG("Override the OTP %"PRIu32": %#"PRIx32" to %#"PRIx32,
		     otp_id, otp_val, otp_val | provis_value);

		if (stm32_bsec_write_otp(otp_val | provis_value, otp_id))
			panic();
	}

	switch (provis_lock) {
	case STICKY_LOCK_SW:
		if (stm32_bsec_set_sw_lock(otp_id))
			panic();
		break;
	case STICKY_LOCK_SR:
		if (stm32_bsec_set_sr_lock(otp_id))
			panic();
		break;
	case STICKY_LOCK_SWSR:
		if (stm32_bsec_set_sw_lock(otp_id) ||
		    stm32_bsec_set_sr_lock(otp_id))
			panic();
		break;
	case STICKY_NO_LOCK:
	default:
		break;
	}

	if (stm32_bsec_shadow_read_otp(&otp_val, otp_id))
		panic();
	DMSG("Read SHADOW %#"PRIx32, otp_val);
}

static void load_provisioning(void)
{
	struct shadow_otp *shadow_otp = NULL;
	size_t i = 0;

	SLIST_FOREACH(shadow_otp, &shadow_otp_head, link) {
		for (i = 0; i < shadow_otp->otp_len_word; i++)
			provision(shadow_otp, i);
	}
}

static void parse_dt(const void *fdt, int node)
{
	struct shadow_otp *shadow_otp = NULL;
	TEE_Result res = TEE_ERROR_GENERIC;
	int child = -1;

	fdt_for_each_subnode(child, fdt, node) {
		const uint32_t *cuint = NULL;
		int len = 0;
		uint32_t phandle = 0;
		uint32_t otp_lock = 0;
		uint32_t otp_id = 0;
		size_t otp_bit_len = 0;
		size_t otp_len_word = 0;
		unsigned int i = 0;

		cuint = fdt_getprop(fdt, child, "nvmem-cells", NULL);
		if (!cuint)
			panic();

		phandle = fdt32_to_cpu(*cuint);
		res = stm32_bsec_find_otp_by_phandle(phandle, &otp_id,
						     NULL, &otp_bit_len);
		if (res)
			panic("Phandle not found");

		shadow_otp = calloc(1, sizeof(*shadow_otp));
		if (!shadow_otp)
			panic();

		cuint = fdt_getprop(fdt, child, "st,shadow-lock", &len);
		if (cuint && len > 0)
			otp_lock = fdt32_to_cpu(*cuint);

		cuint = fdt_getprop(fdt, child, "st,shadow-value", &len);
		if (!cuint || len < 0 || (len % sizeof(uint32_t) != 0))
			panic();

		otp_len_word = otp_bit_len / (sizeof(uint32_t) * CHAR_BIT);
		if (otp_bit_len % (sizeof(uint32_t) * CHAR_BIT))
			otp_len_word++;

		if ((unsigned int)len / sizeof(uint32_t) != otp_len_word) {
			EMSG("Invalid size %d bytes for OTP %"PRIu32" (%zu words)",
			     len, otp_id, otp_len_word);
			panic();
		}

		if (!otp_len_word)
			continue;

		shadow_otp->value = calloc(otp_len_word, BSEC_BYTES_PER_WORD);
		if (!shadow_otp->value)
			panic();

		shadow_otp->otp_id = otp_id;
		shadow_otp->otp_lock = otp_lock;
		shadow_otp->otp_len_word = otp_len_word;

		for (i = 0; i < otp_len_word; i++)
			shadow_otp->value[i] = fdt32_to_cpu(*cuint++);

		SLIST_INSERT_HEAD(&shadow_otp_head, shadow_otp, link);
	}
}

static void provisioning_init(void)
{
	const void *fdt = get_embedded_dt();
	const char __maybe_unused *name = NULL;
	int node = -1;

	if (!fdt)
		return;

	node = fdt_node_offset_by_compatible(fdt, 0, "st,provisioning");
	if (node < 0)
		return;

	if (fdt_first_subnode(fdt, node) == -FDT_ERR_NOTFOUND) {
		name = fdt_get_name(fdt, node, NULL);
		DMSG("no subnode in %s", name);
		return;
	}

	parse_dt(fdt, node);
	load_provisioning();
}

static TEE_Result provisioning_pm(enum pm_op op, unsigned int pm_hint __unused,
				  const struct pm_callback_handle *hdl __unused)
{
	if (op == PM_OP_RESUME)
		load_provisioning();

	return TEE_SUCCESS;
}
DECLARE_KEEP_PAGER_PM(provisioning_pm);

static TEE_Result provisioning_probe(void)
{
	provisioning_init();

	if (!SLIST_EMPTY(&shadow_otp_head)) {
		if (IS_ENABLED(CFG_INSECURE))
			IMSG("WARNING: Embeds insecure stm32mp_provisioning driver");
		else
			panic("Embeds insecure stm32mp_provisioning data");

		register_pm_driver_cb(provisioning_pm, NULL, "provisioning");
	}

	return TEE_SUCCESS;
}

early_init_late(provisioning_probe);
