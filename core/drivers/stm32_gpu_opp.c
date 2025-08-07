// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2024, STMicroelectronics
 */

#include <config.h>
#include <drivers/clk.h>
#include <drivers/clk_dt.h>
#include <drivers/regulator.h>
#include <drivers/stm32_cpu_opp.h>
#include <initcall.h>
#include <io.h>
#include <kernel/dt.h>
#include <kernel/mutex.h>
#include <kernel/panic.h>
#include <libfdt.h>
#include <stm32_util.h>

/*
 * struct gpu_dvfs - GPU DVFS registered operating points
 * @freq_khz: GPU frequency in kilohertz (kHz)
 * @volt_uv: GPU voltage level in microvolts (uV)
 */
struct gpu_dvfs {
	uint64_t freq_khz;
	int volt_uv;
};

/*
 * struct gpu_opp - GPU operating point
 *
 * @current_opp: Index of current GPU operating point in @dvfs array
 * @opp_count: Number of cells of @dvfs
 * @clock: GPU clock handle
 * @regul: GPU regulator supply handle
 * @dvfs: Arrays of the supported GPU operating points
 */
struct gpu_opp {
	struct clk *clock;
	struct regulator *regul;
	struct gpu_dvfs *dvfs;
	unsigned int current_opp;
	unsigned int opp_count;
};

static struct gpu_opp gpu_opp;

static TEE_Result stm32_gpu_opp_is_supported(const void *fdt, int subnode)
{
	const fdt32_t *cuint32 = NULL;
	uint32_t opp = 0;

	cuint32 = fdt_getprop(fdt, subnode, "opp-supported-hw", NULL);
	if (!cuint32) {
		DMSG("Can't find property opp-supported-hw");
		return TEE_ERROR_ITEM_NOT_FOUND;
	}

	opp = fdt32_to_cpu(*cuint32);
	if (!stm32mp_supports_cpu_opp(opp)) {
		DMSG("Not supported opp-supported-hw %#"PRIx32, opp);
		return TEE_ERROR_NOT_SUPPORTED;
	}

	return TEE_SUCCESS;
}

static TEE_Result stm32_gpu_opp_get_dt_subnode(const void *fdt, int node)
{
	const fdt64_t *cuint64 = NULL;
	const fdt32_t *cuint32 = NULL;
	uint64_t freq_hz = 0;
	uint64_t freq_khz = 0;
	uint64_t freq_khz_opp_def = 0;
	uint32_t volt_uv = 0;
	unsigned int i = 0;
	int subnode = -1;

	fdt_for_each_subnode(subnode, fdt, node)
		gpu_opp.opp_count++;

	gpu_opp.dvfs = calloc(gpu_opp.opp_count, sizeof(*gpu_opp.dvfs));
	if (!gpu_opp.dvfs)
		return TEE_ERROR_OUT_OF_MEMORY;

	gpu_opp.current_opp = gpu_opp.opp_count;

	/*
	 * Skip OPP not supported because of:
	 *   - SOC part number value
	 *   - frequency not supported
	 *   - voltage not supported
	 * Also retrieve the default OPP with the highest rate
	 */
	fdt_for_each_subnode(subnode, fdt, node) {
		if (stm32_gpu_opp_is_supported(fdt, subnode) != TEE_SUCCESS) {
			DMSG("Skip OPP %"PRIu64"kHz/%"PRIu32"uV by SOC",
			     freq_khz, volt_uv);
			gpu_opp.opp_count--;
			continue;
		}

		cuint64 = fdt_getprop(fdt, subnode, "opp-hz", NULL);
		if (!cuint64) {
			EMSG("Missing opp-hz");
			return TEE_ERROR_GENERIC;
		}
		freq_hz = fdt64_to_cpu(*cuint64);
		freq_khz = freq_hz / 1000ULL;
		if (freq_khz > (uint64_t)UINT32_MAX) {
			EMSG("Invalid opp-hz %"PRIu64, freq_khz);
			return TEE_ERROR_GENERIC;
		}

		cuint32 = fdt_getprop(fdt, subnode, "opp-microvolt", NULL);
		if (!cuint32) {
			EMSG("Missing opp-microvolt");
			return TEE_ERROR_GENERIC;
		}

		volt_uv = fdt32_to_cpu(*cuint32);

		/* skip OPP when frequency is not supported */
		if (freq_hz != clk_round_rate(gpu_opp.clock, freq_hz)) {
			DMSG("Skip OPP %"PRIu64"kHz/%"PRIu32"uV by freq",
			     freq_khz, volt_uv);
			gpu_opp.opp_count--;
			continue;
		}

		/* skip OPP when voltage is not supported */
		if (!opp_voltage_is_supported(gpu_opp.regul, &volt_uv)) {
			DMSG("Skip OPP %"PRIu64"kHz/%"PRIu32"uV by volt",
			     freq_khz, volt_uv);
			gpu_opp.opp_count--;
			continue;
		}

		gpu_opp.dvfs[i].freq_khz = freq_khz;
		gpu_opp.dvfs[i].volt_uv = volt_uv;

		DMSG("Found OPP %u (%"PRIu64"kHz/%"PRIu32"uV) from DT",
		     i, freq_khz, volt_uv);

		if (fdt_getprop(fdt, subnode, "st,opp-default", NULL) &&
		    freq_khz > freq_khz_opp_def) {
			gpu_opp.current_opp = i;
			freq_khz_opp_def = freq_khz;
		}

		i++;
	}

	/* Erreur when "st,opp-default" is not present */
	if (gpu_opp.current_opp == gpu_opp.opp_count) {
		EMSG("no st,opp-default found");
		return TEE_ERROR_BAD_PARAMETERS;
	}

	return TEE_SUCCESS;
}

static TEE_Result
stm32_gpu_init(const void *fdt, int node, const void *compat_data __unused)
{
	TEE_Result res = TEE_ERROR_GENERIC;

	const fdt32_t *cuint = NULL;
	int opp_node = 0;
	int len = 0;
	uint32_t phandle = 0;

	cuint = fdt_getprop(fdt, node, "operating-points-v2", &len);
	if (!cuint || len != sizeof(uint32_t)) {
		DMSG("Missing operating-points-v2");
		return TEE_SUCCESS;
	}

	res = clk_dt_get_by_index(fdt, node, 0, &gpu_opp.clock);
	if (res) {
		EMSG("clock not found");
		return res;
	}

	res = regulator_dt_get_supply(fdt, node, "gpu", &gpu_opp.regul);
	if (res) {
		if (res != TEE_ERROR_DEFER_DRIVER_INIT)
			EMSG("regulator not found");
		return res;
	}

	phandle = fdt32_to_cpu(*cuint);
	opp_node = fdt_node_offset_by_phandle(fdt, phandle);

	res = stm32_gpu_opp_get_dt_subnode(fdt, opp_node);
	if (res)
		goto error;

	/*
	 * As the GPU is off at this time, voltage and freq can be set
	 * in any order.
	 */
	DMSG("set volt to %"PRIu32"uV",
	     gpu_opp.dvfs[gpu_opp.current_opp].volt_uv);
	res = regulator_set_voltage(gpu_opp.regul,
				    gpu_opp.dvfs[gpu_opp.current_opp].volt_uv);
	if (res) {
		EMSG("set voltage failed");
		goto error;
	}

	DMSG("set clock to %"PRIu64"kHz",
	     gpu_opp.dvfs[gpu_opp.current_opp].freq_khz);
	res = clk_set_rate(gpu_opp.clock,
			   gpu_opp.dvfs[gpu_opp.current_opp].freq_khz * 1000UL);
	if (res) {
		EMSG("set rate failed");
		panic();
	}

	return TEE_SUCCESS;

error:
	free(gpu_opp.dvfs);
	return res;
}

static const struct dt_device_match stm32_gpu_match_table[] = {
	{ .compatible = "vivante,gc" },
	{ }
};

DEFINE_DT_DRIVER(stm32_gpu_dt_driver) = {
	.name = "stm32-gpu",
	.match_table = stm32_gpu_match_table,
	.probe = &stm32_gpu_init,
};

