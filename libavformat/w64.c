/*
 * Copyright (c) 2009 Daniel Verkamp
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

#include "w64.h"

const uint8_t ff_w64_guid_riff[16] = {
    'r', 'i', 'f', 'f',
    0x2E, 0x91, 0xCF, 0x11, 0xA5, 0xD6, 0x28, 0xDB, 0x04, 0xC1, 0x00, 0x00
};

const uint8_t ff_w64_guid_wave[16] = {
    'w', 'a', 'v', 'e',
    0xF3, 0xAC, 0xD3, 0x11, 0x8C, 0xD1, 0x00, 0xC0, 0x4F, 0x8E, 0xDB, 0x8A
};

const uint8_t ff_w64_guid_fmt [16] = {
    'f', 'm', 't', ' ',
    0xF3, 0xAC, 0xD3, 0x11, 0x8C, 0xD1, 0x00, 0xC0, 0x4F, 0x8E, 0xDB, 0x8A
};

const uint8_t ff_w64_guid_fact[16] = { 'f', 'a', 'c', 't',
    0xF3, 0xAC, 0xD3, 0x11, 0x8C, 0xD1, 0x00, 0xC0, 0x4F, 0x8E, 0xDB, 0x8A
};

const uint8_t ff_w64_guid_data[16] = {
    'd', 'a', 't', 'a',
    0xF3, 0xAC, 0xD3, 0x11, 0x8C, 0xD1, 0x00, 0xC0, 0x4F, 0x8E, 0xDB, 0x8A
};

const uint8_t ff_w64_guid_summarylist[16] = {
    0xBC, 0x94, 0x5F, 0x92,
    0x5A, 0x52, 0xD2, 0x11, 0x86, 0xDC, 0x00, 0xC0, 0x4F, 0x8E, 0xDB, 0x8A
};
