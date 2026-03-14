// SPDX-License-Identifier: CC0 OR Apache-2.0
/// @file ov.c
/// @brief The standard implementations for functions in ov.h
///
#include "params.h"

#include "ov_keypair.h"

#include "ov.h"

#include "blas.h"

#include "ov_blas.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "utils_prng.h"
#include "utils_hash.h"
#include "utils_malloc.h"


#define MAX_ATTEMPT_VINEGAR  256

/////////////////////////////

#if defined(_VALGRIND_)
#include "valgrind/memcheck.h"
#endif


/// Core signing: find w such that P(w) = y, given a pre-built target vector y (_PUB_M_BYTE bytes).
/// The vinegar derivation seed (arbitrary bytes) is mixed with sk_seed for deterministic retries.
static
int _ov_sign_target( uint8_t *signature, const sk_t *sk,
                     const uint8_t *y_target,
                     const uint8_t *vinegar_seed, size_t vinegar_seed_len ) {
    uint8_t mat_l1[_O * _O_BYTE];
    uint8_t vinegar[_V_BYTE];
    uint8_t r_l1_F1[_O_BYTE];
    uint8_t y[_PUB_N_BYTE];
    uint8_t x_o1[_O_BYTE];

    // Copy the target into y (will be clobbered later for T computation)
    memcpy( y, y_target, _PUB_M_BYTE );

    #if defined(_VALGRIND_)
    VALGRIND_MAKE_MEM_UNDEFINED(sk, OV_SECRETKEYBYTES );
    #endif

    // Derive vinegar values deterministically: H(vinegar_seed || sk_seed || ctr)
    hash_ctx h_vinegar_base;
    hash_ctx h_vinegar_copy;
    hash_init  (&h_vinegar_base);
    hash_update(&h_vinegar_base, vinegar_seed, vinegar_seed_len);
    hash_update(&h_vinegar_base, sk->sk_seed, LEN_SKSEED );
    hash_ctx_copy(&h_vinegar_copy, &h_vinegar_base);

    unsigned n_attempt = 0;
    while ( MAX_ATTEMPT_VINEGAR > n_attempt ) {
        uint8_t ctr = n_attempt & 0xff;
        n_attempt++;
        hash_ctx h_vinegar;
        hash_ctx_copy(&h_vinegar, &h_vinegar_copy);
        hash_update(&h_vinegar, &ctr, 1 );
        hash_final_digest( vinegar, _V_BYTE, &h_vinegar);

        #if defined(_VALGRIND_)
        VALGRIND_MAKE_MEM_UNDEFINED(vinegar, _V_BYTE );
        #endif

        gfmat_prod( mat_l1, sk->S, _O * _O_BYTE, _V, vinegar );
        batch_quad_trimat_eval( r_l1_F1, sk->P1, vinegar, _V, _O_BYTE );
        gf256v_add( r_l1_F1, y, _O_BYTE );

        #if _GFSIZE == 256
        unsigned l1_succ = gf256mat_gaussian_elim(mat_l1, r_l1_F1, _O);
        #if defined(_VALGRIND_)
        VALGRIND_MAKE_MEM_DEFINED(&l1_succ, sizeof(unsigned) );
        #endif
        if ( !l1_succ ) { continue; }
        gf256mat_back_substitute(r_l1_F1, mat_l1, _O);
        memcpy( x_o1, r_l1_F1, _O_BYTE );
        #elif _GFSIZE == 16
        unsigned l1_succ = gf16mat_gaussian_elim(mat_l1, r_l1_F1, _O);
        #if defined(_VALGRIND_)
        VALGRIND_MAKE_MEM_DEFINED(&l1_succ, sizeof(unsigned) );
        #endif
        if ( !l1_succ ) { continue; }
        gf16mat_back_substitute(r_l1_F1, mat_l1, _O);
        memcpy( x_o1, r_l1_F1, _O_BYTE );
        #else
        error -- _GFSIZE
        #endif
        break;
    }
    hash_final_digest( NULL, 0, &h_vinegar_copy);   // free
    if ( MAX_ATTEMPT_VINEGAR <= n_attempt ) {
        return -1;
    }

    //  w = T^-1 * x
    uint8_t *w = signature;
    memcpy( w, vinegar, _V_BYTE );
    memcpy( w + _V_BYTE, x_o1, _O_BYTE );

    gfmat_prod(y, sk->O, _V_BYTE, _O, x_o1 );
    gf256v_add(w, y, _V_BYTE );

    return 0;
}


/// Helper: build the target vector y = hash_bytes[0.._HASH_EFFECTIVE_BYTE-1] || salt[0.._SALT_BYTE-1]
/// and a vinegar seed from the provided inputs.
static
void _build_target_from_hash( uint8_t *y, const uint8_t *hash_bytes,
                              const uint8_t *salt ) {
#if _SALT_BYTE > 0
    memcpy( y, hash_bytes, _HASH_EFFECTIVE_BYTE );
    memcpy( y + _HASH_EFFECTIVE_BYTE, salt, _SALT_BYTE );
#else
    memcpy( y, hash_bytes, _PUB_M_BYTE );
    (void)salt;
#endif
}


int ov_sign( uint8_t *signature, const sk_t *sk, const uint8_t *message, size_t mlen ) {
    return ov_sign_salt( signature, sk, message, mlen, NULL, 0 );
}

int ov_sign_salt( uint8_t *signature, const sk_t *sk, const uint8_t *message, size_t mlen,
                  const uint8_t *user_salt, size_t user_salt_len ) {
    // Generate or derive salt
#if _SALT_BYTE > 0
    uint8_t salt[_SALT_BYTE];
    if ( user_salt != NULL && user_salt_len > 0 ) {
        hash_ctx hctx_salt;
        hash_init(&hctx_salt);
        hash_update(&hctx_salt, user_salt, user_salt_len);
        hash_final_digest(salt, _SALT_BYTE, &hctx_salt);
    } else {
        randombytes( salt, _SALT_BYTE );
    }
#else
    (void)user_salt; (void)user_salt_len;
#endif

    // Hash the message
    uint8_t hash_bytes[_PUB_M_BYTE];
    {
        hash_ctx hctx_msg;
        hash_init(&hctx_msg);
        hash_update(&hctx_msg, message, mlen);
#if _SALT_BYTE > 0
        hash_final_digest( hash_bytes, _HASH_EFFECTIVE_BYTE, &hctx_msg);
#else
        hash_final_digest( hash_bytes, _PUB_M_BYTE, &hctx_msg);
#endif
    }

    // Build target
    uint8_t y[_PUB_M_BYTE];
#if _SALT_BYTE > 0
    _build_target_from_hash( y, hash_bytes, salt );
#else
    _build_target_from_hash( y, hash_bytes, NULL );
#endif

    // Build vinegar seed: H(message || salt || ...) for deterministic retry
    // We pass message || salt as the vinegar seed
    uint8_t vseed[_PUB_M_BYTE];  // reuse as temp buffer for seed material
    // Build seed by concatenating into a hash: H(message || salt) -> vseed
    {
        hash_ctx hctx_vs;
        hash_init(&hctx_vs);
        hash_update(&hctx_vs, message, mlen);
#if _SALT_BYTE > 0
        hash_update(&hctx_vs, salt, _SALT_BYTE);
#endif
        hash_final_digest(vseed, sizeof(vseed), &hctx_vs);
    }

    return _ov_sign_target( signature, sk, y, vseed, sizeof(vseed) );
}

int ov_sign_digest( uint8_t *signature, const sk_t *sk,
                    const uint8_t *digest, size_t digest_len,
                    const uint8_t *user_salt, size_t user_salt_len ) {
    // Generate or derive salt
#if _SALT_BYTE > 0
    uint8_t salt[_SALT_BYTE];
    if ( user_salt != NULL && user_salt_len > 0 ) {
        hash_ctx hctx_salt;
        hash_init(&hctx_salt);
        hash_update(&hctx_salt, user_salt, user_salt_len);
        hash_final_digest(salt, _SALT_BYTE, &hctx_salt);
    } else {
        randombytes( salt, _SALT_BYTE );
    }
#else
    (void)user_salt; (void)user_salt_len;
#endif

    // The caller provides the digest directly.
    // If digest_len > max recoverable bytes, reject (would silently truncate).
    // If digest_len == needed, copy directly.
    // If digest_len < needed, hash it to expand/normalize.
    uint8_t hash_bytes[_PUB_M_BYTE];
#if _SALT_BYTE > 0
    size_t needed = _HASH_EFFECTIVE_BYTE;
#else
    size_t needed = _PUB_M_BYTE;
#endif
    if ( digest_len > needed ) {
        // Digest too long -- would be truncated and unrecoverable.
        // Reject to prevent silent data loss.
        return -2;
    }
    if ( digest_len == needed ) {
        memcpy( hash_bytes, digest, needed );
    } else {
        // Hash the digest to produce the needed bytes (expand short input)
        hash_ctx hctx_d;
        hash_init(&hctx_d);
        hash_update(&hctx_d, digest, digest_len);
        hash_final_digest( hash_bytes, needed, &hctx_d);
    }

    // Build target
    uint8_t y[_PUB_M_BYTE];
#if _SALT_BYTE > 0
    _build_target_from_hash( y, hash_bytes, salt );
#else
    _build_target_from_hash( y, hash_bytes, NULL );
#endif

    // Build vinegar seed from digest || salt
    uint8_t vseed[_PUB_M_BYTE];
    {
        hash_ctx hctx_vs;
        hash_init(&hctx_vs);
        hash_update(&hctx_vs, digest, digest_len);
#if _SALT_BYTE > 0
        hash_update(&hctx_vs, salt, _SALT_BYTE);
#endif
        hash_final_digest(vseed, sizeof(vseed), &hctx_vs);
    }

    return _ov_sign_target( signature, sk, y, vseed, sizeof(vseed) );
}


/// Compare hash portion of recovered digest against expected hash bytes.
/// The salt portion (last _SALT_BYTE bytes) is not checked.
static
int _ov_verify_hash( const unsigned char *expected_hash, const unsigned char *digest_ck ) {
    unsigned char cc = 0;
#if _SALT_BYTE > 0
    for (unsigned i = 0; i < _HASH_EFFECTIVE_BYTE; i++) {
#else
    for (unsigned i = 0; i < _PUB_M_BYTE; i++) {
#endif
        cc |= (digest_ck[i] ^ expected_hash[i]);
    }
    return (0 == cc) ? 0 : -1;
}


/// Verify by recovering digest from signature via P(w), then checking
/// that the hash portion matches H(message).
static
int _ov_verify_message( const uint8_t *message, size_t mlen, const unsigned char *digest_ck ) {
    unsigned char correct[_PUB_M_BYTE];
    hash_ctx hctx;
    hash_init(&hctx);
    hash_update(&hctx, message, mlen);
#if _SALT_BYTE > 0
    hash_final_digest(correct, _HASH_EFFECTIVE_BYTE, &hctx);
#else
    hash_final_digest(correct, _PUB_M_BYTE, &hctx);
#endif
    return _ov_verify_hash( correct, digest_ck );
}




#if !(defined(_OV_PKC) || defined(_OV_PKC_SKC)) || !defined(_SAVE_MEMORY_)
int ov_verify( const uint8_t *message, size_t mlen, const uint8_t *signature, const pk_t *pk ) {
    #if defined(_VALGRIND_)
    VALGRIND_MAKE_MEM_DEFINED(signature, OV_SIGNATUREBYTES );
    #endif
    unsigned char digest_ck[_PUB_M_BYTE];
    ov_publicmap( digest_ck, pk->pk, signature );

    return _ov_verify_message( message, mlen, digest_ck );
}

int ov_verify_digest( const uint8_t *digest, size_t digest_len,
                      const uint8_t *signature, const pk_t *pk ) {
    #if defined(_VALGRIND_)
    VALGRIND_MAKE_MEM_DEFINED(signature, OV_SIGNATUREBYTES );
    #endif
    unsigned char digest_ck[_PUB_M_BYTE];
    ov_publicmap( digest_ck, pk->pk, signature );

    // Prepare expected hash bytes (same logic as ov_sign_digest)
    unsigned char expected_hash[_PUB_M_BYTE];
#if _SALT_BYTE > 0
    size_t needed = _HASH_EFFECTIVE_BYTE;
#else
    size_t needed = _PUB_M_BYTE;
#endif
    if ( digest_len >= needed ) {
        memcpy( expected_hash, digest, needed );
    } else {
        hash_ctx hctx_d;
        hash_init(&hctx_d);
        hash_update(&hctx_d, digest, digest_len);
        hash_final_digest( expected_hash, needed, &hctx_d);
    }

    return _ov_verify_hash( expected_hash, digest_ck );
}
#endif


#if defined(_OV_PKC) || defined(_OV_PKC_SKC)
#if !defined(PQM4)
#define _MALLOC_
#endif

#if defined(_OV_PKC_SKC)
int ov_expand_and_sign( uint8_t *signature, const csk_t *csk, const uint8_t *message, size_t mlen ) {
    #ifdef _MALLOC_
    sk_t *sk = ov_malloc(sizeof(sk_t));
    if (NULL == sk) {
        return -1;
    }
    #else
    sk_t _sk;
    sk_t *sk = &_sk;
    #endif

    expand_sk( sk, csk->sk_seed );    // generating classic secret key.

    int r = ov_sign( signature, sk, message, mlen );

    #ifdef _MALLOC_
    ov_free(sk, sizeof(sk_t));
    #endif

    return r;
}
#endif

int ov_expand_and_verify( const uint8_t *message, size_t mlen, const uint8_t *signature, const cpk_t *cpk ) {

    #ifdef _SAVE_MEMORY_
    unsigned char digest_ck[_PUB_M_BYTE];
    ov_publicmap_pkc( digest_ck, cpk, signature );
    return _ov_verify_message( message, mlen, digest_ck );
    #else
    int rc;

    #ifdef _MALLOC_
    pk_t *pk = ov_malloc(sizeof(pk_t));
    if (NULL == pk) {
        return -1;
    }
    #else
    pk_t _pk;
    pk_t *pk = &_pk;
    #endif

    #if _GFSIZE == 16  && (defined(_BLAS_NEON_) || defined(_BLAS_M4F_))
    uint8_t xi[_PUB_N];
    for (int i = 0; i < _PUB_N; i++) {
        xi[i] = gfv_get_ele( signature, i );
    }
    expand_pk_predicate( pk, cpk, xi );
    #else
    expand_pk( pk, cpk );
    #endif
    rc = ov_verify( message, mlen, signature, pk );


    #ifdef _MALLOC_
    ov_free(pk, sizeof(pk_t));
    #endif
    return rc;
    #endif
}
#endif


