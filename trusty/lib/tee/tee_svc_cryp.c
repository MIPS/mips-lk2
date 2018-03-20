/*
 * Copyright (c) 2017-2018, MIPS Tech, LLC and/or its affiliated group companies
 * (“MIPS”).
 * Copyright (c) 2014, STMicroelectronics International N.V.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <assert.h>
#include <tee_api_types.h>
#include <utee_defines.h>
#include <lib/syscall.h>
#include <lib/tee/tee_svc.h>
#include <lib/tee/tee_svc_cryp.h>
#include <lib/tee/tee_cryp_utl.h>
#include <list.h>
#include <lib/tee/tee_obj.h>
#include <lib/tee/tee_cryp_provider.h>
#include <trace.h>
#include <string_ext.h>
#include <string.h>
#include <util.h>
#if defined(CFG_CRYPTO_HKDF) || defined(CFG_CRYPTO_CONCAT_KDF) || \
	defined(CFG_CRYPTO_PBKDF2)
#include <tee_api_defines_extensions.h>
#endif
#if defined(CFG_CRYPTO_HKDF)
#include <lib/tee/tee_cryp_hkdf.h>
#endif
#if defined(CFG_CRYPTO_CONCAT_KDF)
#include <lib/tee/tee_cryp_concat_kdf.h>
#endif
#if defined(CFG_CRYPTO_PBKDF2)
#include <lib/tee/tee_cryp_pbkdf2.h>
#endif

typedef void (*tee_cryp_ctx_finalize_func_t) (void *ctx, uint32_t algo);
struct tee_cryp_state {
	struct list_node node;
	uint32_t algo;
	uint32_t mode;
	vaddr_t key1;
	vaddr_t key2;
	size_t ctx_size;
	void *ctx;
	tee_cryp_ctx_finalize_func_t ctx_finalize;
};

struct tee_cryp_obj_secret {
	uint32_t key_size;
	uint32_t alloc_size;

	/*
	 * Pseudo code visualize layout of structure
	 * Next follows data, such as:
	 *	uint8_t data[alloc_size]
	 * key_size must never exceed alloc_size
	 */
};

#define TEE_TYPE_ATTR_OPTIONAL       0x0
#define TEE_TYPE_ATTR_REQUIRED       0x1
#define TEE_TYPE_ATTR_OPTIONAL_GROUP 0x2
#define TEE_TYPE_ATTR_SIZE_INDICATOR 0x4
#define TEE_TYPE_ATTR_GEN_KEY_OPT    0x8
#define TEE_TYPE_ATTR_GEN_KEY_REQ    0x10

    /* Handle storing of generic secret keys of varying lengths */
#define ATTR_OPS_INDEX_SECRET     0
    /* Convert to/from big-endian byte array and provider-specific bignum */
#define ATTR_OPS_INDEX_BIGNUM     1
    /* Convert to/from value attribute depending on direction */
#define ATTR_OPS_INDEX_VALUE      2

struct tee_cryp_obj_type_attrs {
	uint32_t attr_id;
	uint16_t flags;
	uint16_t ops_index;
	uint16_t raw_offs;
	uint16_t raw_size;
};

#define RAW_DATA(_x, _y)	\
	.raw_offs = offsetof(_x, _y), .raw_size = MEMBER_SIZE(_x, _y)

static const struct tee_cryp_obj_type_attrs
	tee_cryp_obj_secret_value_attrs[] = {
	{
	.attr_id = TEE_ATTR_SECRET_VALUE,
	.flags = TEE_TYPE_ATTR_REQUIRED | TEE_TYPE_ATTR_SIZE_INDICATOR,
	.ops_index = ATTR_OPS_INDEX_SECRET,
	.raw_offs = 0,
	.raw_size = 0
	},
};

static const struct tee_cryp_obj_type_attrs tee_cryp_obj_rsa_pub_key_attrs[] = {
	{
	.attr_id = TEE_ATTR_RSA_MODULUS,
	.flags = TEE_TYPE_ATTR_REQUIRED | TEE_TYPE_ATTR_SIZE_INDICATOR,
	.ops_index = ATTR_OPS_INDEX_BIGNUM,
	RAW_DATA(struct rsa_public_key, n)
	},

	{
	.attr_id = TEE_ATTR_RSA_PUBLIC_EXPONENT,
	.flags = TEE_TYPE_ATTR_REQUIRED,
	.ops_index = ATTR_OPS_INDEX_BIGNUM,
	RAW_DATA(struct rsa_public_key, e)
	},
};

static const struct tee_cryp_obj_type_attrs tee_cryp_obj_rsa_keypair_attrs[] = {
	{
	.attr_id = TEE_ATTR_RSA_MODULUS,
	.flags = TEE_TYPE_ATTR_REQUIRED | TEE_TYPE_ATTR_SIZE_INDICATOR,
	.ops_index = ATTR_OPS_INDEX_BIGNUM,
	RAW_DATA(struct rsa_keypair, n)
	},

	{
	.attr_id = TEE_ATTR_RSA_PUBLIC_EXPONENT,
	.flags = TEE_TYPE_ATTR_REQUIRED,
	.ops_index = ATTR_OPS_INDEX_BIGNUM,
	RAW_DATA(struct rsa_keypair, e)
	},

	{
	.attr_id = TEE_ATTR_RSA_PRIVATE_EXPONENT,
	.flags = TEE_TYPE_ATTR_REQUIRED,
	.ops_index = ATTR_OPS_INDEX_BIGNUM,
	RAW_DATA(struct rsa_keypair, d)
	},

	{
	.attr_id = TEE_ATTR_RSA_PRIME1,
	.flags = TEE_TYPE_ATTR_OPTIONAL_GROUP,
	.ops_index = ATTR_OPS_INDEX_BIGNUM,
	RAW_DATA(struct rsa_keypair, p)
	},

	{
	.attr_id = TEE_ATTR_RSA_PRIME2,
	.flags = TEE_TYPE_ATTR_OPTIONAL_GROUP,
	.ops_index = ATTR_OPS_INDEX_BIGNUM,
	RAW_DATA(struct rsa_keypair, q)
	},

	{
	.attr_id = TEE_ATTR_RSA_EXPONENT1,
	.flags = TEE_TYPE_ATTR_OPTIONAL_GROUP,
	.ops_index = ATTR_OPS_INDEX_BIGNUM,
	RAW_DATA(struct rsa_keypair, dp)
	},

	{
	.attr_id = TEE_ATTR_RSA_EXPONENT2,
	.flags = TEE_TYPE_ATTR_OPTIONAL_GROUP,
	.ops_index = ATTR_OPS_INDEX_BIGNUM,
	RAW_DATA(struct rsa_keypair, dq)
	},

	{
	.attr_id = TEE_ATTR_RSA_COEFFICIENT,
	.flags = TEE_TYPE_ATTR_OPTIONAL_GROUP,
	.ops_index = ATTR_OPS_INDEX_BIGNUM,
	RAW_DATA(struct rsa_keypair, qp)
	},
};

static const struct tee_cryp_obj_type_attrs tee_cryp_obj_dsa_pub_key_attrs[] = {
	{
	.attr_id = TEE_ATTR_DSA_PRIME,
	.flags = TEE_TYPE_ATTR_REQUIRED,
	.ops_index = ATTR_OPS_INDEX_BIGNUM,
	RAW_DATA(struct dsa_public_key, p)
	},

	{
	.attr_id = TEE_ATTR_DSA_SUBPRIME,
	.flags = TEE_TYPE_ATTR_REQUIRED | TEE_TYPE_ATTR_SIZE_INDICATOR,
	.ops_index = ATTR_OPS_INDEX_BIGNUM,
	RAW_DATA(struct dsa_public_key, q)
	},

	{
	.attr_id = TEE_ATTR_DSA_BASE,
	.flags = TEE_TYPE_ATTR_REQUIRED,
	.ops_index = ATTR_OPS_INDEX_BIGNUM,
	RAW_DATA(struct dsa_public_key, g)
	},

	{
	.attr_id = TEE_ATTR_DSA_PUBLIC_VALUE,
	.flags = TEE_TYPE_ATTR_REQUIRED,
	.ops_index = ATTR_OPS_INDEX_BIGNUM,
	RAW_DATA(struct dsa_public_key, y)
	},
};

static const struct tee_cryp_obj_type_attrs tee_cryp_obj_dsa_keypair_attrs[] = {
	{
	.attr_id = TEE_ATTR_DSA_PRIME,
	.flags = TEE_TYPE_ATTR_REQUIRED | TEE_TYPE_ATTR_GEN_KEY_REQ,
	.ops_index = ATTR_OPS_INDEX_BIGNUM,
	RAW_DATA(struct dsa_keypair, p)
	},

	{
	.attr_id = TEE_ATTR_DSA_SUBPRIME,
	.flags = TEE_TYPE_ATTR_REQUIRED | TEE_TYPE_ATTR_SIZE_INDICATOR |
		 TEE_TYPE_ATTR_GEN_KEY_REQ,
	.ops_index = ATTR_OPS_INDEX_BIGNUM,
	RAW_DATA(struct dsa_keypair, q)
	},

	{
	.attr_id = TEE_ATTR_DSA_BASE,
	.flags = TEE_TYPE_ATTR_REQUIRED | TEE_TYPE_ATTR_GEN_KEY_REQ,
	.ops_index = ATTR_OPS_INDEX_BIGNUM,
	RAW_DATA(struct dsa_keypair, g)
	},

	{
	.attr_id = TEE_ATTR_DSA_PRIVATE_VALUE,
	.flags = TEE_TYPE_ATTR_REQUIRED,
	.ops_index = ATTR_OPS_INDEX_BIGNUM,
	RAW_DATA(struct dsa_keypair, x)
	},

	{
	.attr_id = TEE_ATTR_DSA_PUBLIC_VALUE,
	.flags = TEE_TYPE_ATTR_REQUIRED,
	.ops_index = ATTR_OPS_INDEX_BIGNUM,
	RAW_DATA(struct dsa_keypair, y)
	},
};

static const struct tee_cryp_obj_type_attrs tee_cryp_obj_dh_keypair_attrs[] = {
	{
	.attr_id = TEE_ATTR_DH_PRIME,
	.flags = TEE_TYPE_ATTR_REQUIRED | TEE_TYPE_ATTR_SIZE_INDICATOR |
		 TEE_TYPE_ATTR_GEN_KEY_REQ,
	.ops_index = ATTR_OPS_INDEX_BIGNUM,
	RAW_DATA(struct dh_keypair, p)
	},

	{
	.attr_id = TEE_ATTR_DH_BASE,
	.flags = TEE_TYPE_ATTR_REQUIRED | TEE_TYPE_ATTR_GEN_KEY_REQ,
	.ops_index = ATTR_OPS_INDEX_BIGNUM,
	RAW_DATA(struct dh_keypair, g)
	},

	{
	.attr_id = TEE_ATTR_DH_PUBLIC_VALUE,
	.flags = TEE_TYPE_ATTR_REQUIRED,
	.ops_index = ATTR_OPS_INDEX_BIGNUM,
	RAW_DATA(struct dh_keypair, y)
	},

	{
	.attr_id = TEE_ATTR_DH_PRIVATE_VALUE,
	.flags = TEE_TYPE_ATTR_REQUIRED,
	.ops_index = ATTR_OPS_INDEX_BIGNUM,
	RAW_DATA(struct dh_keypair, x)
	},

	{
	.attr_id = TEE_ATTR_DH_SUBPRIME,
	.flags = TEE_TYPE_ATTR_OPTIONAL_GROUP |	 TEE_TYPE_ATTR_GEN_KEY_OPT,
	.ops_index = ATTR_OPS_INDEX_BIGNUM,
	RAW_DATA(struct dh_keypair, q)
	},

	{
	.attr_id = TEE_ATTR_DH_X_BITS,
	.flags = TEE_TYPE_ATTR_GEN_KEY_OPT,
	.ops_index = ATTR_OPS_INDEX_VALUE,
	RAW_DATA(struct dh_keypair, xbits)
	},
};

#if defined(CFG_CRYPTO_HKDF)
static const struct tee_cryp_obj_type_attrs
	tee_cryp_obj_hkdf_ikm_attrs[] = {
	{
	.attr_id = TEE_ATTR_HKDF_IKM,
	.flags = TEE_TYPE_ATTR_REQUIRED | TEE_TYPE_ATTR_SIZE_INDICATOR,
	.ops_index = ATTR_OPS_INDEX_SECRET,
	.raw_offs = 0,
	.raw_size = 0
	},
};
#endif

#if defined(CFG_CRYPTO_CONCAT_KDF)
static const struct tee_cryp_obj_type_attrs
	tee_cryp_obj_concat_kdf_z_attrs[] = {
	{
	.attr_id = TEE_ATTR_CONCAT_KDF_Z,
	.flags = TEE_TYPE_ATTR_REQUIRED | TEE_TYPE_ATTR_SIZE_INDICATOR,
	.ops_index = ATTR_OPS_INDEX_SECRET,
	.raw_offs = 0,
	.raw_size = 0
	},
};
#endif

#if defined(CFG_CRYPTO_PBKDF2)
static const struct tee_cryp_obj_type_attrs
	tee_cryp_obj_pbkdf2_passwd_attrs[] = {
	{
	.attr_id = TEE_ATTR_PBKDF2_PASSWORD,
	.flags = TEE_TYPE_ATTR_REQUIRED | TEE_TYPE_ATTR_SIZE_INDICATOR,
	.ops_index = ATTR_OPS_INDEX_SECRET,
	.raw_offs = 0,
	.raw_size = 0
	},
};
#endif

static const struct tee_cryp_obj_type_attrs tee_cryp_obj_ecc_pub_key_attrs[] = {
	{
	.attr_id = TEE_ATTR_ECC_PUBLIC_VALUE_X,
	.flags = TEE_TYPE_ATTR_REQUIRED,
	.ops_index = ATTR_OPS_INDEX_BIGNUM,
	RAW_DATA(struct ecc_public_key, x)
	},

	{
	.attr_id = TEE_ATTR_ECC_PUBLIC_VALUE_Y,
	.flags = TEE_TYPE_ATTR_REQUIRED,
	.ops_index = ATTR_OPS_INDEX_BIGNUM,
	RAW_DATA(struct ecc_public_key, y)
	},

	{
	.attr_id = TEE_ATTR_ECC_CURVE,
	.flags = TEE_TYPE_ATTR_REQUIRED,
	.ops_index = ATTR_OPS_INDEX_VALUE,
	RAW_DATA(struct ecc_public_key, curve)
	},
};

static const struct tee_cryp_obj_type_attrs tee_cryp_obj_ecc_keypair_attrs[] = {
	{
	.attr_id = TEE_ATTR_ECC_PRIVATE_VALUE,
	.flags = TEE_TYPE_ATTR_REQUIRED,
	.ops_index = ATTR_OPS_INDEX_BIGNUM,
	RAW_DATA(struct ecc_keypair, d)
	},

	{
	.attr_id = TEE_ATTR_ECC_PUBLIC_VALUE_X,
	.flags = TEE_TYPE_ATTR_REQUIRED,
	.ops_index = ATTR_OPS_INDEX_BIGNUM,
	RAW_DATA(struct ecc_keypair, x)
	},

	{
	.attr_id = TEE_ATTR_ECC_PUBLIC_VALUE_Y,
	.flags = TEE_TYPE_ATTR_REQUIRED,
	.ops_index = ATTR_OPS_INDEX_BIGNUM,
	RAW_DATA(struct ecc_keypair, y)
	},

	{
	.attr_id = TEE_ATTR_ECC_CURVE,
	.flags = TEE_TYPE_ATTR_REQUIRED,
	.ops_index = ATTR_OPS_INDEX_VALUE,
	RAW_DATA(struct ecc_keypair, curve)
	},
};

struct tee_cryp_obj_type_props {
	TEE_ObjectType obj_type;
	uint16_t min_size;	/* may not be smaller than this */
	uint16_t max_size;	/* may not be larger than this */
	uint16_t alloc_size;	/* this many bytes are allocated to hold data */
	uint8_t quanta;		/* may only be an multiple of this */

	uint8_t num_type_attrs;
	const struct tee_cryp_obj_type_attrs *type_attrs;
};

#define PROP(obj_type, quanta, min_size, max_size, alloc_size, type_attrs) \
		{ (obj_type), (min_size), (max_size), (alloc_size), (quanta), \
		  ARRAY_SIZE(type_attrs), (type_attrs) }

static const struct tee_cryp_obj_type_props tee_cryp_obj_props[] = {
	PROP(TEE_TYPE_AES, 64, 128, 256,	/* valid sizes 128, 192, 256 */
		256 / 8 + sizeof(struct tee_cryp_obj_secret),
		tee_cryp_obj_secret_value_attrs),
	PROP(TEE_TYPE_DES, 64, 64, 64,
		/*
		* Valid size 56 without parity, note that we still allocate
		* for 64 bits since the key is supplied with parity.
		*/
		64 / 8 + sizeof(struct tee_cryp_obj_secret),
		tee_cryp_obj_secret_value_attrs),
	PROP(TEE_TYPE_DES3, 64, 128, 192,
		/*
		* Valid sizes 112, 168 without parity, note that we still
		* allocate for with space for the parity since the key is
		* supplied with parity.
		*/
		192 / 8 + sizeof(struct tee_cryp_obj_secret),
		tee_cryp_obj_secret_value_attrs),
	PROP(TEE_TYPE_HMAC_MD5, 8, 64, 512,
		512 / 8 + sizeof(struct tee_cryp_obj_secret),
		tee_cryp_obj_secret_value_attrs),
	PROP(TEE_TYPE_HMAC_SHA1, 8, 80, 512,
		512 / 8 + sizeof(struct tee_cryp_obj_secret),
		tee_cryp_obj_secret_value_attrs),
	PROP(TEE_TYPE_HMAC_SHA224, 8, 112, 512,
		512 / 8 + sizeof(struct tee_cryp_obj_secret),
		tee_cryp_obj_secret_value_attrs),
	PROP(TEE_TYPE_HMAC_SHA256, 8, 192, 1024,
		1024 / 8 + sizeof(struct tee_cryp_obj_secret),
		tee_cryp_obj_secret_value_attrs),
	PROP(TEE_TYPE_HMAC_SHA384, 8, 256, 1024,
		1024 / 8 + sizeof(struct tee_cryp_obj_secret),
		tee_cryp_obj_secret_value_attrs),
	PROP(TEE_TYPE_HMAC_SHA512, 8, 256, 1024,
		1024 / 8 + sizeof(struct tee_cryp_obj_secret),
		tee_cryp_obj_secret_value_attrs),
	PROP(TEE_TYPE_GENERIC_SECRET, 8, 0, 4096,
		4096 / 8 + sizeof(struct tee_cryp_obj_secret),
		tee_cryp_obj_secret_value_attrs),
#if defined(CFG_CRYPTO_HKDF)
	PROP(TEE_TYPE_HKDF_IKM, 8, 0, 4096,
		4096 / 8 + sizeof(struct tee_cryp_obj_secret),
		tee_cryp_obj_hkdf_ikm_attrs),
#endif
#if defined(CFG_CRYPTO_CONCAT_KDF)
	PROP(TEE_TYPE_CONCAT_KDF_Z, 8, 0, 4096,
		4096 / 8 + sizeof(struct tee_cryp_obj_secret),
		tee_cryp_obj_concat_kdf_z_attrs),
#endif
#if defined(CFG_CRYPTO_PBKDF2)
	PROP(TEE_TYPE_PBKDF2_PASSWORD, 8, 0, 4096,
		4096 / 8 + sizeof(struct tee_cryp_obj_secret),
		tee_cryp_obj_pbkdf2_passwd_attrs),
#endif
	PROP(TEE_TYPE_RSA_PUBLIC_KEY, 1, 256, 2048,
		sizeof(struct rsa_public_key),
		tee_cryp_obj_rsa_pub_key_attrs),

	PROP(TEE_TYPE_RSA_KEYPAIR, 1, 256, 2048,
		sizeof(struct rsa_keypair),
		tee_cryp_obj_rsa_keypair_attrs),

	PROP(TEE_TYPE_DSA_PUBLIC_KEY, 64, 512, 3072,
		sizeof(struct dsa_public_key),
		tee_cryp_obj_dsa_pub_key_attrs),

	PROP(TEE_TYPE_DSA_KEYPAIR, 64, 512, 3072,
		sizeof(struct dsa_keypair),
		tee_cryp_obj_dsa_keypair_attrs),

	PROP(TEE_TYPE_DH_KEYPAIR, 1, 256, 2048,
		sizeof(struct dh_keypair),
		tee_cryp_obj_dh_keypair_attrs),

	PROP(TEE_TYPE_ECDSA_PUBLIC_KEY, 1, 192, 521,
		sizeof(struct ecc_public_key),
		tee_cryp_obj_ecc_pub_key_attrs),

	PROP(TEE_TYPE_ECDSA_KEYPAIR, 1, 192, 521,
		sizeof(struct ecc_keypair),
		tee_cryp_obj_ecc_keypair_attrs),

	PROP(TEE_TYPE_ECDH_PUBLIC_KEY, 1, 192, 521,
		sizeof(struct ecc_public_key),
		tee_cryp_obj_ecc_pub_key_attrs),

	PROP(TEE_TYPE_ECDH_KEYPAIR, 1, 192, 521,
		sizeof(struct ecc_keypair),
		tee_cryp_obj_ecc_keypair_attrs),
};

struct attr_ops {
	TEE_Result (*from_user)(void *attr, const void *buffer, size_t size);
	TEE_Result (*to_user)(void *attr, tee_api_info_t *ta_info,
			      void *buffer, uint64_t *size);
	void (*to_binary)(void *attr, void *data, size_t data_len,
			  size_t *offs);
	bool (*from_binary)(void *attr, const void *data, size_t data_len,
			    size_t *offs);
	TEE_Result (*from_obj)(void *attr, void *src_attr);
	void (*free)(void *attr);
	void (*clear)(void *attr);
};

static void op_u32_to_binary_helper(uint32_t v, uint8_t *data,
				    size_t data_len, size_t *offs)
{
	uint32_t field;

	if (data && (*offs + sizeof(field)) <= data_len) {
		field = TEE_U32_TO_BIG_ENDIAN(v);
		memcpy(data + *offs, &field, sizeof(field));
	}
	(*offs) += sizeof(field);
}

static bool op_u32_from_binary_helper(uint32_t *v, const uint8_t *data,
				      size_t data_len, size_t *offs)
{
	uint32_t field;

	if (!data || (*offs + sizeof(field)) > data_len)
		return false;

	memcpy(&field, data + *offs, sizeof(field));
	*v = TEE_U32_FROM_BIG_ENDIAN(field);
	(*offs) += sizeof(field);
	return true;
}

static TEE_Result op_attr_secret_value_from_user(void *attr, const void *buffer,
						 size_t size)
{
	struct tee_cryp_obj_secret *key = attr;

	/* Data size has to fit in allocated buffer */
	if (size > key->alloc_size)
		return TEE_ERROR_SECURITY;
	memcpy(key + 1, buffer, size);
	key->key_size = size;
	return TEE_SUCCESS;
}

static TEE_Result op_attr_secret_value_to_user(void *attr,
			tee_api_info_t *ta_info __unused,
			void *buffer, uint64_t *size)
{
	TEE_Result res;
	struct tee_cryp_obj_secret *key = attr;
	uint64_t s;
	uint64_t key_size;

	res = tee_svc_copy_from_user(&s, size, sizeof(s));
	if (res != TEE_SUCCESS)
		return res;

	key_size = key->key_size;
	res = tee_svc_copy_to_user(size, &key_size, sizeof(key_size));
	if (res != TEE_SUCCESS)
		return res;

	if (s < key->key_size)
		return TEE_ERROR_SHORT_BUFFER;

	return tee_svc_copy_to_user(buffer, key + 1, key->key_size);
}

static void op_attr_secret_value_to_binary(void *attr, void *data,
					   size_t data_len, size_t *offs)
{
	struct tee_cryp_obj_secret *key = attr;

	op_u32_to_binary_helper(key->key_size, data, data_len, offs);
	if (data && (*offs + key->key_size) <= data_len)
		memcpy((uint8_t *)data + *offs, key + 1, key->key_size);
	(*offs) += key->key_size;
}

static bool op_attr_secret_value_from_binary(void *attr, const void *data,
					     size_t data_len, size_t *offs)
{
	struct tee_cryp_obj_secret *key = attr;
	uint32_t s;

	if (!op_u32_from_binary_helper(&s, data, data_len, offs))
		return false;

	if ((*offs + s) > data_len)
		return false;

	/* Data size has to fit in allocated buffer */
	if (s > key->alloc_size)
		return false;
	key->key_size = s;
	memcpy(key + 1, (const uint8_t *)data + *offs, s);
	(*offs) += s;
	return true;
}


static TEE_Result op_attr_secret_value_from_obj(void *attr, void *src_attr)
{
	struct tee_cryp_obj_secret *key = attr;
	struct tee_cryp_obj_secret *src_key = src_attr;

	if (src_key->key_size > key->alloc_size)
		return TEE_ERROR_BAD_STATE;
	memcpy(key + 1, src_key + 1, src_key->key_size);
	key->key_size = src_key->key_size;
	return TEE_SUCCESS;
}

static void op_attr_secret_value_clear(void *attr)
{
	struct tee_cryp_obj_secret *key = attr;

	key->key_size = 0;
	memset(key + 1, 0, key->alloc_size);
}

static TEE_Result op_attr_bignum_from_user(void *attr, const void *buffer,
					   size_t size)
{
	struct bignum **bn = attr;

	if (!crypto_ops.bignum.bin2bn)
		return TEE_ERROR_NOT_IMPLEMENTED;
	return crypto_ops.bignum.bin2bn(buffer, size, *bn);
}

static TEE_Result op_attr_bignum_to_user(void *attr,
					 tee_api_info_t *ta_info __unused,
					 void *buffer, uint64_t *size)
{
	TEE_Result res;
	struct bignum **bn = attr;
	uint64_t req_size;
	uint64_t s;

	res = tee_svc_copy_from_user(&s, size, sizeof(s));
	if (res != TEE_SUCCESS)
		return res;

	req_size = crypto_ops.bignum.num_bytes(*bn);
	res = tee_svc_copy_to_user(size, &req_size, sizeof(req_size));
	if (res != TEE_SUCCESS)
		return res;
	if (!req_size)
		return TEE_SUCCESS;
	if (s < req_size)
		return TEE_ERROR_SHORT_BUFFER;

	/* Check we can access data using supplied user mode pointer */
	res = tee_mmu_check_access_rights(uthread_get_current(),
					  TEE_MEMORY_ACCESS_READ |
					  TEE_MEMORY_ACCESS_WRITE |
					  TEE_MEMORY_ACCESS_ANY_OWNER,
					  (uaddr_t)buffer, req_size);
	if (res != TEE_SUCCESS)
		return res;
	/*
	* Write the bignum (wich raw data points to) into an array of
	* bytes (stored in buffer)
	*/
	crypto_ops.bignum.bn2bin(*bn, buffer);
	return TEE_SUCCESS;
}

static void op_attr_bignum_to_binary(void *attr, void *data, size_t data_len,
				     size_t *offs)
{
	struct bignum **bn = attr;
	uint32_t n = crypto_ops.bignum.num_bytes(*bn);

	op_u32_to_binary_helper(n, data, data_len, offs);

	if (data && (*offs + n) <= data_len)
		crypto_ops.bignum.bn2bin(*bn, (uint8_t *)data + *offs);
	(*offs) += n;
}

static bool op_attr_bignum_from_binary(void *attr, const void *data,
				       size_t data_len, size_t *offs)
{
	struct bignum **bn = attr;
	uint32_t n;

	if (!op_u32_from_binary_helper(&n, data, data_len, offs))
		return false;

	if ((*offs + n) > data_len)
		return false;
	if (crypto_ops.bignum.bin2bn((const uint8_t *)data + *offs,
				     n, *bn) != TEE_SUCCESS)
		return false;
	(*offs) += n;
	return true;
}

static TEE_Result op_attr_bignum_from_obj(void *attr, void *src_attr)
{
	struct bignum **bn = attr;
	struct bignum **src_bn = src_attr;

	crypto_ops.bignum.copy(*bn, *src_bn);
	return TEE_SUCCESS;
}

static void op_attr_bignum_clear(void *attr)
{
	struct bignum **bn = attr;

	crypto_ops.bignum.clear(*bn);
}

static void op_attr_bignum_free(void *attr)
{
	struct bignum **bn = attr;

	crypto_ops.bignum.free(*bn);
	*bn = NULL;
}

static TEE_Result op_attr_value_from_user(void *attr, const void *buffer,
					  size_t size)
{
	uint32_t *v = attr;

	if (size != sizeof(uint32_t) * 2)
		return TEE_ERROR_GENERIC; /* "can't happen */

	/* Note that only the first value is copied */
	memcpy(v, buffer, sizeof(uint32_t));
	return TEE_SUCCESS;
}

static TEE_Result op_attr_value_to_user(void *attr,
					tee_api_info_t *ta_info __unused,
					void *buffer, uint64_t *size)
{
	TEE_Result res;
	uint32_t *v = attr;
	uint64_t s;
	uint32_t value[2] = { *v };
	uint64_t req_size = sizeof(value);

	res = tee_svc_copy_from_user(&s, size, sizeof(s));
	if (res != TEE_SUCCESS)
		return res;

	if (s < req_size)
		return TEE_ERROR_SHORT_BUFFER;

	return tee_svc_copy_to_user(buffer, value, req_size);
}

static void op_attr_value_to_binary(void *attr, void *data, size_t data_len,
				    size_t *offs)
{
	uint32_t *v = attr;

	op_u32_to_binary_helper(*v, data, data_len, offs);
}

static bool op_attr_value_from_binary(void *attr, const void *data,
				      size_t data_len, size_t *offs)
{
	uint32_t *v = attr;

	return op_u32_from_binary_helper(v, data, data_len, offs);
}

static TEE_Result op_attr_value_from_obj(void *attr, void *src_attr)
{
	uint32_t *v = attr;
	uint32_t *src_v = src_attr;

	*v = *src_v;
	return TEE_SUCCESS;
}

static void op_attr_value_clear(void *attr)
{
	uint32_t *v = attr;

	*v = 0;
}

static const struct attr_ops attr_ops[] = {
	[ATTR_OPS_INDEX_SECRET] = {
		.from_user = op_attr_secret_value_from_user,
		.to_user = op_attr_secret_value_to_user,
		.to_binary = op_attr_secret_value_to_binary,
		.from_binary = op_attr_secret_value_from_binary,
		.from_obj = op_attr_secret_value_from_obj,
		.free = op_attr_secret_value_clear, /* not a typo */
		.clear = op_attr_secret_value_clear,
	},
	[ATTR_OPS_INDEX_BIGNUM] = {
		.from_user = op_attr_bignum_from_user,
		.to_user = op_attr_bignum_to_user,
		.to_binary = op_attr_bignum_to_binary,
		.from_binary = op_attr_bignum_from_binary,
		.from_obj = op_attr_bignum_from_obj,
		.free = op_attr_bignum_free,
		.clear = op_attr_bignum_clear,
	},
	[ATTR_OPS_INDEX_VALUE] = {
		.from_user = op_attr_value_from_user,
		.to_user = op_attr_value_to_user,
		.to_binary = op_attr_value_to_binary,
		.from_binary = op_attr_value_from_binary,
		.from_obj = op_attr_value_from_obj,
		.free = op_attr_value_clear, /* not a typo */
		.clear = op_attr_value_clear,
	},
};

TEE_Result __SYSCALL sys_utee_cryp_obj_get_info(unsigned long obj,
		TEE_ObjectInfo *info)
{
	TEE_Result res;
	tee_api_info_t *ta_info = tee_current_ta_info();
	struct tee_obj *o;

	res = tee_obj_get(ta_info,
			  tee_svc_uref_to_vaddr(obj), &o);
	if (res != TEE_SUCCESS)
		goto exit;

	res = tee_svc_copy_to_user(info, &o->info, sizeof(o->info));

exit:
	return res;
}

TEE_Result __SYSCALL sys_utee_cryp_obj_restrict_usage(unsigned long obj,
			unsigned long usage)
{
	TEE_Result res;
	tee_api_info_t *ta_info = tee_current_ta_info();
	struct tee_obj *o;

	res = tee_obj_get(ta_info,
			  tee_svc_uref_to_vaddr(obj), &o);
	if (res != TEE_SUCCESS)
		goto exit;

	o->info.objectUsage &= usage;

exit:
	return res;
}

static int tee_svc_cryp_obj_find_type_attr_idx(
		uint32_t attr_id,
		const struct tee_cryp_obj_type_props *type_props)
{
	size_t n;

	for (n = 0; n < type_props->num_type_attrs; n++) {
		if (attr_id == type_props->type_attrs[n].attr_id)
			return n;
	}
	return -1;
}

static const struct tee_cryp_obj_type_props *tee_svc_find_type_props(
		TEE_ObjectType obj_type)
{
	size_t n;

	for (n = 0; n < ARRAY_SIZE(tee_cryp_obj_props); n++) {
		if (tee_cryp_obj_props[n].obj_type == obj_type)
			return tee_cryp_obj_props + n;
	}

	return NULL;
}

/* Set an attribute on an object */
static void set_attribute(struct tee_obj *o,
			  const struct tee_cryp_obj_type_props *props,
			  uint32_t attr)
{
	int idx = tee_svc_cryp_obj_find_type_attr_idx(attr, props);

	if (idx < 0)
		return;
	o->have_attrs |= BIT(idx);
}

/* Get an attribute on an object */
static uint32_t get_attribute(const struct tee_obj *o,
			      const struct tee_cryp_obj_type_props *props,
			      uint32_t attr)
{
	int idx = tee_svc_cryp_obj_find_type_attr_idx(attr, props);

	if (idx < 0)
		return 0;
	return o->have_attrs & BIT(idx);
}

TEE_Result __SYSCALL sys_utee_cryp_obj_get_attr(unsigned long obj,
		unsigned long attr_id, void *buffer, uint64_t *size)
{
	TEE_Result res;
	tee_api_info_t *ta_info = tee_current_ta_info();
	struct tee_obj *o;
	const struct tee_cryp_obj_type_props *type_props;
	int idx;
	const struct attr_ops *ops;
	void *attr;

	res = tee_obj_get(ta_info,
			  tee_svc_uref_to_vaddr(obj), &o);
	if (res != TEE_SUCCESS)
		return TEE_ERROR_ITEM_NOT_FOUND;

	/* Check that the object is initialized */
	if (!(o->info.handleFlags & TEE_HANDLE_FLAG_INITIALIZED))
		return TEE_ERROR_BAD_PARAMETERS;

	/* Check that getting the attribute is allowed */
	if (!(attr_id & TEE_ATTR_FLAG_PUBLIC) &&
	    !(o->info.objectUsage & TEE_USAGE_EXTRACTABLE))
		return TEE_ERROR_BAD_PARAMETERS;

	type_props = tee_svc_find_type_props(o->info.objectType);
	if (!type_props) {
		/* Unknown object type, "can't happen" */
		return TEE_ERROR_BAD_STATE;
	}

	idx = tee_svc_cryp_obj_find_type_attr_idx(attr_id, type_props);
	if ((idx < 0) || ((o->have_attrs & (1 << idx)) == 0))
		return TEE_ERROR_ITEM_NOT_FOUND;

	ops = attr_ops + type_props->type_attrs[idx].ops_index;
	attr = (uint8_t *)o->attr + type_props->type_attrs[idx].raw_offs;
	return ops->to_user(attr, ta_info, buffer, size);
}

void tee_obj_attr_free(struct tee_obj *o)
{
	const struct tee_cryp_obj_type_props *tp;
	size_t n;

	if (!o->attr)
		return;
	tp = tee_svc_find_type_props(o->info.objectType);
	if (!tp)
		return;

	for (n = 0; n < tp->num_type_attrs; n++) {
		const struct tee_cryp_obj_type_attrs *ta = tp->type_attrs + n;

		attr_ops[ta->ops_index].free((uint8_t *)o->attr + ta->raw_offs);
	}
}

void tee_obj_attr_clear(struct tee_obj *o)
{
	const struct tee_cryp_obj_type_props *tp;
	size_t n;

	if (!o->attr)
		return;
	tp = tee_svc_find_type_props(o->info.objectType);
	if (!tp)
		return;

	for (n = 0; n < tp->num_type_attrs; n++) {
		const struct tee_cryp_obj_type_attrs *ta = tp->type_attrs + n;

		attr_ops[ta->ops_index].clear((uint8_t *)o->attr +
					      ta->raw_offs);
	}
}

TEE_Result tee_obj_attr_to_binary(struct tee_obj *o, void *data,
				  size_t *data_len)
{
	const struct tee_cryp_obj_type_props *tp;
	size_t n;
	size_t offs = 0;
	size_t len = data ? *data_len : 0;

	if (o->info.objectType == TEE_TYPE_DATA) {
		*data_len = 0;
		return TEE_SUCCESS; /* pure data object */
	}
	if (!o->attr)
		return TEE_ERROR_BAD_STATE;
	tp = tee_svc_find_type_props(o->info.objectType);
	if (!tp)
		return TEE_ERROR_BAD_STATE;

	for (n = 0; n < tp->num_type_attrs; n++) {
		const struct tee_cryp_obj_type_attrs *ta = tp->type_attrs + n;
		void *attr = (uint8_t *)o->attr + ta->raw_offs;

		attr_ops[ta->ops_index].to_binary(attr, data, len, &offs);
	}

	*data_len = offs;
	if (data && offs > len)
		return TEE_ERROR_SHORT_BUFFER;
	return TEE_SUCCESS;
}

TEE_Result tee_obj_attr_from_binary(struct tee_obj *o, const void *data,
				    size_t data_len)
{
	const struct tee_cryp_obj_type_props *tp;
	size_t n;
	size_t offs = 0;

	if (o->info.objectType == TEE_TYPE_DATA)
		return TEE_SUCCESS; /* pure data object */
	if (!o->attr)
		return TEE_ERROR_BAD_STATE;
	tp = tee_svc_find_type_props(o->info.objectType);
	if (!tp)
		return TEE_ERROR_BAD_STATE;

	for (n = 0; n < tp->num_type_attrs; n++) {
		const struct tee_cryp_obj_type_attrs *ta = tp->type_attrs + n;
		void *attr = (uint8_t *)o->attr + ta->raw_offs;

		if (!attr_ops[ta->ops_index].from_binary(attr, data, data_len,
							 &offs))
			return TEE_ERROR_CORRUPT_OBJECT;
	}
	return TEE_SUCCESS;
}

TEE_Result tee_obj_attr_copy_from(struct tee_obj *o, const struct tee_obj *src)
{
	TEE_Result res;
	const struct tee_cryp_obj_type_props *tp;
	const struct tee_cryp_obj_type_attrs *ta;
	size_t n;
	uint32_t have_attrs = 0;
	void *attr;
	void *src_attr;

	if (o->info.objectType == TEE_TYPE_DATA)
		return TEE_SUCCESS; /* pure data object */
	if (!o->attr)
		return TEE_ERROR_BAD_STATE;
	tp = tee_svc_find_type_props(o->info.objectType);
	if (!tp)
		return TEE_ERROR_BAD_STATE;

	if (o->info.objectType == src->info.objectType) {
		have_attrs = src->have_attrs;
		for (n = 0; n < tp->num_type_attrs; n++) {
			ta = tp->type_attrs + n;
			attr = (uint8_t *)o->attr + ta->raw_offs;
			src_attr = (uint8_t *)src->attr + ta->raw_offs;
			res = attr_ops[ta->ops_index].from_obj(attr, src_attr);
			if (res != TEE_SUCCESS)
				return res;
		}
	} else {
		const struct tee_cryp_obj_type_props *tp_src;
		int idx;

		if (o->info.objectType == TEE_TYPE_RSA_PUBLIC_KEY) {
			if (src->info.objectType != TEE_TYPE_RSA_KEYPAIR)
				return TEE_ERROR_BAD_PARAMETERS;
		} else if (o->info.objectType == TEE_TYPE_DSA_PUBLIC_KEY) {
			if (src->info.objectType != TEE_TYPE_DSA_KEYPAIR)
				return TEE_ERROR_BAD_PARAMETERS;
		} else if (o->info.objectType == TEE_TYPE_ECDSA_PUBLIC_KEY) {
			if (src->info.objectType != TEE_TYPE_ECDSA_KEYPAIR)
				return TEE_ERROR_BAD_PARAMETERS;
		} else if (o->info.objectType == TEE_TYPE_ECDH_PUBLIC_KEY) {
			if (src->info.objectType != TEE_TYPE_ECDH_KEYPAIR)
				return TEE_ERROR_BAD_PARAMETERS;
		} else {
			return TEE_ERROR_BAD_PARAMETERS;
		}

		tp_src = tee_svc_find_type_props(src->info.objectType);
		if (!tp_src)
			return TEE_ERROR_BAD_STATE;

		have_attrs = BIT32(tp->num_type_attrs) - 1;
		for (n = 0; n < tp->num_type_attrs; n++) {
			ta = tp->type_attrs + n;

			idx = tee_svc_cryp_obj_find_type_attr_idx(ta->attr_id,
								  tp_src);
			if (idx < 0)
				return TEE_ERROR_BAD_STATE;

			attr = (uint8_t *)o->attr + ta->raw_offs;
			src_attr = (uint8_t *)src->attr +
				   tp_src->type_attrs[idx].raw_offs;
			res = attr_ops[ta->ops_index].from_obj(attr, src_attr);
			if (res != TEE_SUCCESS)
				return res;
		}
	}

	o->have_attrs = have_attrs;
	return TEE_SUCCESS;
}

TEE_Result tee_obj_set_type(struct tee_obj *o, uint32_t obj_type,
			    size_t max_key_size)
{
	TEE_Result res = TEE_SUCCESS;
	const struct tee_cryp_obj_type_props *type_props;

	/* Can only set type for newly allocated objs */
	if (o->attr)
		return TEE_ERROR_BAD_STATE;

	/*
	 * Verify that maxKeySize is supported and find out how
	 * much should be allocated.
	 */

	if (obj_type == TEE_TYPE_DATA) {
		if (max_key_size)
			return TEE_ERROR_NOT_SUPPORTED;
	} else {
		/* Find description of object */
		type_props = tee_svc_find_type_props(obj_type);
		if (!type_props)
			return TEE_ERROR_NOT_SUPPORTED;

		/* Check that maxKeySize follows restrictions */
		if (max_key_size % type_props->quanta != 0)
			return TEE_ERROR_NOT_SUPPORTED;
		if (max_key_size < type_props->min_size)
			return TEE_ERROR_NOT_SUPPORTED;
		if (max_key_size > type_props->max_size)
			return TEE_ERROR_NOT_SUPPORTED;

		o->attr = calloc(1, type_props->alloc_size);
		if (!o->attr)
			return TEE_ERROR_OUT_OF_MEMORY;
	}

	/* If we have a key structure, pre-allocate the bignums inside */
	switch (obj_type) {
	case TEE_TYPE_RSA_PUBLIC_KEY:
		if (!crypto_ops.acipher.alloc_rsa_public_key)
			return TEE_ERROR_NOT_IMPLEMENTED;
		res = crypto_ops.acipher.alloc_rsa_public_key(o->attr,
							      max_key_size);
		break;
	case TEE_TYPE_RSA_KEYPAIR:
		if (!crypto_ops.acipher.alloc_rsa_keypair)
			return TEE_ERROR_NOT_IMPLEMENTED;
		res = crypto_ops.acipher.alloc_rsa_keypair(o->attr,
							   max_key_size);
		break;
	case TEE_TYPE_DSA_PUBLIC_KEY:
		if (!crypto_ops.acipher.alloc_dsa_public_key)
			return TEE_ERROR_NOT_IMPLEMENTED;
		res = crypto_ops.acipher.alloc_dsa_public_key(o->attr,
							      max_key_size);
		break;
	case TEE_TYPE_DSA_KEYPAIR:
		if (!crypto_ops.acipher.alloc_dsa_keypair)
			return TEE_ERROR_NOT_IMPLEMENTED;
		res = crypto_ops.acipher.alloc_dsa_keypair(o->attr,
							   max_key_size);
		break;
	case TEE_TYPE_DH_KEYPAIR:
		if (!crypto_ops.acipher.alloc_dh_keypair)
			return TEE_ERROR_NOT_IMPLEMENTED;
		res = crypto_ops.acipher.alloc_dh_keypair(o->attr,
							  max_key_size);
		break;
	case TEE_TYPE_ECDSA_PUBLIC_KEY:
	case TEE_TYPE_ECDH_PUBLIC_KEY:
		if (!crypto_ops.acipher.alloc_ecc_public_key)
			return TEE_ERROR_NOT_IMPLEMENTED;
		res = crypto_ops.acipher.alloc_ecc_public_key(o->attr,
							      max_key_size);
		break;
	case TEE_TYPE_ECDSA_KEYPAIR:
	case TEE_TYPE_ECDH_KEYPAIR:
		if (!crypto_ops.acipher.alloc_ecc_keypair)
			return TEE_ERROR_NOT_IMPLEMENTED;
		res = crypto_ops.acipher.alloc_ecc_keypair(o->attr,
							   max_key_size);
		break;
	default:
		if (obj_type != TEE_TYPE_DATA) {
			struct tee_cryp_obj_secret *key = o->attr;

			key->alloc_size = type_props->alloc_size -
					  sizeof(*key);
		}
		break;
	}

	if (res != TEE_SUCCESS)
		return res;

	o->info.objectType = obj_type;
	o->info.maxObjectSize = max_key_size;
	o->info.objectUsage = TEE_USAGE_DEFAULT;

	return TEE_SUCCESS;
}

TEE_Result __SYSCALL sys_utee_cryp_obj_alloc(unsigned long obj_type,
			unsigned long max_key_size, uint32_t *obj)
{
	TEE_Result res;
	tee_api_info_t *ta_info = tee_current_ta_info();
	struct tee_obj *o;

	if (obj_type == TEE_TYPE_DATA)
		return TEE_ERROR_NOT_SUPPORTED;

	o = tee_obj_alloc();
	if (!o)
		return TEE_ERROR_OUT_OF_MEMORY;

	res = tee_obj_set_type(o, obj_type, max_key_size);
	if (res != TEE_SUCCESS) {
		tee_obj_free(o);
		return res;
	}

	tee_obj_add(ta_info, o);

	res = tee_svc_copy_kaddr_to_uref(obj, o);
	if (res != TEE_SUCCESS)
		tee_obj_close(o);
	return res;
}

TEE_Result __SYSCALL sys_utee_cryp_obj_close(unsigned long obj)
{
	TEE_Result res;
	tee_api_info_t *ta_info = tee_current_ta_info();
	struct tee_obj *o;

	res = tee_obj_get(ta_info,
			  tee_svc_uref_to_vaddr(obj), &o);
	if (res != TEE_SUCCESS)
		return res;

	/*
	 * If it's busy it's used by an operation, a client should never have
	 * this handle.
	 */
	if (o->busy)
		return TEE_ERROR_ITEM_NOT_FOUND;

	tee_obj_close(o);
	return TEE_SUCCESS;
}

TEE_Result __SYSCALL sys_utee_cryp_obj_reset(unsigned long obj)
{
	TEE_Result res;
	tee_api_info_t *ta_info = tee_current_ta_info();
	struct tee_obj *o;

	res = tee_obj_get(ta_info,
			  tee_svc_uref_to_vaddr(obj), &o);
	if (res != TEE_SUCCESS)
		return res;

	if ((o->info.handleFlags & TEE_HANDLE_FLAG_PERSISTENT) == 0) {
		tee_obj_attr_clear(o);
		o->info.objectSize = 0;
		o->info.objectUsage = TEE_USAGE_DEFAULT;
	} else {
		return TEE_ERROR_BAD_PARAMETERS;
	}

	/* the object is no more initialized */
	o->info.handleFlags &= ~TEE_HANDLE_FLAG_INITIALIZED;

	return TEE_SUCCESS;
}

static TEE_Result copy_in_attrs(uthread_t *ut,
			const struct utee_attribute *usr_attrs,
			uint32_t attr_count, TEE_Attribute *attrs)
{
	TEE_Result res;
	uint32_t n;

	res = tee_mmu_check_access_rights(ut,
			TEE_MEMORY_ACCESS_READ | TEE_MEMORY_ACCESS_ANY_OWNER,
			(uaddr_t)usr_attrs,
			attr_count * sizeof(struct utee_attribute));
	if (res != TEE_SUCCESS)
		return res;

	for (n = 0; n < attr_count; n++) {
		attrs[n].attributeID = usr_attrs[n].attribute_id;
		if (attrs[n].attributeID & TEE_ATTR_FLAG_VALUE) {
			attrs[n].content.value.a = usr_attrs[n].a;
			attrs[n].content.value.b = usr_attrs[n].b;
		} else {
			uintptr_t buf = usr_attrs[n].a;
			size_t len = usr_attrs[n].b;

			res = tee_mmu_check_access_rights(ut,
				TEE_MEMORY_ACCESS_READ |
				TEE_MEMORY_ACCESS_ANY_OWNER, buf, len);
			if (res != TEE_SUCCESS)
				return res;
			attrs[n].content.ref.buffer = (void *)buf;
			attrs[n].content.ref.length = len;
		}
	}

	return TEE_SUCCESS;
}

enum attr_usage {
	ATTR_USAGE_POPULATE,
	ATTR_USAGE_GENERATE_KEY
};

static TEE_Result tee_svc_cryp_check_attr(enum attr_usage usage,
					  const struct tee_cryp_obj_type_props
						*type_props,
					  const TEE_Attribute *attrs,
					  uint32_t attr_count)
{
	uint32_t required_flag;
	uint32_t opt_flag;
	bool all_opt_needed;
	uint32_t req_attrs = 0;
	uint32_t opt_grp_attrs = 0;
	uint32_t attrs_found = 0;
	size_t n;
	uint32_t bit;
	uint32_t flags;
	int idx;

	if (usage == ATTR_USAGE_POPULATE) {
		required_flag = TEE_TYPE_ATTR_REQUIRED;
		opt_flag = TEE_TYPE_ATTR_OPTIONAL_GROUP;
		all_opt_needed = true;
	} else {
		required_flag = TEE_TYPE_ATTR_GEN_KEY_REQ;
		opt_flag = TEE_TYPE_ATTR_GEN_KEY_OPT;
		all_opt_needed = false;
	}

	/*
	 * First find out which attributes are required and which belong to
	 * the optional group
	 */
	for (n = 0; n < type_props->num_type_attrs; n++) {
		bit = 1 << n;
		flags = type_props->type_attrs[n].flags;

		if (flags & required_flag)
			req_attrs |= bit;
		else if (flags & opt_flag)
			opt_grp_attrs |= bit;
	}

	/*
	 * Verify that all required attributes are in place and
	 * that the same attribute isn't repeated.
	 */
	for (n = 0; n < attr_count; n++) {
		idx = tee_svc_cryp_obj_find_type_attr_idx(
							attrs[n].attributeID,
							type_props);

		/* attribute not defined in current object type */
		if (idx < 0)
			return TEE_ERROR_ITEM_NOT_FOUND;

		/* Verify that RSA public exponent is odd number greater
		 * than or equal to 65537.
		 * NOTE: While document [NIST SP800-56B] (to which GP API
		 * specification refers to regarding properties of RSA
		 * attributes) states that: 65537 <= e < 2^256, underlying
		 * implementation uses uint32_t for e and libtomcrypt uses long.
		 * For these reasons check is done having 32-bit values in mind
		 * and longer values are not allowed.
		 */
		if (attrs[n].attributeID == TEE_ATTR_RSA_PUBLIC_EXPONENT) {
			uint32_t check_e = 0;

			if (attrs[n].content.ref.length > sizeof(uint32_t))
				return TEE_ERROR_BAD_PARAMETERS;

			memcpy((void *)&check_e, attrs[n].content.ref.buffer,
				sizeof(uint32_t));
			if (check_e < 65537 || !(check_e & 0x1))
				return TEE_ERROR_BAD_PARAMETERS;
		}

		/* For TEE_ATTR_DH_X_BITS, if value is zero that means
		 * that although this attribute is present, it is undefined,
		 * so return TEE_ERROR_ITEM_NOT_FOUND
		 */
		if (attrs[n].attributeID == TEE_ATTR_DH_X_BITS &&
			(attrs[n].content.value.a == 0 ||
			attrs[n].content.value.b == 0))
			return TEE_ERROR_ITEM_NOT_FOUND;

		bit = 1 << idx;

		/* attribute not repeated */
		if ((attrs_found & bit) != 0)
			return TEE_ERROR_ITEM_NOT_FOUND;

		attrs_found |= bit;
	}
	/* Required attribute missing */
	if ((attrs_found & req_attrs) != req_attrs)
		return TEE_ERROR_ITEM_NOT_FOUND;

	/*
	 * If the flag says that "if one of the optional attributes are included
	 * all of them has to be included" this must be checked.
	 */
	if (all_opt_needed && (attrs_found & opt_grp_attrs) != 0 &&
	    (attrs_found & opt_grp_attrs) != opt_grp_attrs)
		return TEE_ERROR_ITEM_NOT_FOUND;

	return TEE_SUCCESS;
}

static TEE_Result tee_ecc_adjust_max_obj_size(size_t *max_obj_size)
{
	/* Values to which max_obj_size is set correspond to maximum bit lengths
	 * of the underlying fields of the curves recommended in document
	 * FIPS 186-3 (Table D-1).
	 * The only exception is value for 521 curve in which case value is
	 * set to value LTC_MAX_BITS_PER_VARIABLE.
	 */
	switch (*max_obj_size) {
	case 192:		// TEE_ECC_CURVE_NIST_P192
		*max_obj_size = 223;
		break;
	case 224:		// TEE_ECC_CURVE_NIST_P192
		*max_obj_size = 255;
		break;
	case 256:		// TEE_ECC_CURVE_NIST_P192
		*max_obj_size = 383;
		break;
	case 384:		// TEE_ECC_CURVE_NIST_P192
		*max_obj_size = 511;
		break;
	case 521:		// TEE_ECC_CURVE_NIST_P192
		*max_obj_size = 4096;
		break;
	default:
		return TEE_ERROR_NOT_SUPPORTED;
	}
	return TEE_SUCCESS;
}


static TEE_Result tee_svc_cryp_obj_populate_type(
		struct tee_obj *o,
		const struct tee_cryp_obj_type_props *type_props,
		const TEE_Attribute *attrs,
		uint32_t attr_count)
{
	TEE_Result res;
	uint32_t have_attrs = 0;
	size_t obj_size = 0;
	size_t n;
	int idx;
	const struct attr_ops *ops;
	void *attr;

	for (n = 0; n < attr_count; n++) {
		idx = tee_svc_cryp_obj_find_type_attr_idx(
							attrs[n].attributeID,
							type_props);
		/* attribute not defined in current object type */
		if (idx < 0)
			return TEE_ERROR_ITEM_NOT_FOUND;

		have_attrs |= BIT32(idx);
		ops = attr_ops + type_props->type_attrs[idx].ops_index;
		attr = (uint8_t *)o->attr +
		       type_props->type_attrs[idx].raw_offs;
		if (attrs[n].attributeID & TEE_ATTR_FLAG_VALUE)
			res = ops->from_user(attr, &attrs[n].content.value,
					     sizeof(attrs[n].content.value));
		else {
			/* Check if an attribute value is too big to fit within
			 * the maximum object size specified when the object
			 * was created.
			 */
			size_t attr_size = o->info.maxObjectSize;

			if ((o->info.objectType & 0xFFu) == 0x41 ||
				(o->info.objectType & 0xFFu) == 0x42) {
				res = tee_ecc_adjust_max_obj_size(&attr_size);
				if (res != TEE_SUCCESS)
					return res;
			}

			if (attr_size >= attrs[n].content.ref.length * 8)
				res = ops->from_user(attr,
						attrs[n].content.ref.buffer,
						attrs[n].content.ref.length);
			else
				res = TEE_ERROR_EXCESS_DATA;
		}
		if (res != TEE_SUCCESS)
			return res;

		/*
		 * First attr_idx signifies the attribute that gives the size
		 * of the object
		 */
		if (type_props->type_attrs[idx].flags &
		    TEE_TYPE_ATTR_SIZE_INDICATOR)
			obj_size += attrs[n].content.ref.length * 8;
	}

	o->have_attrs = have_attrs;
	o->info.objectSize = obj_size;

	return TEE_SUCCESS;
}

TEE_Result __SYSCALL sys_utee_cryp_obj_populate(unsigned long obj,
			struct utee_attribute *usr_attrs,
			unsigned long attr_count)
{
	TEE_Result res;
	tee_api_info_t *ta_info = tee_current_ta_info();
	struct tee_obj *o;
	const struct tee_cryp_obj_type_props *type_props;
	TEE_Attribute *attrs = NULL;

	res = tee_obj_get(ta_info,
			  tee_svc_uref_to_vaddr(obj), &o);
	if (res != TEE_SUCCESS)
		return res;

	/* Must be a transient object */
	if ((o->info.handleFlags & TEE_HANDLE_FLAG_PERSISTENT) != 0)
		return TEE_ERROR_BAD_PARAMETERS;

	/* Must not be initialized already */
	if ((o->info.handleFlags & TEE_HANDLE_FLAG_INITIALIZED) != 0)
		return TEE_ERROR_BAD_PARAMETERS;

	type_props = tee_svc_find_type_props(o->info.objectType);
	if (!type_props)
		return TEE_ERROR_NOT_IMPLEMENTED;

	attrs = malloc(sizeof(TEE_Attribute) * attr_count);
	if (!attrs)
		return TEE_ERROR_OUT_OF_MEMORY;
	res = copy_in_attrs(uthread_get_current(), usr_attrs, attr_count,
			    attrs);
	if (res != TEE_SUCCESS)
		goto out;

	res = tee_svc_cryp_check_attr(ATTR_USAGE_POPULATE, type_props,
				      attrs, attr_count);
	if (res != TEE_SUCCESS)
		goto out;

	res = tee_svc_cryp_obj_populate_type(o, type_props, attrs, attr_count);
	if (res == TEE_SUCCESS)
		o->info.handleFlags |= TEE_HANDLE_FLAG_INITIALIZED;

out:
	free(attrs);
	return res;
}

TEE_Result __SYSCALL sys_utee_cryp_obj_copy(unsigned long dst,
			unsigned long src)
{
	TEE_Result res;
	tee_api_info_t *ta_info = tee_current_ta_info();
	struct tee_obj *dst_o;
	struct tee_obj *src_o;

	res = tee_obj_get(ta_info,
			  tee_svc_uref_to_vaddr(dst), &dst_o);
	if (res != TEE_SUCCESS)
		return res;

	res = tee_obj_get(ta_info,
			  tee_svc_uref_to_vaddr(src), &src_o);
	if (res != TEE_SUCCESS)
		return res;

	if ((src_o->info.handleFlags & TEE_HANDLE_FLAG_INITIALIZED) == 0)
		return TEE_ERROR_BAD_PARAMETERS;
	if ((dst_o->info.handleFlags & TEE_HANDLE_FLAG_PERSISTENT) != 0)
		return TEE_ERROR_BAD_PARAMETERS;
	if ((dst_o->info.handleFlags & TEE_HANDLE_FLAG_INITIALIZED) != 0)
		return TEE_ERROR_BAD_PARAMETERS;

	res = tee_obj_attr_copy_from(dst_o, src_o);
	if (res != TEE_SUCCESS)
		return res;

	dst_o->info.handleFlags |= TEE_HANDLE_FLAG_INITIALIZED;
	dst_o->info.objectSize = src_o->info.objectSize;
	dst_o->info.objectUsage = src_o->info.objectUsage;
	return TEE_SUCCESS;
}

static TEE_Result tee_svc_obj_generate_key_rsa(
	struct tee_obj *o, const struct tee_cryp_obj_type_props *type_props,
	uint32_t key_size,
	const TEE_Attribute *params, uint32_t param_count)
{
	TEE_Result res;
	struct rsa_keypair *key = o->attr;
	uint32_t e = TEE_U32_TO_BIG_ENDIAN(65537);

	if (!crypto_ops.acipher.gen_rsa_key || !crypto_ops.bignum.bin2bn)
		return TEE_ERROR_NOT_IMPLEMENTED;

	/* Copy the present attributes into the obj before starting */
	res = tee_svc_cryp_obj_populate_type(o, type_props, params,
					     param_count);
	if (res != TEE_SUCCESS)
		return res;
	if (!get_attribute(o, type_props, TEE_ATTR_RSA_PUBLIC_EXPONENT))
		crypto_ops.bignum.bin2bn((const uint8_t *)&e, sizeof(e),
					 key->e);
	res = crypto_ops.acipher.gen_rsa_key(key, key_size);
	if (res != TEE_SUCCESS)
		return res;

	/* Set bits for all known attributes for this object type */
	o->have_attrs = (1 << type_props->num_type_attrs) - 1;

	return TEE_SUCCESS;
}

static TEE_Result tee_svc_obj_generate_key_dsa(
	struct tee_obj *o, const struct tee_cryp_obj_type_props *type_props,
	uint32_t key_size)
{
	TEE_Result res;

	if (!crypto_ops.acipher.gen_dsa_key)
		return TEE_ERROR_NOT_IMPLEMENTED;
	res = crypto_ops.acipher.gen_dsa_key(o->attr, key_size);
	if (res != TEE_SUCCESS)
		return res;

	/* Set bits for all known attributes for this object type */
	o->have_attrs = (1 << type_props->num_type_attrs) - 1;

	return TEE_SUCCESS;
}

static TEE_Result tee_svc_obj_generate_key_dh(
	struct tee_obj *o, const struct tee_cryp_obj_type_props *type_props,
	uint32_t key_size __unused,
	const TEE_Attribute *params, uint32_t param_count)
{
	TEE_Result res;
	struct dh_keypair *tee_dh_key;
	struct bignum *dh_q = NULL;
	uint32_t dh_xbits = 0;

	/* Copy the present attributes into the obj before starting */
	res = tee_svc_cryp_obj_populate_type(o, type_props, params,
					     param_count);
	if (res != TEE_SUCCESS)
		return res;

	tee_dh_key = (struct dh_keypair *)o->attr;

	if (get_attribute(o, type_props, TEE_ATTR_DH_SUBPRIME))
		dh_q = tee_dh_key->q;
	if (get_attribute(o, type_props, TEE_ATTR_DH_X_BITS))
		dh_xbits = tee_dh_key->xbits;
	if (!crypto_ops.acipher.gen_dh_key)
		return TEE_ERROR_NOT_IMPLEMENTED;
	res = crypto_ops.acipher.gen_dh_key(tee_dh_key, dh_q, dh_xbits);
	if (res != TEE_SUCCESS)
		return res;

	/* Set bits for the generated public and private key */
	set_attribute(o, type_props, TEE_ATTR_DH_PUBLIC_VALUE);
	set_attribute(o, type_props, TEE_ATTR_DH_PRIVATE_VALUE);
	set_attribute(o, type_props, TEE_ATTR_DH_X_BITS);
	return TEE_SUCCESS;
}

static TEE_Result tee_svc_obj_generate_key_ecc(
	struct tee_obj *o, const struct tee_cryp_obj_type_props *type_props,
	uint32_t key_size __unused,
	const TEE_Attribute *params, uint32_t param_count)
{
	TEE_Result res;
	struct ecc_keypair *tee_ecc_key;

	/* Copy the present attributes into the obj before starting */
	res = tee_svc_cryp_obj_populate_type(o, type_props, params,
					     param_count);
	if (res != TEE_SUCCESS)
		return res;

	tee_ecc_key = (struct ecc_keypair *)o->attr;

	if (!crypto_ops.acipher.gen_ecc_key)
		return TEE_ERROR_NOT_IMPLEMENTED;
	res = crypto_ops.acipher.gen_ecc_key(tee_ecc_key);
	if (res != TEE_SUCCESS)
		return res;

	/* Set bits for the generated public and private key */
	set_attribute(o, type_props, TEE_ATTR_ECC_PRIVATE_VALUE);
	set_attribute(o, type_props, TEE_ATTR_ECC_PUBLIC_VALUE_X);
	set_attribute(o, type_props, TEE_ATTR_ECC_PUBLIC_VALUE_Y);
	set_attribute(o, type_props, TEE_ATTR_ECC_CURVE);
	return TEE_SUCCESS;
}

TEE_Result __SYSCALL sys_utee_cryp_obj_generate_key(unsigned long obj,
			unsigned long key_size,
			const struct utee_attribute *usr_params,
			unsigned long param_count)
{
	TEE_Result res;
	tee_api_info_t *ta_info = tee_current_ta_info();
	const struct tee_cryp_obj_type_props *type_props;
	struct tee_obj *o;
	struct tee_cryp_obj_secret *key;
	size_t byte_size;
	TEE_Attribute *params = NULL;

	res = tee_obj_get(ta_info,
			  tee_svc_uref_to_vaddr(obj), &o);
	if (res != TEE_SUCCESS)
		return TEE_ERROR_ITEM_NOT_FOUND;

	/* Must be a transient object */
	if ((o->info.handleFlags & TEE_HANDLE_FLAG_PERSISTENT) != 0)
		return TEE_ERROR_BAD_STATE;

	/* Must not be initialized already */
	if ((o->info.handleFlags & TEE_HANDLE_FLAG_INITIALIZED) != 0)
		return TEE_ERROR_BAD_STATE;

	/* Find description of object */
	type_props = tee_svc_find_type_props(o->info.objectType);
	if (!type_props)
		return TEE_ERROR_NOT_SUPPORTED;

	/* Check that maxKeySize follows restrictions */
	if (key_size % type_props->quanta != 0)
		return TEE_ERROR_NOT_SUPPORTED;
	if (key_size < type_props->min_size)
		return TEE_ERROR_NOT_SUPPORTED;
	if (key_size > type_props->max_size)
		return TEE_ERROR_NOT_SUPPORTED;

	params = malloc(sizeof(TEE_Attribute) * param_count);
	if (!params)
		return TEE_ERROR_OUT_OF_MEMORY;
	res = copy_in_attrs(uthread_get_current(), usr_params, param_count,
			    params);
	if (res != TEE_SUCCESS)
		goto out;

	res = tee_svc_cryp_check_attr(ATTR_USAGE_GENERATE_KEY, type_props,
				      params, param_count);
	if (res != TEE_SUCCESS)
		goto out;

	switch (o->info.objectType) {
	case TEE_TYPE_AES:
	case TEE_TYPE_DES:
	case TEE_TYPE_DES3:
	case TEE_TYPE_HMAC_MD5:
	case TEE_TYPE_HMAC_SHA1:
	case TEE_TYPE_HMAC_SHA224:
	case TEE_TYPE_HMAC_SHA256:
	case TEE_TYPE_HMAC_SHA384:
	case TEE_TYPE_HMAC_SHA512:
	case TEE_TYPE_GENERIC_SECRET:
		byte_size = key_size / 8;

		key = (struct tee_cryp_obj_secret *)o->attr;
		if (byte_size > key->alloc_size) {
			res = TEE_ERROR_EXCESS_DATA;
			goto out;
		}

		res = crypto_ops.prng.read((void *)(key + 1), byte_size);
		if (res != TEE_SUCCESS)
			goto out;

		key->key_size = byte_size;

		/* Set bits for all known attributes for this object type */
		o->have_attrs = (1 << type_props->num_type_attrs) - 1;

		break;

	case TEE_TYPE_RSA_KEYPAIR:
		res = tee_svc_obj_generate_key_rsa(o, type_props, key_size,
						   params, param_count);
		if (res != TEE_SUCCESS)
			goto out;
		break;

	case TEE_TYPE_DSA_KEYPAIR:
		res = tee_svc_obj_generate_key_dsa(o, type_props, key_size);
		if (res != TEE_SUCCESS)
			goto out;
		break;

	case TEE_TYPE_DH_KEYPAIR:
		res = tee_svc_obj_generate_key_dh(o, type_props, key_size,
						  params, param_count);
		if (res != TEE_SUCCESS)
			goto out;
		break;

	case TEE_TYPE_ECDSA_KEYPAIR:
	case TEE_TYPE_ECDH_KEYPAIR:
		res = tee_svc_obj_generate_key_ecc(o, type_props, key_size,
						  params, param_count);
		if (res != TEE_SUCCESS)
			goto out;
		break;

	default:
		res = TEE_ERROR_BAD_FORMAT;
	}

out:
	free(params);
	if (res == TEE_SUCCESS) {
		o->info.objectSize = key_size;
		o->info.handleFlags |= TEE_HANDLE_FLAG_INITIALIZED;
	}
	return res;
}

static TEE_Result tee_svc_cryp_get_state(tee_api_info_t *ta_info,
					 uint32_t state_id,
					 struct tee_cryp_state **state)
{
	struct tee_cryp_state *s;

	list_for_every_entry(&ta_info->cryp_states, s, struct tee_cryp_state,
			node) {
		if (state_id == (vaddr_t)s) {
			*state = s;
			return TEE_SUCCESS;
		}
	}
	return TEE_ERROR_BAD_PARAMETERS;
}

static void cryp_state_free(tee_api_info_t *ta_info, struct tee_cryp_state *cs)
{
	struct tee_obj *o;

	if (tee_obj_get(ta_info, cs->key1, &o) == TEE_SUCCESS)
		tee_obj_close(o);
	if (tee_obj_get(ta_info, cs->key2, &o) == TEE_SUCCESS)
		tee_obj_close(o);

	list_delete(&cs->node);
	if (cs->ctx_finalize != NULL)
		cs->ctx_finalize(cs->ctx, cs->algo);
	free(cs->ctx);
	free(cs);
}

static TEE_Result tee_svc_cryp_check_key_type(const struct tee_obj *o,
					      uint32_t algo,
					      TEE_OperationMode mode)
{
	uint32_t req_key_type;
	uint32_t req_key_type2 = 0;

	switch (TEE_ALG_GET_MAIN_ALG(algo)) {
	case TEE_MAIN_ALGO_MD5:
		req_key_type = TEE_TYPE_HMAC_MD5;
		break;
	case TEE_MAIN_ALGO_SHA1:
		req_key_type = TEE_TYPE_HMAC_SHA1;
		break;
	case TEE_MAIN_ALGO_SHA224:
		req_key_type = TEE_TYPE_HMAC_SHA224;
		break;
	case TEE_MAIN_ALGO_SHA256:
		req_key_type = TEE_TYPE_HMAC_SHA256;
		break;
	case TEE_MAIN_ALGO_SHA384:
		req_key_type = TEE_TYPE_HMAC_SHA384;
		break;
	case TEE_MAIN_ALGO_SHA512:
		req_key_type = TEE_TYPE_HMAC_SHA512;
		break;
	case TEE_MAIN_ALGO_AES:
		req_key_type = TEE_TYPE_AES;
		break;
	case TEE_MAIN_ALGO_DES:
		req_key_type = TEE_TYPE_DES;
		break;
	case TEE_MAIN_ALGO_DES3:
		req_key_type = TEE_TYPE_DES3;
		break;
	case TEE_MAIN_ALGO_RSA:
		req_key_type = TEE_TYPE_RSA_KEYPAIR;
		if (mode == TEE_MODE_ENCRYPT || mode == TEE_MODE_VERIFY)
			req_key_type2 = TEE_TYPE_RSA_PUBLIC_KEY;
		break;
	case TEE_MAIN_ALGO_DSA:
		req_key_type = TEE_TYPE_DSA_KEYPAIR;
		if (mode == TEE_MODE_ENCRYPT || mode == TEE_MODE_VERIFY)
			req_key_type2 = TEE_TYPE_DSA_PUBLIC_KEY;
		break;
	case TEE_MAIN_ALGO_DH:
		req_key_type = TEE_TYPE_DH_KEYPAIR;
		break;
	case TEE_MAIN_ALGO_ECC:
		if (TEE_ALG_KEY_TYPE_IS_ECDSA(algo)) {
			req_key_type = TEE_TYPE_ECDSA_KEYPAIR;
			if (mode == TEE_MODE_VERIFY)
				req_key_type2 = TEE_TYPE_ECDSA_PUBLIC_KEY;
		} else if (algo == TEE_ALG_ECDH_DERIVE_SHARED_SECRET)
			req_key_type = TEE_TYPE_ECDH_KEYPAIR;
		else
			return TEE_ERROR_BAD_PARAMETERS;
		break;
#if defined(CFG_CRYPTO_HKDF)
	case TEE_MAIN_ALGO_HKDF:
		req_key_type = TEE_TYPE_HKDF_IKM;
		break;
#endif
#if defined(CFG_CRYPTO_CONCAT_KDF)
	case TEE_MAIN_ALGO_CONCAT_KDF:
		req_key_type = TEE_TYPE_CONCAT_KDF_Z;
		break;
#endif
#if defined(CFG_CRYPTO_PBKDF2)
	case TEE_MAIN_ALGO_PBKDF2:
		req_key_type = TEE_TYPE_PBKDF2_PASSWORD;
		break;
#endif
	default:
		return TEE_ERROR_BAD_PARAMETERS;
	}

	if (req_key_type != o->info.objectType &&
	    req_key_type2 != o->info.objectType)
		return TEE_ERROR_BAD_PARAMETERS;
	return TEE_SUCCESS;
}

TEE_Result __SYSCALL sys_utee_cryp_state_alloc(unsigned long algo,
			unsigned long mode,
			unsigned long key1, unsigned long key2,
			uint32_t *state)
{
	TEE_Result res;
	struct tee_cryp_state *cs;
	tee_api_info_t *ta_info = tee_current_ta_info();
	struct tee_obj *o1 = NULL;
	struct tee_obj *o2 = NULL;

	if (key1 != 0) {
		res = tee_obj_get(ta_info, tee_svc_uref_to_vaddr(key1), &o1);
		if (res != TEE_SUCCESS)
			return res;
		if (o1->busy)
			return TEE_ERROR_BAD_PARAMETERS;
		res = tee_svc_cryp_check_key_type(o1, algo, mode);
		if (res != TEE_SUCCESS)
			return res;
	}
	if (key2 != 0) {
		res = tee_obj_get(ta_info, tee_svc_uref_to_vaddr(key2), &o2);
		if (res != TEE_SUCCESS)
			return res;
		if (o2->busy)
			return TEE_ERROR_BAD_PARAMETERS;
		res = tee_svc_cryp_check_key_type(o2, algo, mode);
		if (res != TEE_SUCCESS)
			return res;
	}

	cs = calloc(1, sizeof(struct tee_cryp_state));
	if (!cs)
		return TEE_ERROR_OUT_OF_MEMORY;
	list_add_tail(&ta_info->cryp_states, &cs->node);
	cs->algo = algo;
	cs->mode = mode;

	switch (TEE_ALG_GET_CLASS(algo)) {
	case TEE_OPERATION_CIPHER:
		if ((algo == TEE_ALG_AES_XTS && (key1 == 0 || key2 == 0)) ||
		    (algo != TEE_ALG_AES_XTS && (key1 == 0 || key2 != 0))) {
			res = TEE_ERROR_BAD_PARAMETERS;
		} else {
			if (crypto_ops.cipher.get_ctx_size)
				res = crypto_ops.cipher.get_ctx_size(algo,
								&cs->ctx_size);
			else
				res = TEE_ERROR_NOT_IMPLEMENTED;
			if (res != TEE_SUCCESS)
				break;
			cs->ctx = calloc(1, cs->ctx_size);
			if (!cs->ctx)
				res = TEE_ERROR_OUT_OF_MEMORY;
		}
		break;
	case TEE_OPERATION_AE:
		if (key1 == 0 || key2 != 0) {
			res = TEE_ERROR_BAD_PARAMETERS;
		} else {
			if (crypto_ops.authenc.get_ctx_size)
				res = crypto_ops.authenc.get_ctx_size(algo,
								&cs->ctx_size);
			else
				res = TEE_ERROR_NOT_IMPLEMENTED;
			if (res != TEE_SUCCESS)
				break;
			cs->ctx = calloc(1, cs->ctx_size);
			if (!cs->ctx)
				res = TEE_ERROR_OUT_OF_MEMORY;
		}
		break;
	case TEE_OPERATION_MAC:
		if (key1 == 0 || key2 != 0) {
			res = TEE_ERROR_BAD_PARAMETERS;
		} else {
			if (crypto_ops.mac.get_ctx_size)
				res = crypto_ops.mac.get_ctx_size(algo,
								&cs->ctx_size);
			else
				res = TEE_ERROR_NOT_IMPLEMENTED;
			if (res != TEE_SUCCESS)
				break;
			cs->ctx = calloc(1, cs->ctx_size);
			if (!cs->ctx)
				res = TEE_ERROR_OUT_OF_MEMORY;
		}
		break;
	case TEE_OPERATION_DIGEST:
		if (key1 != 0 || key2 != 0) {
			res = TEE_ERROR_BAD_PARAMETERS;
		} else {
			if (crypto_ops.hash.get_ctx_size)
				res = crypto_ops.hash.get_ctx_size(algo,
								&cs->ctx_size);
			else
				res = TEE_ERROR_NOT_IMPLEMENTED;
			if (res != TEE_SUCCESS)
				break;
			cs->ctx = calloc(1, cs->ctx_size);
			if (!cs->ctx)
				res = TEE_ERROR_OUT_OF_MEMORY;
		}
		break;
	case TEE_OPERATION_ASYMMETRIC_CIPHER:
	case TEE_OPERATION_ASYMMETRIC_SIGNATURE:
		if (key1 == 0 || key2 != 0)
			res = TEE_ERROR_BAD_PARAMETERS;
		break;
	case TEE_OPERATION_KEY_DERIVATION:
		if (key1 == 0 || key2 != 0)
			res = TEE_ERROR_BAD_PARAMETERS;
		break;
	default:
		res = TEE_ERROR_NOT_SUPPORTED;
		break;
	}
	if (res != TEE_SUCCESS)
		goto out;

	res = tee_svc_copy_kaddr_to_uref(state, cs);
	if (res != TEE_SUCCESS)
		goto out;

	/* Register keys */
	if (o1 != NULL) {
		o1->busy = true;
		cs->key1 = (vaddr_t)o1;
	}
	if (o2 != NULL) {
		o2->busy = true;
		cs->key2 = (vaddr_t)o2;
	}

out:
	if (res != TEE_SUCCESS)
		cryp_state_free(ta_info, cs);
	return res;
}

TEE_Result __SYSCALL sys_utee_cryp_state_copy(unsigned long dst,
			unsigned long src)
{
	TEE_Result res;
	struct tee_cryp_state *cs_dst;
	struct tee_cryp_state *cs_src;
	tee_api_info_t *ta_info = tee_current_ta_info();

	res = tee_svc_cryp_get_state(ta_info, tee_svc_uref_to_vaddr(dst),
			&cs_dst);
	if (res != TEE_SUCCESS)
		return res;

	res = tee_svc_cryp_get_state(ta_info, tee_svc_uref_to_vaddr(src),
			&cs_src);
	if (res != TEE_SUCCESS)
		return res;
	if (cs_dst->algo != cs_src->algo || cs_dst->mode != cs_src->mode)
		return TEE_ERROR_BAD_PARAMETERS;
	/* "Can't happen" */
	if (cs_dst->ctx_size != cs_src->ctx_size)
		return TEE_ERROR_BAD_STATE;

	memcpy(cs_dst->ctx, cs_src->ctx, cs_src->ctx_size);
	return TEE_SUCCESS;
}

void tee_svc_cryp_free_states(tee_api_info_t *ta_info)
{
	struct tee_cryp_state *cs, *next_cs;

	list_for_every_entry_safe(&ta_info->cryp_states, cs, next_cs,
			struct tee_cryp_state, node) {
		cryp_state_free(ta_info, cs);
	}
}

TEE_Result __SYSCALL sys_utee_cryp_state_free(unsigned long state)
{
	TEE_Result res;
	struct tee_cryp_state *cs;
	tee_api_info_t *ta_info = tee_current_ta_info();

	res = tee_svc_cryp_get_state(ta_info, tee_svc_uref_to_vaddr(state),
			&cs);
	if (res != TEE_SUCCESS)
		return res;
	cryp_state_free(ta_info, cs);
	return TEE_SUCCESS;
}

TEE_Result __SYSCALL sys_utee_hash_init(unsigned long state,
			     const void *iv __maybe_unused,
			     size_t iv_len __maybe_unused)
{
	TEE_Result res;
	struct tee_cryp_state *cs;
	tee_api_info_t *ta_info = tee_current_ta_info();

	res = tee_svc_cryp_get_state(ta_info, tee_svc_uref_to_vaddr(state),
			&cs);
	if (res != TEE_SUCCESS)
		return res;

	switch (TEE_ALG_GET_CLASS(cs->algo)) {
	case TEE_OPERATION_DIGEST:
		if (!crypto_ops.hash.init)
			return TEE_ERROR_NOT_IMPLEMENTED;
		res = crypto_ops.hash.init(cs->ctx, cs->algo);
		if (res != TEE_SUCCESS)
			return res;
		break;
	case TEE_OPERATION_MAC:
		{
			struct tee_obj *o;
			struct tee_cryp_obj_secret *key;

			res = tee_obj_get(ta_info,
					  cs->key1, &o);
			if (res != TEE_SUCCESS)
				return res;
			if ((o->info.handleFlags &
			     TEE_HANDLE_FLAG_INITIALIZED) == 0)
				return TEE_ERROR_BAD_PARAMETERS;

			key = (struct tee_cryp_obj_secret *)o->attr;
			if (!crypto_ops.mac.init)
				return TEE_ERROR_NOT_IMPLEMENTED;
			res = crypto_ops.mac.init(cs->ctx, cs->algo,
						  (void *)(key + 1),
						  key->key_size);
			if (res != TEE_SUCCESS)
				return res;
			break;
		}
	default:
		return TEE_ERROR_BAD_PARAMETERS;
	}

	return TEE_SUCCESS;
}

TEE_Result __SYSCALL sys_utee_hash_update(unsigned long state,
			const void *chunk,
			size_t chunk_size)
{
	TEE_Result res;
	struct tee_cryp_state *cs;
	tee_api_info_t *ta_info = tee_current_ta_info();

	/* No data, but size provided isn't valid parameters. */
	if (!chunk && chunk_size)
		return TEE_ERROR_BAD_PARAMETERS;

	/* Zero length hash is valid, but nothing we need to do. */
	if (!chunk_size)
		return TEE_SUCCESS;

	res = tee_mmu_check_access_rights(uthread_get_current(),
					  TEE_MEMORY_ACCESS_READ |
					  TEE_MEMORY_ACCESS_ANY_OWNER,
					  (uaddr_t)chunk, chunk_size);
	if (res != TEE_SUCCESS)
		return res;

	res = tee_svc_cryp_get_state(ta_info, tee_svc_uref_to_vaddr(state),
			&cs);
	if (res != TEE_SUCCESS)
		return res;

	switch (TEE_ALG_GET_CLASS(cs->algo)) {
	case TEE_OPERATION_DIGEST:
		if (!crypto_ops.hash.update)
			return TEE_ERROR_NOT_IMPLEMENTED;
		res = crypto_ops.hash.update(cs->ctx, cs->algo, chunk,
					     chunk_size);
		if (res != TEE_SUCCESS)
			return res;
		break;
	case TEE_OPERATION_MAC:
		if (!crypto_ops.mac.update)
			return TEE_ERROR_NOT_IMPLEMENTED;
		res = crypto_ops.mac.update(cs->ctx, cs->algo, chunk,
					    chunk_size);
		if (res != TEE_SUCCESS)
			return res;
		break;
	default:
		return TEE_ERROR_BAD_PARAMETERS;
	}

	return TEE_SUCCESS;
}

TEE_Result __SYSCALL sys_utee_hash_final(unsigned long state, const void *chunk,
			size_t chunk_size, void *hash, uint64_t *hash_len)
{
	TEE_Result res, res2;
	size_t hash_size;
	uint64_t hlen;
	struct tee_cryp_state *cs;
	tee_api_info_t *ta_info = tee_current_ta_info();
	uthread_t *ut = uthread_get_current();

	/* No data, but size provided isn't valid parameters. */
	if (!chunk && chunk_size)
		return TEE_ERROR_BAD_PARAMETERS;

	res = tee_mmu_check_access_rights(ut,
					  TEE_MEMORY_ACCESS_READ |
					  TEE_MEMORY_ACCESS_ANY_OWNER,
					  (uaddr_t)chunk, chunk_size);
	if (res != TEE_SUCCESS)
		return res;

	res = tee_svc_copy_from_user(&hlen, hash_len, sizeof(hlen));
	if (res != TEE_SUCCESS)
		return res;

	res = tee_mmu_check_access_rights(ut,
					  TEE_MEMORY_ACCESS_READ |
					  TEE_MEMORY_ACCESS_WRITE |
					  TEE_MEMORY_ACCESS_ANY_OWNER,
					  (uaddr_t)hash, hlen);
	if (res != TEE_SUCCESS)
		return res;

	res = tee_svc_cryp_get_state(ta_info, tee_svc_uref_to_vaddr(state),
			&cs);
	if (res != TEE_SUCCESS)
		return res;

	switch (TEE_ALG_GET_CLASS(cs->algo)) {
	case TEE_OPERATION_DIGEST:
		if (!crypto_ops.hash.update || !crypto_ops.hash.final)
			return TEE_ERROR_NOT_IMPLEMENTED;
		res = tee_hash_get_digest_size(cs->algo, &hash_size);
		if (res != TEE_SUCCESS)
			return res;
		if (*hash_len < hash_size) {
			res = TEE_ERROR_SHORT_BUFFER;
			goto out;
		}

		if (chunk_size) {
			res = crypto_ops.hash.update(cs->ctx, cs->algo, chunk,
						     chunk_size);
			if (res != TEE_SUCCESS)
				return res;
		}

		res = crypto_ops.hash.final(cs->ctx, cs->algo, hash,
					    hash_size);
		if (res != TEE_SUCCESS)
			return res;
		break;

	case TEE_OPERATION_MAC:
		if (!crypto_ops.mac.update || !crypto_ops.mac.final)
			return TEE_ERROR_NOT_IMPLEMENTED;
		res = tee_mac_get_digest_size(cs->algo, &hash_size);
		if (res != TEE_SUCCESS)
			return res;
		if (*hash_len < hash_size) {
			res = TEE_ERROR_SHORT_BUFFER;
			goto out;
		}

		if (chunk_size) {
			res = crypto_ops.mac.update(cs->ctx, cs->algo, chunk,
						    chunk_size);
			if (res != TEE_SUCCESS)
				return res;
		}

		res = crypto_ops.mac.final(cs->ctx, cs->algo, hash, hash_size);
		if (res != TEE_SUCCESS)
			return res;
		break;

	default:
		return TEE_ERROR_BAD_PARAMETERS;
	}
out:
	hlen = hash_size;
	res2 = tee_svc_copy_to_user(hash_len, &hlen, sizeof(*hash_len));
	if (res2 != TEE_SUCCESS)
		return res2;
	return res;
}

TEE_Result __SYSCALL sys_utee_cipher_init(unsigned long state, const void *iv,
			size_t iv_len)
{
	TEE_Result res;
	struct tee_cryp_state *cs;
	tee_api_info_t *ta_info = tee_current_ta_info();
	struct tee_obj *o;
	struct tee_cryp_obj_secret *key1;

	res = tee_svc_cryp_get_state(ta_info, tee_svc_uref_to_vaddr(state),
			&cs);
	if (res != TEE_SUCCESS)
		return res;

	res = tee_mmu_check_access_rights(uthread_get_current(),
					  TEE_MEMORY_ACCESS_READ |
					  TEE_MEMORY_ACCESS_ANY_OWNER,
					  (uaddr_t) iv, iv_len);
	if (res != TEE_SUCCESS)
		return res;

	res = tee_obj_get(ta_info, cs->key1, &o);
	if (res != TEE_SUCCESS)
		return res;
	if ((o->info.handleFlags & TEE_HANDLE_FLAG_INITIALIZED) == 0)
		return TEE_ERROR_BAD_PARAMETERS;

	key1 = o->attr;

	if (!crypto_ops.cipher.init)
		return TEE_ERROR_NOT_IMPLEMENTED;

	if (tee_obj_get(ta_info, cs->key2, &o) == TEE_SUCCESS) {
		struct tee_cryp_obj_secret *key2 = o->attr;

		if ((o->info.handleFlags & TEE_HANDLE_FLAG_INITIALIZED) == 0)
			return TEE_ERROR_BAD_PARAMETERS;

		res = crypto_ops.cipher.init(cs->ctx, cs->algo, cs->mode,
					     (uint8_t *)(key1 + 1),
					     key1->key_size,
					     (uint8_t *)(key2 + 1),
					     key2->key_size,
					     iv, iv_len);
	} else {
		res = crypto_ops.cipher.init(cs->ctx, cs->algo, cs->mode,
					     (uint8_t *)(key1 + 1),
					     key1->key_size,
					     NULL,
					     0,
					     iv, iv_len);
	}
	if (res != TEE_SUCCESS)
		return res;

	cs->ctx_finalize = crypto_ops.cipher.final;
	return TEE_SUCCESS;
}

static TEE_Result tee_svc_cipher_update_helper(unsigned long state,
			bool last_block, const void *src, size_t src_len,
			void *dst, uint64_t *dst_len)
{
	TEE_Result res;
	struct tee_cryp_state *cs;
	tee_api_info_t *ta_info = tee_current_ta_info();
	uint64_t dlen;
	uthread_t *ut = uthread_get_current();

	res = tee_svc_cryp_get_state(ta_info, tee_svc_uref_to_vaddr(state),
			&cs);
	if (res != TEE_SUCCESS)
		return res;

	res = tee_mmu_check_access_rights(ut,
					  TEE_MEMORY_ACCESS_READ |
					  TEE_MEMORY_ACCESS_ANY_OWNER,
					  (uaddr_t)src, src_len);
	if (res != TEE_SUCCESS)
		return res;

	if (!dst_len) {
		dlen = 0;
	} else {
		res = tee_svc_copy_from_user(&dlen, dst_len, sizeof(dlen));
		if (res != TEE_SUCCESS)
			return res;

		res = tee_mmu_check_access_rights(ut,
						  TEE_MEMORY_ACCESS_READ |
						  TEE_MEMORY_ACCESS_WRITE |
						  TEE_MEMORY_ACCESS_ANY_OWNER,
						  (uaddr_t)dst, dlen);
		if (res != TEE_SUCCESS)
			return res;
	}

	if (dlen < src_len) {
		res = TEE_ERROR_SHORT_BUFFER;
		goto out;
	}

	if (src_len > 0) {
		/* Permit src_len == 0 to finalize the operation */
		res = tee_do_cipher_update(cs->ctx, cs->algo, cs->mode,
					   last_block, src, src_len, dst);
	}

	if (last_block && cs->ctx_finalize != NULL) {
		cs->ctx_finalize(cs->ctx, cs->algo);
		cs->ctx_finalize = NULL;
	}

out:
	if ((res == TEE_SUCCESS || res == TEE_ERROR_SHORT_BUFFER) &&
	    dst_len != NULL) {
		TEE_Result res2;

		dlen = src_len;
		res2 = tee_svc_copy_to_user(dst_len, &dlen, sizeof(*dst_len));
		if (res2 != TEE_SUCCESS)
			res = res2;
	}

	return res;
}

TEE_Result __SYSCALL sys_utee_cipher_update(unsigned long state,
			const void *src,
			size_t src_len, void *dst, uint64_t *dst_len)
{
	return tee_svc_cipher_update_helper(state, false /* last_block */,
					    src, src_len, dst, dst_len);
}

TEE_Result __SYSCALL sys_utee_cipher_final(unsigned long state, const void *src,
			size_t src_len, void *dst, uint64_t *dst_len)
{
	return tee_svc_cipher_update_helper(state, true /* last_block */,
					    src, src_len, dst, dst_len);
}

#if defined(CFG_CRYPTO_HKDF)
static TEE_Result get_hkdf_params(const TEE_Attribute *params,
				  uint32_t param_count,
				  void **salt, size_t *salt_len, void **info,
				  size_t *info_len, size_t *okm_len)
{
	size_t n;
	enum { SALT = 0x1, LENGTH = 0x2, INFO = 0x4 };
	uint8_t found = 0;

	*salt = *info = NULL;
	*salt_len = *info_len = *okm_len = 0;

	for (n = 0; n < param_count; n++) {
		switch (params[n].attributeID) {
		case TEE_ATTR_HKDF_SALT:
			if (!(found & SALT)) {
				*salt = params[n].content.ref.buffer;
				*salt_len = params[n].content.ref.length;
				found |= SALT;
			}
			break;
		case TEE_ATTR_HKDF_OKM_LENGTH:
			if (!(found & LENGTH)) {
				*okm_len = params[n].content.value.a;
				found |= LENGTH;
			}
			break;
		case TEE_ATTR_HKDF_INFO:
			if (!(found & INFO)) {
				*info = params[n].content.ref.buffer;
				*info_len = params[n].content.ref.length;
				found |= INFO;
			}
			break;
		default:
			/* Unexpected attribute */
			return TEE_ERROR_BAD_PARAMETERS;
		}

	}

	if (!(found & LENGTH))
		return TEE_ERROR_BAD_PARAMETERS;

	return TEE_SUCCESS;
}
#endif

#if defined(CFG_CRYPTO_CONCAT_KDF)
static TEE_Result get_concat_kdf_params(const TEE_Attribute *params,
					uint32_t param_count,
					void **other_info,
					size_t *other_info_len,
					size_t *derived_key_len)
{
	size_t n;
	enum { LENGTH = 0x1, INFO = 0x2 };
	uint8_t found = 0;

	*other_info = NULL;
	*other_info_len = *derived_key_len = 0;

	for (n = 0; n < param_count; n++) {
		switch (params[n].attributeID) {
		case TEE_ATTR_CONCAT_KDF_OTHER_INFO:
			if (!(found & INFO)) {
				*other_info = params[n].content.ref.buffer;
				*other_info_len = params[n].content.ref.length;
				found |= INFO;
			}
			break;
		case TEE_ATTR_CONCAT_KDF_DKM_LENGTH:
			if (!(found & LENGTH)) {
				*derived_key_len = params[n].content.value.a;
				found |= LENGTH;
			}
			break;
		default:
			/* Unexpected attribute */
			return TEE_ERROR_BAD_PARAMETERS;
		}
	}

	if (!(found & LENGTH))
		return TEE_ERROR_BAD_PARAMETERS;

	return TEE_SUCCESS;
}
#endif

#if defined(CFG_CRYPTO_PBKDF2)
static TEE_Result get_pbkdf2_params(const TEE_Attribute *params,
				   uint32_t param_count, void **salt,
				   size_t *salt_len, size_t *derived_key_len,
				   size_t *iteration_count)
{
	size_t n;
	enum { SALT = 0x1, LENGTH = 0x2, COUNT = 0x4 };
	uint8_t found = 0;

	*salt = NULL;
	*salt_len = *derived_key_len = *iteration_count = 0;

	for (n = 0; n < param_count; n++) {
		switch (params[n].attributeID) {
		case TEE_ATTR_PBKDF2_SALT:
			if (!(found & SALT)) {
				*salt = params[n].content.ref.buffer;
				*salt_len = params[n].content.ref.length;
				found |= SALT;
			}
			break;
		case TEE_ATTR_PBKDF2_DKM_LENGTH:
			if (!(found & LENGTH)) {
				*derived_key_len = params[n].content.value.a;
				found |= LENGTH;
			}
			break;
		case TEE_ATTR_PBKDF2_ITERATION_COUNT:
			if (!(found & COUNT)) {
				*iteration_count = params[n].content.value.a;
				found |= COUNT;
			}
			break;
		default:
			/* Unexpected attribute */
			return TEE_ERROR_BAD_PARAMETERS;
		}
	}

	if ((found & (LENGTH|COUNT)) != (LENGTH|COUNT))
		return TEE_ERROR_BAD_PARAMETERS;

	return TEE_SUCCESS;
}
#endif

TEE_Result __SYSCALL sys_utee_cryp_derive_key(unsigned long state,
			const struct utee_attribute *usr_params,
			unsigned long param_count, unsigned long derived_key)
{
	TEE_Result res = TEE_ERROR_NOT_SUPPORTED;
	tee_api_info_t *ta_info = tee_current_ta_info();
	struct tee_obj *ko;
	struct tee_obj *so;
	struct tee_cryp_state *cs;
	struct tee_cryp_obj_secret *sk;
	const struct tee_cryp_obj_type_props *type_props;
	TEE_Attribute *params = NULL;

	res = tee_svc_cryp_get_state(ta_info, tee_svc_uref_to_vaddr(state),
			&cs);
	if (res != TEE_SUCCESS)
		return res;

	params = malloc(sizeof(TEE_Attribute) * param_count);
	if (!params)
		return TEE_ERROR_OUT_OF_MEMORY;
	res = copy_in_attrs(uthread_get_current(), usr_params, param_count,
			params);
	if (res != TEE_SUCCESS)
		goto out;

	/* Get key set in operation */
	res = tee_obj_get(ta_info, cs->key1, &ko);
	if (res != TEE_SUCCESS)
		goto out;

	res = tee_obj_get(ta_info, tee_svc_uref_to_vaddr(derived_key), &so);
	if (res != TEE_SUCCESS)
		goto out;

	/* Find information needed about the object to initialize */
	sk = so->attr;

	/* Find description of object */
	type_props = tee_svc_find_type_props(so->info.objectType);
	if (!type_props) {
		res = TEE_ERROR_NOT_SUPPORTED;
		goto out;
	}

	if (cs->algo == TEE_ALG_DH_DERIVE_SHARED_SECRET) {
		if (!crypto_ops.acipher.dh_shared_secret) {
			res = TEE_ERROR_NOT_IMPLEMENTED;
			goto out;
		}
		if (param_count != 1 ||
		    params[0].attributeID != TEE_ATTR_DH_PUBLIC_VALUE) {
			res = TEE_ERROR_BAD_PARAMETERS;
			goto out;
		}
		sk->key_size = sk->alloc_size;
		res = crypto_ops.acipher.dh_shared_secret(ko->attr,
						params[0].content.ref.buffer,
						params[0].content.ref.length,
						(unsigned char *)(sk + 1),
						&sk->key_size);
		if (res == TEE_SUCCESS) {
			so->info.handleFlags |=
					TEE_HANDLE_FLAG_INITIALIZED;
			set_attribute(so, type_props,
				      TEE_ATTR_SECRET_VALUE);
		}

	} else if (cs->algo == TEE_ALG_ECDH_DERIVE_SHARED_SECRET) {
		size_t alloc_size;
		struct ecc_public_key key_public;
		uint8_t *pt_secret;
		unsigned long pt_secret_len;

		if (!crypto_ops.bignum.bin2bn ||
		    !crypto_ops.acipher.alloc_ecc_public_key ||
		    !crypto_ops.acipher.free_ecc_public_key ||
		    !crypto_ops.acipher.ecc_shared_secret) {
			res = TEE_ERROR_NOT_IMPLEMENTED;
			goto out;
		}
		if (param_count != 2 ||
		    params[0].attributeID != TEE_ATTR_ECC_PUBLIC_VALUE_X ||
		    params[1].attributeID != TEE_ATTR_ECC_PUBLIC_VALUE_Y) {
			res = TEE_ERROR_BAD_PARAMETERS;
			goto out;
		}

		/* Create the public key */
		alloc_size = so->info.maxObjectSize;
		res = crypto_ops.acipher.alloc_ecc_public_key(&key_public,
							      alloc_size);
		if (res != TEE_SUCCESS)
			goto out;
		key_public.curve = ((struct ecc_keypair *)ko->attr)->curve;
		crypto_ops.bignum.bin2bn(params[0].content.ref.buffer,
					 params[0].content.ref.length,
					 key_public.x);
		crypto_ops.bignum.bin2bn(params[1].content.ref.buffer,
					 params[1].content.ref.length,
					 key_public.y);

		pt_secret = (uint8_t *)(sk + 1);
		pt_secret_len = sk->alloc_size;
		res = crypto_ops.acipher.ecc_shared_secret(ko->attr,
				&key_public, pt_secret, &pt_secret_len);

		if (res == TEE_SUCCESS) {
			sk->key_size = pt_secret_len;
			so->info.handleFlags |= TEE_HANDLE_FLAG_INITIALIZED;
			set_attribute(so, type_props, TEE_ATTR_SECRET_VALUE);
		}

		/* free the public key */
		crypto_ops.acipher.free_ecc_public_key(&key_public);
	}
#if defined(CFG_CRYPTO_HKDF)
	else if (TEE_ALG_GET_MAIN_ALG(cs->algo) == TEE_MAIN_ALGO_HKDF) {
		void *salt, *info;
		size_t salt_len, info_len, okm_len;
		uint32_t hash_id = TEE_ALG_GET_DIGEST_HASH(cs->algo);
		struct tee_cryp_obj_secret *ik = ko->attr;
		const uint8_t *ikm = (const uint8_t *)(ik + 1);

		res = get_hkdf_params(params, param_count, &salt, &salt_len,
				      &info, &info_len, &okm_len);
		if (res != TEE_SUCCESS)
			goto out;

		/* Requested size must fit into the output object's buffer */
		if (okm_len > ik->alloc_size) {
			res = TEE_ERROR_BAD_PARAMETERS;
			goto out;
		}

		res = tee_cryp_hkdf(hash_id, ikm, ik->key_size, salt, salt_len,
				    info, info_len, (uint8_t *)(sk + 1),
				    okm_len);
		if (res == TEE_SUCCESS) {
			sk->key_size = okm_len;
			so->info.handleFlags |= TEE_HANDLE_FLAG_INITIALIZED;
			set_attribute(so, type_props, TEE_ATTR_SECRET_VALUE);
		}
	}
#endif
#if defined(CFG_CRYPTO_CONCAT_KDF)
	else if (TEE_ALG_GET_MAIN_ALG(cs->algo) == TEE_MAIN_ALGO_CONCAT_KDF) {
		void *info;
		size_t info_len, derived_key_len;
		uint32_t hash_id = TEE_ALG_GET_DIGEST_HASH(cs->algo);
		struct tee_cryp_obj_secret *ss = ko->attr;
		const uint8_t *shared_secret = (const uint8_t *)(ss + 1);

		res = get_concat_kdf_params(params, param_count, &info,
					    &info_len, &derived_key_len);
		if (res != TEE_SUCCESS)
			goto out;

		/* Requested size must fit into the output object's buffer */
		if (derived_key_len > ss->alloc_size) {
			res = TEE_ERROR_BAD_PARAMETERS;
			goto out;
		}

		res = tee_cryp_concat_kdf(hash_id, shared_secret, ss->key_size,
					  info, info_len, (uint8_t *)(sk + 1),
					  derived_key_len);
		if (res == TEE_SUCCESS) {
			sk->key_size = derived_key_len;
			so->info.handleFlags |= TEE_HANDLE_FLAG_INITIALIZED;
			set_attribute(so, type_props, TEE_ATTR_SECRET_VALUE);
		}
	}
#endif
#if defined(CFG_CRYPTO_PBKDF2)
	else if (TEE_ALG_GET_MAIN_ALG(cs->algo) == TEE_MAIN_ALGO_PBKDF2) {
		void *salt;
		size_t salt_len, iteration_count, derived_key_len;
		uint32_t hash_id = TEE_ALG_GET_DIGEST_HASH(cs->algo);
		struct tee_cryp_obj_secret *ss = ko->attr;
		const uint8_t *password = (const uint8_t *)(ss + 1);

		res = get_pbkdf2_params(params, param_count, &salt, &salt_len,
					&derived_key_len, &iteration_count);
		if (res != TEE_SUCCESS)
			goto out;

		/* Requested size must fit into the output object's buffer */
		if (derived_key_len > ss->alloc_size) {
			res = TEE_ERROR_BAD_PARAMETERS;
			goto out;
		}

		res = tee_cryp_pbkdf2(hash_id, password, ss->key_size, salt,
				      salt_len, iteration_count,
				      (uint8_t *)(sk + 1), derived_key_len);
		if (res == TEE_SUCCESS) {
			sk->key_size = derived_key_len;
			so->info.handleFlags |= TEE_HANDLE_FLAG_INITIALIZED;
			set_attribute(so, type_props, TEE_ATTR_SECRET_VALUE);
		}
	}
#endif
	else
		res = TEE_ERROR_NOT_SUPPORTED;

out:
	free(params);
	return res;
}

TEE_Result __SYSCALL sys_utee_cryp_random_number_generate(void *buf,
			size_t blen)
{
	TEE_Result res;

	res = tee_mmu_check_access_rights(uthread_get_current(),
					  TEE_MEMORY_ACCESS_WRITE |
					  TEE_MEMORY_ACCESS_ANY_OWNER,
					  (uaddr_t)buf, blen);
	if (res != TEE_SUCCESS)
		return res;

	res = crypto_ops.prng.read(buf, blen);
	if (res != TEE_SUCCESS)
		return res;

	return res;
}

TEE_Result __SYSCALL sys_utee_authenc_init(unsigned long state,
			const void *nonce,
			size_t nonce_len, size_t tag_len,
			size_t aad_len, size_t payload_len)
{
	TEE_Result res;
	struct tee_cryp_state *cs;
	tee_api_info_t *ta_info = tee_current_ta_info();
	struct tee_obj *o;
	struct tee_cryp_obj_secret *key;

	res = tee_svc_cryp_get_state(ta_info, tee_svc_uref_to_vaddr(state),
			&cs);
	if (res != TEE_SUCCESS)
		return res;

	res = tee_obj_get(ta_info, cs->key1, &o);
	if (res != TEE_SUCCESS)
		return res;
	if ((o->info.handleFlags & TEE_HANDLE_FLAG_INITIALIZED) == 0)
		return TEE_ERROR_BAD_PARAMETERS;

	if (!crypto_ops.authenc.init)
		return TEE_ERROR_NOT_IMPLEMENTED;
	key = o->attr;
	res = crypto_ops.authenc.init(cs->ctx, cs->algo, cs->mode,
				      (uint8_t *)(key + 1), key->key_size,
				      nonce, nonce_len, tag_len, aad_len,
				      payload_len);
	if (res != TEE_SUCCESS)
		return res;

	cs->ctx_finalize = (tee_cryp_ctx_finalize_func_t)
				crypto_ops.authenc.final;
	return TEE_SUCCESS;
}

TEE_Result __SYSCALL sys_utee_authenc_update_aad(unsigned long state,
			const void *aad_data, size_t aad_data_len)
{
	TEE_Result res;
	struct tee_cryp_state *cs;
	tee_api_info_t *ta_info = tee_current_ta_info();

	res = tee_mmu_check_access_rights(uthread_get_current(),
					  TEE_MEMORY_ACCESS_READ |
					  TEE_MEMORY_ACCESS_ANY_OWNER,
					  (uaddr_t) aad_data,
					  aad_data_len);
	if (res != TEE_SUCCESS)
		return res;

	res = tee_svc_cryp_get_state(ta_info, tee_svc_uref_to_vaddr(state),
			&cs);
	if (res != TEE_SUCCESS)
		return res;

	if (!crypto_ops.authenc.update_aad)
		return TEE_ERROR_NOT_IMPLEMENTED;
	res = crypto_ops.authenc.update_aad(cs->ctx, cs->algo, cs->mode,
					    aad_data, aad_data_len);
	if (res != TEE_SUCCESS)
		return res;

	return TEE_SUCCESS;
}

TEE_Result __SYSCALL sys_utee_authenc_update_payload(unsigned long state,
			const void *src_data, size_t src_len, void *dst_data,
			uint64_t *dst_len)
{
	TEE_Result res;
	struct tee_cryp_state *cs;
	tee_api_info_t *ta_info = tee_current_ta_info();
	uint64_t dlen;
	size_t tmp_dlen;
	uthread_t *ut = uthread_get_current();

	res = tee_svc_cryp_get_state(ta_info, tee_svc_uref_to_vaddr(state),
			&cs);
	if (res != TEE_SUCCESS)
		return res;

	res = tee_mmu_check_access_rights(ut,
					  TEE_MEMORY_ACCESS_READ |
					  TEE_MEMORY_ACCESS_ANY_OWNER,
					  (uaddr_t) src_data, src_len);
	if (res != TEE_SUCCESS)
		return res;

	res = tee_svc_copy_from_user(&dlen, dst_len, sizeof(dlen));
	if (res != TEE_SUCCESS)
		return res;

	res = tee_mmu_check_access_rights(ut,
					  TEE_MEMORY_ACCESS_READ |
					  TEE_MEMORY_ACCESS_WRITE |
					  TEE_MEMORY_ACCESS_ANY_OWNER,
					  (uaddr_t)dst_data, dlen);
	if (res != TEE_SUCCESS)
		return res;

	if (dlen < src_len) {
		res = TEE_ERROR_SHORT_BUFFER;
		goto out;
	}

	if (!crypto_ops.authenc.update_payload)
		return TEE_ERROR_NOT_IMPLEMENTED;
	tmp_dlen = dlen;
	res = crypto_ops.authenc.update_payload(cs->ctx, cs->algo, cs->mode,
						src_data, src_len, dst_data,
						&tmp_dlen);
	dlen = tmp_dlen;

out:
	if (res == TEE_SUCCESS || res == TEE_ERROR_SHORT_BUFFER) {
		TEE_Result res2 = tee_svc_copy_to_user(dst_len, &dlen,
						       sizeof(*dst_len));
		if (res2 != TEE_SUCCESS)
			res = res2;
	}

	return res;
}

TEE_Result __SYSCALL sys_utee_authenc_enc_final(unsigned long state,
			const void *src_data, size_t src_len, void *dst_data,
			uint64_t *dst_len, void *tag, uint64_t *tag_len)
{
	TEE_Result res;
	struct tee_cryp_state *cs;
	tee_api_info_t *ta_info = tee_current_ta_info();
	uint64_t dlen;
	uint64_t tlen = 0;
	size_t tmp_dlen;
	size_t tmp_tlen;
	uthread_t *ut = uthread_get_current();

	res = tee_svc_cryp_get_state(ta_info, tee_svc_uref_to_vaddr(state),
			&cs);
	if (res != TEE_SUCCESS)
		return res;

	if (cs->mode != TEE_MODE_ENCRYPT)
		return TEE_ERROR_BAD_PARAMETERS;

	res = tee_mmu_check_access_rights(ut,
					  TEE_MEMORY_ACCESS_READ |
					  TEE_MEMORY_ACCESS_ANY_OWNER,
					  (uaddr_t)src_data, src_len);
	if (res != TEE_SUCCESS)
		return res;

	if (!dst_len) {
		dlen = 0;
	} else {
		res = tee_svc_copy_from_user(&dlen, dst_len, sizeof(dlen));
		if (res != TEE_SUCCESS)
			return res;

		res = tee_mmu_check_access_rights(ut,
						  TEE_MEMORY_ACCESS_READ |
						  TEE_MEMORY_ACCESS_WRITE |
						  TEE_MEMORY_ACCESS_ANY_OWNER,
						  (uaddr_t)dst_data, dlen);
		if (res != TEE_SUCCESS)
			return res;
	}

	if (dlen < src_len) {
		res = TEE_ERROR_SHORT_BUFFER;
		goto out;
	}

	res = tee_svc_copy_from_user(&tlen, tag_len, sizeof(tlen));
	if (res != TEE_SUCCESS)
		return res;

	res = tee_mmu_check_access_rights(ut,
					  TEE_MEMORY_ACCESS_READ |
					  TEE_MEMORY_ACCESS_WRITE |
					  TEE_MEMORY_ACCESS_ANY_OWNER,
					  (uaddr_t)tag, tlen);
	if (res != TEE_SUCCESS)
		return res;

	if (!crypto_ops.authenc.enc_final)
		return TEE_ERROR_NOT_IMPLEMENTED;
	tmp_dlen = dlen;
	tmp_tlen = tlen;
	res = crypto_ops.authenc.enc_final(cs->ctx, cs->algo, src_data,
					   src_len, dst_data, &tmp_dlen, tag,
					   &tmp_tlen);
	dlen = tmp_dlen;
	tlen = tmp_tlen;

out:
	if (res == TEE_SUCCESS || res == TEE_ERROR_SHORT_BUFFER) {
		TEE_Result res2;

		if (dst_len != NULL) {
			res2 = tee_svc_copy_to_user(dst_len, &dlen,
						    sizeof(*dst_len));
			if (res2 != TEE_SUCCESS)
				return res2;
		}

		res2 = tee_svc_copy_to_user(tag_len, &tlen, sizeof(*tag_len));
		if (res2 != TEE_SUCCESS)
			return res2;
	}

	return res;
}

TEE_Result __SYSCALL sys_utee_authenc_dec_final(unsigned long state,
			const void *src_data, size_t src_len, void *dst_data,
			uint64_t *dst_len, const void *tag, size_t tag_len)
{
	TEE_Result res;
	struct tee_cryp_state *cs;
	tee_api_info_t *ta_info = tee_current_ta_info();
	uint64_t dlen;
	size_t tmp_dlen;
	uthread_t *ut = uthread_get_current();

	res = tee_svc_cryp_get_state(ta_info, tee_svc_uref_to_vaddr(state),
			&cs);
	if (res != TEE_SUCCESS)
		return res;

	if (cs->mode != TEE_MODE_DECRYPT)
		return TEE_ERROR_BAD_PARAMETERS;

	res = tee_mmu_check_access_rights(ut,
					  TEE_MEMORY_ACCESS_READ |
					  TEE_MEMORY_ACCESS_ANY_OWNER,
					  (uaddr_t)src_data, src_len);
	if (res != TEE_SUCCESS)
		return res;

	if (!dst_len) {
		dlen = 0;
	} else {
		res = tee_svc_copy_from_user(&dlen, dst_len, sizeof(dlen));
		if (res != TEE_SUCCESS)
			return res;

		res = tee_mmu_check_access_rights(ut,
						  TEE_MEMORY_ACCESS_READ |
						  TEE_MEMORY_ACCESS_WRITE |
						  TEE_MEMORY_ACCESS_ANY_OWNER,
						  (uaddr_t)dst_data, dlen);
		if (res != TEE_SUCCESS)
			return res;
	}

	if (dlen < src_len) {
		res = TEE_ERROR_SHORT_BUFFER;
		goto out;
	}

	res = tee_mmu_check_access_rights(ut,
					  TEE_MEMORY_ACCESS_READ |
					  TEE_MEMORY_ACCESS_ANY_OWNER,
					  (uaddr_t)tag, tag_len);
	if (res != TEE_SUCCESS)
		return res;

	if (!crypto_ops.authenc.dec_final)
		return TEE_ERROR_NOT_IMPLEMENTED;
	tmp_dlen = dlen;
	res = crypto_ops.authenc.dec_final(cs->ctx, cs->algo, src_data,
					   src_len, dst_data, &tmp_dlen, tag,
					   tag_len);
	dlen = tmp_dlen;

out:
	if ((res == TEE_SUCCESS || res == TEE_ERROR_SHORT_BUFFER) &&
	    dst_len != NULL) {
		TEE_Result res2;

		res2 = tee_svc_copy_to_user(dst_len, &dlen, sizeof(*dst_len));
		if (res2 != TEE_SUCCESS)
			return res2;
	}

	return res;
}

static int pkcs1_get_salt_len(const TEE_Attribute *params, uint32_t num_params,
			      size_t default_len)
{
	size_t n;

	assert(default_len < INT_MAX);

	for (n = 0; n < num_params; n++) {
		if (params[n].attributeID == TEE_ATTR_RSA_PSS_SALT_LENGTH) {
			if (params[n].content.value.a < INT_MAX)
				return params[n].content.value.a;
			break;
		}
	}
	/*
	 * If salt length isn't provided use the default value which is
	 * the length of the digest.
	 */
	return default_len;
}

TEE_Result __SYSCALL sys_utee_asymm_operate(unsigned long state,
			const struct utee_attribute *usr_params,
			size_t num_params, const void *src_data, size_t src_len,
			void *dst_data, uint64_t *dst_len)
{
	TEE_Result res;
	struct tee_cryp_state *cs;
	tee_api_info_t *ta_info = tee_current_ta_info();
	uint64_t dlen64;
	size_t dlen;
	struct tee_obj *o;
	void *label = NULL;
	size_t label_len = 0;
	size_t n;
	int salt_len;
	TEE_Attribute *params = NULL;
	uthread_t *ut = uthread_get_current();

	res = tee_svc_cryp_get_state(ta_info, tee_svc_uref_to_vaddr(state),
			&cs);
	if (res != TEE_SUCCESS)
		return res;

	res = tee_mmu_check_access_rights(ut,
		TEE_MEMORY_ACCESS_READ | TEE_MEMORY_ACCESS_ANY_OWNER,
		(uaddr_t) src_data, src_len);
	if (res != TEE_SUCCESS)
		return res;

	res = tee_svc_copy_from_user(&dlen64, dst_len, sizeof(dlen64));
	if (res != TEE_SUCCESS)
		return res;
	dlen = dlen64;

	res = tee_mmu_check_access_rights(ut,
		TEE_MEMORY_ACCESS_READ | TEE_MEMORY_ACCESS_WRITE |
			TEE_MEMORY_ACCESS_ANY_OWNER,
		(uaddr_t) dst_data, dlen);
	if (res != TEE_SUCCESS)
		return res;

	params = malloc(sizeof(TEE_Attribute) * num_params);
	if (!params)
		return TEE_ERROR_OUT_OF_MEMORY;
	res = copy_in_attrs(ut, usr_params, num_params, params);
	if (res != TEE_SUCCESS)
		goto out;

	res = tee_obj_get(ta_info, cs->key1, &o);
	if (res != TEE_SUCCESS)
		goto out;
	if ((o->info.handleFlags & TEE_HANDLE_FLAG_INITIALIZED) == 0) {
		res = TEE_ERROR_GENERIC;
		goto out;
	}

	switch (cs->algo) {
	case TEE_ALG_RSA_NOPAD:
		if (cs->mode == TEE_MODE_ENCRYPT) {
			if (crypto_ops.acipher.rsanopad_encrypt)
				res = crypto_ops.acipher.rsanopad_encrypt(
					o->attr, src_data, src_len,
					dst_data, &dlen);
			else
				res = TEE_ERROR_NOT_IMPLEMENTED;
		} else if (cs->mode == TEE_MODE_DECRYPT) {
			if (crypto_ops.acipher.rsanopad_decrypt)
				res = crypto_ops.acipher.rsanopad_decrypt(
					o->attr, src_data, src_len, dst_data,
					&dlen);
			else
				res = TEE_ERROR_NOT_IMPLEMENTED;
		} else {
			/*
			 * We will panic because "the mode is not compatible
			 * with the function"
			 */
			res = TEE_ERROR_GENERIC;
		}
		break;

	case TEE_ALG_RSAES_PKCS1_V1_5:
	case TEE_ALG_RSAES_PKCS1_OAEP_MGF1_SHA1:
	case TEE_ALG_RSAES_PKCS1_OAEP_MGF1_SHA224:
	case TEE_ALG_RSAES_PKCS1_OAEP_MGF1_SHA256:
	case TEE_ALG_RSAES_PKCS1_OAEP_MGF1_SHA384:
	case TEE_ALG_RSAES_PKCS1_OAEP_MGF1_SHA512:
		for (n = 0; n < num_params; n++) {
			if (params[n].attributeID == TEE_ATTR_RSA_OAEP_LABEL) {
				label = params[n].content.ref.buffer;
				label_len = params[n].content.ref.length;
				break;
			}
		}

		if (cs->mode == TEE_MODE_ENCRYPT) {
			if (crypto_ops.acipher.rsaes_encrypt)
				res = crypto_ops.acipher.rsaes_encrypt(
					cs->algo, o->attr, label, label_len,
					src_data, src_len, dst_data, &dlen);
			else
				res = TEE_ERROR_NOT_IMPLEMENTED;
		} else if (cs->mode == TEE_MODE_DECRYPT) {
			if (crypto_ops.acipher.rsaes_decrypt)
				res = crypto_ops.acipher.rsaes_decrypt(
					cs->algo, o->attr,
					label, label_len,
					src_data, src_len, dst_data, &dlen);
			else
				res = TEE_ERROR_NOT_IMPLEMENTED;
		} else {
			res = TEE_ERROR_BAD_PARAMETERS;
		}
		break;

	case TEE_ALG_RSASSA_PKCS1_V1_5_MD5:
	case TEE_ALG_RSASSA_PKCS1_V1_5_SHA1:
	case TEE_ALG_RSASSA_PKCS1_V1_5_SHA224:
	case TEE_ALG_RSASSA_PKCS1_V1_5_SHA256:
	case TEE_ALG_RSASSA_PKCS1_V1_5_SHA384:
	case TEE_ALG_RSASSA_PKCS1_V1_5_SHA512:
	case TEE_ALG_RSASSA_PKCS1_PSS_MGF1_SHA1:
	case TEE_ALG_RSASSA_PKCS1_PSS_MGF1_SHA224:
	case TEE_ALG_RSASSA_PKCS1_PSS_MGF1_SHA256:
	case TEE_ALG_RSASSA_PKCS1_PSS_MGF1_SHA384:
	case TEE_ALG_RSASSA_PKCS1_PSS_MGF1_SHA512:
		if (cs->mode != TEE_MODE_SIGN) {
			res = TEE_ERROR_BAD_PARAMETERS;
			break;
		}
		salt_len = pkcs1_get_salt_len(params, num_params, src_len);
		if (!crypto_ops.acipher.rsassa_sign) {
			res = TEE_ERROR_NOT_IMPLEMENTED;
			break;
		}
		res = crypto_ops.acipher.rsassa_sign(cs->algo, o->attr,
						     salt_len, src_data,
						     src_len, dst_data, &dlen);
		break;

	case TEE_ALG_DSA_SHA1:
	case TEE_ALG_DSA_SHA224:
	case TEE_ALG_DSA_SHA256:
		if (!crypto_ops.acipher.dsa_sign) {
			res = TEE_ERROR_NOT_IMPLEMENTED;
			break;
		}
		res = crypto_ops.acipher.dsa_sign(cs->algo, o->attr, src_data,
						  src_len, dst_data, &dlen);
		break;
	// case TEE_ALG_ECDSA_P192: /* deprecated */
	// case TEE_ALG_ECDSA_P224: /* deprecated */
	// case TEE_ALG_ECDSA_P256: /* deprecated */
	// case TEE_ALG_ECDSA_P384: /* deprecated */
	// case TEE_ALG_ECDSA_P521: /* deprecated */
	case TEE_ALG_ECDSA_SHA1:
	case TEE_ALG_ECDSA_SHA224:
	case TEE_ALG_ECDSA_SHA256:
	case TEE_ALG_ECDSA_SHA384:
	case TEE_ALG_ECDSA_SHA512:
		if (!crypto_ops.acipher.ecc_sign) {
			res = TEE_ERROR_NOT_IMPLEMENTED;
			break;
		}
		res = crypto_ops.acipher.ecc_sign(cs->algo, o->attr, src_data,
						  src_len, dst_data, &dlen);
		break;

	default:
		res = TEE_ERROR_BAD_PARAMETERS;
		break;
	}

out:
	free(params);

	if (res == TEE_SUCCESS || res == TEE_ERROR_SHORT_BUFFER) {
		TEE_Result res2;

		dlen64 = dlen;
		res2 = tee_svc_copy_to_user(dst_len, &dlen64, sizeof(*dst_len));
		if (res2 != TEE_SUCCESS)
			return res2;
	}

	return res;
}

TEE_Result __SYSCALL sys_utee_asymm_verify(unsigned long state,
			const struct utee_attribute *usr_params,
			size_t num_params, const void *data, size_t data_len,
			const void *sig, size_t sig_len)
{
	TEE_Result res;
	struct tee_cryp_state *cs;
	tee_api_info_t *ta_info = tee_current_ta_info();
	struct tee_obj *o;
	size_t hash_size;
	int salt_len;
	TEE_Attribute *params = NULL;
	uint32_t hash_algo;
	uthread_t *ut = uthread_get_current();

	res = tee_svc_cryp_get_state(ta_info, tee_svc_uref_to_vaddr(state),
			&cs);
	if (res != TEE_SUCCESS)
		return res;

	if (cs->mode != TEE_MODE_VERIFY)
		return TEE_ERROR_BAD_PARAMETERS;

	res = tee_mmu_check_access_rights(ut,
					  TEE_MEMORY_ACCESS_READ |
					  TEE_MEMORY_ACCESS_ANY_OWNER,
					  (uaddr_t)data, data_len);
	if (res != TEE_SUCCESS)
		return res;

	res = tee_mmu_check_access_rights(ut,
					  TEE_MEMORY_ACCESS_READ |
					  TEE_MEMORY_ACCESS_ANY_OWNER,
					  (uaddr_t)sig, sig_len);
	if (res != TEE_SUCCESS)
		return res;

	params = malloc(sizeof(TEE_Attribute) * num_params);
	if (!params)
		return TEE_ERROR_OUT_OF_MEMORY;
	res = copy_in_attrs(ut, usr_params, num_params, params);
	if (res != TEE_SUCCESS)
		goto out;

	res = tee_obj_get(ta_info, cs->key1, &o);
	if (res != TEE_SUCCESS)
		goto out;
	if ((o->info.handleFlags & TEE_HANDLE_FLAG_INITIALIZED) == 0) {
		res = TEE_ERROR_BAD_PARAMETERS;
		goto out;
	}

	switch (TEE_ALG_GET_MAIN_ALG(cs->algo)) {
	case TEE_MAIN_ALGO_RSA:
		hash_algo = TEE_DIGEST_HASH_TO_ALGO(cs->algo);
		res = tee_hash_get_digest_size(hash_algo, &hash_size);
		if (res != TEE_SUCCESS)
			break;
		if (data_len != hash_size) {
			res = TEE_ERROR_BAD_PARAMETERS;
			break;
		}
		salt_len = pkcs1_get_salt_len(params, num_params, hash_size);
		if (!crypto_ops.acipher.rsassa_verify) {
			res = TEE_ERROR_NOT_IMPLEMENTED;
			break;
		}
		res = crypto_ops.acipher.rsassa_verify(cs->algo, o->attr,
						       salt_len, data,
						       data_len, sig, sig_len);
		break;

	case TEE_MAIN_ALGO_DSA:
		hash_algo = TEE_DIGEST_HASH_TO_ALGO(cs->algo);
		res = tee_hash_get_digest_size(hash_algo, &hash_size);
		if (res != TEE_SUCCESS)
			break;
		/*
		 * Depending on the DSA algorithm (NIST), the digital signature
		 * output size may be truncated to the size of a key pair
		 * (Q prime size). Q prime size must be less or equal than the
		 * hash output length of the hash algorithm involved.
		 */
		if (data_len > hash_size) {
			res = TEE_ERROR_BAD_PARAMETERS;
			break;
		}
		/* GP Spec v1.1.2 for TEE_AsymmetricVerifyDigest specifies as a
		 * panic reason: digestLen is not equal to the hash size of the
		 * algorithm
		 */
		if (data_len != hash_size) {
			res = TEE_ERROR_BAD_PARAMETERS;
			break;
		}
		if (!crypto_ops.acipher.dsa_verify) {
			res = TEE_ERROR_NOT_IMPLEMENTED;
			break;
		}
		res = crypto_ops.acipher.dsa_verify(cs->algo, o->attr, data,
						    data_len, sig, sig_len);
		break;

	case TEE_MAIN_ALGO_ECC:
		if (cs->algo == TEE_ALG_ECDH_DERIVE_SHARED_SECRET) {
			res = TEE_ERROR_BAD_PARAMETERS;
			break;
		}
		if (!crypto_ops.acipher.ecc_verify) {
			res = TEE_ERROR_NOT_IMPLEMENTED;
			break;
		}
		res = crypto_ops.acipher.ecc_verify(cs->algo, o->attr, data,
						    data_len, sig, sig_len);
		break;

	default:
		res = TEE_ERROR_NOT_SUPPORTED;
	}

out:
	free(params);
	return res;
}