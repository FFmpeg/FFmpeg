/*
 * copyright (c) 2006 Michael Niedermayer <michaelni@gmx.at>
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

#include "config.h"
#include "common.h"
#include "bswap.h"
#include "crc.h"

#if CONFIG_HARDCODED_TABLES
#include "crc_data.h"
#else
static struct {
    uint8_t  le;
    uint8_t  bits;
    uint32_t poly;
} av_crc_table_params[AV_CRC_MAX] = {
    [AV_CRC_8_ATM]      = { 0,  8,       0x07 },
    [AV_CRC_16_ANSI]    = { 0, 16,     0x8005 },
    [AV_CRC_16_CCITT]   = { 0, 16,     0x1021 },
    [AV_CRC_32_IEEE]    = { 0, 32, 0x04C11DB7 },
    [AV_CRC_32_IEEE_LE] = { 1, 32, 0xEDB88320 },
};
static AVCRC av_crc_table[AV_CRC_MAX][257];
#endif

/**
 * Initialize a CRC table.
 * @param ctx must be an array of size sizeof(AVCRC)*257 or sizeof(AVCRC)*1024
 * @param le If 1, the lowest bit represents the coefficient for the highest
 *           exponent of the corresponding polynomial (both for poly and
 *           actual CRC).
 *           If 0, you must swap the CRC parameter and the result of av_crc
 *           if you need the standard representation (can be simplified in
 *           most cases to e.g. bswap16):
 *           av_bswap32(crc << (32-bits))
 * @param bits number of bits for the CRC
 * @param poly generator polynomial without the x**bits coefficient, in the
 *             representation as specified by le
 * @param ctx_size size of ctx in bytes
 * @return <0 on failure
 */
int av_crc_init(AVCRC *ctx, int le, int bits, uint32_t poly, int ctx_size){
    unsigned i, j;
    uint32_t c;

    if (bits < 8 || bits > 32 || poly >= (1LL<<bits))
        return -1;
    if (ctx_size != sizeof(AVCRC)*257 && ctx_size != sizeof(AVCRC)*1024)
        return -1;

    for (i = 0; i < 256; i++) {
        if (le) {
            for (c = i, j = 0; j < 8; j++)
                c = (c>>1)^(poly & (-(c&1)));
            ctx[i] = c;
        } else {
            for (c = i << 24, j = 0; j < 8; j++)
                c = (c<<1) ^ ((poly<<(32-bits)) & (((int32_t)c)>>31) );
            ctx[i] = av_bswap32(c);
        }
    }
    ctx[256]=1;
#if !CONFIG_SMALL
    if(ctx_size >= sizeof(AVCRC)*1024)
        for (i = 0; i < 256; i++)
            for(j=0; j<3; j++)
                ctx[256*(j+1) + i]= (ctx[256*j + i]>>8) ^ ctx[ ctx[256*j + i]&0xFF ];
#endif

    return 0;
}

/**
 * Get an initialized standard CRC table.
 * @param crc_id ID of a standard CRC
 * @return a pointer to the CRC table or NULL on failure
 */
const AVCRC *av_crc_get_table(AVCRCId crc_id){
#if !CONFIG_HARDCODED_TABLES
    if (!av_crc_table[crc_id][FF_ARRAY_ELEMS(av_crc_table[crc_id])-1])
        if (av_crc_init(av_crc_table[crc_id],
                        av_crc_table_params[crc_id].le,
                        av_crc_table_params[crc_id].bits,
                        av_crc_table_params[crc_id].poly,
                        sizeof(av_crc_table[crc_id])) < 0)
            return NULL;
#endif
    return av_crc_table[crc_id];
}

/**
 * Calculate the CRC of a block.
 * @param crc CRC of previous blocks if any or initial value for CRC
 * @return CRC updated with the data from the given block
 *
 * @see av_crc_init() "le" parameter
 */
uint32_t av_crc(const AVCRC *ctx, uint32_t crc, const uint8_t *buffer, size_t length){
    const uint8_t *end= buffer+length;

#if !CONFIG_SMALL
    if(!ctx[256]) {
        while(((intptr_t) buffer & 3) && buffer < end)
            crc = ctx[((uint8_t)crc) ^ *buffer++] ^ (crc >> 8);

        while(buffer<end-3){
            crc ^= av_le2ne32(*(const uint32_t*)buffer); buffer+=4;
            crc =  ctx[3*256 + ( crc     &0xFF)]
                  ^ctx[2*256 + ((crc>>8 )&0xFF)]
                  ^ctx[1*256 + ((crc>>16)&0xFF)]
                  ^ctx[0*256 + ((crc>>24)     )];
        }
    }
#endif
    while(buffer<end)
        crc = ctx[((uint8_t)crc) ^ *buffer++] ^ (crc >> 8);

    return crc;
}

#ifdef TEST
#undef printf
int main(void){
    uint8_t buf[1999];
    int i;
    int p[4][3]={{AV_CRC_32_IEEE_LE, 0xEDB88320, 0x3D5CDD04},
                 {AV_CRC_32_IEEE   , 0x04C11DB7, 0xC0F5BAE0},
                 {AV_CRC_16_ANSI   , 0x8005,     0x1FBB    },
                 {AV_CRC_8_ATM     , 0x07,       0xE3      },};
    const AVCRC *ctx;

    for(i=0; i<sizeof(buf); i++)
        buf[i]= i+i*i;

    for(i=0; i<4; i++){
        ctx = av_crc_get_table(p[i][0]);
        printf("crc %08X =%X\n", p[i][1], av_crc(ctx, 0, buf, sizeof(buf)));
    }
    return 0;
}
#endif
