flavor_dts_file-215F_DK = stm32mp215f-dk.dts
flavor_dts_file-235F_DK = stm32mp235f-dk.dts
flavor_dts_file-257F_DK = stm32mp257f-dk.dts
flavor_dts_file-257F_EV1 = stm32mp257f-ev1.dts
flavor_dts_file-257X_MYB2GB = myb-stm32mp257x-2GB.dts
flavor_dts_file-257X_MYB1GB = myb-stm32mp257x-1GB.dts

flavorlist-MP25 = $(flavor_dts_file-257F_DK) \
                  $(flavor_dts_file-257X_MYB2GB) \
                  $(flavor_dts_file-257X_MYB1GB) \
		  $(flavor_dts_file-257F_EV1)


flavorlist-MP23 = $(flavor_dts_file-235F_DK)

flavorlist-MP21 = $(flavor_dts_file-215F_DK)

# List of all DTS for this PLATFORM
ALL_DTS = $(flavorlist-MP25) $(flavorlist-MP23) $(flavorlist-MP21)

# Check if device-tree exist in OP-TEE source code, else search it in external
# device tree repository
ifeq ($(wildcard $(arch-dir)/dts/$(CFG_EMBED_DTB_SOURCE_FILE)),)
# External device tree default path
CFG_EXT_DTS ?= $(arch-dir)/dts/external-dt/optee
ifneq ($(wildcard $(CFG_EXT_DTS)/$(CFG_EMBED_DTB_SOURCE_FILE)),)
override dts-source-path := $(CFG_EXT_DTS)
-include $(CFG_EXT_DTS)/conf.mk
else
$(error Cannot find DTS file $(CFG_EXT_DTS)/$(CFG_EMBED_DTB_SOURCE_FILE))
endif
endif

ifneq ($(PLATFORM_FLAVOR),)
ifeq ($(flavor_dts_file-$(PLATFORM_FLAVOR)),)
$(error Invalid platform flavor $(PLATFORM_FLAVOR))
endif
CFG_EMBED_DTB_SOURCE_FILE ?= $(flavor_dts_file-$(PLATFORM_FLAVOR))
endif
CFG_EMBED_DTB_SOURCE_FILE ?= stm32mp257f-ev1.dts

# CFG_STM32MP2x switches are exclusive.
# - CFG_STM32MP25 is enabled for STM32MP25x-* targets (default)
# - CFG_STM32MP23 is enabled for STM32MP23x-* targets
# - CFG_STM32MP21 is enabled for STM32MP21x-* targets
# We try to guess the variant from the embedded DT source file name
ifneq ($(filter $(CFG_EMBED_DTB_SOURCE_FILE),$(flavorlist-MP25)),)
$(call force,CFG_STM32MP25,y)
endif
ifneq ($(filter $(CFG_EMBED_DTB_SOURCE_FILE),$(flavorlist-MP23)),)
$(call force,CFG_STM32MP23,y)
endif
ifneq ($(filter $(CFG_EMBED_DTB_SOURCE_FILE),$(flavorlist-MP21)),)
$(call force,CFG_STM32MP21,y)
endif

# CFG_STM32MP2x switches are exclusive.
# - CFG_STM32MP25 is enabled for STM32MP25x-* targets (default)
# - CFG_STM32MP23 is enabled for STM32MP23x-* targets
# - CFG_STM32MP21 is enabled for STM32MP21x-* targets
ifeq ($(CFG_STM32MP21),y)
$(call force,CFG_STM32MP23,n)
$(call force,CFG_STM32MP25,n)
else ifeq ($(CFG_STM32MP23),y)
$(call force,CFG_STM32MP21,n)
$(call force,CFG_STM32MP25,n)
else
$(call force,CFG_STM32MP21,n)
$(call force,CFG_STM32MP23,n)
$(call force,CFG_STM32MP25,y)
endif

# CFG_STM32MP_PROFILE selects the profile of the services embedded in OP-TEE
CFG_STM32MP_PROFILE ?= secure_and_system_services

ifeq ($(filter $(CFG_STM32MP_PROFILE),system_services secure_and_system_services),)
$(error CFG_STM32MP_PROFILE shall be one of system_services or secure_and_system_services)
endif

ifeq ($(CFG_STM32MP_PROFILE),system_services)
include $(platform-dir)/conf.disable-secure-services.mk
endif

include core/arch/arm/cpu/cortex-armv8-0.mk
supported-ta-targets ?= ta_arm64

# Enable flag when Cortex-M33 is TDCID
ifneq ($(filter $(CFG_EMBED_DTB_SOURCE_FILE),$(flavorlist-M33-TDCID)),)
$(call force,CFG_STM32_CM33TDCID,y)
endif

CFG_STM32_CM33TDCID ?= n
ifeq ($(CFG_STM32_CM33TDCID),y)
$(call force,CFG_SCMI_CORTEXM_AGENT,n)
$(call force,CFG_SCMI_MSG_DRIVERS,n)
$(call force,CFG_SCMI_PTA,n)
$(call force,CFG_SCMI_SCPFW,n)
$(call force,CFG_SCMI_SERVER_CLOCK_CONSUMER,n)
$(call force,CFG_SCMI_SERVER_PD_CONSUMER,n)
$(call force,CFG_SCMI_SERVER_REGULATOR_CONSUMER,n)
$(call force,CFG_SCMI_SERVER_RESET_CONSUMER,n)
$(call force,CFG_STM32_BSEC_PTA,n)
$(call force,CFG_STM32_BSEC3,n)
$(call force,CFG_STM32_BSEC_WRITE,n)
$(call force,CFG_STM32MP_PROVISIONING,n)
$(call force,CFG_TA_STM32MP_NVMEM,n)
endif

$(call force,CFG_ARM_GIC_PM,y)
$(call force,CFG_ARM64_core,y)
$(call force,CFG_CORE_ASYNC_NOTIF,y)
$(call force,CFG_CORE_ASYNC_NOTIF_GIC_INTID,31)
$(call force,CFG_DRIVERS_CLK,y)
$(call force,CFG_DRIVERS_CLK_DT,y)
$(call force,CFG_DRIVERS_GPIO,y)
$(call force,CFG_DRIVERS_MAILBOX,y)
$(call force,CFG_DRIVERS_PINCTRL,y)
$(call force,CFG_DRIVERS_REGULATOR,y)
$(call force,CFG_DT,y)
$(call force,CFG_GIC,y)
$(call force,CFG_HALT_CORES_ON_PANIC_SGI,15)
$(call force,CFG_INIT_CNTVOFF,y)
$(call force,CFG_SCMI_SCPFW_PRODUCT,optee-stm32mp2)
$(call force,CFG_SECURE_TIME_SOURCE_CNTPCT,y)
$(call force,CFG_STM32_SHARED_IO,y)
$(call force,CFG_STM32_HSE_MONITORING,y)
$(call force,CFG_STM32_PWR,y)
$(call force,CFG_STM32_PWR_REGUL,y)
$(call force,CFG_STM32MP_CLK_CORE,y)
CFG_STM32MP_REMOTEPROC ?= y
$(call force,CFG_WITH_ARM_TRUSTED_FW,y)
$(call force,CFG_WITH_LPAE,y)

ifeq ($(CFG_STM32MP21),y)
$(call force,CFG_STM32MP21_RSTCTRL,y)
$(call force,CFG_STM32MP21_CLK,y)
else
$(call force,CFG_STM32MP25_RSTCTRL,y)
$(call force,CFG_STM32MP25_CLK,y)
$(call force,CFG_STM32_PKA,n)
$(call force,CFG_STM32_SAES,n)
$(call force,CFG_STM32_CRYP,n)
$(call force,CFG_STM32_HASH,n)
endif

CFG_TZDRAM_START ?= 0x82000000
CFG_TZDRAM_SIZE  ?= 0x02000000

# Support DDR ranges up to 8GBytes (address range: 0x80000000 + DDR size)
CFG_CORE_LARGE_PHYS_ADDR ?= y
CFG_CORE_ARM64_PA_BITS ?= 34

CFG_CORE_HEAP_SIZE ?= 262144
CFG_CORE_RESERVED_SHM ?= n
CFG_DTB_MAX_SIZE ?= 262144
CFG_HALT_CORES_ON_PANIC ?= y
CFG_MMAP_REGIONS ?= 30
CFG_NUM_THREADS ?= 5
ifneq ($(filter $(CFG_EMBED_DTB_SOURCE_FILE),$(flavorlist-MP21)),)
$(call force,CFG_TEE_CORE_NB_CORE,1)
endif
CFG_TEE_CORE_NB_CORE ?= 2
CFG_STM32MP_OPP_COUNT ?= 3

ifeq ($(CFG_STM32MP21),y)
CFG_STM32_EXTI ?= n
endif

CFG_STM32_BSEC3 ?= y
CFG_STM32_BSEC_WRITE ?= y
CFG_STM32_CPU_OPP ?= y
CFG_STM32_GPU_OPP ?= y
CFG_STM32_CRYP ?= y
CFG_STM32_EXTI ?= y
CFG_STM32_FMC ?= y
CFG_STM32_GPIO ?= y
CFG_STM32_HASH ?= y
CFG_STM32_HPDMA ?= y
CFG_STM32_HSEM ?= y
CFG_STM32_I2C ?= y
CFG_STM32_IAC ?= y
CFG_STM32_IPCC ?= y
CFG_STM32_IWDG ?= y
CFG_STM32_LPTIMER ?= y
CFG_STM32_OMM ?= y
CFG_STM32_PKA ?= y
CFG_STM32_PWR_IRQ ?= y
CFG_STM32_RIF ?= y
CFG_STM32_RIFSC ?= y
CFG_STM32_RISAB ?= y
CFG_STM32_RISAF ?= y
CFG_STM32_RNG ?= y
CFG_STM32_RTC ?= y
CFG_STM32_SAES ?= y
CFG_STM32_SERC ?= y
CFG_STM32_STGEN ?= y
CFG_STM32_TAMP ?= y
CFG_STM32_UART ?= y
CFG_STM32_VREFBUF ?= y
CFG_STPMIC2 ?= y
CFG_WITH_TUI ?= y

CFG_SCMI_MSG_DRIVERS ?= n
CFG_SCMI_PTA ?= y
CFG_SCMI_SCPFW ?= y
CFG_SCMI_SERVER_CLOCK_CONSUMER ?= y
CFG_SCMI_SERVER_PD_CONSUMER ?= y
CFG_SCMI_SERVER_REGULATOR_CONSUMER ?= y
CFG_SCMI_SERVER_RESET_CONSUMER ?= y

CFG_DRIVERS_I2C ?= $(CFG_STM32_I2C)
CFG_REGULATOR_FIXED ?= y
CFG_REGULATOR_GPIO ?= y

# Default enable some test facitilites
CFG_DRIVERS_CLK_PRINT_TREE ?= y
CFG_DRIVERS_REGULATOR_PRINT_TREE ?= y
CFG_ENABLE_EMBEDDED_TESTS ?= y
CFG_WITH_STATS ?= y
CFG_WERROR ?= y

# Default disable ASLR
CFG_CORE_ASLR ?= n

# UART instance used for early console (0 disables early console)
CFG_STM32_EARLY_CONSOLE_UART ?= 2

# Default disable external DT support
CFG_EXTERNAL_DT ?= n

ifeq ($(CFG_STM32_HSE_MONITORING),y)
$(call force,CFG_STM32_LPTIMER,y)
endif

#SAES and CRYP cannot be register at the same time in the crypto framework
#If CRYP and SAES are enable, disable CRYP for safety purpose
ifeq ($(call cfg-all-enabled, CFG_STM32_CRYP CFG_STM32_SAES), y)
override CFG_STM32_CRYP := n
endif # cfg-all-enabled, CFG_STM32_CRYP CFG_STM32_SAES

# If any crypto driver is enabled, enable the crypto-framework layer
ifeq ($(call cfg-one-enabled, CFG_STM32_CRYP CFG_STM32_HASH CFG_STM32_PKA \
			      CFG_STM32_SAES ),y)
$(call force,CFG_STM32_CRYPTO_DRIVER,y)
endif

CFG_DRIVERS_REMOTEPROC ?= $(CFG_STM32MP_REMOTEPROC)
CFG_REMOTEPROC_PTA ?= $(CFG_STM32MP_REMOTEPROC)
ifeq ($(CFG_REMOTEPROC_PTA),y)
# Remoteproc early TA for coprocessor firmware management in boot stages
CFG_IN_TREE_EARLY_TAS += remoteproc/80a4c275-0a47-4905-8285-1486a9771a08
# Embed public part of this key in OP-TEE OS
RPROC_SIGN_KEY ?= keys/default.pem
# Increase the RESERVED VA SPACE to be able to map reserved-memory regions
# assigned to the remote processor.
CFG_RESERVED_VASPACE_SIZE = 32 * 1024 * 1024
endif

# Default enable HWRNG PTA support
CFG_HWRNG_PTA ?= $(CFG_STM32_RNG)
ifeq ($(CFG_HWRNG_PTA),y)
$(call force,CFG_STM32_RNG,y,Mandated by CFG_HWRNG_PTA)
$(call force,CFG_WITH_SOFTWARE_PRNG,n,Mandated by CFG_HWRNG_PTA)
CFG_HWRNG_QUALITY ?= 1024
endif

# Platform specific configuration
CFG_STM32MP_PANIC_ON_TZC_PERM_VIOLATION ?= y

# Watchdog SMC service to non-secure world
CFG_WDT ?= $(CFG_STM32_IWDG)
CFG_WDT_SM_HANDLER ?= $(CFG_WDT)
CFG_WDT_SM_HANDLER_ID ?= 0xbc000000

# Enable reset control
ifeq ($(CFG_STM32MP21_RSTCTRL),y)
$(call force,CFG_DRIVERS_RSTCTRL,y)
$(call force,CFG_STM32_RSTCTRL,y)
endif
ifeq ($(CFG_STM32MP25_RSTCTRL),y)
$(call force,CFG_DRIVERS_RSTCTRL,y)
$(call force,CFG_STM32_RSTCTRL,y)
endif

# Enable BSEC PTA for fuses access management
CFG_STM32_BSEC_PTA ?= y
ifeq ($(CFG_STM32_BSEC_PTA),y)
$(call force,CFG_STM32_BSEC3,y,Mandated by CFG_STM32_BSEC_PTA)
endif

# Enable RTC
ifeq ($(CFG_STM32_RTC),y)
$(call force,CFG_DRIVERS_RTC,y)
$(call force,CFG_RTC_PTA,y)
endif

# Enable Early TA NVMEM for provisioning management
CFG_TA_STM32MP_NVMEM ?= y
ifeq ($(CFG_TA_STM32MP_NVMEM),y)
$(call force,CFG_STM32_BSEC_PTA,y,Mandated by CFG_TA_STM32MP_NVMEM)
CFG_IN_TREE_EARLY_TAS += stm32mp_nvmem/1a8342cc-81a5-4512-99fe-9e2b3e37d626
endif
CFG_DRIVERS_NVMEM ?= y
CFG_STM32_TAMP_NVRAM ?= y

# Default enable PKCS11 TA to have PKCS11 tests built in OP-TEE Test
CFG_PKCS11_TA ?= y

# Provisioning support for BSEC shadow OTP is dedicated to insecure development
# configuration only.
CFG_STM32MP_PROVISIONING ?= y
ifeq ($(CFG_STM32MP_PROVISIONING),y)
$(call force,CFG_INSECURE,y,Required by CFG_STM32MP_PROVISIONING)
endif

# Optional behavior upon receiving illegal access events
CFG_STM32_PANIC_ON_IAC_EVENT ?= y
ifeq ($(CFG_TEE_CORE_DEBUG),y)
CFG_STM32_PANIC_ON_SERC_EVENT ?= n
else
CFG_STM32_PANIC_ON_SERC_EVENT ?= y
endif

# Enable associated counter driver
ifeq ($(CFG_STM32_LPTIMER),y)
$(call force,CFG_COUNTER_DRIVER,y)
endif

CFG_DRIVERS_FIREWALL ?= y
ifeq ($(call cfg-one-enabled, CFG_STM32_RISAF CFG_STM32_RIFSC),y)
$(call force,CFG_DRIVERS_FIREWALL,y)
endif

# Trusted User Interface
ifeq ($(CFG_WITH_TUI),y)
$(call force,CFG_DISPLAY,y)
$(call force,CFG_FRAME_BUFFER,y)
$(call force,CFG_STM32_LTDC,y)
# Provision virtual space to fit 10MByte plus the TUI frame buffer
CFG_TUI_FRAME_BUFFER_SIZE_MAX ?= 0x01000000
CFG_RESERVED_VASPACE_SIZE ?= (10 * 1024 * 1024 + $(CFG_TUI_FRAME_BUFFER_SIZE_MAX))
endif

# Support for STM32MP OSTL legacy SCMI messages
CFG_STM32_OTSL_SCMI_CLOCK_SUPPORT ?= y

# When CFG_DT_CACHED_NODE_INFO is enabled, boot time may be divided by 2 or
# more (depending on DTB size).
CFG_DT_CACHED_NODE_INFO ?= y
