/*
 * An implementation of the TwoFish algorithm
 * Copyright (c) 2015 Supraja Meedinti
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

#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/twofish.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[])
{
    uint8_t Key[32] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff
    };
    const uint8_t rct[6][16] = {
        {0x9f, 0x58, 0x9f, 0x5c, 0xf6, 0x12, 0x2c, 0x32, 0xb6, 0xbf, 0xec, 0x2f, 0x2a, 0xe8, 0xc3, 0x5a},
        {0xcf, 0xd1, 0xd2, 0xe5, 0xa9, 0xbe, 0x9c, 0xdf, 0x50, 0x1f, 0x13, 0xb8, 0x92, 0xbd, 0x22, 0x48},
        {0x37, 0x52, 0x7b, 0xe0, 0x05, 0x23, 0x34, 0xb8, 0x9f, 0x0c, 0xfc, 0xca, 0xe8, 0x7c, 0xfa, 0x20},
        {0x5d, 0x9d, 0x4e, 0xef, 0xfa, 0x91, 0x51, 0x57, 0x55, 0x24, 0xf1, 0x15, 0x81, 0x5a, 0x12, 0xe0},
        {0xe7, 0x54, 0x49, 0x21, 0x2b, 0xee, 0xf9, 0xf4, 0xa3, 0x90, 0xbd, 0x86, 0x0a, 0x64, 0x09, 0x41},
        {0x37, 0xfe, 0x26, 0xff, 0x1c, 0xf6, 0x61, 0x75, 0xf5, 0xdd, 0xf4, 0xc3, 0x3b, 0x97, 0xa2, 0x05}
    };
    uint8_t temp[32], iv[16], rpt[32] = {0};
    const int kbits[3] = {128, 192, 256};
    int i, j, k, err = 0;
    struct AVTWOFISH *cs;
    cs = av_twofish_alloc();
    if (!cs)
        return 1;
    for (j = 1; j < 3; j++) {
        av_twofish_init(cs, Key, kbits[j]);
        av_twofish_crypt(cs, temp, rpt, 1, NULL, 0);
        for (i = 0; i < 16; i++) {
            if (rct[j][i] != temp[i]) {
                av_log(NULL, AV_LOG_ERROR, "%d %02x %02x\n", i, rct[j][i], temp[i]);
                err = 1;
            }
        }
        av_twofish_crypt(cs, temp, rct[j], 1, NULL, 1);
        for (i = 0; i < 16; i++) {
            if (rpt[i] != temp[i]) {
                av_log(NULL, AV_LOG_ERROR, "%d %02x %02x\n", i, rpt[i], temp[i]);
                err = 1;
            }
        }
    }
    for (j = 0; j < 3; j++) {
        memset(Key, 0, sizeof(Key));
        memset(rpt, 0, sizeof(rpt));
        for (i = 1; i < 50; i++) {
            av_twofish_init(cs, Key, kbits[j]);
            av_twofish_crypt(cs, temp, rpt, 1, NULL, 0);
            memcpy(Key+16,Key,(kbits[j]-128) >> 3);
            memcpy(Key,rpt,16);
            memcpy(rpt,temp,16);
            av_twofish_crypt(cs, temp, temp, 1, NULL, 1);
            for (k = 0; k < 16; k++) {
                // Need to compare to Key here, because the plaintext comes
                // from rpt but was moved over to Key.
                if (Key[k] != temp[k]) {
                    av_log(NULL, AV_LOG_ERROR, "%d %02x %02x\n", k, Key[k], temp[k]);
                    err = 1;
                }
            }
        }
        for (i = 0; i < 16; i++) {
            if (rct[3 + j][i] != rpt[i]) {
                av_log(NULL, AV_LOG_ERROR, "%d %02x %02x\n", i, rct[3 + j][i], rpt[i]);
                err = 1;
            }
        }
    }
    memset(rpt, 0, sizeof(rpt));
    memcpy(iv, "HALLO123HALLO123", 16);
    av_twofish_crypt(cs, temp, rpt, 2, iv, 0);
    memcpy(iv, "HALLO123HALLO123", 16);
    av_twofish_crypt(cs, temp, temp, 2, iv, 1);
    for (i = 0; i < 32; i++) {
        if (rpt[i] != temp[i]) {
            av_log(NULL, AV_LOG_ERROR, "%d %02x %02x\n", i, rpt[i], temp[i]);
            err = 1;
        }
    }
    av_free(cs);
    return err;
}
