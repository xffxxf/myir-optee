# Deactivate TA support
$(call force,CFG_ARM32_ldelf,n)
$(call force,CFG_ARM32_ta_arm32,n)
$(call force,CFG_ARM64_ldelf,n)
$(call force,CFG_ARM64_ta_arm32,n)
$(call force,CFG_ARM64_ta_arm64,n)
$(call force,CFG_EARLY_TA,n)
$(call force,CFG_PKCS11_TA,n)
$(call force,CFG_SECURE_DATA_PATH,n)
$(call force,CFG_TA_GPROF_SUPPORT,n)
$(call force,CFG_TA_STM32MP_NVMEM,n)
$(call force,CFG_TEE_BENCHMARK,n)
$(call force,CFG_WITH_TUI,n)
$(call force,CFG_WITH_USER_TA,n)

# Deactivate crypto support
$(call force,CFG_CRYPTO,n)
$(call force,CFG_STM32_CRYP,n)
$(call force,CFG_STM32_HASH,n)
$(call force,CFG_STM32_PKA,n)
$(call force,CFG_STM32_SAES,n)
$(call force,CFG_WITH_SOFTWARE_PRNG,n)

# Deactivate all PTAs support except BSEC and SCMI PTAs
$(call force,CFG_APDU_PTA,n)
$(call force,CFG_ATTESTATION_PTA,n)
$(call force,CFG_REMOTEPROC_PTA,n)
$(call force,CFG_SCP03_PTA,n)
$(call force,CFG_SECSTOR_TA_MGMT_PTA,n)
$(call force,CFG_SYSTEM_PTA,n)

# Deactivate secure storage support
$(call force,CFG_REE_FS,n)
$(call force,CFG_REE_FS_TA,n)
$(call force,CFG_RPMB_FS,n)

# Default deactivate services related to security configuration
CFG_STM32_PANIC_ON_IAC_EVENT ?= n
CFG_STM32_PANIC_ON_SERC_EVENT ?= n

# Default disable some service usually bound to service features
CFG_STM32_ADC ?= n
