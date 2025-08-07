/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (C) 2023, STMicroelectronics
 */

#ifndef __REMOTEPROC_PTA_H
#define __REMOTEPROC_PTA_H

#include <stdint.h>
#include <tee_api_defines.h>
#include <utee_defines.h>
#include <util.h>

/*
 * Interface to the pseudo TA which provides platform implementation
 * of the remote processor management
 */

#define PTA_RPROC_UUID { 0x54af4a68, 0x19be, 0x40d7, \
		{ 0xbb, 0xe6, 0x89, 0x50, 0x35, 0x0a, 0x87, 0x44 } }

/* Hardware capability: firmware format */
#define PTA_RPROC_HWCAP_FMT_ELF     BIT32(0)  /* Standard ELF format  */
#define PTA_RPROC_HWCAP_FMT_ENC_ELF BIT32(1)  /* Encrypted ELF format */

/* Hardware capability: image protection method */
/* The platform supports load of segment with hash protection */
#define PTA_RPROC_HWCAP_PROT_HASH_TABLE	BIT32(0)

/* Platform predefined TLV ID */
/* boot address of the remoteproc firmware */
#define PTA_REMOTEPROC_TLV_BOOTADDR		U(0x00010001)
/* Activate secure boot */
#define PTA_REMOTEPROC_TLV_BOOT_SEC		U(0x00010002)

/* Platform predefined TLV LENGTH (byte) */
#define PTA_REMOTEPROC_TLV_BOOTADDR_LGTH	U(4)
#define PTA_REMOTEPROC_TLV_BOOT_SEC_LGTH	U(4)

#define PTA_REMOTEPROC_MAX_HASH_SIZE	U(64)

/* No encryption */
#define RPROC_PTA_ENC_NONE 0

/**
 * struct rproc_pta_key_info - public key information
 * @algo:	Algorithm, defined by public key algorithms TEE_ALG_*
 * @info_size:	Byte size of @info
 * @info:	Append key information data
 */
struct rproc_pta_key_info {
	uint32_t algo;
	uint32_t info_size;
	uint8_t info[];
};

/*
 * struct rproc_pta_seg_info -  segment info for authentication and decryption
 * @hash_algo: Algorithm used for hash calculation
 * @hash:      Hash associated to the program segment.
 * @enc_algo:  Algorithm used for encryption, 0 means no encryption
 * @iv:        Initialisation vector for segment decryption
 */
struct rproc_pta_seg_info {
	uint32_t hash_algo;
	uint8_t hash[PTA_REMOTEPROC_MAX_HASH_SIZE];
	uint32_t enc_algo;
	uint8_t iv[TEE_AES_BLOCK_SIZE];
};

static inline size_t rproc_pta_keyinfo_size(struct rproc_pta_key_info *keyinf)
{
	size_t s = 0;

	if (!keyinf || ADD_OVERFLOW(sizeof(*keyinf), keyinf->info_size, &s))
		return 0;

	return s;
}

/*
 * Platform capabilities.
 *
 * Get Platform firmware loader service capabilities.
 *
 * [in]  params[0].value.a:	Unique 32bit remote processor identifier
 * [out] params[1].value.a:	Firmware format (PTA_RPROC_HWCAP_FMT_*)
 * [out] params[2].value.a:	Image protection method (PTA_RPROC_HWCAP_PROT_*)
 */
#define PTA_RPROC_HW_CAPABILITIES	1

/*
 * Firmware loading.
 *
 * Optional service to implement only in case of proprietary format.
 *
 * [in]  params[0].value.a:	Unique 32bit remote processor identifier
 * [in]  params[1].memref:	Loadable firmware image
 */
#define PTA_RPROC_FIRMWARE_LOAD		2

/*
 * Load a segment with a hash.
 *
 * This command is used when the platform secure memory is too constrained to
 * save the whole firmware image. Upon segment load, a successful completion
 * ensures the loaded image complies with the provided hash.
 *
 * [in]  params[0].value.a:	Unique 32bit remote processor identifier
 * [in]  params[1].memref:	Section data to load
 * [in]  params[2].value.a:	32bit LSB load device segment address
 * [in]  params[2].value.b:	32bit MSB load device segment address
 * [in]  params[3].memref:	Segment information (struct rproc_pta_seg_info)
 */
#define PTA_RPROC_LOAD_SEGMENT		3

/*
 * Memory set.
 *
 * Fill a remote device memory with requested value. this is used for instance
 * to clear a memory on the remote firmware load.
 *
 * [in]  params[0].value.a:	Unique 32bit remote processor identifier
 * [in]  params[1].value.a:	32bit LSB device memory address
 * [in]  params[1].value.b:	32bit MSB device memory address
 * [in]  params[2].value.a:	32bit LSB device memory size
 * [in]  params[2].value.b:	32bit MSB device memory size
 * [in]  params[3].value.a:	Byte value to be set
 */
#define PTA_RPROC_SET_MEMORY		4

/*
 * Firmware start.
 *
 * Start up a successfully loaded remote processor firmware.
 *
 * [in]  params[0].value.a:	Unique 32bit remote processor identifier
 */
#define PTA_RPROC_FIRMWARE_START	5

/*
 * Firmware stop.
 *
 * Stop of the remote processor firmware and release/clean resources.
 * After the command successful completion, remote processor firmware must be
 * reloaded prior being started again.
 *
 * [in]  params[0].value.a:	Unique 32bit remote processor identifier
 */
#define PTA_RPROC_FIRMWARE_STOP		6

/*
 * Firmware device to physical address conversion.
 *
 * Convert the physical address corresponding to an address got from the
 * firmware address layout.
 *
 * [in]  params[0].value.a:	Unique 32bit remote processor identifier
 * [in]  params[1].value.a:	32bit LSB Device memory address
 * [in]  params[1].value.b:	32bit MSB Device memory address
 * [in]  params[2].value.a:	32bit LSB Device memory size
 * [in]  params[2].value.b:	32bit MSB Device memory size
 * [out] params[3].value.a:	32bit LSB converted physical address
 * [out] params[3].value.b:	32bit MSB converted physical address
 */
#define PTA_RPROC_FIRMWARE_DA_TO_PA	7

/*
 * Verify the firmware digest against a signature
 *
 * Return TEE_SUCCESS if the signature is verified,
 *        TEE_ERROR_SIGNATURE_INVALID when signature is not valid,
 *        another error code for other error cases.
 *
 * [in]  params[0].value.a:	Unique 32bit remote processor identifier
 * [in]  params[1].memref:	Key information (refer to @rproc_pta_key_info)
 * [in]  params[2].memref:	Digest of the firmware authenticated data
 * [in]  params[3].memref:	Signature of the firmware authenticated data
 */
#define PTA_RPROC_VERIFY_DIGEST		8

/*
 * Provide platform parameter in Type-Length-Value format
 *
 * Return TEE_SUCCESS if the TLV is valid, else an error
 *
 * [in]  params[0].value.a:	Unique 32bit remote processor identifier
 * [in]  params[1].value.a:	16bit Type identifier
 * [in]  params[2].memref:	Value associated to the type ID
 */
#define PTA_REMOTEPROC_TLV_PARAM	9

/*
 * Remote processor resources clean-up.
 *
 * Clean the resources of the remote processor firmware.
 *
 * [in]  params[0].value.a:	Unique 32bit remote processor identifier
 */
#define PTA_REMOTEPROC_CLEAN		10

/*
 * Get remote processor memories access right.
 *
 * Request the access rights to the remote processor memories.
 *
 * [in]  params[0].value.a:	Unique 32bit remote processor identifier
 */
#define PTA_REMOTEPROC_GET_MEM		11

/*
 * Release remote processor memories access right.
 *
 * Release the access rights to the remote processor memories.
 *
 * [in]  params[0].value.a:	Unique 32bit remote processor identifier
 */
#define PTA_REMOTEPROC_RELEASE_MEM	12

#endif /* __REMOTEPROC_PTA_H */
