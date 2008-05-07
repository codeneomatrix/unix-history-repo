/*
 * Copyright (c) 2004 - 2007 Kungliga Tekniska H�gskolan
 * (Royal Institute of Technology, Stockholm, Sweden). 
 * All rights reserved. 
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions 
 * are met: 
 *
 * 1. Redistributions of source code must retain the above copyright 
 *    notice, this list of conditions and the following disclaimer. 
 *
 * 2. Redistributions in binary form must reproduce the above copyright 
 *    notice, this list of conditions and the following disclaimer in the 
 *    documentation and/or other materials provided with the distribution. 
 *
 * 3. Neither the name of the Institute nor the names of its contributors 
 *    may be used to endorse or promote products derived from this software 
 *    without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE 
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL 
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS 
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) 
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY 
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF 
 * SUCH DAMAGE. 
 */

#include "hx_locl.h"
RCSID("$Id: crypto.c 22435 2008-01-14 20:53:56Z lha $");

struct hx509_crypto;

struct signature_alg;

enum crypto_op_type {
    COT_SIGN
};

struct hx509_generate_private_context {
    const heim_oid *key_oid;
    int isCA;
    unsigned long num_bits;
};

struct hx509_private_key_ops {
    const char *pemtype;
    const heim_oid *(*key_oid)(void);
    int (*get_spki)(hx509_context,
		    const hx509_private_key,
		    SubjectPublicKeyInfo *);
    int (*export)(hx509_context context,
		  const hx509_private_key,
		  heim_octet_string *);
    int (*import)(hx509_context,
		  const void *data,
		  size_t len,
		  hx509_private_key private_key);
    int (*generate_private_key)(hx509_context,
				struct hx509_generate_private_context *,
				hx509_private_key);
    BIGNUM *(*get_internal)(hx509_context, hx509_private_key, const char *);
    int (*handle_alg)(const hx509_private_key,
		      const AlgorithmIdentifier *,
		      enum crypto_op_type);
    int (*sign)(hx509_context context,
		const hx509_private_key,
		const AlgorithmIdentifier *,
		const heim_octet_string *,
		AlgorithmIdentifier *,
		heim_octet_string *);
#if 0
    const AlgorithmIdentifier *(*preferred_sig_alg)
	(const hx509_private_key,
	 const hx509_peer_info);
    int (*unwrap)(hx509_context context,
		  const hx509_private_key,
		  const AlgorithmIdentifier *,
		  const heim_octet_string *,
		  heim_octet_string *);
#endif
};

struct hx509_private_key {
    unsigned int ref;
    const struct signature_alg *md;
    const heim_oid *signature_alg;
    union {
	RSA *rsa;
	void *keydata;
    } private_key;
    /* new crypto layer */
    hx509_private_key_ops *ops;
};

/*
 *
 */

struct signature_alg {
    const char *name;
    const heim_oid *(*sig_oid)(void);
    const AlgorithmIdentifier *(*sig_alg)(void);
    const heim_oid *(*key_oid)(void);
    const heim_oid *(*digest_oid)(void);
    int flags;
#define PROVIDE_CONF 1
#define REQUIRE_SIGNER 2

#define SIG_DIGEST	0x100
#define SIG_PUBLIC_SIG	0x200
#define SIG_SECRET	0x400

#define RA_RSA_USES_DIGEST_INFO 0x1000000


    int (*verify_signature)(hx509_context context,
			    const struct signature_alg *,
			    const Certificate *,
			    const AlgorithmIdentifier *,
			    const heim_octet_string *,
			    const heim_octet_string *);
    int (*create_signature)(hx509_context,
			    const struct signature_alg *,
			    const hx509_private_key,
			    const AlgorithmIdentifier *,
			    const heim_octet_string *,
			    AlgorithmIdentifier *,
			    heim_octet_string *);
};

/*
 *
 */

static BIGNUM *
heim_int2BN(const heim_integer *i)
{
    BIGNUM *bn;

    bn = BN_bin2bn(i->data, i->length, NULL);
    BN_set_negative(bn, i->negative);
    return bn;
}

/*
 *
 */

static int
set_digest_alg(DigestAlgorithmIdentifier *id,
	       const heim_oid *oid,
	       const void *param, size_t length)
{
    int ret;
    if (param) {
	id->parameters = malloc(sizeof(*id->parameters));
	if (id->parameters == NULL)
	    return ENOMEM;
	id->parameters->data = malloc(length);
	if (id->parameters->data == NULL) {
	    free(id->parameters);
	    id->parameters = NULL;
	    return ENOMEM;
	}
	memcpy(id->parameters->data, param, length);
	id->parameters->length = length;
    } else
	id->parameters = NULL;
    ret = der_copy_oid(oid, &id->algorithm);
    if (ret) {
	if (id->parameters) {
	    free(id->parameters->data);
	    free(id->parameters);
	    id->parameters = NULL;
	}
	return ret;
    }
    return 0;
}

/*
 *
 */

static int
rsa_verify_signature(hx509_context context,
		     const struct signature_alg *sig_alg,
		     const Certificate *signer,
		     const AlgorithmIdentifier *alg,
		     const heim_octet_string *data,
		     const heim_octet_string *sig)
{
    const SubjectPublicKeyInfo *spi;
    DigestInfo di;
    unsigned char *to;
    int tosize, retsize;
    int ret;
    RSA *rsa;
    RSAPublicKey pk;
    size_t size;

    memset(&di, 0, sizeof(di));

    spi = &signer->tbsCertificate.subjectPublicKeyInfo;

    rsa = RSA_new();
    if (rsa == NULL) {
	hx509_set_error_string(context, 0, ENOMEM, "out of memory");
	return ENOMEM;
    }
    ret = decode_RSAPublicKey(spi->subjectPublicKey.data,
			      spi->subjectPublicKey.length / 8,
			      &pk, &size);
    if (ret) {
	hx509_set_error_string(context, 0, ret, "Failed to decode RSAPublicKey");
	goto out;
    }

    rsa->n = heim_int2BN(&pk.modulus);
    rsa->e = heim_int2BN(&pk.publicExponent);

    free_RSAPublicKey(&pk);

    if (rsa->n == NULL || rsa->e == NULL) {
	ret = ENOMEM;
	hx509_set_error_string(context, 0, ret, "out of memory");
	goto out;
    }

    tosize = RSA_size(rsa);
    to = malloc(tosize);
    if (to == NULL) {
	ret = ENOMEM;
	hx509_set_error_string(context, 0, ret, "out of memory");
	goto out;
    }

    retsize = RSA_public_decrypt(sig->length, (unsigned char *)sig->data, 
				 to, rsa, RSA_PKCS1_PADDING);
    if (retsize <= 0) {
	ret = HX509_CRYPTO_SIG_INVALID_FORMAT;
	hx509_set_error_string(context, 0, ret, 
			       "RSA public decrypt failed: %d", retsize);
	free(to);
	goto out;
    }
    if (retsize > tosize)
	_hx509_abort("internal rsa decryption failure: ret > tosize");

    if (sig_alg->flags & RA_RSA_USES_DIGEST_INFO) {

	ret = decode_DigestInfo(to, retsize, &di, &size);
	free(to);
	if (ret) {
	    goto out;
	}
	
	/* Check for extra data inside the sigature */
	if (size != retsize) {
	    ret = HX509_CRYPTO_SIG_INVALID_FORMAT;
	    hx509_set_error_string(context, 0, ret, "size from decryption mismatch");
	    goto out;
	}
	
	if (sig_alg->digest_oid &&
	    der_heim_oid_cmp(&di.digestAlgorithm.algorithm, 
			     (*sig_alg->digest_oid)()) != 0) 
	{
	    ret = HX509_CRYPTO_OID_MISMATCH;
	    hx509_set_error_string(context, 0, ret, "object identifier in RSA sig mismatch");
	    goto out;
	}
	
	/* verify that the parameters are NULL or the NULL-type */
	if (di.digestAlgorithm.parameters != NULL &&
	    (di.digestAlgorithm.parameters->length != 2 ||
	     memcmp(di.digestAlgorithm.parameters->data, "\x05\x00", 2) != 0))
	{
	    ret = HX509_CRYPTO_SIG_INVALID_FORMAT;
	    hx509_set_error_string(context, 0, ret, "Extra parameters inside RSA signature");
	    goto out;
	}

	ret = _hx509_verify_signature(context,
				      NULL,
				      &di.digestAlgorithm,
				      data,
				      &di.digest);
    } else {
	if (retsize != data->length ||
	    memcmp(to, data->data, retsize) != 0)
	{
	    ret = HX509_CRYPTO_SIG_INVALID_FORMAT;
	    hx509_set_error_string(context, 0, ret, "RSA Signature incorrect");
	    goto out;
	}
	free(to);
    }

 out:
    free_DigestInfo(&di);
    RSA_free(rsa);
    return ret;
}

static int
rsa_create_signature(hx509_context context,
		     const struct signature_alg *sig_alg,
		     const hx509_private_key signer,
		     const AlgorithmIdentifier *alg,
		     const heim_octet_string *data,
		     AlgorithmIdentifier *signatureAlgorithm,
		     heim_octet_string *sig)
{
    const AlgorithmIdentifier *digest_alg;
    heim_octet_string indata;
    const heim_oid *sig_oid;
    size_t size;
    int ret;
    
    if (alg)
	sig_oid = &alg->algorithm;
    else
	sig_oid = signer->signature_alg;

    if (der_heim_oid_cmp(sig_oid, oid_id_pkcs1_sha256WithRSAEncryption()) == 0) {
	digest_alg = hx509_signature_sha256();
    } else if (der_heim_oid_cmp(sig_oid, oid_id_pkcs1_sha1WithRSAEncryption()) == 0) {
	digest_alg = hx509_signature_sha1();
    } else if (der_heim_oid_cmp(sig_oid, oid_id_pkcs1_md5WithRSAEncryption()) == 0) {
	digest_alg = hx509_signature_md5();
    } else if (der_heim_oid_cmp(sig_oid, oid_id_pkcs1_md5WithRSAEncryption()) == 0) {
	digest_alg = hx509_signature_md5();
    } else if (der_heim_oid_cmp(sig_oid, oid_id_dsa_with_sha1()) == 0) {
	digest_alg = hx509_signature_sha1();
    } else if (der_heim_oid_cmp(sig_oid, oid_id_pkcs1_rsaEncryption()) == 0) {
	digest_alg = hx509_signature_sha1();
    } else if (der_heim_oid_cmp(sig_oid, oid_id_heim_rsa_pkcs1_x509()) == 0) {
	digest_alg = NULL;
    } else
	return HX509_ALG_NOT_SUPP;

    if (signatureAlgorithm) {
	ret = set_digest_alg(signatureAlgorithm, sig_oid, "\x05\x00", 2);
	if (ret) {
	    hx509_clear_error_string(context);
	    return ret;
	}
    }

    if (digest_alg) {
	DigestInfo di;
	memset(&di, 0, sizeof(di));

	ret = _hx509_create_signature(context,
				      NULL,
				      digest_alg,
				      data,
				      &di.digestAlgorithm,
				      &di.digest);
	if (ret)
	    return ret;
	ASN1_MALLOC_ENCODE(DigestInfo,
			   indata.data,
			   indata.length,
			   &di,
			   &size,
			   ret);
	free_DigestInfo(&di);
	if (ret) {
	    hx509_set_error_string(context, 0, ret, "out of memory");
	    return ret;
	}
	if (indata.length != size)
	    _hx509_abort("internal ASN.1 encoder error");
    } else {
	indata = *data;
    }

    sig->length = RSA_size(signer->private_key.rsa);
    sig->data = malloc(sig->length);
    if (sig->data == NULL) {
	der_free_octet_string(&indata);
	hx509_set_error_string(context, 0, ENOMEM, "out of memory");
	return ENOMEM;
    }

    ret = RSA_private_encrypt(indata.length, indata.data, 
			      sig->data, 
			      signer->private_key.rsa,
			      RSA_PKCS1_PADDING);
    if (indata.data != data->data)
	der_free_octet_string(&indata);
    if (ret <= 0) {
	ret = HX509_CMS_FAILED_CREATE_SIGATURE;
	hx509_set_error_string(context, 0, ret,
			       "RSA private decrypt failed: %d", ret);
	return ret;
    }
    if (ret > sig->length)
	_hx509_abort("RSA signature prelen longer the output len");

    sig->length = ret;
    
    return 0;
}

static int
rsa_private_key_import(hx509_context context,
		       const void *data,
		       size_t len,
		       hx509_private_key private_key)
{
    const unsigned char *p = data;

    private_key->private_key.rsa = 
	d2i_RSAPrivateKey(NULL, &p, len);
    if (private_key->private_key.rsa == NULL) {
	hx509_set_error_string(context, 0, HX509_PARSING_KEY_FAILED,
			       "Failed to parse RSA key");
	return HX509_PARSING_KEY_FAILED;
    }
    private_key->signature_alg = oid_id_pkcs1_sha1WithRSAEncryption();

    return 0;
}

static int
rsa_private_key2SPKI(hx509_context context,
		     hx509_private_key private_key,
		     SubjectPublicKeyInfo *spki)
{
    int len, ret;

    memset(spki, 0, sizeof(*spki));

    len = i2d_RSAPublicKey(private_key->private_key.rsa, NULL);

    spki->subjectPublicKey.data = malloc(len);
    if (spki->subjectPublicKey.data == NULL) {
	hx509_set_error_string(context, 0, ENOMEM, "malloc - out of memory");
	return ENOMEM;
    }
    spki->subjectPublicKey.length = len * 8;

    ret = set_digest_alg(&spki->algorithm,oid_id_pkcs1_rsaEncryption(), 
			 "\x05\x00", 2);
    if (ret) {
	hx509_set_error_string(context, 0, ret, "malloc - out of memory");
	free(spki->subjectPublicKey.data);
	spki->subjectPublicKey.data = NULL;
	spki->subjectPublicKey.length = 0;
	return ret;
    }

    {
	unsigned char *pp = spki->subjectPublicKey.data;
	i2d_RSAPublicKey(private_key->private_key.rsa, &pp);
    }

    return 0;
}

static int
rsa_generate_private_key(hx509_context context, 
			 struct hx509_generate_private_context *ctx,
			 hx509_private_key private_key)
{
    BIGNUM *e;
    int ret;
    unsigned long bits;

    static const int default_rsa_e = 65537;
    static const int default_rsa_bits = 1024;

    private_key->private_key.rsa = RSA_new();
    if (private_key->private_key.rsa == NULL) {
	hx509_set_error_string(context, 0, HX509_PARSING_KEY_FAILED,
			       "Failed to generate RSA key");
	return HX509_PARSING_KEY_FAILED;
    }
    
    e = BN_new();
    BN_set_word(e, default_rsa_e);

    bits = default_rsa_bits;

    if (ctx->num_bits)
	bits = ctx->num_bits;
    else if (ctx->isCA)
	bits *= 2;

    ret = RSA_generate_key_ex(private_key->private_key.rsa, bits, e, NULL);
    BN_free(e);
    if (ret != 1) {
	hx509_set_error_string(context, 0, HX509_PARSING_KEY_FAILED,
			       "Failed to generate RSA key");
	return HX509_PARSING_KEY_FAILED;
    }
    private_key->signature_alg = oid_id_pkcs1_sha1WithRSAEncryption();

    return 0;
}

static int 
rsa_private_key_export(hx509_context context,
		       const hx509_private_key key,
		       heim_octet_string *data)
{
    int ret;

    data->data = NULL;
    data->length = 0;

    ret = i2d_RSAPrivateKey(key->private_key.rsa, NULL);
    if (ret <= 0) {
	ret = EINVAL;
	hx509_set_error_string(context, 0, ret,
			       "Private key is not exportable");
	return ret;
    }

    data->data = malloc(ret);
    if (data->data == NULL) {
	ret = ENOMEM;
	hx509_set_error_string(context, 0, ret, "malloc out of memory");
	return ret;
    }
    data->length = ret;
    
    {
	unsigned char *p = data->data;
	i2d_RSAPrivateKey(key->private_key.rsa, &p);
    }

    return 0;
}

static BIGNUM *
rsa_get_internal(hx509_context context, hx509_private_key key, const char *type)
{
    if (strcasecmp(type, "rsa-modulus") == 0) {
	return BN_dup(key->private_key.rsa->n);
    } else if (strcasecmp(type, "rsa-exponent") == 0) {
	return BN_dup(key->private_key.rsa->e);
    } else
	return NULL;
}



static hx509_private_key_ops rsa_private_key_ops = {
    "RSA PRIVATE KEY",
    oid_id_pkcs1_rsaEncryption,
    rsa_private_key2SPKI,
    rsa_private_key_export,
    rsa_private_key_import,
    rsa_generate_private_key,
    rsa_get_internal
};


/*
 *
 */

static int
dsa_verify_signature(hx509_context context,
		     const struct signature_alg *sig_alg,
		     const Certificate *signer,
		     const AlgorithmIdentifier *alg,
		     const heim_octet_string *data,
		     const heim_octet_string *sig)
{
    const SubjectPublicKeyInfo *spi;
    DSAPublicKey pk;
    DSAParams param;
    size_t size;
    DSA *dsa;
    int ret;

    spi = &signer->tbsCertificate.subjectPublicKeyInfo;

    dsa = DSA_new();
    if (dsa == NULL) {
	hx509_set_error_string(context, 0, ENOMEM, "out of memory");
	return ENOMEM;
    }

    ret = decode_DSAPublicKey(spi->subjectPublicKey.data,
			      spi->subjectPublicKey.length / 8,
			      &pk, &size);
    if (ret)
	goto out;

    dsa->pub_key = heim_int2BN(&pk);

    free_DSAPublicKey(&pk);

    if (dsa->pub_key == NULL) {
	ret = ENOMEM;
	hx509_set_error_string(context, 0, ret, "out of memory");
	goto out;
    }

    if (spi->algorithm.parameters == NULL) {
	ret = HX509_CRYPTO_SIG_INVALID_FORMAT;
	hx509_set_error_string(context, 0, ret, "DSA parameters missing");
	goto out;
    }

    ret = decode_DSAParams(spi->algorithm.parameters->data,
			   spi->algorithm.parameters->length,
			   &param,
			   &size);
    if (ret) {
	hx509_set_error_string(context, 0, ret, "DSA parameters failed to decode");
	goto out;
    }

    dsa->p = heim_int2BN(&param.p);
    dsa->q = heim_int2BN(&param.q);
    dsa->g = heim_int2BN(&param.g);

    free_DSAParams(&param);

    if (dsa->p == NULL || dsa->q == NULL || dsa->g == NULL) {
	ret = ENOMEM;
	hx509_set_error_string(context, 0, ret, "out of memory");
	goto out;
    }

    ret = DSA_verify(-1, data->data, data->length,
		     (unsigned char*)sig->data, sig->length,
		     dsa);
    if (ret == 1)
	ret = 0;
    else if (ret == 0 || ret == -1) {
	ret = HX509_CRYPTO_BAD_SIGNATURE;
	hx509_set_error_string(context, 0, ret, "BAD DSA sigature");
    } else {
	ret = HX509_CRYPTO_SIG_INVALID_FORMAT;
	hx509_set_error_string(context, 0, ret, "Invalid format of DSA sigature");
    }

 out:
    DSA_free(dsa);

    return ret;
}

#if 0
static int
dsa_parse_private_key(hx509_context context,
		      const void *data,
		      size_t len,
		      hx509_private_key private_key)
{
    const unsigned char *p = data;

    private_key->private_key.dsa = 
	d2i_DSAPrivateKey(NULL, &p, len);
    if (private_key->private_key.dsa == NULL)
	return EINVAL;
    private_key->signature_alg = oid_id_dsa_with_sha1();

    return 0;
/* else */
    hx509_set_error_string(context, 0, HX509_PARSING_KEY_FAILED,
			   "No support to parse DSA keys");
    return HX509_PARSING_KEY_FAILED;
}
#endif


static int
sha1_verify_signature(hx509_context context,
		      const struct signature_alg *sig_alg,
		      const Certificate *signer,
		      const AlgorithmIdentifier *alg,
		      const heim_octet_string *data,
		      const heim_octet_string *sig)
{
    unsigned char digest[SHA_DIGEST_LENGTH];
    SHA_CTX m;
    
    if (sig->length != SHA_DIGEST_LENGTH) {
	hx509_set_error_string(context, 0, HX509_CRYPTO_SIG_INVALID_FORMAT,
			       "SHA1 sigature have wrong length");
	return HX509_CRYPTO_SIG_INVALID_FORMAT;
    }

    SHA1_Init(&m);
    SHA1_Update(&m, data->data, data->length);
    SHA1_Final (digest, &m);
	
    if (memcmp(digest, sig->data, SHA_DIGEST_LENGTH) != 0) {
	hx509_set_error_string(context, 0, HX509_CRYPTO_BAD_SIGNATURE,
			       "Bad SHA1 sigature");
	return HX509_CRYPTO_BAD_SIGNATURE;
    }

    return 0;
}

static int
sha256_create_signature(hx509_context context,
			const struct signature_alg *sig_alg,
			const hx509_private_key signer,
			const AlgorithmIdentifier *alg,
			const heim_octet_string *data,
			AlgorithmIdentifier *signatureAlgorithm,
			heim_octet_string *sig)
{
    SHA256_CTX m;
    
    memset(sig, 0, sizeof(*sig));

    if (signatureAlgorithm) {
	int ret;
	ret = set_digest_alg(signatureAlgorithm, (*sig_alg->sig_oid)(),
			     "\x05\x00", 2);
	if (ret)
	    return ret;
    }
	    

    sig->data = malloc(SHA256_DIGEST_LENGTH);
    if (sig->data == NULL) {
	sig->length = 0;
	return ENOMEM;
    }
    sig->length = SHA256_DIGEST_LENGTH;

    SHA256_Init(&m);
    SHA256_Update(&m, data->data, data->length);
    SHA256_Final (sig->data, &m);

    return 0;
}

static int
sha256_verify_signature(hx509_context context,
			const struct signature_alg *sig_alg,
			const Certificate *signer,
			const AlgorithmIdentifier *alg,
			const heim_octet_string *data,
			const heim_octet_string *sig)
{
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256_CTX m;
    
    if (sig->length != SHA256_DIGEST_LENGTH) {
	hx509_set_error_string(context, 0, HX509_CRYPTO_SIG_INVALID_FORMAT,
			       "SHA256 sigature have wrong length");
	return HX509_CRYPTO_SIG_INVALID_FORMAT;
    }

    SHA256_Init(&m);
    SHA256_Update(&m, data->data, data->length);
    SHA256_Final (digest, &m);
	
    if (memcmp(digest, sig->data, SHA256_DIGEST_LENGTH) != 0) {
	hx509_set_error_string(context, 0, HX509_CRYPTO_BAD_SIGNATURE,
			       "Bad SHA256 sigature");
	return HX509_CRYPTO_BAD_SIGNATURE;
    }

    return 0;
}

static int
sha1_create_signature(hx509_context context,
		      const struct signature_alg *sig_alg,
		      const hx509_private_key signer,
		      const AlgorithmIdentifier *alg,
		      const heim_octet_string *data,
		      AlgorithmIdentifier *signatureAlgorithm,
		      heim_octet_string *sig)
{
    SHA_CTX m;
    
    memset(sig, 0, sizeof(*sig));

    if (signatureAlgorithm) {
	int ret;
	ret = set_digest_alg(signatureAlgorithm, (*sig_alg->sig_oid)(), 
			     "\x05\x00", 2);
	if (ret)
	    return ret;
    }
	    

    sig->data = malloc(SHA_DIGEST_LENGTH);
    if (sig->data == NULL) {
	sig->length = 0;
	return ENOMEM;
    }
    sig->length = SHA_DIGEST_LENGTH;

    SHA1_Init(&m);
    SHA1_Update(&m, data->data, data->length);
    SHA1_Final (sig->data, &m);

    return 0;
}

static int
md5_verify_signature(hx509_context context,
		     const struct signature_alg *sig_alg,
		     const Certificate *signer,
		     const AlgorithmIdentifier *alg,
		     const heim_octet_string *data,
		     const heim_octet_string *sig)
{
    unsigned char digest[MD5_DIGEST_LENGTH];
    MD5_CTX m;
    
    if (sig->length != MD5_DIGEST_LENGTH) {
	hx509_set_error_string(context, 0, HX509_CRYPTO_SIG_INVALID_FORMAT,
			       "MD5 sigature have wrong length");
	return HX509_CRYPTO_SIG_INVALID_FORMAT;
    }

    MD5_Init(&m);
    MD5_Update(&m, data->data, data->length);
    MD5_Final (digest, &m);
	
    if (memcmp(digest, sig->data, MD5_DIGEST_LENGTH) != 0) {
	hx509_set_error_string(context, 0, HX509_CRYPTO_BAD_SIGNATURE,
			       "Bad MD5 sigature");
	return HX509_CRYPTO_BAD_SIGNATURE;
    }

    return 0;
}

static int
md2_verify_signature(hx509_context context,
		     const struct signature_alg *sig_alg,
		     const Certificate *signer,
		     const AlgorithmIdentifier *alg,
		     const heim_octet_string *data,
		     const heim_octet_string *sig)
{
    unsigned char digest[MD2_DIGEST_LENGTH];
    MD2_CTX m;
    
    if (sig->length != MD2_DIGEST_LENGTH) {
	hx509_set_error_string(context, 0, HX509_CRYPTO_SIG_INVALID_FORMAT,
			       "MD2 sigature have wrong length");
	return HX509_CRYPTO_SIG_INVALID_FORMAT;
    }

    MD2_Init(&m);
    MD2_Update(&m, data->data, data->length);
    MD2_Final (digest, &m);
	
    if (memcmp(digest, sig->data, MD2_DIGEST_LENGTH) != 0) {
	hx509_set_error_string(context, 0, HX509_CRYPTO_BAD_SIGNATURE,
			       "Bad MD2 sigature");
	return HX509_CRYPTO_BAD_SIGNATURE;
    }

    return 0;
}

static const struct signature_alg heim_rsa_pkcs1_x509 = {
    "rsa-pkcs1-x509",
    oid_id_heim_rsa_pkcs1_x509,
    hx509_signature_rsa_pkcs1_x509,
    oid_id_pkcs1_rsaEncryption,
    NULL,
    PROVIDE_CONF|REQUIRE_SIGNER|SIG_PUBLIC_SIG,
    rsa_verify_signature,
    rsa_create_signature
};

static const struct signature_alg pkcs1_rsa_sha1_alg = {
    "rsa",
    oid_id_pkcs1_rsaEncryption,
    hx509_signature_rsa_with_sha1,
    oid_id_pkcs1_rsaEncryption,
    NULL,
    PROVIDE_CONF|REQUIRE_SIGNER|RA_RSA_USES_DIGEST_INFO|SIG_PUBLIC_SIG,
    rsa_verify_signature,
    rsa_create_signature
};

static const struct signature_alg rsa_with_sha256_alg = {
    "rsa-with-sha256",
    oid_id_pkcs1_sha256WithRSAEncryption,
    hx509_signature_rsa_with_sha256,
    oid_id_pkcs1_rsaEncryption,
    oid_id_sha256,
    PROVIDE_CONF|REQUIRE_SIGNER|RA_RSA_USES_DIGEST_INFO|SIG_PUBLIC_SIG,
    rsa_verify_signature,
    rsa_create_signature
};

static const struct signature_alg rsa_with_sha1_alg = {
    "rsa-with-sha1",
    oid_id_pkcs1_sha1WithRSAEncryption,
    hx509_signature_rsa_with_sha1,
    oid_id_pkcs1_rsaEncryption,
    oid_id_secsig_sha_1,
    PROVIDE_CONF|REQUIRE_SIGNER|RA_RSA_USES_DIGEST_INFO|SIG_PUBLIC_SIG,
    rsa_verify_signature,
    rsa_create_signature
};

static const struct signature_alg rsa_with_md5_alg = {
    "rsa-with-md5",
    oid_id_pkcs1_md5WithRSAEncryption,
    hx509_signature_rsa_with_md5,
    oid_id_pkcs1_rsaEncryption,
    oid_id_rsa_digest_md5,
    PROVIDE_CONF|REQUIRE_SIGNER|RA_RSA_USES_DIGEST_INFO|SIG_PUBLIC_SIG,
    rsa_verify_signature,
    rsa_create_signature
};

static const struct signature_alg rsa_with_md2_alg = {
    "rsa-with-md2",
    oid_id_pkcs1_md2WithRSAEncryption,
    hx509_signature_rsa_with_md2,
    oid_id_pkcs1_rsaEncryption,
    oid_id_rsa_digest_md2,
    PROVIDE_CONF|REQUIRE_SIGNER|RA_RSA_USES_DIGEST_INFO|SIG_PUBLIC_SIG,
    rsa_verify_signature,
    rsa_create_signature
};

static const struct signature_alg dsa_sha1_alg = {
    "dsa-with-sha1",
    oid_id_dsa_with_sha1,
    NULL,
    oid_id_dsa, 
    oid_id_secsig_sha_1,
    PROVIDE_CONF|REQUIRE_SIGNER|SIG_PUBLIC_SIG,
    dsa_verify_signature,
    /* create_signature */ NULL,
};

static const struct signature_alg sha256_alg = {
    "sha-256",
    oid_id_sha256,
    hx509_signature_sha256,
    NULL,
    NULL,
    SIG_DIGEST,
    sha256_verify_signature,
    sha256_create_signature
};

static const struct signature_alg sha1_alg = {
    "sha1",
    oid_id_secsig_sha_1,
    hx509_signature_sha1,
    NULL,
    NULL,
    SIG_DIGEST,
    sha1_verify_signature,
    sha1_create_signature
};

static const struct signature_alg md5_alg = {
    "rsa-md5",
    oid_id_rsa_digest_md5,
    hx509_signature_md5,
    NULL,
    NULL,
    SIG_DIGEST,
    md5_verify_signature
};

static const struct signature_alg md2_alg = {
    "rsa-md2",
    oid_id_rsa_digest_md2,
    hx509_signature_md2,
    NULL,
    NULL,
    SIG_DIGEST,
    md2_verify_signature
};

/* 
 * Order matter in this structure, "best" first for each "key
 * compatible" type (type is RSA, DSA, none, etc)
 */

static const struct signature_alg *sig_algs[] = {
    &rsa_with_sha256_alg,
    &rsa_with_sha1_alg,
    &pkcs1_rsa_sha1_alg,
    &rsa_with_md5_alg,
    &rsa_with_md2_alg,
    &heim_rsa_pkcs1_x509,
    &dsa_sha1_alg,
    &sha256_alg,
    &sha1_alg,
    &md5_alg,
    &md2_alg,
    NULL
};

static const struct signature_alg *
find_sig_alg(const heim_oid *oid)
{
    int i;
    for (i = 0; sig_algs[i]; i++)
	if (der_heim_oid_cmp((*sig_algs[i]->sig_oid)(), oid) == 0)
	    return sig_algs[i];
    return NULL;
}

/*
 *
 */

static struct hx509_private_key_ops *private_algs[] = {
    &rsa_private_key_ops,
    NULL
};

static hx509_private_key_ops *
find_private_alg(const heim_oid *oid)
{
    int i;
    for (i = 0; private_algs[i]; i++) {
	if (private_algs[i]->key_oid == NULL)
	    continue;
	if (der_heim_oid_cmp((*private_algs[i]->key_oid)(), oid) == 0)
	    return private_algs[i];
    }
    return NULL;
}


int
_hx509_verify_signature(hx509_context context,
			const Certificate *signer,
			const AlgorithmIdentifier *alg,
			const heim_octet_string *data,
			const heim_octet_string *sig)
{
    const struct signature_alg *md;

    md = find_sig_alg(&alg->algorithm);
    if (md == NULL) {
	hx509_clear_error_string(context);
	return HX509_SIG_ALG_NO_SUPPORTED;
    }
    if (signer && (md->flags & PROVIDE_CONF) == 0) {
	hx509_clear_error_string(context);
	return HX509_CRYPTO_SIG_NO_CONF;
    }
    if (signer == NULL && (md->flags & REQUIRE_SIGNER)) {
	    hx509_clear_error_string(context);
	return HX509_CRYPTO_SIGNATURE_WITHOUT_SIGNER;
    }
    if (md->key_oid && signer) {
	const SubjectPublicKeyInfo *spi;
	spi = &signer->tbsCertificate.subjectPublicKeyInfo;

	if (der_heim_oid_cmp(&spi->algorithm.algorithm, (*md->key_oid)()) != 0) {
	    hx509_clear_error_string(context);
	    return HX509_SIG_ALG_DONT_MATCH_KEY_ALG;
	}
    }
    return (*md->verify_signature)(context, md, signer, alg, data, sig);
}

int
_hx509_verify_signature_bitstring(hx509_context context,
				  const Certificate *signer,
				  const AlgorithmIdentifier *alg,
				  const heim_octet_string *data,
				  const heim_bit_string *sig)
{
    heim_octet_string os;

    if (sig->length & 7) {
	hx509_set_error_string(context, 0, HX509_CRYPTO_SIG_INVALID_FORMAT,
			       "signature not multiple of 8 bits");
	return HX509_CRYPTO_SIG_INVALID_FORMAT;
    }

    os.data = sig->data;
    os.length = sig->length / 8;
    
    return _hx509_verify_signature(context, signer, alg, data, &os);
}

int
_hx509_create_signature(hx509_context context,
			const hx509_private_key signer,
			const AlgorithmIdentifier *alg,
			const heim_octet_string *data,
			AlgorithmIdentifier *signatureAlgorithm,
			heim_octet_string *sig)
{
    const struct signature_alg *md;

    if (signer && signer->ops && signer->ops->handle_alg &&
	(*signer->ops->handle_alg)(signer, alg, COT_SIGN))
    {
	return (*signer->ops->sign)(context, signer, alg, data, 
				    signatureAlgorithm, sig);
    }

    md = find_sig_alg(&alg->algorithm);
    if (md == NULL) {
	hx509_set_error_string(context, 0, HX509_SIG_ALG_NO_SUPPORTED,
	    "algorithm no supported");
	return HX509_SIG_ALG_NO_SUPPORTED;
    }

    if (signer && (md->flags & PROVIDE_CONF) == 0) {
	hx509_set_error_string(context, 0, HX509_SIG_ALG_NO_SUPPORTED,
	    "algorithm provides no conf");
	return HX509_CRYPTO_SIG_NO_CONF;
    }

    return (*md->create_signature)(context, md, signer, alg, data, 
				   signatureAlgorithm, sig);
}

int
_hx509_create_signature_bitstring(hx509_context context,
				  const hx509_private_key signer,
				  const AlgorithmIdentifier *alg,
				  const heim_octet_string *data,
				  AlgorithmIdentifier *signatureAlgorithm,
				  heim_bit_string *sig)
{
    heim_octet_string os;
    int ret;

    ret = _hx509_create_signature(context, signer, alg,
				  data, signatureAlgorithm, &os);
    if (ret)
	return ret;
    sig->data = os.data;
    sig->length = os.length * 8;
    return 0;
}

int
_hx509_public_encrypt(hx509_context context,
		      const heim_octet_string *cleartext,
		      const Certificate *cert,
		      heim_oid *encryption_oid,
		      heim_octet_string *ciphertext)
{
    const SubjectPublicKeyInfo *spi;
    unsigned char *to;
    int tosize;
    int ret;
    RSA *rsa;
    RSAPublicKey pk;
    size_t size;

    ciphertext->data = NULL;
    ciphertext->length = 0;

    spi = &cert->tbsCertificate.subjectPublicKeyInfo;

    rsa = RSA_new();
    if (rsa == NULL) {
	hx509_set_error_string(context, 0, ENOMEM, "out of memory");
	return ENOMEM;
    }

    ret = decode_RSAPublicKey(spi->subjectPublicKey.data,
			      spi->subjectPublicKey.length / 8,
			      &pk, &size);
    if (ret) {
	RSA_free(rsa);
	hx509_set_error_string(context, 0, ret, "RSAPublicKey decode failure");
	return ret;
    }
    rsa->n = heim_int2BN(&pk.modulus);
    rsa->e = heim_int2BN(&pk.publicExponent);

    free_RSAPublicKey(&pk);

    if (rsa->n == NULL || rsa->e == NULL) {
	RSA_free(rsa);
	hx509_set_error_string(context, 0, ENOMEM, "out of memory");
	return ENOMEM;
    }

    tosize = RSA_size(rsa);
    to = malloc(tosize);
    if (to == NULL) {
	RSA_free(rsa);
	hx509_set_error_string(context, 0, ENOMEM, "out of memory");
	return ENOMEM;
    }

    ret = RSA_public_encrypt(cleartext->length, 
			     (unsigned char *)cleartext->data, 
			     to, rsa, RSA_PKCS1_PADDING);
    RSA_free(rsa);
    if (ret <= 0) {
	free(to);
	hx509_set_error_string(context, 0, HX509_CRYPTO_RSA_PUBLIC_ENCRYPT,
			       "RSA public encrypt failed with %d", ret);
	return HX509_CRYPTO_RSA_PUBLIC_ENCRYPT;
    }
    if (ret > tosize)
	_hx509_abort("internal rsa decryption failure: ret > tosize");

    ciphertext->length = ret;
    ciphertext->data = to;

    ret = der_copy_oid(oid_id_pkcs1_rsaEncryption(), encryption_oid);
    if (ret) {
	der_free_octet_string(ciphertext);
	hx509_set_error_string(context, 0, ENOMEM, "out of memory");
	return ENOMEM;
    }

    return 0;
}

int
_hx509_private_key_private_decrypt(hx509_context context,
				   const heim_octet_string *ciphertext,
				   const heim_oid *encryption_oid,
				   hx509_private_key p,
				   heim_octet_string *cleartext)
{
    int ret;

    cleartext->data = NULL;
    cleartext->length = 0;

    if (p->private_key.rsa == NULL) {
	hx509_set_error_string(context, 0, HX509_PRIVATE_KEY_MISSING,
			       "Private RSA key missing");
	return HX509_PRIVATE_KEY_MISSING;
    }

    cleartext->length = RSA_size(p->private_key.rsa);
    cleartext->data = malloc(cleartext->length);
    if (cleartext->data == NULL) {
	hx509_set_error_string(context, 0, ENOMEM, "out of memory");
	return ENOMEM;
    }
    ret = RSA_private_decrypt(ciphertext->length, ciphertext->data,
			      cleartext->data,
			      p->private_key.rsa,
			      RSA_PKCS1_PADDING);
    if (ret <= 0) {
	der_free_octet_string(cleartext);
	hx509_set_error_string(context, 0, HX509_CRYPTO_RSA_PRIVATE_DECRYPT,
			       "Failed to decrypt using private key: %d", ret);
	return HX509_CRYPTO_RSA_PRIVATE_DECRYPT;
    }
    if (cleartext->length < ret)
	_hx509_abort("internal rsa decryption failure: ret > tosize");

    cleartext->length = ret;

    return 0;
}


int
_hx509_parse_private_key(hx509_context context,
			 const heim_oid *key_oid,
			 const void *data,
			 size_t len,
			 hx509_private_key *private_key)
{
    struct hx509_private_key_ops *ops;
    int ret;

    *private_key = NULL;

    ops = find_private_alg(key_oid);
    if (ops == NULL) {
	hx509_clear_error_string(context);
	return HX509_SIG_ALG_NO_SUPPORTED;
    }

    ret = _hx509_private_key_init(private_key, ops, NULL);
    if (ret) {
	hx509_set_error_string(context, 0, ret, "out of memory");
	return ret;
    }

    ret = (*ops->import)(context, data, len, *private_key);
    if (ret)
	_hx509_private_key_free(private_key);

    return ret;
}

/*
 *
 */

int
_hx509_private_key2SPKI(hx509_context context,
			hx509_private_key private_key,
			SubjectPublicKeyInfo *spki)
{
    const struct hx509_private_key_ops *ops = private_key->ops;
    if (ops == NULL || ops->get_spki == NULL) {
	hx509_set_error_string(context, 0, HX509_UNIMPLEMENTED_OPERATION,
			       "Private key have no key2SPKI function");
	return HX509_UNIMPLEMENTED_OPERATION;
    }
    return (*ops->get_spki)(context, private_key, spki);
}

int
_hx509_generate_private_key_init(hx509_context context,
				 const heim_oid *oid,
				 struct hx509_generate_private_context **ctx)
{
    *ctx = NULL;

    if (der_heim_oid_cmp(oid, oid_id_pkcs1_rsaEncryption()) != 0) {
	hx509_set_error_string(context, 0, EINVAL, 
			       "private key not an RSA key");
	return EINVAL;
    }

    *ctx = calloc(1, sizeof(**ctx));
    if (*ctx == NULL) {
	hx509_set_error_string(context, 0, ENOMEM, "out of memory");
	return ENOMEM;
    }
    (*ctx)->key_oid = oid;

    return 0;
}

int
_hx509_generate_private_key_is_ca(hx509_context context,
				  struct hx509_generate_private_context *ctx)
{
    ctx->isCA = 1;
    return 0;
}

int
_hx509_generate_private_key_bits(hx509_context context,
				 struct hx509_generate_private_context *ctx,
				 unsigned long bits)
{
    ctx->num_bits = bits;
    return 0;
}


void
_hx509_generate_private_key_free(struct hx509_generate_private_context **ctx)
{
    free(*ctx);
    *ctx = NULL;
}

int
_hx509_generate_private_key(hx509_context context,
			    struct hx509_generate_private_context *ctx,
			    hx509_private_key *private_key)
{
    struct hx509_private_key_ops *ops;
    int ret;

    *private_key = NULL;

    ops = find_private_alg(ctx->key_oid);
    if (ops == NULL) {
	hx509_clear_error_string(context);
	return HX509_SIG_ALG_NO_SUPPORTED;
    }

    ret = _hx509_private_key_init(private_key, ops, NULL);
    if (ret) {
	hx509_set_error_string(context, 0, ret, "out of memory");
	return ret;
    }

    ret = (*ops->generate_private_key)(context, ctx, *private_key);
    if (ret)
	_hx509_private_key_free(private_key);

    return ret;
}


/*
 *
 */

static const heim_octet_string null_entry_oid = { 2, rk_UNCONST("\x05\x00") };

static const unsigned sha512_oid_tree[] = { 2, 16, 840, 1, 101, 3, 4, 2, 3 };
const AlgorithmIdentifier _hx509_signature_sha512_data = { 
    { 9, rk_UNCONST(sha512_oid_tree) }, rk_UNCONST(&null_entry_oid)
};

static const unsigned sha384_oid_tree[] = { 2, 16, 840, 1, 101, 3, 4, 2, 2 };
const AlgorithmIdentifier _hx509_signature_sha384_data = { 
    { 9, rk_UNCONST(sha384_oid_tree) }, rk_UNCONST(&null_entry_oid)
};

static const unsigned sha256_oid_tree[] = { 2, 16, 840, 1, 101, 3, 4, 2, 1 };
const AlgorithmIdentifier _hx509_signature_sha256_data = { 
    { 9, rk_UNCONST(sha256_oid_tree) }, rk_UNCONST(&null_entry_oid)
};

static const unsigned sha1_oid_tree[] = { 1, 3, 14, 3, 2, 26 };
const AlgorithmIdentifier _hx509_signature_sha1_data = { 
    { 6, rk_UNCONST(sha1_oid_tree) }, rk_UNCONST(&null_entry_oid)
};

static const unsigned md5_oid_tree[] = { 1, 2, 840, 113549, 2, 5 };
const AlgorithmIdentifier _hx509_signature_md5_data = { 
    { 6, rk_UNCONST(md5_oid_tree) }, rk_UNCONST(&null_entry_oid)
};

static const unsigned md2_oid_tree[] = { 1, 2, 840, 113549, 2, 2 };
const AlgorithmIdentifier _hx509_signature_md2_data = { 
    { 6, rk_UNCONST(md2_oid_tree) }, rk_UNCONST(&null_entry_oid)
};

static const unsigned rsa_with_sha512_oid[] ={ 1, 2, 840, 113549, 1, 1, 13 };
const AlgorithmIdentifier _hx509_signature_rsa_with_sha512_data = { 
    { 7, rk_UNCONST(rsa_with_sha512_oid) }, NULL
};

static const unsigned rsa_with_sha384_oid[] ={ 1, 2, 840, 113549, 1, 1, 12 };
const AlgorithmIdentifier _hx509_signature_rsa_with_sha384_data = { 
    { 7, rk_UNCONST(rsa_with_sha384_oid) }, NULL
};

static const unsigned rsa_with_sha256_oid[] ={ 1, 2, 840, 113549, 1, 1, 11 };
const AlgorithmIdentifier _hx509_signature_rsa_with_sha256_data = { 
    { 7, rk_UNCONST(rsa_with_sha256_oid) }, NULL
};

static const unsigned rsa_with_sha1_oid[] ={ 1, 2, 840, 113549, 1, 1, 5 };
const AlgorithmIdentifier _hx509_signature_rsa_with_sha1_data = { 
    { 7, rk_UNCONST(rsa_with_sha1_oid) }, NULL
};

static const unsigned rsa_with_md5_oid[] ={ 1, 2, 840, 113549, 1, 1, 4 };
const AlgorithmIdentifier _hx509_signature_rsa_with_md5_data = { 
    { 7, rk_UNCONST(rsa_with_md5_oid) }, NULL
};

static const unsigned rsa_with_md2_oid[] ={ 1, 2, 840, 113549, 1, 1, 2 };
const AlgorithmIdentifier _hx509_signature_rsa_with_md2_data = { 
    { 7, rk_UNCONST(rsa_with_md2_oid) }, NULL
};

static const unsigned rsa_oid[] ={ 1, 2, 840, 113549, 1, 1, 1 };
const AlgorithmIdentifier _hx509_signature_rsa_data = { 
    { 7, rk_UNCONST(rsa_oid) }, NULL
};

static const unsigned rsa_pkcs1_x509_oid[] ={ 1, 2, 752, 43, 16, 1 };
const AlgorithmIdentifier _hx509_signature_rsa_pkcs1_x509_data = { 
    { 6, rk_UNCONST(rsa_pkcs1_x509_oid) }, NULL
};

static const unsigned des_rsdi_ede3_cbc_oid[] ={ 1, 2, 840, 113549, 3, 7 };
const AlgorithmIdentifier _hx509_des_rsdi_ede3_cbc_oid = {
    { 6, rk_UNCONST(des_rsdi_ede3_cbc_oid) }, NULL
};

static const unsigned aes128_cbc_oid[] ={ 2, 16, 840, 1, 101, 3, 4, 1, 2 };
const AlgorithmIdentifier _hx509_crypto_aes128_cbc_data = {
    { 9, rk_UNCONST(aes128_cbc_oid) }, NULL
};

static const unsigned aes256_cbc_oid[] ={ 2, 16, 840, 1, 101, 3, 4, 1, 42 };
const AlgorithmIdentifier _hx509_crypto_aes256_cbc_data = {
    { 9, rk_UNCONST(aes256_cbc_oid) }, NULL
};

const AlgorithmIdentifier *
hx509_signature_sha512(void)
{ return &_hx509_signature_sha512_data; }

const AlgorithmIdentifier *
hx509_signature_sha384(void)
{ return &_hx509_signature_sha384_data; }

const AlgorithmIdentifier *
hx509_signature_sha256(void)
{ return &_hx509_signature_sha256_data; }

const AlgorithmIdentifier *
hx509_signature_sha1(void)
{ return &_hx509_signature_sha1_data; }

const AlgorithmIdentifier *
hx509_signature_md5(void)
{ return &_hx509_signature_md5_data; }

const AlgorithmIdentifier *
hx509_signature_md2(void)
{ return &_hx509_signature_md2_data; }

const AlgorithmIdentifier *
hx509_signature_rsa_with_sha512(void)
{ return &_hx509_signature_rsa_with_sha512_data; }

const AlgorithmIdentifier *
hx509_signature_rsa_with_sha384(void)
{ return &_hx509_signature_rsa_with_sha384_data; }

const AlgorithmIdentifier *
hx509_signature_rsa_with_sha256(void)
{ return &_hx509_signature_rsa_with_sha256_data; }

const AlgorithmIdentifier *
hx509_signature_rsa_with_sha1(void)
{ return &_hx509_signature_rsa_with_sha1_data; }

const AlgorithmIdentifier *
hx509_signature_rsa_with_md5(void)
{ return &_hx509_signature_rsa_with_md5_data; }

const AlgorithmIdentifier *
hx509_signature_rsa_with_md2(void)
{ return &_hx509_signature_rsa_with_md2_data; }

const AlgorithmIdentifier *
hx509_signature_rsa(void)
{ return &_hx509_signature_rsa_data; }

const AlgorithmIdentifier *
hx509_signature_rsa_pkcs1_x509(void)
{ return &_hx509_signature_rsa_pkcs1_x509_data; }

const AlgorithmIdentifier *
hx509_crypto_des_rsdi_ede3_cbc(void)
{ return &_hx509_des_rsdi_ede3_cbc_oid; }

const AlgorithmIdentifier *
hx509_crypto_aes128_cbc(void)
{ return &_hx509_crypto_aes128_cbc_data; }

const AlgorithmIdentifier *
hx509_crypto_aes256_cbc(void)
{ return &_hx509_crypto_aes256_cbc_data; }

/*
 *
 */

const AlgorithmIdentifier * _hx509_crypto_default_sig_alg = 
    &_hx509_signature_rsa_with_sha1_data;
const AlgorithmIdentifier * _hx509_crypto_default_digest_alg = 
    &_hx509_signature_sha1_data;
const AlgorithmIdentifier * _hx509_crypto_default_secret_alg = 
    &_hx509_crypto_aes128_cbc_data;

/*
 *
 */

int
_hx509_private_key_init(hx509_private_key *key,
			hx509_private_key_ops *ops,
			void *keydata)
{
    *key = calloc(1, sizeof(**key));
    if (*key == NULL)
	return ENOMEM;
    (*key)->ref = 1;
    (*key)->ops = ops;
    (*key)->private_key.keydata = keydata;
    return 0;
}

hx509_private_key
_hx509_private_key_ref(hx509_private_key key)
{
    if (key->ref <= 0)
	_hx509_abort("refcount <= 0");
    key->ref++;
    if (key->ref == 0)
	_hx509_abort("refcount == 0");
    return key;
}

const char *
_hx509_private_pem_name(hx509_private_key key)
{
    return key->ops->pemtype;
}

int
_hx509_private_key_free(hx509_private_key *key)
{
    if (key == NULL || *key == NULL)
	return 0;

    if ((*key)->ref <= 0)
	_hx509_abort("refcount <= 0");
    if (--(*key)->ref > 0)
	return 0;

    if ((*key)->private_key.rsa)
	RSA_free((*key)->private_key.rsa);
    (*key)->private_key.rsa = NULL;
    free(*key);
    *key = NULL;
    return 0;
}

void
_hx509_private_key_assign_rsa(hx509_private_key key, void *ptr)
{
    if (key->private_key.rsa)
	RSA_free(key->private_key.rsa);
    key->private_key.rsa = ptr;
    key->signature_alg = oid_id_pkcs1_sha1WithRSAEncryption();
    key->md = &pkcs1_rsa_sha1_alg;
}

int 
_hx509_private_key_oid(hx509_context context,
		       const hx509_private_key key,
		       heim_oid *data)
{
    int ret;
    ret = der_copy_oid((*key->ops->key_oid)(), data);
    if (ret)
	hx509_set_error_string(context, 0, ret, "malloc out of memory");
    return ret;
}

int
_hx509_private_key_exportable(hx509_private_key key)
{
    if (key->ops->export == NULL)
	return 0;
    return 1;
}

BIGNUM *
_hx509_private_key_get_internal(hx509_context context,
				hx509_private_key key, 
				const char *type)
{
    if (key->ops->get_internal == NULL)
	return NULL;
    return (*key->ops->get_internal)(context, key, type);
}

int 
_hx509_private_key_export(hx509_context context,
			  const hx509_private_key key,
			  heim_octet_string *data)
{
    if (key->ops->export == NULL) {
	hx509_clear_error_string(context);
	return HX509_UNIMPLEMENTED_OPERATION;
    }
    return (*key->ops->export)(context, key, data);
}

/*
 *
 */

struct hx509cipher {
    const char *name;
    const heim_oid *(*oid_func)(void);
    const AlgorithmIdentifier *(*ai_func)(void);
    const EVP_CIPHER *(*evp_func)(void);
    int (*get_params)(hx509_context, const hx509_crypto,
		      const heim_octet_string *, heim_octet_string *);
    int (*set_params)(hx509_context, const heim_octet_string *, 
		      hx509_crypto, heim_octet_string *);
};

struct hx509_crypto_data {
    char *name;
    const struct hx509cipher *cipher;
    const EVP_CIPHER *c;
    heim_octet_string key;
    heim_oid oid;
    void *param;
};

/*
 *
 */

static const heim_oid *
oid_private_rc2_40(void)
{
    static unsigned oid_data[] = { 127, 1 };
    static const heim_oid oid = { 2, oid_data };

    return &oid;
}


/*
 *
 */

static int
CMSCBCParam_get(hx509_context context, const hx509_crypto crypto,
		 const heim_octet_string *ivec, heim_octet_string *param)
{
    size_t size;
    int ret;

    assert(crypto->param == NULL);
    if (ivec == NULL)
	return 0;

    ASN1_MALLOC_ENCODE(CMSCBCParameter, param->data, param->length,
		       ivec, &size, ret);
    if (ret == 0 && size != param->length)
	_hx509_abort("Internal asn1 encoder failure");
    if (ret)
	hx509_clear_error_string(context);
    return ret;
}

static int
CMSCBCParam_set(hx509_context context, const heim_octet_string *param,
		hx509_crypto crypto, heim_octet_string *ivec)
{
    int ret;
    if (ivec == NULL)
	return 0;

    ret = decode_CMSCBCParameter(param->data, param->length, ivec, NULL);
    if (ret)
	hx509_clear_error_string(context);

    return ret;
}

struct _RC2_params {
    int maximum_effective_key;
};

static int
CMSRC2CBCParam_get(hx509_context context, const hx509_crypto crypto,
		   const heim_octet_string *ivec, heim_octet_string *param)
{
    CMSRC2CBCParameter rc2params;
    const struct _RC2_params *p = crypto->param;
    int maximum_effective_key = 128;
    size_t size;
    int ret;

    memset(&rc2params, 0, sizeof(rc2params));

    if (p)
	maximum_effective_key = p->maximum_effective_key;

    switch(maximum_effective_key) {
    case 40:
	rc2params.rc2ParameterVersion = 160;
	break;
    case 64:
	rc2params.rc2ParameterVersion = 120;
	break;
    case 128:
	rc2params.rc2ParameterVersion = 58;
	break;
    }
    rc2params.iv = *ivec;

    ASN1_MALLOC_ENCODE(CMSRC2CBCParameter, param->data, param->length,
		       &rc2params, &size, ret);
    if (ret == 0 && size != param->length)
	_hx509_abort("Internal asn1 encoder failure");

    return ret;
}

static int
CMSRC2CBCParam_set(hx509_context context, const heim_octet_string *param,
		   hx509_crypto crypto, heim_octet_string *ivec)
{
    CMSRC2CBCParameter rc2param;
    struct _RC2_params *p;
    size_t size;
    int ret;

    ret = decode_CMSRC2CBCParameter(param->data, param->length,
				    &rc2param, &size);
    if (ret) {
	hx509_clear_error_string(context);
	return ret;
    }

    p = calloc(1, sizeof(*p));
    if (p == NULL) {
	free_CMSRC2CBCParameter(&rc2param);
	hx509_clear_error_string(context);
	return ENOMEM;
    }
    switch(rc2param.rc2ParameterVersion) {
    case 160:
	crypto->c = EVP_rc2_40_cbc();
	p->maximum_effective_key = 40;
	break;
    case 120:
	crypto->c = EVP_rc2_64_cbc();
	p->maximum_effective_key = 64;
	break;
    case 58:
	crypto->c = EVP_rc2_cbc();
	p->maximum_effective_key = 128;
	break;
    default:
	free(p);
	free_CMSRC2CBCParameter(&rc2param);
	return HX509_CRYPTO_SIG_INVALID_FORMAT;
    }
    if (ivec)
	ret = der_copy_octet_string(&rc2param.iv, ivec);
    free_CMSRC2CBCParameter(&rc2param);
    if (ret) {
	free(p);
	hx509_clear_error_string(context);
    } else
	crypto->param = p;

    return ret;
}

/*
 *
 */

static const struct hx509cipher ciphers[] = {
    {
	"rc2-cbc",
	oid_id_pkcs3_rc2_cbc,
	NULL,
	EVP_rc2_cbc,
	CMSRC2CBCParam_get,
	CMSRC2CBCParam_set
    },
    {
	"rc2-cbc",
	oid_id_rsadsi_rc2_cbc,
	NULL,
	EVP_rc2_cbc,
	CMSRC2CBCParam_get,
	CMSRC2CBCParam_set
    },
    {
	"rc2-40-cbc",
	oid_private_rc2_40,
	NULL,
	EVP_rc2_40_cbc,
	CMSRC2CBCParam_get,
	CMSRC2CBCParam_set
    },
    {
	"des-ede3-cbc",
	oid_id_pkcs3_des_ede3_cbc,
	NULL,
	EVP_des_ede3_cbc,
	CMSCBCParam_get,
	CMSCBCParam_set
    },
    {
	"des-ede3-cbc",
	oid_id_rsadsi_des_ede3_cbc,
	hx509_crypto_des_rsdi_ede3_cbc,
	EVP_des_ede3_cbc,
	CMSCBCParam_get,
	CMSCBCParam_set
    },
    {
	"aes-128-cbc",
	oid_id_aes_128_cbc,
	hx509_crypto_aes128_cbc,
	EVP_aes_128_cbc,
	CMSCBCParam_get,
	CMSCBCParam_set
    },
    {
	"aes-192-cbc",
	oid_id_aes_192_cbc,
	NULL,
	EVP_aes_192_cbc,
	CMSCBCParam_get,
	CMSCBCParam_set
    },
    {
	"aes-256-cbc",
	oid_id_aes_256_cbc,
	hx509_crypto_aes256_cbc,
	EVP_aes_256_cbc,
	CMSCBCParam_get,
	CMSCBCParam_set
    }
};

static const struct hx509cipher *
find_cipher_by_oid(const heim_oid *oid)
{
    int i;

    for (i = 0; i < sizeof(ciphers)/sizeof(ciphers[0]); i++)
	if (der_heim_oid_cmp(oid, (*ciphers[i].oid_func)()) == 0)
	    return &ciphers[i];

    return NULL;
}

static const struct hx509cipher *
find_cipher_by_name(const char *name)
{
    int i;

    for (i = 0; i < sizeof(ciphers)/sizeof(ciphers[0]); i++)
	if (strcasecmp(name, ciphers[i].name) == 0)
	    return &ciphers[i];

    return NULL;
}


const heim_oid *
hx509_crypto_enctype_by_name(const char *name)
{
    const struct hx509cipher *cipher;

    cipher = find_cipher_by_name(name);
    if (cipher == NULL)
	return NULL;
    return (*cipher->oid_func)();
}

int
hx509_crypto_init(hx509_context context,
		  const char *provider,
		  const heim_oid *enctype,
		  hx509_crypto *crypto)
{
    const struct hx509cipher *cipher;

    *crypto = NULL;

    cipher = find_cipher_by_oid(enctype);
    if (cipher == NULL) {
	hx509_set_error_string(context, 0, HX509_ALG_NOT_SUPP,
			       "Algorithm not supported");
	return HX509_ALG_NOT_SUPP;
    }

    *crypto = calloc(1, sizeof(**crypto));
    if (*crypto == NULL) {
	hx509_clear_error_string(context);
	return ENOMEM;
    }

    (*crypto)->cipher = cipher;
    (*crypto)->c = (*cipher->evp_func)();

    if (der_copy_oid(enctype, &(*crypto)->oid)) {
	hx509_crypto_destroy(*crypto);
	*crypto = NULL;
	hx509_clear_error_string(context);
	return ENOMEM;
    }

    return 0;
}

const char *
hx509_crypto_provider(hx509_crypto crypto)
{
    return "unknown";
}

void
hx509_crypto_destroy(hx509_crypto crypto)
{
    if (crypto->name)
	free(crypto->name);
    if (crypto->key.data)
	free(crypto->key.data);
    if (crypto->param)
	free(crypto->param);
    der_free_oid(&crypto->oid);
    memset(crypto, 0, sizeof(*crypto));
    free(crypto);
}

int
hx509_crypto_set_key_name(hx509_crypto crypto, const char *name)
{
    return 0;
}

int
hx509_crypto_set_key_data(hx509_crypto crypto, const void *data, size_t length)
{
    if (EVP_CIPHER_key_length(crypto->c) > length)
	return HX509_CRYPTO_INTERNAL_ERROR;

    if (crypto->key.data) {
	free(crypto->key.data);
	crypto->key.data = NULL;
	crypto->key.length = 0;
    }
    crypto->key.data = malloc(length);
    if (crypto->key.data == NULL)
	return ENOMEM;
    memcpy(crypto->key.data, data, length);
    crypto->key.length = length;

    return 0;
}

int
hx509_crypto_set_random_key(hx509_crypto crypto, heim_octet_string *key)
{
    if (crypto->key.data) {
	free(crypto->key.data);
	crypto->key.length = 0;
    }

    crypto->key.length = EVP_CIPHER_key_length(crypto->c);
    crypto->key.data = malloc(crypto->key.length);
    if (crypto->key.data == NULL) {
	crypto->key.length = 0;
	return ENOMEM;
    }
    if (RAND_bytes(crypto->key.data, crypto->key.length) <= 0) {
	free(crypto->key.data);
	crypto->key.data = NULL;
	crypto->key.length = 0;
	return HX509_CRYPTO_INTERNAL_ERROR;
    }
    if (key)
	return der_copy_octet_string(&crypto->key, key);
    else
	return 0;
}

int
hx509_crypto_set_params(hx509_context context,
			hx509_crypto crypto, 
			const heim_octet_string *param,
			heim_octet_string *ivec)
{
    return (*crypto->cipher->set_params)(context, param, crypto, ivec);
}

int
hx509_crypto_get_params(hx509_context context,
			hx509_crypto crypto, 
			const heim_octet_string *ivec,
			heim_octet_string *param)
{
    return (*crypto->cipher->get_params)(context, crypto, ivec, param);
}

int
hx509_crypto_random_iv(hx509_crypto crypto, heim_octet_string *ivec)
{
    ivec->length = EVP_CIPHER_iv_length(crypto->c);
    ivec->data = malloc(ivec->length);
    if (ivec->data == NULL) {
	ivec->length = 0;
	return ENOMEM;
    }

    if (RAND_bytes(ivec->data, ivec->length) <= 0) {
	free(ivec->data);
	ivec->data = NULL;
	ivec->length = 0;
	return HX509_CRYPTO_INTERNAL_ERROR;
    }
    return 0;
}

int
hx509_crypto_encrypt(hx509_crypto crypto,
		     const void *data,
		     const size_t length,
		     const heim_octet_string *ivec,
		     heim_octet_string **ciphertext)
{
    EVP_CIPHER_CTX evp;
    size_t padsize;
    int ret;

    *ciphertext = NULL;

    assert(EVP_CIPHER_iv_length(crypto->c) == ivec->length);

    EVP_CIPHER_CTX_init(&evp);

    ret = EVP_CipherInit_ex(&evp, crypto->c, NULL,
			    crypto->key.data, ivec->data, 1);
    if (ret != 1) {
	EVP_CIPHER_CTX_cleanup(&evp);
	ret = HX509_CRYPTO_INTERNAL_ERROR;
	goto out;
    }

    *ciphertext = calloc(1, sizeof(**ciphertext));
    if (*ciphertext == NULL) {
	ret = ENOMEM;
	goto out;
    }
    
    if (EVP_CIPHER_block_size(crypto->c) == 1) {
	padsize = 0;
    } else {
	int bsize = EVP_CIPHER_block_size(crypto->c);
	padsize = bsize - (length % bsize);
    }
    (*ciphertext)->length = length + padsize;
    (*ciphertext)->data = malloc(length + padsize);
    if ((*ciphertext)->data == NULL) {
	ret = ENOMEM;
	goto out;
    }
	
    memcpy((*ciphertext)->data, data, length);
    if (padsize) {
	int i;
	unsigned char *p = (*ciphertext)->data;
	p += length;
	for (i = 0; i < padsize; i++)
	    *p++ = padsize;
    }

    ret = EVP_Cipher(&evp, (*ciphertext)->data,
		     (*ciphertext)->data,
		     length + padsize);
    if (ret != 1) {
	ret = HX509_CRYPTO_INTERNAL_ERROR;
	goto out;
    }
    ret = 0;

 out:
    if (ret) {
	if (*ciphertext) {
	    if ((*ciphertext)->data) {
		free((*ciphertext)->data);
	    }
	    free(*ciphertext);
	    *ciphertext = NULL;
	}
    }
    EVP_CIPHER_CTX_cleanup(&evp);

    return ret;
}

int
hx509_crypto_decrypt(hx509_crypto crypto,
		     const void *data,
		     const size_t length,
		     heim_octet_string *ivec,
		     heim_octet_string *clear)
{
    EVP_CIPHER_CTX evp;
    void *idata = NULL;
    int ret;

    clear->data = NULL;
    clear->length = 0;

    if (ivec && EVP_CIPHER_iv_length(crypto->c) < ivec->length)
	return HX509_CRYPTO_INTERNAL_ERROR;

    if (crypto->key.data == NULL)
	return HX509_CRYPTO_INTERNAL_ERROR;

    if (ivec)
	idata = ivec->data;

    EVP_CIPHER_CTX_init(&evp);

    ret = EVP_CipherInit_ex(&evp, crypto->c, NULL,
			    crypto->key.data, idata, 0);
    if (ret != 1) {
	EVP_CIPHER_CTX_cleanup(&evp);
	return HX509_CRYPTO_INTERNAL_ERROR;
    }

    clear->length = length;
    clear->data = malloc(length);
    if (clear->data == NULL) {
	EVP_CIPHER_CTX_cleanup(&evp);
	clear->length = 0;
	return ENOMEM;
    }

    if (EVP_Cipher(&evp, clear->data, data, length) != 1) {
	return HX509_CRYPTO_INTERNAL_ERROR;
    }
    EVP_CIPHER_CTX_cleanup(&evp);

    if (EVP_CIPHER_block_size(crypto->c) > 1) {
	int padsize;
	unsigned char *p; 
	int j, bsize = EVP_CIPHER_block_size(crypto->c);

	if (clear->length < bsize) {
	    ret = HX509_CMS_PADDING_ERROR;
	    goto out;
	}

	p = clear->data;
	p += clear->length - 1;
	padsize = *p;
	if (padsize > bsize) {
	    ret = HX509_CMS_PADDING_ERROR;
	    goto out;
	}
	clear->length -= padsize;
	for (j = 0; j < padsize; j++) {
	    if (*p-- != padsize) {
		ret = HX509_CMS_PADDING_ERROR;
		goto out;
	    }
	}
    }

    return 0;

 out:
    if (clear->data)
	free(clear->data);
    clear->data = NULL;
    clear->length = 0;
    return ret;
}

typedef int (*PBE_string2key_func)(hx509_context,
				   const char *,
				   const heim_octet_string *,
				   hx509_crypto *, heim_octet_string *, 
				   heim_octet_string *,
				   const heim_oid *, const EVP_MD *);

static int
PBE_string2key(hx509_context context,
	       const char *password,
	       const heim_octet_string *parameters,
	       hx509_crypto *crypto, 
	       heim_octet_string *key, heim_octet_string *iv,
	       const heim_oid *enc_oid,
	       const EVP_MD *md)
{
    PKCS12_PBEParams p12params;
    int passwordlen;
    hx509_crypto c;
    int iter, saltlen, ret;
    unsigned char *salt;

    passwordlen = password ? strlen(password) : 0;

    if (parameters == NULL)
 	return HX509_ALG_NOT_SUPP;

    ret = decode_PKCS12_PBEParams(parameters->data,
				  parameters->length,
				  &p12params, NULL);
    if (ret)
	goto out;

    if (p12params.iterations)
	iter = *p12params.iterations;
    else
	iter = 1;
    salt = p12params.salt.data;
    saltlen = p12params.salt.length;

    if (!PKCS12_key_gen (password, passwordlen, salt, saltlen, 
			 PKCS12_KEY_ID, iter, key->length, key->data, md)) {
	ret = HX509_CRYPTO_INTERNAL_ERROR;
	goto out;
    }
    
    if (!PKCS12_key_gen (password, passwordlen, salt, saltlen, 
			 PKCS12_IV_ID, iter, iv->length, iv->data, md)) {
	ret = HX509_CRYPTO_INTERNAL_ERROR;
	goto out;
    }

    ret = hx509_crypto_init(context, NULL, enc_oid, &c);
    if (ret)
	goto out;

    ret = hx509_crypto_set_key_data(c, key->data, key->length);
    if (ret) {
	hx509_crypto_destroy(c);
	goto out;
    }

    *crypto = c;
out:
    free_PKCS12_PBEParams(&p12params);
    return ret;
}

static const heim_oid *
find_string2key(const heim_oid *oid, 
		const EVP_CIPHER **c, 
		const EVP_MD **md,
		PBE_string2key_func *s2k)
{
    if (der_heim_oid_cmp(oid, oid_id_pbewithSHAAnd40BitRC2_CBC()) == 0) {
	*c = EVP_rc2_40_cbc();
	*md = EVP_sha1();
	*s2k = PBE_string2key;
	return oid_private_rc2_40();
    } else if (der_heim_oid_cmp(oid, oid_id_pbeWithSHAAnd128BitRC2_CBC()) == 0) {
	*c = EVP_rc2_cbc();
	*md = EVP_sha1();
	*s2k = PBE_string2key;
	return oid_id_pkcs3_rc2_cbc();
#if 0
    } else if (der_heim_oid_cmp(oid, oid_id_pbeWithSHAAnd40BitRC4()) == 0) {
	*c = EVP_rc4_40();
	*md = EVP_sha1();
	*s2k = PBE_string2key;
	return NULL;
    } else if (der_heim_oid_cmp(oid, oid_id_pbeWithSHAAnd128BitRC4()) == 0) {
	*c = EVP_rc4();
	*md = EVP_sha1();
	*s2k = PBE_string2key;
	return oid_id_pkcs3_rc4();
#endif
    } else if (der_heim_oid_cmp(oid, oid_id_pbeWithSHAAnd3_KeyTripleDES_CBC()) == 0) {
	*c = EVP_des_ede3_cbc();
	*md = EVP_sha1();
	*s2k = PBE_string2key;
	return oid_id_pkcs3_des_ede3_cbc();
    }

    return NULL;
}

/*
 *
 */

int
_hx509_pbe_encrypt(hx509_context context,
		   hx509_lock lock,
		   const AlgorithmIdentifier *ai,
		   const heim_octet_string *content,
		   heim_octet_string *econtent)
{
    hx509_clear_error_string(context);
    return EINVAL;
}

/*
 *
 */

int
_hx509_pbe_decrypt(hx509_context context,
		   hx509_lock lock,
		   const AlgorithmIdentifier *ai,
		   const heim_octet_string *econtent,
		   heim_octet_string *content)
{
    const struct _hx509_password *pw;
    heim_octet_string key, iv;
    const heim_oid *enc_oid;
    const EVP_CIPHER *c;
    const EVP_MD *md;
    PBE_string2key_func s2k;
    int i, ret = 0;

    memset(&key, 0, sizeof(key));
    memset(&iv, 0, sizeof(iv));

    memset(content, 0, sizeof(*content));

    enc_oid = find_string2key(&ai->algorithm, &c, &md, &s2k);
    if (enc_oid == NULL) {
	hx509_set_error_string(context, 0, HX509_ALG_NOT_SUPP,
			       "String to key algorithm not supported");
	ret = HX509_ALG_NOT_SUPP;
	goto out;
    }

    key.length = EVP_CIPHER_key_length(c);
    key.data = malloc(key.length);
    if (key.data == NULL) {
	ret = ENOMEM;
	hx509_clear_error_string(context);
	goto out;
    }

    iv.length = EVP_CIPHER_iv_length(c);
    iv.data = malloc(iv.length);
    if (iv.data == NULL) {
	ret = ENOMEM;
	hx509_clear_error_string(context);
	goto out;
    }

    pw = _hx509_lock_get_passwords(lock);

    ret = HX509_CRYPTO_INTERNAL_ERROR;
    for (i = 0; i < pw->len + 1; i++) {
	hx509_crypto crypto;
	const char *password;

	if (i < pw->len)
	    password = pw->val[i];
	else if (i < pw->len + 1)
	    password = "";
	else
	    password = NULL;

	ret = (*s2k)(context, password, ai->parameters, &crypto, 
		     &key, &iv, enc_oid, md);
	if (ret)
	    goto out;

	ret = hx509_crypto_decrypt(crypto,
				   econtent->data,
				   econtent->length,
				   &iv,
				   content);
	hx509_crypto_destroy(crypto);
	if (ret == 0)
	    goto out;
				   
    }
out:
    if (key.data)
	der_free_octet_string(&key);
    if (iv.data)
	der_free_octet_string(&iv);
    return ret;
}

/*
 *
 */


int
_hx509_match_keys(hx509_cert c, hx509_private_key private_key)
{
    const Certificate *cert;
    const SubjectPublicKeyInfo *spi;
    RSAPublicKey pk;
    RSA *rsa;
    size_t size;
    int ret;

    if (private_key->private_key.rsa == NULL)
	return 0;

    rsa = private_key->private_key.rsa;
    if (rsa->d == NULL || rsa->p == NULL || rsa->q == NULL)
	return 0;

    cert = _hx509_get_cert(c);
    spi = &cert->tbsCertificate.subjectPublicKeyInfo;

    rsa = RSA_new();
    if (rsa == NULL)
	return 0;

    ret = decode_RSAPublicKey(spi->subjectPublicKey.data,
			      spi->subjectPublicKey.length / 8,
			      &pk, &size);
    if (ret) {
	RSA_free(rsa);
	return 0;
    }
    rsa->n = heim_int2BN(&pk.modulus);
    rsa->e = heim_int2BN(&pk.publicExponent);

    free_RSAPublicKey(&pk);

    rsa->d = BN_dup(private_key->private_key.rsa->d);
    rsa->p = BN_dup(private_key->private_key.rsa->p);
    rsa->q = BN_dup(private_key->private_key.rsa->q);
    rsa->dmp1 = BN_dup(private_key->private_key.rsa->dmp1);
    rsa->dmq1 = BN_dup(private_key->private_key.rsa->dmq1);
    rsa->iqmp = BN_dup(private_key->private_key.rsa->iqmp);

    if (rsa->n == NULL || rsa->e == NULL || 
	rsa->d == NULL || rsa->p == NULL|| rsa->q == NULL ||
	rsa->dmp1 == NULL || rsa->dmq1 == NULL) {
	RSA_free(rsa);
	return 0;
    }

    ret = RSA_check_key(rsa);
    RSA_free(rsa);

    return ret == 1;
}

static const heim_oid *
find_keytype(const hx509_private_key key)
{
    const struct signature_alg *md;

    if (key == NULL)
	return NULL;

    md = find_sig_alg(key->signature_alg);
    if (md == NULL)
	return NULL;
    return (*md->key_oid)();
}


int
hx509_crypto_select(const hx509_context context,
		    int type,
		    const hx509_private_key source,
		    hx509_peer_info peer,
		    AlgorithmIdentifier *selected)
{
    const AlgorithmIdentifier *def;
    size_t i, j;
    int ret, bits;

    memset(selected, 0, sizeof(*selected));

    if (type == HX509_SELECT_DIGEST) {
	bits = SIG_DIGEST;
	def = _hx509_crypto_default_digest_alg;
    } else if (type == HX509_SELECT_PUBLIC_SIG) {
	bits = SIG_PUBLIC_SIG;
	/* XXX depend on `source� and `peer� */
	def = _hx509_crypto_default_sig_alg;
    } else if (type == HX509_SELECT_SECRET_ENC) {
	bits = SIG_SECRET;
	def = _hx509_crypto_default_secret_alg;
    } else {
	hx509_set_error_string(context, 0, EINVAL, 
			       "Unknown type %d of selection", type);
	return EINVAL;
    }

    if (peer) {
	const heim_oid *keytype = NULL;

	keytype = find_keytype(source);

	for (i = 0; i < peer->len; i++) {
	    for (j = 0; sig_algs[j]; j++) {
		if ((sig_algs[j]->flags & bits) != bits)
		    continue;
		if (der_heim_oid_cmp((*sig_algs[j]->sig_oid)(), 
				     &peer->val[i].algorithm) != 0)
		    continue;
		if (keytype && sig_algs[j]->key_oid && 
		    der_heim_oid_cmp(keytype, (*sig_algs[j]->key_oid)()))
		    continue;

		/* found one, use that */
		ret = copy_AlgorithmIdentifier(&peer->val[i], selected);
		if (ret)
		    hx509_clear_error_string(context);
		return ret;
	    }
	    if (bits & SIG_SECRET) {
		const struct hx509cipher *cipher;

		cipher = find_cipher_by_oid(&peer->val[i].algorithm);
		if (cipher == NULL)
		    continue;
		if (cipher->ai_func == NULL)
		    continue;
		ret = copy_AlgorithmIdentifier(cipher->ai_func(), selected);
		if (ret)
		    hx509_clear_error_string(context);
		return ret;
	    }
	}
    }

    /* use default */
    ret = copy_AlgorithmIdentifier(def, selected);
    if (ret)
	hx509_clear_error_string(context);
    return ret;
}

int
hx509_crypto_available(hx509_context context,
		       int type,
		       hx509_cert source,
		       AlgorithmIdentifier **val,
		       unsigned int *plen)
{
    const heim_oid *keytype = NULL;
    unsigned int len, i;
    void *ptr;
    int bits, ret;

    *val = NULL;

    if (type == HX509_SELECT_ALL) {
	bits = SIG_DIGEST | SIG_PUBLIC_SIG | SIG_SECRET;
    } else if (type == HX509_SELECT_DIGEST) {
	bits = SIG_DIGEST;
    } else if (type == HX509_SELECT_PUBLIC_SIG) {
	bits = SIG_PUBLIC_SIG;
    } else {
	hx509_set_error_string(context, 0, EINVAL, 
			       "Unknown type %d of available", type);
	return EINVAL;
    }

    if (source)
	keytype = find_keytype(_hx509_cert_private_key(source));

    len = 0;
    for (i = 0; sig_algs[i]; i++) {
	if ((sig_algs[i]->flags & bits) == 0)
	    continue;
	if (sig_algs[i]->sig_alg == NULL)
	    continue;
	if (keytype && sig_algs[i]->key_oid && 
	    der_heim_oid_cmp((*sig_algs[i]->key_oid)(), keytype))
	    continue;

	/* found one, add that to the list */
	ptr = realloc(*val, sizeof(**val) * (len + 1));
	if (ptr == NULL)
	    goto out;
	*val = ptr;

	ret = copy_AlgorithmIdentifier((*sig_algs[i]->sig_alg)(), &(*val)[len]);
	if (ret)
	    goto out;
	len++;
    }

    /* Add AES */
    if (bits & SIG_SECRET) {

	for (i = 0; i < sizeof(ciphers)/sizeof(ciphers[0]); i++) {
	
	    if (ciphers[i].ai_func == NULL)
		continue;

	    ptr = realloc(*val, sizeof(**val) * (len + 1));
	    if (ptr == NULL)
		goto out;
	    *val = ptr;
	    
	    ret = copy_AlgorithmIdentifier((ciphers[i].ai_func)(), &(*val)[len]);
	    if (ret)
		goto out;
	    len++;
	}
    }

    *plen = len;
    return 0;

out:
    for (i = 0; i < len; i++)
	free_AlgorithmIdentifier(&(*val)[i]);
    free(*val);
    *val = NULL;
    hx509_set_error_string(context, 0, ENOMEM, "out of memory");
    return ENOMEM;
}

void
hx509_crypto_free_algs(AlgorithmIdentifier *val,
		       unsigned int len)
{
    unsigned int i;
    for (i = 0; i < len; i++)
	free_AlgorithmIdentifier(&val[i]);
    free(val);
}    
