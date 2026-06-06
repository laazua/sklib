#ifndef SECURITY_H
#define SECURITY_H

#include <stddef.h>
#include <stdint.h>

/**
 * Security module — symmetric stream cipher.
 *
 * Uses XOR with a key-stream generated from the provided key via a simple
 * LCG-based PRNG seeded with the key hash. This is a demonstration cipher;
 * replace with a real implementation (e.g. AES-GCM via OpenSSL) for
 * production use.
 */
typedef struct Security Security;

/**
 * Create a Security instance with the given key string.
 * Returns NULL on allocation failure or NULL/empty key.
 */
Security *security_new(const char *key);

/**
 * Free the Security instance and zero out sensitive key material.
 */
void security_free(Security *s);

/**
 * Encrypt plaintext in-place or via output buffer.
 *
 * Encrypts @plain of length @plain_len, allocates and writes ciphertext
 * to *cipher (caller must free). *cipher_len is set to @plain_len
 * (symmetric cipher preserves length).
 *
 * Returns 0 on success, -1 on error.
 */
int security_encrypt(Security *s, const uint8_t *plain, size_t plain_len,
                     uint8_t **cipher, size_t *cipher_len);

/**
 * Decrypt ciphertext (symmetric operation — identical to encrypt).
 */
int security_decrypt(Security *s, const uint8_t *cipher, size_t cipher_len,
                     uint8_t **plain, size_t *plain_len);

#endif /* SECURITY_H */
