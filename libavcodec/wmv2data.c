/*
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdint.h>

#include "wmv2data.h"

const uint8_t ff_wmv2_scantableA[64] = {
    0x00, 0x01, 0x02, 0x08, 0x03, 0x09, 0x0A, 0x10,
    0x04, 0x0B, 0x11, 0x18, 0x12, 0x0C, 0x05, 0x13,
    0x19, 0x0D, 0x14, 0x1A, 0x1B, 0x06, 0x15, 0x1C,
    0x0E, 0x16, 0x1D, 0x07, 0x1E, 0x0F, 0x17, 0x1F,
};

const uint8_t ff_wmv2_scantableB[64] = {
    0x00, 0x08, 0x01, 0x10, 0x09, 0x18, 0x11, 0x02,
    0x20, 0x0A, 0x19, 0x28, 0x12, 0x30, 0x21, 0x1A,
    0x38, 0x29, 0x22, 0x03, 0x31, 0x39, 0x0B, 0x2A,
    0x13, 0x32, 0x1B, 0x3A, 0x23, 0x2B, 0x33, 0x3B,
};
