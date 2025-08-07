// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2019-2022, STMicroelectronics
 */
#include <assert.h>
#include <compiler.h>
#include <config.h>
#include <confine_array_index.h>
#include <drivers/clk.h>
#include <drivers/clk_dt.h>
#include <drivers/regulator.h>
#include <drivers/rstctrl.h>
#include <drivers/scmi-msg.h>
#include <drivers/scmi.h>
#include <drivers/stm32_cpu_opp.h>
#include <drivers/stm32mp_dt_bindings.h>
#include <drivers/stm32_etzpc.h>
#include <drivers/stm32_remoteproc.h>
#include <initcall.h>
#include <keep.h>
#include <kernel/pm.h>
#include <mm/core_memprot.h>
#include <mm/core_mmu.h>
#include <platform_config.h>
#include <stdint.h>
#include <speculation_barrier.h>
#include <stm32_util.h>
#include <string.h>
#include <tee_api_defines.h>
#include <util.h>

#define TIMEOUT_US_1MS		1000

#define SCMI_CLOCK_NAME_SIZE	16
#define SCMI_RD_NAME_SIZE	16
#define SCMI_VOLTD_NAME_SIZE	16

/*
 * struct stm32_scmi_clk - Data for the exposed clock
 * @clock_id: Clock identifier in RCC clock driver
 * @name: Clock string ID exposed to channel
 * @enabled: State of the SCMI clock
 */
struct stm32_scmi_clk {
	unsigned long clock_id;
	struct clk *clk;
	const char *name;
	bool enabled;
};

/*
 * struct stm32_scmi_rd - Data for the exposed reset controller
 * @reset_id: Reset identifier in RCC reset driver
 * @fw_id: Firewall ID of the peripheral
 * @name: Reset string ID exposed to channel
 * @rstctrl: Reset controller device
 */
struct stm32_scmi_rd {
	unsigned long reset_id;
	unsigned int fw_id;
	const char *name;
	struct rstctrl *rstctrl;
};

/*
 * struct stm32_scmi_perfd - Data for the exposed performance domains
 * @name: Performance domain string ID (aka name) exposed to channel
 */
struct stm32_scmi_perfd {
	const char *name;
};

#if CFG_STM32MP1_SCMI_SHM_BASE
register_phys_mem(MEM_AREA_IO_NSEC, CFG_STM32MP1_SCMI_SHM_BASE,
		  CFG_STM32MP1_SCMI_SHM_SIZE);

/* Locate all non-secure SMT message buffers in last page of SYSRAM */
#define SMT_BUFFER_BASE		CFG_STM32MP1_SCMI_SHM_BASE

#if (SMT_BUFFER_BASE + SMT_BUF_SLOT_SIZE > \
	CFG_STM32MP1_SCMI_SHM_BASE + CFG_STM32MP1_SCMI_SHM_SIZE)
#error "SCMI shared memory mismatch"
#endif
#endif /*CFG_STM32MP1_SCMI_SHM_BASE*/

#define CLOCK_CELL(_scmi_id, _id, _name, _init_enabled) \
	[(_scmi_id)] = { \
		.clock_id = (_id), \
		.name = (_name), \
		.enabled = (_init_enabled), \
	}

#define RESET_CELL(_scmi_id, _id, _fw_id, _name) \
	[(_scmi_id)] = { \
		.reset_id = (_id), \
		.fw_id = (_fw_id), \
		.name = (_name), \
	}

#ifdef CFG_STM32MP13
static struct stm32_scmi_clk stm32_scmi_clock[] = {
	CLOCK_CELL(CK_SCMI_HSE, CK_HSE, "ck_hse", true),
	CLOCK_CELL(CK_SCMI_HSI, CK_HSI, "ck_hsi", true),
	CLOCK_CELL(CK_SCMI_CSI, CK_CSI, "ck_csi", true),
	CLOCK_CELL(CK_SCMI_LSE, CK_LSE, "ck_lse", true),
	CLOCK_CELL(CK_SCMI_LSI, CK_LSI, "ck_lsi", true),
	CLOCK_CELL(CK_SCMI_HSE_DIV2, CK_HSE_DIV2, "clk-hse-div2", true),
	CLOCK_CELL(CK_SCMI_PLL2_Q, PLL2_Q, "pll2_q", true),
	CLOCK_CELL(CK_SCMI_PLL2_R, PLL2_R, "pll2_r", true),
	CLOCK_CELL(CK_SCMI_PLL3_P, PLL3_P, "pll3_p", true),
	CLOCK_CELL(CK_SCMI_PLL3_Q, PLL3_Q, "pll3_q", true),
	CLOCK_CELL(CK_SCMI_PLL3_R, PLL3_R, "pll3_r", true),
	CLOCK_CELL(CK_SCMI_PLL4_P, PLL4_P, "pll4_p", true),
	CLOCK_CELL(CK_SCMI_PLL4_Q, PLL4_Q, "pll4_q", true),
	CLOCK_CELL(CK_SCMI_PLL4_R, PLL4_R, "pll4_r", true),
	CLOCK_CELL(CK_SCMI_MPU, CK_MPU, "ck_mpu", true),
	CLOCK_CELL(CK_SCMI_AXI, CK_AXI, "ck_axi", true),
	CLOCK_CELL(CK_SCMI_MLAHB, CK_MLAHB, "ck_mlahb", true),
	CLOCK_CELL(CK_SCMI_CKPER, CK_PER, "ck_per", true),
	CLOCK_CELL(CK_SCMI_PCLK1, PCLK1, "pclk1", true),
	CLOCK_CELL(CK_SCMI_PCLK2, PCLK2, "pclk2", true),
	CLOCK_CELL(CK_SCMI_PCLK3, PCLK3, "pclk3", true),
	CLOCK_CELL(CK_SCMI_PCLK4, PCLK4, "pclk4", true),
	CLOCK_CELL(CK_SCMI_PCLK5, PCLK5, "pclk5", true),
	CLOCK_CELL(CK_SCMI_PCLK6, PCLK6, "pclk6", true),
	CLOCK_CELL(CK_SCMI_CKTIMG1, CK_TIMG1, "timg1_ck", true),
	CLOCK_CELL(CK_SCMI_CKTIMG2, CK_TIMG2, "timg2_ck", true),
	CLOCK_CELL(CK_SCMI_CKTIMG3, CK_TIMG3, "timg3_ck", true),
	CLOCK_CELL(CK_SCMI_RTC, RTC, "ck_rtc", true),
	CLOCK_CELL(CK_SCMI_RTCAPB, RTCAPB, "rtcapb", true),
	CLOCK_CELL(CK_SCMI_BSEC, BSEC, "bsec", true),
};
#endif

#ifdef CFG_STM32MP15
static struct stm32_scmi_clk stm32_scmi_clock[] = {
	CLOCK_CELL(CK_SCMI_HSE, CK_HSE, "ck_hse", true),
	CLOCK_CELL(CK_SCMI_HSI, CK_HSI, "ck_hsi", true),
	CLOCK_CELL(CK_SCMI_CSI, CK_CSI, "ck_csi", true),
	CLOCK_CELL(CK_SCMI_LSE, CK_LSE, "ck_lse", true),
	CLOCK_CELL(CK_SCMI_LSI, CK_LSI, "ck_lsi", true),
	CLOCK_CELL(CK_SCMI_PLL2_Q, PLL2_Q, "pll2_q", true),
	CLOCK_CELL(CK_SCMI_PLL2_R, PLL2_R, "pll2_r", true),
	CLOCK_CELL(CK_SCMI_MPU, CK_MPU, "ck_mpu", true),
	CLOCK_CELL(CK_SCMI_AXI, CK_AXI, "ck_axi", true),
	CLOCK_CELL(CK_SCMI_BSEC, BSEC, "bsec", true),
	CLOCK_CELL(CK_SCMI_CRYP1, CRYP1, "cryp1", false),
	CLOCK_CELL(CK_SCMI_GPIOZ, GPIOZ, "gpioz", false),
	CLOCK_CELL(CK_SCMI_HASH1, HASH1, "hash1", false),
	CLOCK_CELL(CK_SCMI_I2C4, I2C4_K, "i2c4_k", false),
	CLOCK_CELL(CK_SCMI_I2C6, I2C6_K, "i2c6_k", false),
	CLOCK_CELL(CK_SCMI_IWDG1, IWDG1, "iwdg1", false),
	CLOCK_CELL(CK_SCMI_RNG1, RNG1_K, "rng1_k", true),
	CLOCK_CELL(CK_SCMI_RTC, RTC, "ck_rtc", true),
	CLOCK_CELL(CK_SCMI_RTCAPB, RTCAPB, "rtcapb", true),
	CLOCK_CELL(CK_SCMI_SPI6, SPI6_K, "spi6_k", false),
	CLOCK_CELL(CK_SCMI_USART1, USART1_K, "usart1_k", false),
};
#endif

#ifdef CFG_STM32MP13
static struct stm32_scmi_rd stm32_scmi_reset_domain[] = {
	RESET_CELL(RST_SCMI_LTDC, LTDC_R, STM32MP1_ETZPC_LTDC_ID, "ltdc"),
	RESET_CELL(RST_SCMI_MDMA, MDMA_R, STM32MP1_ETZPC_MAX_ID, "mdma"),
};
#endif

#ifdef CFG_STM32MP15
static struct stm32_scmi_rd stm32_scmi_reset_domain[] = {
	RESET_CELL(RST_SCMI_SPI6, SPI6_R, STM32MP1_ETZPC_SPI6_ID, "spi6"),
	RESET_CELL(RST_SCMI_I2C4, I2C4_R, STM32MP1_ETZPC_I2C4_ID, "i2c4"),
	RESET_CELL(RST_SCMI_I2C6, I2C6_R, STM32MP1_ETZPC_I2C6_ID, "i2c6"),
	RESET_CELL(RST_SCMI_USART1, USART1_R, STM32MP1_ETZPC_USART1_ID,
		   "usart1"),
	RESET_CELL(RST_SCMI_STGEN, STGEN_R, STM32MP1_ETZPC_STGENC_ID, "stgen"),
	RESET_CELL(RST_SCMI_GPIOZ, GPIOZ_R, STM32MP1_ETZPC_GPIOZ_ID, "gpioz"),
	RESET_CELL(RST_SCMI_CRYP1, CRYP1_R, STM32MP1_ETZPC_CRYP1_ID, "cryp1"),
	RESET_CELL(RST_SCMI_HASH1, HASH1_R, STM32MP1_ETZPC_HASH1_ID, "hash1"),
	RESET_CELL(RST_SCMI_RNG1, RNG1_R, STM32MP1_ETZPC_RNG1_ID, "rng1"),
	RESET_CELL(RST_SCMI_MDMA, MDMA_R, STM32MP1_ETZPC_MAX_ID, "mdma"),
	RESET_CELL(RST_SCMI_MCU, MCU_R, STM32MP1_ETZPC_MAX_ID, "mcu"),
	RESET_CELL(RST_SCMI_MCU_HOLD_BOOT, MCU_HOLD_BOOT_R,
		   STM32MP1_ETZPC_MAX_ID, "mcu_hold_boot"),
};
#endif

struct channel_resources {
	struct scmi_msg_channel *channel;
	struct stm32_scmi_clk *clock;
	size_t clock_count;
	struct stm32_scmi_rd *rd;
	size_t rd_count;
#ifdef CFG_SCMI_MSG_PERF_DOMAIN
	struct stm32_scmi_perfd *perfd;
	size_t perfd_count;
#endif
};

#define PERFD_CELL(_scmi_id, _name) \
	[_scmi_id] = { \
		.name = (_name), \
	}

struct stm32_scmi_perfd scmi_performance_domain[] = {
	PERFD_CELL(0 /* PERFD_SCMI0_CPU_OPP */, "cpu-opp"),
};

static const struct channel_resources scmi_channel[] = {
	[0] = {
		.channel = &(struct scmi_msg_channel){
#if CFG_STM32MP1_SCMI_SHM_BASE
			.shm_addr = { .pa = SMT_BUFFER_BASE },
			.shm_size = SMT_BUF_SLOT_SIZE,
#endif
		},
		.clock = stm32_scmi_clock,
		.clock_count = ARRAY_SIZE(stm32_scmi_clock),
		.rd = stm32_scmi_reset_domain,
		.rd_count = ARRAY_SIZE(stm32_scmi_reset_domain),
#ifdef CFG_SCMI_MSG_PERF_DOMAIN
		.perfd = scmi_performance_domain,
		.perfd_count = ARRAY_SIZE(scmi_performance_domain),
#endif
	},
};

static const struct channel_resources *find_resource(unsigned int channel_id)
{
	assert(channel_id < ARRAY_SIZE(scmi_channel));

	return scmi_channel + channel_id;
}

struct scmi_msg_channel *plat_scmi_get_channel(unsigned int channel_id)
{
	const size_t max_id = ARRAY_SIZE(scmi_channel);
	unsigned int confined_id = confine_array_index(channel_id, max_id);

	if (channel_id >= max_id)
		return NULL;

	return find_resource(confined_id)->channel;
}

static size_t __maybe_unused plat_scmi_protocol_count_paranoid(void)
{
	unsigned int n = 0;
	unsigned int count = 0;
	const size_t channel_count = ARRAY_SIZE(scmi_channel);

	for (n = 0; n < channel_count; n++)
		if (scmi_channel[n].clock_count)
			break;
	if (n < channel_count)
		count++;

	for (n = 0; n < channel_count; n++)
		if (scmi_channel[n].rd_count)
			break;
	if (n < channel_count)
		count++;

	for (n = 0; n < channel_count; n++)
		if (IS_ENABLED(CFG_SCMI_MSG_REGULATOR_CONSUMER) &&
		    plat_scmi_voltd_count(n))
			break;
	if (n < channel_count)
		count++;

#ifdef CFG_SCMI_MSG_PERF_DOMAIN
	for (n = 0; n < channel_count; n++)
		if (scmi_channel[n].perfd_count)
			break;
	if (n < channel_count)
		count++;
#endif

	return count;
}

static const char vendor[] = "ST";
static const char sub_vendor[] = "";

const char *plat_scmi_vendor_name(void)
{
	return vendor;
}

const char *plat_scmi_sub_vendor_name(void)
{
	return sub_vendor;
}

/* Currently supporting Clocks and Reset Domains */
static const uint8_t plat_protocol_list[] = {
	SCMI_PROTOCOL_ID_CLOCK,
	SCMI_PROTOCOL_ID_RESET_DOMAIN,
#ifdef CFG_SCMI_MSG_REGULATOR_CONSUMER
	SCMI_PROTOCOL_ID_VOLTAGE_DOMAIN,
#endif
#ifdef CFG_SCMI_MSG_PERF_DOMAIN
	SCMI_PROTOCOL_ID_PERF,
#endif
	0 /* Null termination */
};

size_t plat_scmi_protocol_count(void)
{
	const size_t count = ARRAY_SIZE(plat_protocol_list) - 1;

	assert(count == plat_scmi_protocol_count_paranoid());

	return count;
}

const uint8_t *plat_scmi_protocol_list(unsigned int channel_id __unused)
{
	assert(plat_scmi_protocol_count_paranoid() ==
	       (ARRAY_SIZE(plat_protocol_list) - 1));

	return plat_protocol_list;
}

/*
 * Platform SCMI clocks
 */
static struct stm32_scmi_clk *find_clock(unsigned int channel_id,
					 unsigned int scmi_id)
{
	const struct channel_resources *resource = find_resource(channel_id);

	if (resource && scmi_id < resource->clock_count)
		return &resource->clock[scmi_id];

	return NULL;
}

size_t plat_scmi_clock_count(unsigned int channel_id)
{
	const struct channel_resources *resource = find_resource(channel_id);

	if (!resource)
		return 0;

	return resource->clock_count;
}

const char *plat_scmi_clock_get_name(unsigned int channel_id,
				     unsigned int scmi_id)
{
	struct stm32_scmi_clk *clock = find_clock(channel_id, scmi_id);

	if (!clock || !stm32mp_nsec_can_access_clock(clock->clock_id))
		return NULL;

	return clock->name;
}

int32_t plat_scmi_clock_rates_array(unsigned int channel_id,
				    unsigned int scmi_id, size_t start_index,
				    unsigned long *array, size_t *nb_elts)
{
	/*
	 * Be warned that Linux kernel as of v5.4.24 may not fully support
	 * clock rates by array description.
	 */
	struct stm32_scmi_clk *clock = find_clock(channel_id, scmi_id);
	TEE_Result __maybe_unused res = TEE_ERROR_GENERIC;

	assert(nb_elts && (!*nb_elts || array));

	if (!clock)
		return SCMI_NOT_FOUND;

	if (!stm32mp_nsec_can_access_clock(clock->clock_id))
		return SCMI_DENIED;

	switch (scmi_id) {
	case CK_SCMI_MPU:
		/* MPU clock shall use plat_scmi_clock_rates_by_step() */
		return SCMI_NOT_SUPPORTED;
#ifdef CFG_STM32MP13
	case CK_SCMI_PLL4_Q:
		res = clk_get_rates_array(clock->clk, start_index, array,
					  nb_elts);
		if (res == TEE_ERROR_NOT_SUPPORTED)
			return SCMI_NOT_SUPPORTED;
		if (res)
			return SCMI_GENERIC_ERROR;
		return SCMI_SUCCESS;
#endif
	default:
		if (start_index)
			return SCMI_INVALID_PARAMETERS;
		if (*nb_elts)
			*array = clk_get_rate(clock->clk);
		*nb_elts = 1;

		return SCMI_SUCCESS;
	}
}

int32_t plat_scmi_clock_rates_by_step(unsigned int channel_id,
				      unsigned int scmi_id,
				      unsigned long *array)
{
	struct stm32_scmi_clk *clock = find_clock(channel_id, scmi_id);

	if (!clock)
		return SCMI_NOT_FOUND;

	if (!stm32mp_nsec_can_access_clock(clock->clock_id))
		return SCMI_DENIED;

	switch (scmi_id) {
	case CK_SCMI_MPU:
		/*
		 * Pretend we support all rates for MPU clock,
		 * CLOCK_RATE_SET will reject unsupported rates.
		 */
		array[0] = 0U;
		array[1] = UINT32_MAX;
		array[2] = 1U;
		return SCMI_SUCCESS;
	default:
		return SCMI_NOT_SUPPORTED;
	}
}

int32_t plat_scmi_clock_set_rate(unsigned int channel_id, unsigned int scmi_id,
				 unsigned long rate)
{
	struct stm32_scmi_clk *clock = find_clock(channel_id, scmi_id);

	if (!clock)
		return SCMI_NOT_FOUND;

	if (!stm32mp_nsec_can_access_clock(clock->clock_id))
		return SCMI_DENIED;

	switch (scmi_id) {
#if defined(CFG_STM32MP15) && defined(CFG_STM32_CPU_OPP)
	case CK_SCMI_MPU:
		if (stm32mp1_set_opp_khz(rate / 1000))
			return SCMI_INVALID_PARAMETERS;
		break;
#endif
#ifdef CFG_STM32MP13
	case CK_SCMI_PLL4_Q:
		if (clk_set_rate(clock->clk, rate))
			return SCMI_INVALID_PARAMETERS;
		break;
#endif
	default:
		if (rate != clk_get_rate(clock->clk))
			return SCMI_INVALID_PARAMETERS;
		break;
	}

	return SCMI_SUCCESS;
}

unsigned long plat_scmi_clock_get_rate(unsigned int channel_id,
				       unsigned int scmi_id)
{
	struct stm32_scmi_clk *clock = find_clock(channel_id, scmi_id);

	if (!clock || !stm32mp_nsec_can_access_clock(clock->clock_id))
		return 0;

	return clk_get_rate(clock->clk);
}

unsigned long plat_scmi_clock_round_rate(unsigned int channel_id,
					 unsigned int scmi_id,
					 unsigned long rate)
{
	struct stm32_scmi_clk *clock = find_clock(channel_id, scmi_id);

	if (!clock || !stm32mp_nsec_can_access_clock(clock->clock_id))
		return 0;

	return clk_round_rate(stm32mp_rcc_clock_id_to_clk(clock->clock_id), rate);
}

int32_t plat_scmi_clock_get_state(unsigned int channel_id, unsigned int scmi_id)
{
	struct stm32_scmi_clk *clock = find_clock(channel_id, scmi_id);

	if (!clock || !stm32mp_nsec_can_access_clock(clock->clock_id))
		return 0;

	return (int32_t)clock->enabled;
}

int32_t plat_scmi_clock_set_state(unsigned int channel_id, unsigned int scmi_id,
				  bool enable_not_disable)
{
	struct stm32_scmi_clk *clock = find_clock(channel_id, scmi_id);

	if (!clock)
		return SCMI_NOT_FOUND;

	if (!stm32mp_nsec_can_access_clock(clock->clock_id))
		return SCMI_DENIED;

	if (enable_not_disable) {
		if (!clock->enabled) {
			FMSG("SCMI clock %u enable", scmi_id);
			clk_enable(clock->clk);
			clock->enabled = true;
		}
	} else {
		if (clock->enabled) {
			FMSG("SCMI clock %u disable", scmi_id);
			clk_disable(clock->clk);
			clock->enabled = false;
		}
	}

	return SCMI_SUCCESS;
}

int32_t plat_scmi_clock_get_duty_cycle(unsigned int channel_id, unsigned int scmi_id,
				       struct clk_duty *duty)
{
	struct stm32_scmi_clk *clock = find_clock(channel_id, scmi_id);
	struct clk *clk_ref = NULL;
	int ret = 0;

	if (!clock || !stm32mp_nsec_can_access_clock(clock->clock_id))
		return SCMI_DENIED;

	clk_ref = stm32mp_rcc_clock_id_to_clk(clock->clock_id);

	ret = clk_get_duty_cycle(clk_ref, duty);
	if (ret)
		return SCMI_NOT_SUPPORTED;

	return SCMI_SUCCESS;
}

/*
 * Platform SCMI reset domains
 */
static struct stm32_scmi_rd *find_rd(unsigned int channel_id,
				     unsigned int scmi_id)
{
	const struct channel_resources *resource = find_resource(channel_id);

	if (resource && scmi_id < resource->rd_count)
		return &resource->rd[scmi_id];

	return NULL;
}

const char *plat_scmi_rd_get_name(unsigned int channel_id, unsigned int scmi_id)
{
	const struct stm32_scmi_rd *rd = find_rd(channel_id, scmi_id);

	if (!rd)
		return NULL;

	return rd->name;
}

size_t plat_scmi_rd_count(unsigned int channel_id)
{
	const struct channel_resources *resource = find_resource(channel_id);

	if (!resource)
		return 0;

	return resource->rd_count;
}

int32_t plat_scmi_rd_autonomous(unsigned int channel_id, unsigned int scmi_id,
				uint32_t state)
{
	const struct stm32_scmi_rd *rd = find_rd(channel_id, scmi_id);
	unsigned int id = 0;

	if (!rd)
		return SCMI_NOT_FOUND;

	id = rd->fw_id;

	if (id != STM32MP1_ETZPC_MAX_ID && stm32_etzpc_check_ns_access(id))
		return SCMI_DENIED;
	assert(rd->rstctrl);

#ifdef CFG_STM32MP15
	/* Reset cycle on MCU hold boot is not supported */
	if (rd->reset_id == MCU_HOLD_BOOT_R)
		return SCMI_NOT_SUPPORTED;

	/* Remoteproc driver may handle all MCU reset controllers */
	if (rd->reset_id == MCU_R &&
	    stm32_rproc_is_secure(STM32MP1_M4_RPROC_ID))
		return SCMI_DENIED;
#endif

	/* Supports only reset with context loss */
	if (state)
		return SCMI_NOT_SUPPORTED;

	FMSG("SCMI reset %u cycle", scmi_id);

	if (rstctrl_assert_to(rd->rstctrl, TIMEOUT_US_1MS))
		return SCMI_HARDWARE_ERROR;

	if (rstctrl_deassert_to(rd->rstctrl, TIMEOUT_US_1MS))
		return SCMI_HARDWARE_ERROR;

	return SCMI_SUCCESS;
}

int32_t plat_scmi_rd_set_state(unsigned int channel_id, unsigned int scmi_id,
			       bool assert_not_deassert)
{
	const struct stm32_scmi_rd *rd = find_rd(channel_id, scmi_id);
	TEE_Result res = TEE_ERROR_GENERIC;
	unsigned int id = 0;

	if (!rd)
		return SCMI_NOT_FOUND;

	id = rd->fw_id;

	if (id != STM32MP1_ETZPC_MAX_ID && stm32_etzpc_check_ns_access(id))
		return SCMI_DENIED;

#ifdef CFG_STM32MP15
	/* Remoteproc driver may handle all MCU reset controllers */
	if ((rd->reset_id == MCU_HOLD_BOOT_R || rd->reset_id == MCU_R) &&
	    stm32_rproc_is_secure(STM32MP1_M4_RPROC_ID))
		return SCMI_DENIED;
#endif

	assert(rd->rstctrl);

	if (assert_not_deassert) {
		FMSG("SCMI reset %u set", scmi_id);
		res = rstctrl_assert(rd->rstctrl);
	} else {
		FMSG("SCMI reset %u release", scmi_id);
		res = rstctrl_deassert(rd->rstctrl);
	}

	if (res)
		return SCMI_HARDWARE_ERROR;

	return SCMI_SUCCESS;
}

#ifdef CFG_SCMI_MSG_PERF_DOMAIN
/*
 * Platform SCMI performance domains
 *
 * Note: currently exposes a single performance domain that is the CPU DVFS
 * operating point. Non-secure is allowed to access this performance domain.
 */
static struct stm32_scmi_perfd *find_perfd(unsigned int channel_id,
					   unsigned int domain_id)
{
	const struct channel_resources *resource = find_resource(channel_id);
	size_t n = 0;

	if (resource) {
		for (n = 0; n < resource->perfd_count; n++)
			if (n == domain_id)
				return &resource->perfd[n];
	}

	return NULL;
}

size_t plat_scmi_perf_count(unsigned int channel_id)
{
	const struct channel_resources *resource = find_resource(channel_id);

	if (!resource)
		return 0;

	return resource->perfd_count;
}

const char *plat_scmi_perf_domain_name(unsigned int channel_id,
				       unsigned int domain_id)
{
	struct stm32_scmi_perfd *perfd = find_perfd(channel_id, domain_id);

	if (!perfd)
		return NULL;

	return perfd->name;
}

int32_t plat_scmi_perf_levels_array(unsigned int channel_id,
				    unsigned int domain_id, size_t start_index,
				    unsigned int *levels, size_t *nb_elts)
{
	struct stm32_scmi_perfd *perfd = find_perfd(channel_id, domain_id);
	size_t full_count = 0;
	size_t out_count = 0;
	size_t n = 0;

	if (!perfd)
		return SCMI_NOT_FOUND;

	full_count = stm32_cpu_opp_count();

	if (!levels) {
		*nb_elts = full_count - start_index;

		return SCMI_SUCCESS;
	}

	if (SUB_OVERFLOW(full_count, start_index, &out_count))
		return SCMI_GENERIC_ERROR;

	out_count = MIN(out_count, *nb_elts);

	FMSG("%zu levels: start %zu requested %zu output %zu",
	     full_count, start_index, *nb_elts, out_count);

	for (n = 0; n < out_count; n++)
		levels[n] = stm32_cpu_opp_level(n);

	*nb_elts = out_count;

	return SCMI_SUCCESS;
}

int32_t plat_scmi_perf_level_get(unsigned int channel_id,
				 unsigned int domain_id, unsigned int *level)
{
	struct stm32_scmi_perfd *perfd = find_perfd(channel_id, domain_id);
	unsigned int current_level = 0;

	if (!perfd)
		return SCMI_NOT_FOUND;

	if (stm32_cpu_opp_read_level(&current_level))
		return SCMI_GENERIC_ERROR;

	*level = current_level;

	return SCMI_SUCCESS;
}

int32_t plat_scmi_perf_level_set(unsigned int channel_id,
				 unsigned int domain_id, unsigned int level)
{
	struct stm32_scmi_perfd *perfd = find_perfd(channel_id, domain_id);

	if (!perfd)
		return SCMI_NOT_FOUND;

	switch (stm32_cpu_opp_set_level(level)) {
	case TEE_SUCCESS:
		return SCMI_SUCCESS;
	case TEE_ERROR_BAD_PARAMETERS:
		return SCMI_OUT_OF_RANGE;
	default:
		return SCMI_GENERIC_ERROR;
	}
}
#endif

static TEE_Result stm32_scmi_pm(enum pm_op op, unsigned int pm_hint __unused,
				const struct pm_callback_handle *hdl __unused)
{
	struct scmi_msg_channel *chan = NULL;
	size_t i = 0;

	if (op == PM_OP_RESUME) {
		for (i = 0; i < ARRAY_SIZE(scmi_channel); i++) {
			chan = plat_scmi_get_channel(i);
			assert(chan);
			if (chan->shm_addr.va)
				scmi_smt_init_agent_channel(chan);
		}
	}

	return TEE_SUCCESS;
}
DECLARE_KEEP_PAGER_PM(stm32_scmi_pm);

/*
 * Initialize platform SCMI resources
 */
static TEE_Result stm32mp1_init_scmi_server(void)
{
	size_t i = 0;
	size_t j = 0;

	/* Only need to reset the SYSRAM shared memory on PM resume */
	if (IS_ENABLED(CFG_STM32MP1_SCMI_SHM_BASE))
		register_pm_driver_cb(stm32_scmi_pm, NULL,
				      "scmi-server-driver");

	for (i = 0; i < ARRAY_SIZE(scmi_channel); i++) {
		const struct channel_resources *res = scmi_channel + i;
		struct scmi_msg_channel *chan = res->channel;
		size_t voltd_count = 0;

		if (chan->shm_addr.pa) {
			struct io_pa_va *addr = &chan->shm_addr;

			/* Enforce non-secure shm mapped as device memory */
			addr->va = (vaddr_t)phys_to_virt(addr->pa,
							 MEM_AREA_IO_NSEC,
							 chan->shm_size);
			assert(addr->va);

			scmi_smt_init_agent_channel(chan);
		}

		for (j = 0; j < res->clock_count; j++) {
			struct stm32_scmi_clk *clk = &res->clock[j];

			if (!clk->name ||
			    strlen(clk->name) >= SCMI_CLOCK_NAME_SIZE)
				panic("SCMI clock name invalid");

			clk->clk = stm32mp_rcc_clock_id_to_clk(clk->clock_id);
			assert(clk->clk);

			/* Sync SCMI clocks with their targeted initial state */
			if (clk->enabled &&
			    stm32mp_nsec_can_access_clock(clk->clock_id))
				clk_enable(clk->clk);
		}

		for (j = 0; j < res->rd_count; j++) {
			struct stm32_scmi_rd *rd = &res->rd[j];
			struct rstctrl *rstctrl = NULL;

			if (!rd->name ||
			    strlen(rd->name) >= SCMI_RD_NAME_SIZE)
				panic("SCMI reset domain name invalid");

			rstctrl = stm32mp_rcc_reset_id_to_rstctrl(rd->reset_id);
			assert(rstctrl);

			rd->rstctrl = rstctrl;
		}

		if (!IS_ENABLED(CFG_SCMI_MSG_REGULATOR_CONSUMER))
			continue;

		voltd_count = plat_scmi_voltd_count(i);
		for (j = 0; j < voltd_count; j++) {
			const char *name = plat_scmi_voltd_get_name(i, j);

			if (!name || strlen(name) >= SCMI_VOLTD_NAME_SIZE)
				panic("SCMI voltage domain name invalid");
		}
	}

	return TEE_SUCCESS;
}

driver_init_late(stm32mp1_init_scmi_server);
