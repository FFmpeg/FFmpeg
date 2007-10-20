/*
 * ASF decryption
 * Copyright (c) 2007 Reimar Doeffinger
 * This is a rewrite of code contained in freeme/freeme2
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */
#include "common.h"
#include "intreadwrite.h"
#include "bswap.h"
#include "des.h"
#include "rc4.h"
#include "asfcrypt.h"

/**
 * \brief find multiplicative inverse modulo 2 ^ 32
 * \param v number to invert, must be odd!
 * \return number so that result * v = 1 (mod 2^32)
 */
static uint32_t inverse(uint32_t v) {
    // v ^ 3 gives the inverse (mod 16), could also be implemented
    // as table etc. (only lowest 4 bits matter!)
    uint32_t inverse = v * v * v;
    // uses a fixpoint-iteration that doubles the number
    // of correct lowest bits each time
    inverse *= 2 - v * inverse;
    inverse *= 2 - v * inverse;
    inverse *= 2 - v * inverse;
    return inverse;
}

/**
 * \brief read keys from keybuf into keys
 * \param keybuf buffer containing the keys
 * \param keys output key array containing the keys for encryption in
 *             native endianness
 */
static void multiswap_init(const uint8_t keybuf[48], uint32_t keys[12]) {
    int i;
    for (i = 0; i < 12; i++)
        keys[i] = AV_RL32(keybuf + (i << 2)) | 1;
}

/**
 * \brief invert the keys so that encryption become decryption keys and
 *        the other way round.
 * \param keys key array of ints to invert
 */
static void multiswap_invert_keys(uint32_t keys[12]) {
    int i;
    for (i = 0; i < 5; i++)
        keys[i] = inverse(keys[i]);
    for (i = 6; i < 11; i++)
        keys[i] = inverse(keys[i]);
}

static uint32_t multiswap_step(const uint32_t keys[12], uint32_t v) {
    int i;
    v *= keys[0];
    for (i = 1; i < 5; i++) {
        v = (v >> 16) | (v << 16);
        v *= keys[i];
    }
    v += keys[5];
    return v;
}

static uint32_t multiswap_inv_step(const uint32_t keys[12], uint32_t v) {
    int i;
    v -= keys[5];
    for (i = 4; i > 0; i--) {
        v *= keys[i];
        v = (v >> 16) | (v << 16);
    }
    v *= keys[0];
    return v;
}

/**
 * \brief "MultiSwap" encryption
 * \param keys 32 bit numbers in machine endianness,
 *             0-4 and 6-10 must be inverted from decryption
 * \param key another key, this one must be the same for the decryption
 * \param data data to encrypt
 * \return encrypted data
 */
static uint64_t multiswap_enc(const uint32_t keys[12], uint64_t key, uint64_t data) {
    uint32_t a = data;
    uint32_t b = data >> 32;
    uint32_t c;
    uint32_t tmp;
    a += key;
    tmp = multiswap_step(keys    , a);
    b += tmp;
    c = (key >> 32) + tmp;
    tmp = multiswap_step(keys + 6, b);
    c += tmp;
    return ((uint64_t)c << 32) | tmp;
}

/**
 * \brief "MultiSwap" decryption
 * \param keys 32 bit numbers in machine endianness,
 *             0-4 and 6-10 must be inverted from encryption
 * \param key another key, this one must be the same as for the encryption
 * \param data data to decrypt
 * \return decrypted data
 */
static uint64_t multiswap_dec(const uint32_t keys[12], uint64_t key, uint64_t data) {
    uint32_t a;
    uint32_t b;
    uint32_t c = data >> 32;
    uint32_t tmp = data;
    c -= tmp;
    b = multiswap_inv_step(keys + 6, tmp);
    tmp = c - (key >> 32);
    b -= tmp;
    a = multiswap_inv_step(keys    , tmp);
    a -= key;
    return ((uint64_t)b << 32) | a;
}

void ff_asfcrypt_dec(const uint8_t key[20], uint8_t *data, int len) {
    int num_qwords = len >> 3;
    uint64_t *qwords = (uint64_t *)data;
    uint64_t rc4buff[8];
    uint64_t packetkey;
    uint32_t ms_keys[12];
    uint64_t ms_state;
    int i;
    if (len < 16) {
        for (i = 0; i < len; i++)
            data[i] ^= key[i];
        return;
    }

    memset(rc4buff, 0, sizeof(rc4buff));
    ff_rc4_enc(key, 12, (uint8_t *)rc4buff, sizeof(rc4buff));
    multiswap_init((uint8_t *)rc4buff, ms_keys);

    packetkey = qwords[num_qwords - 1];
    packetkey ^= rc4buff[7];
    packetkey = be2me_64(packetkey);
    packetkey = ff_des_encdec(packetkey, AV_RB64(key + 12), 1);
    packetkey = be2me_64(packetkey);
    packetkey ^= rc4buff[6];

    ff_rc4_enc((uint8_t *)&packetkey, 8, data, len);

    ms_state = 0;
    for (i = 0; i < num_qwords - 1; i++, qwords++)
        ms_state = multiswap_enc(ms_keys, ms_state, AV_RL64(qwords));
    multiswap_invert_keys(ms_keys);
    packetkey = (packetkey << 32) | (packetkey >> 32);
    packetkey = le2me_64(packetkey);
    packetkey = multiswap_dec(ms_keys, ms_state, packetkey);
    AV_WL64(qwords, packetkey);
}
