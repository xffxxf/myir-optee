srcs-$(CFG_DDR_LOWPOWER) += stm32mp1_ddrc.c
srcs-$(CFG_STM32_CLKCALIB) += stm32mp1_calib.c
srcs-$(CFG_STPMIC1) += stm32mp1_stpmic1.c
srcs-$(CFG_STPMIC2) += stm32mp1_stpmic2.c
srcs-y 	+= stm32mp1_pwr.c
srcs-$(CFG_SYSCFG) += stm32mp1_syscfg.c
srcs-y 	+= stm32mp1_pwr_irq.c
