#ifndef AES128_H
#define AES128_H

#ifdef CONFIG_GCRYPT
#include <gcrypt.h>
typedef struct {
    gcry_cipher_hd_t ch;
} AES128Context;
#else
typedef struct {
    uint32_t multbl[4][256];
    uint8_t subst[256];
    uint8_t key[11][16];
} AES128Context;
#endif
AES128Context *aes128_init(void);
void aes128_set_key(AES128Context *c, const uint8_t *key);
void aes128_cbc_decrypt(AES128Context *c, uint8_t *mem, int blockcnt, uint8_t *IV);

#endif
