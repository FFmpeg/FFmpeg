/*
 * Realmedia RTSP protocol (RDT) support.
 * Copyright (c) 2007 Ronald S. Bultje
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

/**
 * @file rdt.c
 * @brief Realmedia RTSP protocol (RDT) support
 * @author Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 */

#include "avformat.h"
#include "libavutil/avstring.h"
#include "rdt.h"
#include "libavutil/base64.h"
#include "libavutil/md5.h"
#include "rm.h"
#include "internal.h"

void
ff_rdt_calc_response_and_checksum(char response[41], char chksum[9],
                                  const char *challenge)
{
    int ch_len = strlen (challenge), i;
    unsigned char zres[16],
        buf[64] = { 0xa1, 0xe9, 0x14, 0x9d, 0x0e, 0x6b, 0x3b, 0x59 };
#define XOR_TABLE_SIZE 37
    const unsigned char xor_table[XOR_TABLE_SIZE] = {
        0x05, 0x18, 0x74, 0xd0, 0x0d, 0x09, 0x02, 0x53,
        0xc0, 0x01, 0x05, 0x05, 0x67, 0x03, 0x19, 0x70,
        0x08, 0x27, 0x66, 0x10, 0x10, 0x72, 0x08, 0x09,
        0x63, 0x11, 0x03, 0x71, 0x08, 0x08, 0x70, 0x02,
        0x10, 0x57, 0x05, 0x18, 0x54 };

    /* some (length) checks */
    if (ch_len == 40) /* what a hack... */
        ch_len = 32;
    else if (ch_len > 56)
        ch_len = 56;
    memcpy(buf + 8, challenge, ch_len);

    /* xor challenge bytewise with xor_table */
    for (i = 0; i < XOR_TABLE_SIZE; i++)
        buf[8 + i] ^= xor_table[i];

    av_md5_sum(zres, buf, 64);
    ff_data_to_hex(response, zres, 16);
    for (i=0;i<32;i++) response[i] = tolower(response[i]);

    /* add tail */
    strcpy (response + 32, "01d0a8e3");

    /* calculate checksum */
    for (i = 0; i < 8; i++)
        chksum[i] = response[i * 4];
    chksum[8] = 0;
}
