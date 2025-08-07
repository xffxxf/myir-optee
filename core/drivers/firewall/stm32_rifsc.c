// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2021-2023, STMicroelectronics
 */

#include <drivers/firewall.h>
#include <drivers/stm32_rif.h>
#include <drivers/stm32_rifsc.h>
#include <drivers/stm32_shared_io.h>
#include <drivers/stm32mp_dt_bindings.h>
#include <io.h>
#include <kernel/boot.h>
#include <kernel/dt.h>
#include <kernel/dt_driver.h>
#include <kernel/panic.h>
#include <kernel/pm.h>
#include <libfdt.h>
#include <mm/core_memprot.h>
#include <stm32_util.h>
#include <tee_api_defines.h>
#include <trace.h>
#include <util.h>

/* RIFSC offset register */
#define _RIFSC_RISC_SECCFGR0		U(0x10)
#define _RIFSC_RISC_PRIVCFGR0		U(0x30)
#define _RIFSC_RISC_RCFGLOCKR0		U(0x50)
#define _RIFSC_RISC_PER0_CIDCFGR	U(0x100)
#define _RIFSC_RISC_PER0_SEMCR		U(0x104)
#define _RIFSC_RIMC_CR			U(0xC00)
#define _RIFSC_RIMC_ATTR0		U(0xC10)
#if defined(CFG_STM32MP25)
#define _RIFSC_RISAL_CFGR0_A(y)		(U(0x900) + (0x10 * ((y) - 1)))
#define _RIFSC_RISAL_CFGR0_B(y)		(U(0x908) + (0x10 * ((y) - 1)))
#define _RIFSC_RISAL_ADDR_A		U(0x924)
#define _RIFSC_RISAL_ADDR_B		U(0x92C)
#endif /* defined(CFG_STM32MP25) */
#define _RIFSC_HWCFGR3			U(0xFE8)
#define _RIFSC_HWCFGR2			U(0xFEC)
#define _RIFSC_HWCFGR1			U(0xFF0)
#define _RIFSC_VERR			U(0xFF4)

/* RIFSC_HWCFGR2 register fields */
#define _RIFSC_HWCFGR2_CFG1_MASK	GENMASK_32(15, 0)
#define _RIFSC_HWCFGR2_CFG1_SHIFT	U(0)
#define _RIFSC_HWCFGR2_CFG2_MASK	GENMASK_32(23, 16)
#define _RIFSC_HWCFGR2_CFG2_SHIFT	U(16)
#define _RIFSC_HWCFGR2_CFG3_MASK	GENMASK_32(31, 24)
#define _RIFSC_HWCFGR2_CFG3_SHIFT	U(24)

/* RIFSC_HWCFGR1 register fields */
#define _RIFSC_HWCFGR1_CFG1_MASK	GENMASK_32(3, 0)
#define _RIFSC_HWCFGR1_CFG1_SHIFT	U(0)
#define _RIFSC_HWCFGR1_CFG2_MASK	GENMASK_32(7, 4)
#define _RIFSC_HWCFGR1_CFG2_SHIFT	U(4)
#define _RIFSC_HWCFGR1_CFG3_MASK	GENMASK_32(11, 8)
#define _RIFSC_HWCFGR1_CFG3_SHIFT	U(8)
#define _RIFSC_HWCFGR1_CFG4_MASK	GENMASK_32(15, 12)
#define _RIFSC_HWCFGR1_CFG4_SHIFT	U(12)
#define _RIFSC_HWCFGR1_CFG5_MASK	GENMASK_32(19, 16)
#define _RIFSC_HWCFGR1_CFG5_SHIFT	U(16)
#define _RIFSC_HWCFGR1_CFG6_MASK	GENMASK_32(23, 20)
#define _RIFSC_HWCFGR1_CFG6_SHIFT	U(20)

/*
 * RISC_CR register fields
 */
#define _RIFSC_RISC_CR_GLOCK		BIT(0)

/*
 * RIMC_CR register fields
 */
#define _RIFSC_RIMC_CR_GLOCK		BIT(0)
#define _RIFSC_RIMC_CR_TDCID_MASK	GENMASK_32(6, 4)

/* RIFSC_VERR register fields */
#define _RIFSC_VERR_MINREV_MASK		GENMASK_32(3, 0)
#define _RIFSC_VERR_MINREV_SHIFT	U(0)
#define _RIFSC_VERR_MAJREV_MASK		GENMASK_32(7, 4)
#define _RIFSC_VERR_MAJREV_SHIFT	U(4)

/* Periph id per register */
#define _PERIPH_IDS_PER_REG		U(32)
#define _OFFSET_PERX_CIDCFGR		U(0x8)

#define RIFSC_RISC_CIDCFGR_CFEN_MASK	BIT(0)
#define RIFSC_RISC_CIDCFGR_CFEN_SHIFT	U(0)
#define RIFSC_RISC_CIDCFGR_SEM_EN_MASK	BIT(1)
#define RIFSC_RISC_CIDCFGR_SEM_EN_SHIFT	U(1)
#define RIFSC_RISC_CIDCFGR_SCID_MASK	GENMASK_32(6, 4)
#define RIFSC_RISC_CIDCFGR_SCID_SHIFT	U(4)
#define RIFSC_RISC_CIDCFGR_LOCK_MASK	BIT(10)
#define RIFSC_RISC_CIDCFGR_LOCK_SHIFT	U(10)
#define RIFSC_RISC_CIDCFGR_SEML_MASK	GENMASK_32(23, 16)
#define RIFSC_RISC_CIDCFGR_SEML_SHIFT	U(16)

#define RIFSC_RISC_PERx_CID_MASK	(RIFSC_RISC_CFEN_MASK | \
					 RIFSC_RISC_SEM_EN_MASK | \
					 RIFSC_RISC_SCID_MASK | \
					 RIFSC_RISC_SEML_MASK)

#define RIFSC_RIMC_CIDSEL_MASK		BIT(2)
#define RIFSC_RIMC_CIDSEL_SHIFT		U(2)
#define RIFSC_RIMC_MCID_MASK		GENMASK_32(6, 4)
#define RIFSC_RIMC_MCID_SHIFT		U(4)
#define RIFSC_RIMC_MSEC_MASK		BIT(8)
#define RIFSC_RIMC_MPRIV_MASK		BIT(9)

struct rifsc_driver_data {
	uint32_t version;
	uint8_t nb_rimu;
	uint8_t nb_risup;
#if defined(CFG_STM32MP25)
	uint8_t nb_risal;
#endif /* defined(CFG_STM32MP25) */
	bool rif_en;
	bool sec_en;
	bool priv_en;
};

struct rifsc_platdata {
	uintptr_t base;
	struct rifsc_driver_data *drv_data;
	struct risup_cfg *risup;
	int nrisup;
	struct rimu_cfg *rimu;
	int nrimu;
	bool is_tdcid;
#if defined(CFG_STM32MP25)
	struct risal_cfg *risal;
	int nrisal;
#endif /* defined(CFG_STM32MP25) */
	bool errata_ahbrisab;
};

struct rimu_risup_pairs {
	uint32_t rimu_id;
	uint32_t risup_id;
};

#if defined(CFG_STM32MP25) || defined(CFG_STM32MP23)
static const struct rimu_risup_pairs rimu_risup[] = {
	[0] = {
		.rimu_id = 0,
		.risup_id = 0,
	},
	[1] = {
		.rimu_id = 1,
		.risup_id = STM32MP25_RIFSC_SDMMC1_ID,
	},
	[2] = {
		.rimu_id = 2,
		.risup_id = STM32MP25_RIFSC_SDMMC2_ID,
	},
	[3] = {
		.rimu_id = 3,
		.risup_id = STM32MP25_RIFSC_SDMMC3_ID,
	},
	[4] = {
		.rimu_id = 4,
		.risup_id = STM32MP25_RIFSC_USB3DR_ID,
	},
	[5] = {
		.rimu_id = 5,
		.risup_id = STM32MP25_RIFSC_USBH_ID,
	},
	[6] = {
		.rimu_id = 6,
		.risup_id = STM32MP25_RIFSC_ETH1_ID,
	},
	[7] = {
		.rimu_id = 7,
		.risup_id = STM32MP25_RIFSC_ETH2_ID,
	},
	[8] = {
		.rimu_id = 8,
		.risup_id = STM32MP25_RIFSC_PCIE_ID,
	},
	[9] = {
		.rimu_id = 9,
		.risup_id = STM32MP25_RIFSC_GPU_ID,
	},
	[10] = {
		.rimu_id = 10,
		.risup_id = STM32MP25_RIFSC_DCMIPP_ID,
	},
	[11] = {
		.rimu_id = 11,
		.risup_id = 0,
	},
	[12] = {
		.rimu_id = 12,
		.risup_id = 0,
	},
	[13] = {
		.rimu_id = 13,
		.risup_id = 0,
	},
	[14] = {
		.rimu_id = 14,
		.risup_id = STM32MP25_RIFSC_VDEC_ID,
	},
	[15] = {
		.rimu_id = 15,
		.risup_id = STM32MP25_RIFSC_VENC_ID,
	},
};
#endif /* defined(CFG_STM32MP25) || defined(CFG_STM32MP23) */
#if defined(CFG_STM32MP21)
static const struct rimu_risup_pairs rimu_risup[] = {
	[0] = {
		.rimu_id = 0,
		.risup_id = 0,
	},
	[1] = {
		.rimu_id = 1,
		.risup_id = STM32MP21_RIFSC_SDMMC1_ID,
	},
	[2] = {
		.rimu_id = 2,
		.risup_id = STM32MP21_RIFSC_SDMMC2_ID,
	},
	[3] = {
		.rimu_id = 3,
		.risup_id = STM32MP21_RIFSC_SDMMC3_ID,
	},
	[4] = {
		.rimu_id = 4,
		.risup_id = STM32MP21_RIFSC_OTG_HS_ID,
	},
	[5] = {
		.rimu_id = 5,
		.risup_id = STM32MP21_RIFSC_USBH_ID,
	},
	[6] = {
		.rimu_id = 6,
		.risup_id = STM32MP21_RIFSC_ETH1_ID,
	},
	[7] = {
		.rimu_id = 7,
		.risup_id = STM32MP21_RIFSC_ETH2_ID,
	},
	[10] = {
		.rimu_id = 10,
		.risup_id = STM32MP21_RIFSC_DCMIPP_ID,
	},
	[11] = {
		.rimu_id = 11,
		.risup_id = 0,
	},
	[12] = {
		.rimu_id = 12,
		.risup_id = 0,
	},
};
#endif /* defined(CFG_STM32MP21) */

static struct rifsc_driver_data rifsc_drvdata;
static struct rifsc_platdata rifsc_pdata;

static void stm32_rifsc_get_driverdata(struct rifsc_platdata *pdata)
{
	uint32_t regval = 0;

	regval = io_read32(pdata->base + _RIFSC_HWCFGR1);
	rifsc_drvdata.rif_en = _RIF_FLD_GET(_RIFSC_HWCFGR1_CFG1, regval) != 0;
	rifsc_drvdata.sec_en = _RIF_FLD_GET(_RIFSC_HWCFGR1_CFG2, regval) != 0;
	rifsc_drvdata.priv_en = _RIF_FLD_GET(_RIFSC_HWCFGR1_CFG3, regval) != 0;

	regval = io_read32(pdata->base + _RIFSC_HWCFGR2);
	rifsc_drvdata.nb_risup = _RIF_FLD_GET(_RIFSC_HWCFGR2_CFG1, regval);
	rifsc_drvdata.nb_rimu = _RIF_FLD_GET(_RIFSC_HWCFGR2_CFG2, regval);
#if defined(CFG_STM32MP25)
	rifsc_drvdata.nb_risal = _RIF_FLD_GET(_RIFSC_HWCFGR2_CFG3, regval);
#endif /* defined(CFG_STM32MP25) */

	pdata->drv_data = &rifsc_drvdata;

	regval = io_read8(pdata->base + _RIFSC_VERR);

	DMSG("RIFSC version %"PRIu32".%"PRIu32,
	     _RIF_FLD_GET(_RIFSC_VERR_MAJREV, regval),
	     _RIF_FLD_GET(_RIFSC_VERR_MINREV, regval));

#if defined(CFG_STM32MP25)
	DMSG("HW cap: enabled[rif:sec:priv]:[%s:%s:%s] nb[risup|rimu|risal]:[%"PRIu8",%"PRIu8",%"PRIu8"]",
	     rifsc_drvdata.rif_en ? "true" : "false",
	     rifsc_drvdata.sec_en ? "true" : "false",
	     rifsc_drvdata.priv_en ? "true" : "false",
	     rifsc_drvdata.nb_risup,
	     rifsc_drvdata.nb_rimu,
	     rifsc_drvdata.nb_risal);
#else /* defined(CFG_STM32MP25) */
	DMSG("HW cap: enabled[rif:sec:priv]:[%s:%s:%s] nb[risup|rimu]:[%"PRIu8",%"PRIu8"]",
	     rifsc_drvdata.rif_en ? "true" : "false",
	     rifsc_drvdata.sec_en ? "true" : "false",
	     rifsc_drvdata.priv_en ? "true" : "false",
	     rifsc_drvdata.nb_risup,
	     rifsc_drvdata.nb_rimu);
#endif
}

static TEE_Result stm32_rifsc_glock_config(const void *fdt, int node,
					   struct rifsc_platdata *pdata)
{
	const fdt32_t *cuint = NULL;
	int len = 0;
	uint32_t glock_conf = 0;

	cuint = fdt_getprop(fdt, node, "st,glocked", &len);
	if (!cuint) {
		DMSG("No global lock on RIF configuration");
		return TEE_SUCCESS;
	}

	assert(len == sizeof(uint32_t));

	glock_conf = fdt32_to_cpu(*cuint);

	if (glock_conf & RIFSC_RIMU_GLOCK) {
		DMSG("Setting global lock on RIMU configuration");

		io_setbits32_stm32shregs(pdata->base + _RIFSC_RIMC_CR,
					 _RIFSC_RIMC_CR_GLOCK);

		if (!(io_read32(pdata->base + _RIFSC_RIMC_CR) &
		      _RIFSC_RIMC_CR_GLOCK))
			return TEE_ERROR_ACCESS_DENIED;
	}

	if (glock_conf & RIFSC_RISUP_GLOCK) {
		DMSG("Setting global lock on RISUP configuration");

		io_setbits32_stm32shregs(pdata->base, _RIFSC_RISC_CR_GLOCK);

		if (!(io_read32(pdata->base) & _RIFSC_RISC_CR_GLOCK))
			return TEE_ERROR_ACCESS_DENIED;
	}

	return TEE_SUCCESS;
}

static TEE_Result stm32_rifsc_dt_conf_risup(const void *fdt, int node,
					    int *nrisup,
					    struct risup_cfg **risups)
{
	const fdt32_t *cuint = NULL;
	int i = 0;
	int len = 0;

	cuint = fdt_getprop(fdt, node, "st,protreg", &len);
	if (!cuint) {
		DMSG("No RISUP configuration in DT");
		return TEE_ERROR_ITEM_NOT_FOUND;
	}

	*nrisup = len / sizeof(uint32_t);
	*risups = calloc(*nrisup, sizeof(**risups));
	if (!*risups)
		return TEE_ERROR_OUT_OF_MEMORY;

	for (i = 0; i < *nrisup; i++) {
		uint32_t value = fdt32_to_cpu(cuint[i]);
		struct risup_cfg *risup = *risups + i;

		risup->id = _RIF_FLD_GET(RIF_PER_ID, value);
		risup->sec = (value & BIT(RIF_SEC_SHIFT)) != 0;
		risup->priv = (value & BIT(RIF_PRIV_SHIFT)) != 0;
		risup->lock = (value & BIT(RIF_LOCK_SHIFT)) != 0;
		risup->cid_attr = _RIF_FLD_GET(RIF_PERx_CID, value);
	}

	return TEE_SUCCESS;
}

static TEE_Result stm32_rifsc_dt_conf_rimu(const void *fdt, int node,
					   struct rifsc_platdata *pdata)
{
	const fdt32_t *cuint = NULL;
	int i = 0;
	int len = 0;

	cuint = fdt_getprop(fdt, node, "st,rimu", &len);
	if (!cuint) {
		DMSG("No RIMU configuration in DT");
		return TEE_ERROR_ITEM_NOT_FOUND;
	}

	len = len / sizeof(uint32_t);

	pdata->nrimu = len;
	pdata->rimu = calloc(len, sizeof(*pdata->rimu));
	if (!pdata->rimu)
		return TEE_ERROR_OUT_OF_MEMORY;

	for (i = 0; i < len; i++) {
		uint32_t value = fdt32_to_cpu(cuint[i]);
		struct rimu_cfg *rimu = pdata->rimu + i;

		rimu->id = _RIF_FLD_GET(RIMUPROT_RIMC_M_ID, value) -
			   RIMU_ID_OFFSET;
		rimu->attr = _RIF_FLD_GET(RIMUPROT_RIMC_ATTRx, value);
	}

	return TEE_SUCCESS;
}

#if defined(CFG_STM32MP25)
static TEE_Result stm32_rifsc_dt_conf_risal(const void *fdt, int node,
					    struct rifsc_platdata *pdata)
{
	const fdt32_t *cuint = NULL;
	int i = 0;
	int len = 0;

	cuint = fdt_getprop(fdt, node, "st,risal", &len);
	if (!cuint) {
		DMSG("No RISAL configuration in DT");
		pdata->nrisal = 0;
		return TEE_ERROR_ITEM_NOT_FOUND;
	}

	len = len / sizeof(uint32_t);

	pdata->nrisal = len;
	pdata->risal = calloc(len, sizeof(*pdata->risal));
	if (!pdata->risal)
		return TEE_ERROR_OUT_OF_MEMORY;

	for (i = 0; i < len; i++) {
		uint32_t value = fdt32_to_cpu(cuint[i]);
		struct risal_cfg *risal = pdata->risal + i;

		risal->id = _RIF_FLD_GET(RIFSC_RISAL_REG_ID, value);
		risal->blockid = _RIF_FLD_GET(RIFSC_RISAL_BLOCK_ID, value);
		risal->attr = _RIF_FLD_GET(RIFSC_RISAL_REGx_CFGR, value);
	}

	return TEE_SUCCESS;
}
#endif /* defined(CFG_STM32MP25) */

static TEE_Result stm32_rifsc_parse_fdt(const void *fdt, int node,
					struct rifsc_platdata *pdata)
{
	static struct io_pa_va base;
	TEE_Result res = TEE_ERROR_GENERIC;
	size_t reg_size = 0;

	base.pa = fdt_reg_base_address(fdt, node);
	if (base.pa == DT_INFO_INVALID_REG)
		return TEE_ERROR_BAD_PARAMETERS;

	reg_size = fdt_reg_size(fdt, node);
	if (reg_size == DT_INFO_INVALID_REG_SIZE)
		return TEE_ERROR_BAD_PARAMETERS;

	pdata->base = io_pa_or_va_secure(&base, reg_size);

	res = stm32_rifsc_check_tdcid(&rifsc_pdata.is_tdcid);
	if (res)
		panic();

	res = stm32_rifsc_dt_conf_risup(fdt, node, &pdata->nrisup,
					&pdata->risup);
	if (res && res != TEE_ERROR_ITEM_NOT_FOUND)
		return res;

	if (rifsc_pdata.is_tdcid) {
		res = stm32_rifsc_dt_conf_rimu(fdt, node, pdata);
		if (res && res != TEE_ERROR_ITEM_NOT_FOUND)
			return res;

#if defined(CFG_STM32MP25)
		res = stm32_rifsc_dt_conf_risal(fdt, node, pdata);
		if (res && res != TEE_ERROR_ITEM_NOT_FOUND)
			return res;
#endif /* defined(CFG_STM32MP25) */
	}

	rifsc_pdata.errata_ahbrisab = fdt_getprop(fdt, node,
						  "st,errata-ahbrisab", NULL);

	return TEE_SUCCESS;
}

static TEE_Result stm32_risup_cfg(struct rifsc_platdata *pdata,
				  struct risup_cfg *risup)
{
	struct rifsc_driver_data *drv_data = pdata->drv_data;
	uintptr_t cidcfgr_offset = _OFFSET_PERX_CIDCFGR * risup->id;
	uintptr_t offset = sizeof(uint32_t) * (risup->id / _PERIPH_IDS_PER_REG);
	uint32_t shift = risup->id % _PERIPH_IDS_PER_REG;
	TEE_Result res = TEE_ERROR_GENERIC;

	if (!risup || risup->id >= drv_data->nb_risup)
		return TEE_ERROR_BAD_PARAMETERS;

	if (drv_data->sec_en)
		io_clrsetbits32_stm32shregs(pdata->base + _RIFSC_RISC_SECCFGR0 +
					    offset, BIT(shift),
					    risup->sec << shift);

	if (drv_data->priv_en)
		io_clrsetbits32_stm32shregs(pdata->base +
					    _RIFSC_RISC_PRIVCFGR0 + offset,
					    BIT(shift), risup->priv << shift);

	if (rifsc_pdata.is_tdcid) {
		if (drv_data->rif_en)
			io_write32(pdata->base + _RIFSC_RISC_PER0_CIDCFGR +
				   cidcfgr_offset, risup->cid_attr);

		/* Lock configuration for this RISUP */
		if (risup->lock) {
			DMSG("Locking RIF conf for peripheral n°%"PRIu32,
			     risup->id);
			io_setbits32_stm32shregs(pdata->base +
						 _RIFSC_RISC_RCFGLOCKR0 +
						 offset, BIT(shift));
		}
	}

	/*
	 * Take semaphore if the resource is in semaphore mode
	 * and secured.
	 */
	if (SEM_MODE_INCORRECT(risup->cid_attr) ||
	    !(io_read32(pdata->base + _RIFSC_RISC_SECCFGR0 + offset) &
	      BIT(shift))) {
		res = stm32_rif_release_semaphore(pdata->base +
						  _RIFSC_RISC_PER0_SEMCR +
						  cidcfgr_offset,
						  MAX_CID_SUPPORTED);
		if (res) {
			EMSG("Couldn't release semaphore for resource %u",
			     risup->id);
			return TEE_ERROR_ACCESS_DENIED;
		}
	} else {
		res = stm32_rif_acquire_semaphore(pdata->base +
						  _RIFSC_RISC_PER0_SEMCR +
						  cidcfgr_offset,
						  MAX_CID_SUPPORTED);
		if (res) {
			EMSG("Couldn't acquire semaphore for resource %u",
			     risup->id);
			return TEE_ERROR_ACCESS_DENIED;
		}
	}

	return TEE_SUCCESS;
}

static TEE_Result stm32_risup_setup(struct rifsc_platdata *pdata)
{
	struct rifsc_driver_data *drv_data = pdata->drv_data;
	int i = 0;
	TEE_Result res = TEE_ERROR_GENERIC;

	for (i = 0; i < pdata->nrisup && i < drv_data->nb_risup; i++) {
		struct risup_cfg *risup = pdata->risup + i;

		res = stm32_risup_cfg(pdata, risup);
		if (res) {
			EMSG("risup cfg(%d/%d) error", i + 1, pdata->nrisup);
			return res;
		}
	}

	return TEE_SUCCESS;
}

/*
 * Errata: When CID filtering is enabled on one of RISAB 3/4/5 instances, we
 * forbid the use of CID0 for any initiator on the bus to handle spurious CID0
 * transactions on these RAMs.
 */
static void stm32_rimu_errata_ahbrisab(struct rifsc_platdata *pdata,
				       struct rimu_cfg *rimu)
{
	unsigned int i = 0;

	if (!pdata->errata_ahbrisab)
		return;

	for (i = 0; i < ARRAY_SIZE(rimu_risup); i++) {
		if (rimu->id == rimu_risup[i].rimu_id) {
			rimu->risup_id = rimu_risup[i].risup_id;
			break;
		}
	}

	if (rimu->attr & RIFSC_RIMC_CIDSEL_MASK) {
		/* No inheritance mode for this RIMU */
		if ((rimu->attr & RIFSC_RIMC_MCID_MASK) >>
		    RIFSC_RIMC_MCID_SHIFT == RIF_CID0) {
			EMSG("A CID should be set for RIMU %u", rimu->id);
			if (!IS_ENABLED(CFG_INSECURE))
				panic();
			}
	} else {
		struct risup_cfg *risup = NULL;
		uint32_t risup_cidcfgr = 0;
		int j = 0;

		/* Handle RIMU with no inheritance mode */
		if (!rimu->risup_id) {
			EMSG("RIMU%u cannot be set in inheritance mode",
			     rimu->id);
			if (!IS_ENABLED(CFG_INSECURE))
				panic();
			return;
		}

		for (j = 0; j < pdata->nrisup; j++) {
			if (rimu->risup_id == pdata->risup[j].id) {
				risup = &pdata->risup[j];
				break;
			}
		}

		if (!risup)
			panic();

		risup_cidcfgr = io_read32(pdata->base +
					  _RIFSC_RISC_PER0_CIDCFGR +
					  _OFFSET_PERX_CIDCFGR * risup->id);

		if (!(risup_cidcfgr & RIFSC_RISC_CIDCFGR_CFEN_MASK) ||
		    (!(risup_cidcfgr & RIFSC_RISC_CIDCFGR_SEM_EN_MASK) &&
		     ((risup_cidcfgr & RIFSC_RISC_CIDCFGR_SCID_MASK) >>
		      RIFSC_RISC_CIDCFGR_SCID_SHIFT) == RIF_CID0) ||
		    (risup_cidcfgr & RIFSC_RISC_CIDCFGR_SEM_EN_MASK &&
		     risup_cidcfgr & BIT(RIFSC_RISC_CIDCFGR_SEML_SHIFT))) {
			EMSG("RIMU%u in inheritance mode with CID0", rimu->id);
			if (!IS_ENABLED(CFG_INSECURE))
				panic();
		}
	}
}

static TEE_Result stm32_rimu_cfg(struct rifsc_platdata *pdata,
				 struct rimu_cfg *rimu)
{
	struct rifsc_driver_data *drv_data = pdata->drv_data;
	uintptr_t offset =  _RIFSC_RIMC_ATTR0 + (sizeof(uint32_t) * rimu->id);

	if (!rimu || rimu->id >= drv_data->nb_rimu)
		return TEE_ERROR_BAD_PARAMETERS;

	stm32_rimu_errata_ahbrisab(pdata, rimu);

	if (drv_data->rif_en)
		io_write32(pdata->base + offset, rimu->attr);

	return TEE_SUCCESS;
}

static TEE_Result stm32_rimu_setup(struct rifsc_platdata *pdata)
{
	struct rifsc_driver_data *drv_data = pdata->drv_data;
	int i = 0;
	TEE_Result res = TEE_ERROR_GENERIC;

	for (i = 0; i < pdata->nrimu && i < drv_data->nb_rimu; i++) {
		struct rimu_cfg *rimu = pdata->rimu + i;

		res = stm32_rimu_cfg(pdata, rimu);
		if (res) {
			EMSG("rimu cfg(%d/%d) error", i + 1, pdata->nrimu);
			return res;
		}
	}

	return 0;
}

#if defined(CFG_STM32MP25)
static TEE_Result stm32_risal_cfg(struct rifsc_platdata *pdata,
				  struct risal_cfg *risal)
{
	struct rifsc_driver_data *drv_data = pdata->drv_data;
	uintptr_t offset_a = _RIFSC_RISAL_CFGR0_A(risal->id);
	uintptr_t offset_b = _RIFSC_RISAL_CFGR0_B(risal->id);

	if (!risal || risal->id > drv_data->nb_risal)
		return TEE_ERROR_BAD_PARAMETERS;

	if (drv_data->rif_en) {
		if (risal->blockid == RIFSC_RISAL_BLOCK_A)
			io_write32(pdata->base + offset_a, risal->attr);
		if (risal->blockid == RIFSC_RISAL_BLOCK_B)
			io_write32(pdata->base + offset_b, risal->attr);
	}

	return TEE_SUCCESS;
}

static TEE_Result stm32_risal_setup(struct rifsc_platdata *pdata)
{
	int i = 0;
	TEE_Result res = TEE_ERROR_GENERIC;

	for (i = 0; i < pdata->nrisal; i++) {
		struct risal_cfg *risal = pdata->risal + i;

		res = stm32_risal_cfg(pdata, risal);
		if (res) {
			EMSG("risal cfg(%d/%d) error", i + 1, pdata->nrisal);
			return res;
		}
	}

	return TEE_SUCCESS;
}
#endif /* defined(CFG_STM32MP25) */

TEE_Result stm32_rifsc_reconfigure_risup(unsigned int risup_id,
					 unsigned int cid,
					 bool sec, bool priv, bool cfen)
{
	unsigned int offset = sizeof(uint32_t) *
			      (risup_id / _PERIPH_IDS_PER_REG);
	TEE_Result res = TEE_ERROR_GENERIC;
	struct risup_cfg *risup = NULL;

	if (risup_id > rifsc_pdata.drv_data->nb_risup ||
	    cid > MAX_CID_SUPPORTED)
		return TEE_ERROR_BAD_PARAMETERS;

	risup = &rifsc_pdata.risup[risup_id];

	if (io_read32(rifsc_pdata.base + _RIFSC_RISC_RCFGLOCKR0 + offset) &
	    BIT(risup_id % _PERIPH_IDS_PER_REG)) {
		DMSG("RIMU configuration is locked");
		return TEE_ERROR_ACCESS_DENIED;
	}

	risup->cid_attr = cid << RIFSC_RISC_CIDCFGR_SCID_SHIFT;
	if (cfen)
		risup->cid_attr |= RIFSC_RISC_CIDCFGR_CFEN_MASK;
	else
		risup->cid_attr &= ~RIFSC_RISC_CIDCFGR_CFEN_MASK;

	risup->sec = sec;
	risup->priv = priv;

	res = stm32_risup_cfg(&rifsc_pdata, risup);
	if (res) {
		EMSG("RISUP %u reconfiguration error", risup_id);
		return res;
	}

	return TEE_SUCCESS;
}

bool stm32_rifsc_cid_is_enabled(unsigned int  rifc_id)
{
	struct io_pa_va rifsc_addr = { .pa = RIFSC_BASE };
	vaddr_t rifsc_base = io_pa_or_va(&rifsc_addr, 1);
	uint32_t cidcfgr = io_read32(rifsc_base + _RIFSC_RISC_PER0_CIDCFGR +
				     _OFFSET_PERX_CIDCFGR * rifc_id);

	return (cidcfgr & _CIDCFGR_CFEN) == _CIDCFGR_CFEN;
}

void stm32_rifsc_cid_enable(unsigned int  rifc_id)
{
	struct io_pa_va rifsc_addr = { .pa = RIFSC_BASE };
	vaddr_t rifsc_base = io_pa_or_va(&rifsc_addr, 1);

	io_setbits32_stm32shregs(rifsc_base + _RIFSC_RISC_PER0_CIDCFGR +
				 _OFFSET_PERX_CIDCFGR * rifc_id, _CIDCFGR_CFEN);
}

void stm32_rifsc_cid_disable(unsigned int  rifc_id)
{
	struct io_pa_va rifsc_addr = { .pa = RIFSC_BASE };
	vaddr_t rifsc_base = io_pa_or_va(&rifsc_addr, 1);

	io_clrbits32_stm32shregs(rifsc_base + _RIFSC_RISC_PER0_CIDCFGR +
				 _OFFSET_PERX_CIDCFGR * rifc_id, _CIDCFGR_CFEN);
}

static TEE_Result stm32_rifsc_check_access(struct firewall_query *firewall)
{
	uintptr_t rifsc_base = rifsc_pdata.base;
	unsigned int cid_reg_offset = 0;
	unsigned int periph_offset = 0;
	unsigned int resource_id = 0;
	uint32_t cid_to_check = 0;
	unsigned int reg_id = 0;
	bool priv_check = true;
	bool sec_check = true;
	uint32_t privcfgr = 0;
	uint32_t seccfgr = 0;
	uint32_t cidcfgr = 0;

	assert(rifsc_base);

	if (!firewall || firewall->arg_count != 1)
		return TEE_ERROR_BAD_PARAMETERS;

	/*
	 * Peripheral configuration, we assume the configuration is as
	 * follows:
	 * firewall->args[0]: RIF configuration to check
	 */
	resource_id = firewall->args[0] & RIF_PER_ID_MASK;
	if (resource_id >= RIMU_ID_OFFSET)
		return TEE_SUCCESS;

	reg_id = resource_id / _PERIPH_IDS_PER_REG;
	periph_offset = resource_id % _PERIPH_IDS_PER_REG;
	cid_reg_offset = _OFFSET_PERX_CIDCFGR * resource_id;
	cidcfgr = io_read32(rifsc_base + _RIFSC_RISC_PER0_CIDCFGR +
			    cid_reg_offset);
	seccfgr = io_read32(rifsc_base + _RIFSC_RISC_SECCFGR0 + 0x4 * reg_id);
	privcfgr = io_read32(rifsc_base + _RIFSC_RISC_PRIVCFGR0 + 0x4 * reg_id);
	sec_check = (BIT(RIF_SEC_SHIFT) & firewall->args[0]) != 0;
	priv_check = (BIT(RIF_PRIV_SHIFT) & firewall->args[0]) != 0;
	cid_to_check = (firewall->args[0] & RIF_SCID_MASK) >> RIF_SCID_SHIFT;

	if (!sec_check && seccfgr & BIT(periph_offset))
		return TEE_ERROR_ACCESS_DENIED;

	if (!priv_check && (privcfgr & BIT(periph_offset)))
		return TEE_ERROR_ACCESS_DENIED;

	if (!(cidcfgr & _CIDCFGR_CFEN))
		return TEE_SUCCESS;

	if ((cidcfgr & _CIDCFGR_SEMEN &&
	     !SEM_EN_AND_OK(cidcfgr, cid_to_check)) ||
	    (!(cidcfgr & _CIDCFGR_SEMEN) &&
	     !SCID_OK(cidcfgr, RIFSC_RISC_CIDCFGR_SCID_MASK, cid_to_check)))
		return TEE_ERROR_BAD_PARAMETERS;

	return TEE_SUCCESS;
}

static TEE_Result stm32_rifsc_acquire_access(struct firewall_query *firewall)
{
	uintptr_t rifsc_base = rifsc_pdata.base;
	unsigned int cid_reg_offset = 0;
	unsigned int periph_offset = 0;
	unsigned int resource_id = 0;
	unsigned int reg_id = 0;
	uint32_t cidcfgr = 0;
	uint32_t seccfgr = 0;

	assert(rifsc_base);

	if (!firewall || !firewall->arg_count)
		return TEE_ERROR_BAD_PARAMETERS;

	/*
	 * Peripheral configuration, we assume the configuration is as
	 * follows:
	 * firewall->args[0]: Firewall ID of the resource to acquire
	 */
	resource_id = firewall->args[0] & RIF_PER_ID_MASK;
	if (resource_id >= RIMU_ID_OFFSET)
		return TEE_SUCCESS;

	reg_id = resource_id / _PERIPH_IDS_PER_REG;
	periph_offset = resource_id % _PERIPH_IDS_PER_REG;

	cid_reg_offset = _OFFSET_PERX_CIDCFGR * resource_id;
	cidcfgr = io_read32(rifsc_base + _RIFSC_RISC_PER0_CIDCFGR +
			    cid_reg_offset);

	seccfgr = io_read32(rifsc_base + _RIFSC_RISC_SECCFGR0 + 0x4 * reg_id);
	if (!(seccfgr & BIT(periph_offset)))
		return TEE_ERROR_ACCESS_DENIED;

	/* Only check CID attributes */
	if (!(cidcfgr & _CIDCFGR_CFEN))
		return TEE_SUCCESS;

	if (cidcfgr & _CIDCFGR_SEMEN) {
		if (!SEM_EN_AND_OK(cidcfgr, RIF_CID1))
			return TEE_ERROR_BAD_PARAMETERS;

		/* Take the semaphore, static CID is irrelevant here */
		return stm32_rif_acquire_semaphore(rifsc_base +
						   _RIFSC_RISC_PER0_SEMCR +
						   cid_reg_offset,
						   MAX_CID_SUPPORTED);
	}

	if (!SCID_OK(cidcfgr, RIFSC_RISC_CIDCFGR_SCID_MASK, RIF_CID1))
		return TEE_ERROR_ACCESS_DENIED;

	return TEE_SUCCESS;
}

static TEE_Result stm32_rifsc_set_config(struct firewall_query *firewall)
{
	struct rimu_cfg rimu = { };
	unsigned int id = 0;
	uint32_t conf = 0;

	if (!firewall || firewall->arg_count != 1)
		return TEE_ERROR_BAD_PARAMETERS;

	/*
	 * Peripheral configuration, we assume the configuration is as
	 * follows:
	 * firewall->args[0]: RIF configuration to set
	 */
	id = firewall->args[0] & RIF_PER_ID_MASK;
	conf = firewall->args[0];

	if (id < RIMU_ID_OFFSET) {
		struct risup_cfg risup = { };
		uint32_t cidcfgr = 0;

		risup.id = id;
		risup.sec = (BIT(RIF_SEC_SHIFT) & conf) != 0;
		risup.priv = (BIT(RIF_PRIV_SHIFT) & conf) != 0;
		risup.lock = (BIT(RIF_LOCK_SHIFT) & conf) != 0;
		risup.cid_attr = _RIF_FLD_GET(RIF_PERx_CID, conf);

		if (!rifsc_pdata.is_tdcid) {
			cidcfgr = io_read32(rifsc_pdata.base +
					    _OFFSET_PERX_CIDCFGR * risup.id +
					    _RIFSC_RISC_PER0_CIDCFGR);

			if (cidcfgr != risup.cid_attr)
				return TEE_ERROR_BAD_PARAMETERS;
		}

		DMSG("Setting config for peripheral: %u, %s, %s, cid attr: %#"PRIx32", %s",
		     id, risup.sec ? "Secure" : "Non secure",
		     risup.priv ? "Privileged" : "Non privileged",
		     risup.cid_attr, risup.lock ? "Locked" : "Unlocked");

		return stm32_risup_cfg(&rifsc_pdata, &risup);
	}

	if (!rifsc_pdata.is_tdcid)
		return TEE_ERROR_ACCESS_DENIED;

	rimu.id = _RIF_FLD_GET(RIMUPROT_RIMC_M_ID, conf) - RIMU_ID_OFFSET;
	rimu.attr = _RIF_FLD_GET(RIMUPROT_RIMC_ATTRx, conf);

	return stm32_rimu_cfg(&rifsc_pdata, &rimu);
}

static void stm32_rifsc_release_access(struct firewall_query *firewall)
{
	uintptr_t rifsc_base = rifsc_pdata.base;
	uint32_t cidcfgr = 0;
	uint32_t id = 0;

	assert(rifsc_base && firewall && firewall->arg_count);

	id = firewall->args[0];

	if (id >= RIMU_ID_OFFSET)
		return;

	cidcfgr = io_read32(rifsc_base + _RIFSC_RISC_PER0_CIDCFGR +
			    _OFFSET_PERX_CIDCFGR * id);

	/* Only thing possible is to release a semaphore taken by OP-TEE CID */
	if (SEM_EN_AND_OK(cidcfgr, RIF_CID1))
		if (stm32_rif_release_semaphore(rifsc_base +
						_RIFSC_RISC_PER0_SEMCR +
						id * _OFFSET_PERX_CIDCFGR,
						MAX_CID_SUPPORTED))
			panic("Could not release the RIF semaphore");
}

static TEE_Result stm32_rifsc_sem_pm_suspend(void)
{
	int i = 0;

	for (i = 0; i < rifsc_pdata.nrisup && i < rifsc_drvdata.nb_risup; i++) {
		uint32_t semcfgr = io_read32(rifsc_pdata.base +
					     _RIFSC_RISC_PER0_SEMCR +
					     _OFFSET_PERX_CIDCFGR * i);
		struct risup_cfg *risup = rifsc_pdata.risup + i;

		/* Save semaphores that were taken by the CID1 */
		risup->pm_sem = semcfgr & _SEMCR_MUTEX &&
				((semcfgr & _SEMCR_SEMCID_MASK) >>
				 _SEMCR_SEMCID_SHIFT == RIF_CID1) ?
				true : false;

		FMSG("RIF semaphore %s for ID: %u",
		     risup->pm_sem ? "SAVED" : "NOT SAVED", risup->id);
	}

	return TEE_SUCCESS;
}

static TEE_Result stm32_rifsc_sem_pm_resume(void)
{
	TEE_Result res = TEE_ERROR_GENERIC;
	int i = 0;

	for (i = 0; i < rifsc_pdata.nrisup && i < rifsc_drvdata.nb_risup; i++) {
		struct risup_cfg *risup = rifsc_pdata.risup + i;
		uintptr_t cidcfgr_offset = _OFFSET_PERX_CIDCFGR * risup->id;
		uintptr_t offset = sizeof(uint32_t) *
				   (risup->id / _PERIPH_IDS_PER_REG);
		uintptr_t perih_offset = risup->id % _PERIPH_IDS_PER_REG;
		uint32_t seccgfr = io_read32(rifsc_pdata.base +
					     _RIFSC_RISC_SECCFGR0 + offset);
		uint32_t privcgfr = io_read32(rifsc_pdata.base +
					      _RIFSC_RISC_PRIVCFGR0 + offset);
		uint32_t lockcfgr = io_read32(rifsc_pdata.base +
					      _RIFSC_RISC_RCFGLOCKR0 + offset);

		/* Update RISUPs fields */
		risup->cid_attr = io_read32(rifsc_pdata.base +
					    _RIFSC_RISC_PER0_CIDCFGR +
					    cidcfgr_offset);
		risup->sec = (bool)(seccgfr & BIT(perih_offset));
		risup->priv = (bool)(privcgfr & BIT(perih_offset));
		risup->lock = (bool)(lockcfgr & BIT(perih_offset));

		/* Acquire available appropriate semaphores */
		if (SEM_MODE_INCORRECT(risup->cid_attr) || !risup->pm_sem)
			continue;

		res = stm32_rif_acquire_semaphore(rifsc_pdata.base +
						  _RIFSC_RISC_PER0_SEMCR +
						  cidcfgr_offset,
						  MAX_CID_SUPPORTED);
		if (res) {
			EMSG("Could not acquire semaphore for resource %u",
			     risup->id);
			return TEE_ERROR_ACCESS_DENIED;
		}
	}

	return TEE_SUCCESS;
}

static TEE_Result
stm32_rifsc_sem_pm(enum pm_op op, unsigned int pm_hint,
		   const struct pm_callback_handle *pm_handle __unused)
{
	TEE_Result res = TEE_ERROR_GENERIC;

	if (!PM_HINT_IS_STATE(pm_hint, CONTEXT))
		return TEE_SUCCESS;

	if (op == PM_OP_RESUME)
		res = stm32_rifsc_sem_pm_resume();
	else
		res = stm32_rifsc_sem_pm_suspend();

	return res;
}

TEE_Result stm32_rifsc_check_tdcid(bool *tdcid_state)
{
	if (!rifsc_pdata.base)
		return TEE_ERROR_DEFER_DRIVER_INIT;

	*tdcid_state = false;

	if (((io_read32(rifsc_pdata.base + _RIFSC_RIMC_CR) &
	     _RIFSC_RIMC_CR_TDCID_MASK)) == (RIF_CID1 << _CIDCFGR_SCID_SHIFT))
		*tdcid_state = true;

	return TEE_SUCCESS;
}

static TEE_Result
stm32_rifsc_dt_probe_bus(const void *fdt, int node,
			 struct firewall_controller *ctrl __maybe_unused)
{
	TEE_Result res = TEE_ERROR_GENERIC;
	struct firewall_query *fw = NULL;
	int subnode = 0;

	DMSG("Populating %s firewall bus", ctrl->name);

	fdt_for_each_subnode(subnode, fdt, node) {
		unsigned int i = 0;

		if (fdt_get_status(fdt, subnode) == DT_STATUS_DISABLED)
			continue;

		if (IS_ENABLED(CFG_INSECURE) &&
		    stm32mp_allow_probe_shared_device(fdt, subnode)) {
			DMSG("Skipping firewall attributes check for %s",
			     fdt_get_name(fdt, subnode, NULL));
			goto skip_check;
		}

		DMSG("Acquiring firewall access for %s when probing bus",
		     fdt_get_name(fdt, subnode, NULL));

		do {
			/*
			 * The access-controllers property is mandatory for
			 * firewall bus devices
			 */
			res = firewall_dt_get_by_index(fdt, subnode, i, &fw);
			if (res == TEE_ERROR_ITEM_NOT_FOUND) {
				/* Stop when nothing more to parse */
				break;
			} else if (res) {
				EMSG("%s: Error on node %s: %#"PRIx32,
				     ctrl->name,
				     fdt_get_name(fdt, subnode, NULL), res);
				panic();
			}

			res = firewall_acquire_access(fw);
			if (res) {
				EMSG("%s: %s not accessible: %#"PRIx32,
				     ctrl->name,
				     fdt_get_name(fdt, subnode, NULL), res);
				panic();
			}

			firewall_put(fw);
			i++;
		} while (true);

skip_check:
		res = dt_driver_maybe_add_probe_node(fdt, subnode);
		if (res) {
			EMSG("Failed on node %s with %#"PRIx32,
			     fdt_get_name(fdt, subnode, NULL), res);
			panic();
		}
	}

	return TEE_SUCCESS;
}

static const struct firewall_controller_ops firewall_ops = {
	.set_conf = stm32_rifsc_set_config,
	.check_access = stm32_rifsc_check_access,
	.acquire_access = stm32_rifsc_acquire_access,
	.release_access = stm32_rifsc_release_access,
};

static TEE_Result stm32_rifsc_probe(const void *fdt, int node,
				    const void *compat_data __unused)
{
	struct firewall_controller *controller = NULL;
	TEE_Result res = TEE_ERROR_GENERIC;

	res = stm32_rifsc_parse_fdt(fdt, node, &rifsc_pdata);
	if (res)
		return res;

	if (!rifsc_pdata.drv_data)
		stm32_rifsc_get_driverdata(&rifsc_pdata);

	res = stm32_risup_setup(&rifsc_pdata);
	if (res)
		panic();

	if (rifsc_pdata.is_tdcid) {
		res = stm32_rimu_setup(&rifsc_pdata);
		if (res)
			panic();

#if defined(CFG_STM32MP25)
		res = stm32_risal_setup(&rifsc_pdata);
		if (res)
			panic();
#endif /* defined(CFG_STM32MP25) */
	}

	stm32_rifsc_glock_config(fdt, node, &rifsc_pdata);

	controller = calloc(1, sizeof(*controller));
	if (!controller)
		panic();

	controller->name = "RIFSC";
	controller->priv = &rifsc_pdata;
	controller->ops = &firewall_ops;

	res = firewall_dt_controller_register(fdt, node, controller);
	if (res)
		panic();

	res = stm32_rifsc_dt_probe_bus(fdt, node, controller);
	if (res)
		panic();

	register_pm_core_service_cb(stm32_rifsc_sem_pm, NULL,
				    "stm32-rifsc-semaphores");

	return TEE_SUCCESS;
}

static const struct dt_device_match rifsc_match_table[] = {
	{
		.compatible = "st,stm32mp25-rifsc",
	},
	{ }
};

DEFINE_DT_DRIVER(rifsc_dt_driver) = {
	.name = "stm32-rifsc",
	.match_table = rifsc_match_table,
	.probe = stm32_rifsc_probe,
};
