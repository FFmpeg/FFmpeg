/*
 * An implementation of the CAMELLIA algorithm as mentioned in RFC3713
 * Copyright (c) 2014 Supraja Meedinti
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

#include "libavutil/camellia.h"
#include "libavutil/log.h"

int main(int argc, char *argv[])
{
    const uint8_t Key[3][32] = {
        {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10},
        {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77},
        {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff}
    };
    const uint8_t rct[3][16] = {
        {0x67, 0x67, 0x31, 0x38, 0x54, 0x96, 0x69, 0x73, 0x08, 0x57, 0x06, 0x56, 0x48, 0xea, 0xbe, 0x43},
        {0xb4, 0x99, 0x34, 0x01, 0xb3, 0xe9,0x96, 0xf8, 0x4e, 0xe5, 0xce, 0xe7, 0xd7, 0x9b, 0x09, 0xb9},
        {0x9a, 0xcc, 0x23, 0x7d, 0xff, 0x16, 0xd7, 0x6c, 0x20, 0xef, 0x7c, 0x91, 0x9e, 0x3a, 0x75, 0x09}
    };
    const uint8_t rpt[32] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10};
    const int kbits[3] = {128, 192, 256};
    int i, j, err = 0;
    uint8_t temp[32], iv[16];
    struct AVCAMELLIA *cs;
    cs = av_camellia_alloc();
    if (!cs)
        return 1;
    for (j = 0; j < 3; j++) {
        av_camellia_init(cs, Key[j], kbits[j]);
        av_camellia_crypt(cs, temp, rpt, 1, NULL, 0);
        for (i = 0; i < 16; i++) {
            if (rct[j][i] != temp[i]) {
                av_log(NULL, AV_LOG_ERROR, "%d %02x %02x\n", i, rct[j][i], temp[i]);
                err = 1;
            }
        }
        av_camellia_crypt(cs, temp, rct[j], 1, NULL, 1);
        for (i = 0; i < 16; i++) {
            if (rpt[i] != temp[i]) {
                av_log(NULL, AV_LOG_ERROR, "%d %02x %02x\n", i, rpt[i], temp[i]);
                err = 1;
            }
        }
    }
    av_camellia_init(cs, Key[0], 128);
    memcpy(iv, "HALLO123HALLO123", 16);
    av_camellia_crypt(cs, temp, rpt, 2, iv, 0);
    memcpy(iv, "HALLO123HALLO123", 16);
    av_camellia_crypt(cs, temp, temp, 2, iv, 1);
    for (i = 0; i < 32; i++) {
        if (rpt[i] != temp[i]) {
            av_log(NULL, AV_LOG_ERROR, "%d %02x %02x\n", i, rpt[i], temp[i]);
            err = 1;
        }
    }
    av_free(cs);
    return err;
}
