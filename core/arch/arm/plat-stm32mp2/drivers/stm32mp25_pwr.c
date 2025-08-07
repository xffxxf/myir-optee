// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2022, STMicroelectronics
 */

#include <arm.h>
#include <config.h>
#include <drivers/clk.h>
#include <drivers/clk_dt.h>
#include <drivers/stm32_rif.h>
#include <drivers/stm32mp25_pwr.h>
#include <io.h>
#include <kernel/boot.h>
#include <kernel/delay.h>
#include <kernel/dt.h>
#include <kernel/dt_driver.h>
#include <kernel/panic.h>
#include <libfdt.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stm32_util.h>
#include <trace.h>

/*PWR control registers */
#define _PWR_CR2			U(0x004)
#define _PWR_CR5			U(0x010)
#define _PWR_CR6			U(0x014)

/* Non-shareable resources registers */
#define _PWR_RSECCFGR			U(0x100)
#define _PWR_RPRIVCFGR			U(0x104)
#define _PWR_R_CIDCFGR(x)		(U(0x108) + U(0x4) * (x))

/* Shareable resources registers */
#define _PWR_WIOSECCFGR			U(0x180)
#define _PWR_WIOPRIVCFGR		U(0x184)
#define _PWR_WIO_CIDCFGR(x)		(U(0x188) + U(0x8) * ((x) - 1))
#define _PWR_WIO_SEMCR(x)		(U(0x18C) + U(0x8) * ((x) - 1))

/*PWR_CR2 bitfields*/
#define _PWR_CR2_MONEN			BIT(0)

/*PWR_CR5 bitfields*/
#define _PWR_CR5_VCOREMONEN		BIT(0)

/*PWR_CR6 bitfields*/
#define _PWR_CR6_VCPUMONEN		BIT(0)

/*
 * CIDCFGR register bitfields
 */
#define _PWR_CIDCFGR_SEMWL_MASK		GENMASK_32(23, 16)
#define _PWR_CIDCFGR_SCID_MASK		GENMASK_32(6, 4)
#define _PWR_CIDCFGR_W_CONF_MASK	(_CIDCFGR_CFEN |	 \
					 _CIDCFGR_SEMEN |	 \
					 _PWR_CIDCFGR_SCID_MASK |\
					 _PWR_CIDCFGR_SEMWL_MASK)
#define _PWR_CIDCFGR_R_CONF_MASK	(_CIDCFGR_CFEN |	 \
					 _PWR_CIDCFGR_SCID_MASK)

/*
 * PRIVCFGR register bitfields
 */
#define _PWR_R_PRIVCFGR_MASK		GENMASK_32(6, 0)
#define _PWR_WIO_PRIVCFGR_C_MASK	GENMASK_32(12, 7)
#define _PWR_WIO_PRIVCFGR_MASK		GENMASK_32(5, 0)
/*
 * SECCFGR register bitfields
 */
#define _PWR_R_SECCFGR_MASK		GENMASK_32(6, 0)
#define _PWR_WIO_SECCFGR_C_MASK		GENMASK_32(12, 7)
#define _PWR_WIO_SECCFGR_MASK		GENMASK_32(5, 0)

/*
 * SEMCR register bitfields
 */
#define _PWR_SEMCR_SCID_MASK		GENMASK_32(6, 4)
#define _PWR_SEMCR_SCID_SHIFT		U(4)

#define _PWR_NB_RESSOURCES		U(13)
#define _PWR_NB_NS_RESSOURCES		U(7)
#define _PWR_NB_MAX_CID_SUPPORTED	U(7)

struct pwr_pdata {
	vaddr_t base;
	int interrupt;
	uint8_t nb_ressources;
	struct rif_conf_data *conf_data;
};

static struct pwr_pdata *pwr_d;

vaddr_t stm32_pwr_base(void)
{
	static struct io_pa_va base = { .pa = PWR_BASE };

	if (!pwr_d)
		return io_pa_or_va_secure(&base, 1);

	assert(pwr_d->base);

	return pwr_d->base;
}

static TEE_Result apply_rif_config(bool is_tdcid)
{
	TEE_Result res = TEE_ERROR_ACCESS_DENIED;
	uint32_t cidcfgr = 0;
	uint32_t r_priv = 0;
	uint32_t r_sec = 0;
	uint32_t wio_priv = 0;
	uint32_t wio_sec = 0;
	unsigned int wio_offset = 0;
	unsigned int i = 0;

	if (!pwr_d->conf_data)
		return TEE_SUCCESS;

	for (i = 0; i < _PWR_NB_RESSOURCES; i++) {
		if (!(BIT(i) & pwr_d->conf_data->access_mask[0]))
			continue;

		/*
		 * When TDCID, OP-TEE should be the one to set the CID filtering
		 * configuration. Clearing previous configuration prevents
		 * undesired events during the only legitimate configuration.
		 */
		if (i < _PWR_NB_NS_RESSOURCES) {
			if (is_tdcid)
				io_clrbits32(pwr_d->base + _PWR_R_CIDCFGR(i),
					     _PWR_CIDCFGR_R_CONF_MASK);

			cidcfgr = io_read32(pwr_d->base + _PWR_R_CIDCFGR(i));
		} else {
			wio_offset = i - _PWR_NB_NS_RESSOURCES + 1;
			if (is_tdcid)
				io_clrbits32(pwr_d->base +
					     _PWR_WIO_CIDCFGR(wio_offset),
					     _PWR_CIDCFGR_W_CONF_MASK);

			cidcfgr = io_read32(pwr_d->base +
					    _PWR_WIO_CIDCFGR(wio_offset));
		}

		/*
		 * Check if the resources is in semaphore mode.
		 * Non shareable resources won't ever have SEMEN bit set to 1.
		 * Therefore, there's no need to handle SEMCR not existing for
		 * these resources.
		 */
		if (SEM_MODE_INCORRECT(cidcfgr))
			continue;

		/* If not TDCID, we want to acquire semaphores assigned to us */
		res = stm32_rif_acquire_semaphore(pwr_d->base +
						  _PWR_WIO_SEMCR(wio_offset),
						  _PWR_NB_MAX_CID_SUPPORTED);
		if (res) {
			EMSG("Couldn't acquire semaphore for resources %u", i);
			return res;
		}
	}

	/* Separate non-shareable resources RIF configuration */
	r_priv = pwr_d->conf_data->priv_conf[0] & _PWR_R_PRIVCFGR_MASK;
	r_sec = pwr_d->conf_data->sec_conf[0] & _PWR_R_SECCFGR_MASK;

	wio_priv = (pwr_d->conf_data->priv_conf[0] &
		    _PWR_WIO_PRIVCFGR_C_MASK) >> _PWR_NB_NS_RESSOURCES;
	wio_sec = (pwr_d->conf_data->sec_conf[0] & _PWR_WIO_SECCFGR_C_MASK) >>
		  _PWR_NB_NS_RESSOURCES;

	/* Security and privilege RIF configuration */
	io_clrsetbits32(pwr_d->base + _PWR_RPRIVCFGR, _PWR_R_PRIVCFGR_MASK,
			r_priv);
	io_clrsetbits32(pwr_d->base + _PWR_RSECCFGR, _PWR_R_SECCFGR_MASK,
			r_sec);
	io_clrsetbits32(pwr_d->base + _PWR_WIOPRIVCFGR, _PWR_WIO_PRIVCFGR_MASK,
			wio_priv);
	io_clrsetbits32(pwr_d->base + _PWR_WIOSECCFGR, _PWR_WIO_SECCFGR_MASK,
			wio_sec);

	for (i = 0; i < _PWR_NB_RESSOURCES; i++) {
		if (!(BIT(i) & pwr_d->conf_data->access_mask[0]))
			continue;

		if (i < _PWR_NB_NS_RESSOURCES) {
			io_clrsetbits32(pwr_d->base + _PWR_R_CIDCFGR(i),
					_PWR_CIDCFGR_R_CONF_MASK,
					pwr_d->conf_data->cid_confs[i]);
			cidcfgr = io_read32(pwr_d->base + _PWR_R_CIDCFGR(i));
		} else {
			wio_offset = i - _PWR_NB_NS_RESSOURCES + 1;
			io_clrsetbits32(pwr_d->base +
					_PWR_WIO_CIDCFGR(wio_offset),
					_PWR_CIDCFGR_W_CONF_MASK,
					pwr_d->conf_data->cid_confs[i]);
			cidcfgr = io_read32(pwr_d->base +
					    _PWR_WIO_CIDCFGR(wio_offset));
		}

		/* Check if the resources is in semaphore mode */
		if (SEM_MODE_INCORRECT(cidcfgr))
			continue;

		res = stm32_rif_release_semaphore(pwr_d->base +
						  _PWR_WIO_SEMCR(wio_offset),
						  _PWR_NB_MAX_CID_SUPPORTED);
		if (res) {
			EMSG("Couldn't release semaphore resources %u", i);
			return res;
		}
	}

	if (IS_ENABLED(CFG_TEE_CORE_DEBUG)) {
		/* Check that RIF config are applied, panic otherwise */
		if ((io_read32(pwr_d->base + _PWR_RPRIVCFGR) &
		     pwr_d->conf_data->access_mask[0]) != r_priv) {
			EMSG("pwr r resources priv conf is incorrect");
			panic();
		}

		if ((io_read32(pwr_d->base + _PWR_WIOPRIVCFGR) &
		     (pwr_d->conf_data->access_mask[0] >>
		      _PWR_NB_NS_RESSOURCES)) != wio_priv) {
			EMSG("pwr wio resources priv conf is incorrect");
			panic();
		}

		if ((io_read32(pwr_d->base + _PWR_RSECCFGR) &
		     pwr_d->conf_data->access_mask[0]) != r_sec) {
			EMSG("pwr r resources sec conf is incorrect");
			panic();
		}

		if ((io_read32(pwr_d->base + _PWR_WIOSECCFGR) &
		     (pwr_d->conf_data->access_mask[0] >>
		      _PWR_NB_NS_RESSOURCES)) != wio_sec) {
			EMSG("pwr wio resources sec conf is incorrect");
			panic();
		}
	}

	return TEE_SUCCESS;
}

static void parse_dt(const void *fdt, int node)
{
	unsigned int i = 0;
	int lenp = 0;
	const fdt32_t *cuint = NULL;
	struct dt_node_info info = { };
	struct io_pa_va addr = { };

	fdt_fill_device_info(fdt, &info, node);
	addr.pa = info.reg;
	pwr_d->base = io_pa_or_va_secure(&addr, info.reg_size);

	cuint = fdt_getprop(fdt, node, "st,protreg", &lenp);
	if (!cuint) {
		DMSG("No RIF configuration available");
		goto skip_rif;
	}

	pwr_d->nb_ressources = (unsigned int)(lenp / sizeof(uint32_t));
	assert(pwr_d->nb_ressources <= _PWR_NB_RESSOURCES);

	pwr_d->conf_data = calloc(1, sizeof(*pwr_d->conf_data));
	if (!pwr_d->conf_data)
		panic();

	pwr_d->conf_data->cid_confs = calloc(_PWR_NB_RESSOURCES,
					     sizeof(uint32_t));
	pwr_d->conf_data->sec_conf = calloc(1, sizeof(uint32_t));
	pwr_d->conf_data->priv_conf = calloc(1, sizeof(uint32_t));
	pwr_d->conf_data->access_mask = calloc(1, sizeof(uint32_t));
	if (!pwr_d->conf_data->cid_confs || !pwr_d->conf_data->sec_conf ||
	    !pwr_d->conf_data->priv_conf || !pwr_d->conf_data->access_mask)
		panic("Missing memory capacity for PWR RIF configuration");

	for (i = 0; i < pwr_d->nb_ressources; i++)
		stm32_rif_parse_cfg(fdt32_to_cpu(cuint[i]), pwr_d->conf_data,
				    _PWR_NB_MAX_CID_SUPPORTED,
				    _PWR_NB_RESSOURCES);

skip_rif:
#ifdef CFG_STM32_PWR_IRQ
	if (info.interrupt == DT_INFO_INVALID_INTERRUPT)
		panic("No interrupt defined in PWR");

	pwr_d->interrupt = info.interrupt;
#endif
}

static TEE_Result stm32mp_pwr_probe(const void *fdt, int node,
				    const void *compat_data __unused)
{
	TEE_Result res = TEE_ERROR_GENERIC;
	bool is_tdcid = false;
	int subnode = 0;

	FMSG("PWR probe");

	res = stm32_rifsc_check_tdcid(&is_tdcid);
	if (res)
		return res;

	pwr_d = calloc(1, sizeof(*pwr_d));
	if (!pwr_d)
		return TEE_ERROR_OUT_OF_MEMORY;

	parse_dt(fdt, node);

	res = apply_rif_config(is_tdcid);
	if (res)
		panic("Failed to apply rif_config");

#ifdef CFG_STM32_PWR_IRQ
	res = stm32mp25_pwr_irq_probe(fdt, node, pwr_d->interrupt);
	if (res) {
		if (pwr_d->conf_data) {
			free(pwr_d->conf_data->access_mask);
			free(pwr_d->conf_data->cid_confs);
			free(pwr_d->conf_data->priv_conf);
			free(pwr_d->conf_data->sec_conf);
		}
		free(pwr_d->conf_data);
		free(pwr_d);
		pwr_d = NULL;
		return res;
	}
#endif

	fdt_for_each_subnode(subnode, fdt, node) {
		res = dt_driver_maybe_add_probe_node(fdt, subnode);
		if (res) {
			EMSG("Failed on node %s with %#"PRIx32,
			     fdt_get_name(fdt, subnode, NULL), res);
			panic();
		}
	}

	return TEE_SUCCESS;
}

static const struct dt_device_match stm32mp_pwr_match_table[] = {
	{ .compatible = "st,stm32mp21-pwr" },
	{ .compatible = "st,stm32mp25-pwr" },
	{ }
};

DEFINE_DT_DRIVER(stm32mp_pwr_dt_driver) = {
	.name = "stm32mp_pwr",
	.match_table = stm32mp_pwr_match_table,
	.probe = stm32mp_pwr_probe,
};

void stm32mp_pwr_monitoring_enable(enum pwr_monitoring monitoring)
{
	vaddr_t pwr_base = stm32_pwr_base();

	switch (monitoring) {
	/* On MP21 V08CAP is called VBAT */
	case PWR_MON_V08CAP_TEMP:
		io_setbits32(pwr_base + _PWR_CR2, _PWR_CR2_MONEN);
		break;
#ifndef CFG_STM32MP21
	case PWR_MON_VCORE:
		io_setbits32(pwr_base + _PWR_CR5, _PWR_CR5_VCOREMONEN);
		break;
	case PWR_MON_VCPU:
		io_setbits32(pwr_base + _PWR_CR6, _PWR_CR6_VCPUMONEN);
		break;
#endif /* !CFG_STM32MP21 */
	default:
		break;
	}
}
