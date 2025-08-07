/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2020, STMicroelectronics
 */
#ifndef __DRIVERS_STM32_RIFSC_H
#define __DRIVERS_STM32_RIFSC_H

#include <types_ext.h>
#include <util.h>

struct risup_cfg {
	uint32_t cid_attr;
	uint32_t id;
	bool sec;
	bool priv;
	bool lock;
	bool pm_sem;
};

/**
 * struct rimu_cfg - RIMU configuration
 *
 * @id: ID of the RIMU
 * @risup_id: ID of the associated RISUP
 * @attr: RIMU configuration attributes
 */
struct rimu_cfg {
	uint32_t id;
	uint32_t risup_id;
	uint32_t attr;
};

#if defined(CFG_STM32MP25)
struct risal_cfg {
	uint32_t id;
	uint32_t blockid;
	uint32_t attr;
};
#endif /* defined(CFG_STM32MP25) */

#endif /* __DRIVERS_STM32_RIFSC_H */
