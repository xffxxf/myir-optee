// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2022-2024, STMicroelectronics
 */

#include <config.h>
#include <drivers/regulator.h>
#include <drivers/stm32mp_dt_bindings.h>
#include <drivers/stpmic2.h>
#include <kernel/pm.h>
#include <platform_config.h>
#include <stm32mp_pm.h>
#include <trace.h>

/*
 * Low power configurations:
 *
 * STM32_PM_DEFAULT
 *   "default" sub nodes in device-tree
 *   is applied at probe, and re-applied at PM resume.
 *   should support STOP1, LP-STOP1, STOP2, LP-STOP2
 *
 * STM32_PM_LPLV
 *   "lplv" sub nodes in device-tree
 *   should support STOP1, LPLV-STOP1, STOP2, LPLV-STOP2
 *
 * STM32_PM_STANDBY
 *   "standby" sub nodes in device-tree
 *   should support STANDBY1-DDR-SR
 *   is applied in pm suspend call back
 *
 * STM32_PM_OFF
 *   "off" sub nodes in device-tree
 *   should support STANDBY-DDR-OFF mode
 *   and should be applied before shutdown
 *
 */
#define STM32_PM_DEFAULT		0
#define STM32_PM_LPLV			1
#define STM32_PM_STANDBY		2
#define STM32_PM_OFF			3
#define STM32_PM_NB_SOC_MODES		4

size_t plat_pmic2_get_lp_mode_count(void)
{
	return STM32_PM_NB_SOC_MODES;
}

const char *plat_pmic2_get_lp_mode_name(int mode)
{
	switch (mode) {
	case STM32_PM_DEFAULT:
		return "default";
	case STM32_PM_LPLV:
		return "lplv";
	case STM32_PM_STANDBY:
		return "standby";
	case STM32_PM_OFF:
		return "off";
	default:
		EMSG("Invalid lp mode %d", mode);
		panic();
	}
}

static TEE_Result pmic_regu_pm(enum pm_op op, uint32_t pm_hint,
			       const struct pm_callback_handle *pm_handle)
{
	struct regulator *regulator = pm_handle->handle;
	unsigned int pwrlvl = PM_HINT_PLATFORM_STATE(pm_hint);
	uint8_t mode = STM32_PM_DEFAULT;

	if (op == PM_OP_SUSPEND) {
		/* configure PMIC level according MAX PM domain OFF */
		switch (pwrlvl) {
		case PM_D1_LEVEL:
		case PM_D2_LEVEL:
			mode = STM32_PM_LPLV;
			break;
		case PM_D2_LPLV_LEVEL:
			mode = STM32_PM_STANDBY;
			break;
		case PM_MAX_LEVEL:
			mode = STM32_PM_OFF;
			break;
		default:
			mode = STM32_PM_DEFAULT;
			break;
		}
	} else if (op == PM_OP_RESUME) {
		mode = STM32_PM_DEFAULT;
	}

	return stm32_pmic2_apply_pm_state(regulator, mode);
}

TEE_Result plat_pmic2_supplied_init(struct regulator *regulator)
{
	TEE_Result res = TEE_ERROR_GENERIC;

	res = stm32_pmic2_apply_pm_state(regulator, STM32_PM_DEFAULT);
	if (res) {
		EMSG("Failed to prepare regu suspend %s",
		     regulator_name(regulator));
		return res;
	}

	register_pm_core_service_cb(pmic_regu_pm, regulator,
				    regulator_name(regulator));

	return TEE_SUCCESS;
}
