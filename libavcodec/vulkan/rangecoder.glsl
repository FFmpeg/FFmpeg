/*
 * FFv1 codec
 *
 * Copyright (c) 2024 Lynne <dev@lynne.ee>
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

#ifndef VULKAN_RANGECODER_H
#define VULKAN_RANGECODER_H

#define CONTEXT_SIZE 32
#define MAX_OVERREAD 2

#if !defined(GOLOMB) && (defined(DECODE))
#define RC_BTYPE readonly buffer
#else
#define RC_BTYPE uniform
#endif

layout (set = 0, binding = 0, scalar) RC_BTYPE rangecoder_buf {
    uint8_t zero_one_state[512];
};

struct RangeCoder {
    uint     bs_start;
    uint     bs_off;
    uint     bs_end;
    uint     low;
    uint     range;
    uint16_t outstanding_count;
    uint8_t  outstanding_byte;
};

shared RangeCoder rc;
shared uint8_t rc_state[CONTEXT_SIZE];
shared bool rc_data[CONTEXT_SIZE];
shared bool rc_dec[CONTEXT_SIZE];

void rac_init(uint bs_start, uint bs_len)
{
    rc.bs_start = bs_start;
    rc.bs_off = bs_start;
    rc.bs_end = bs_start + bs_len;
    rc.low = 0;
    rc.range = 0xFF00;
    rc.outstanding_count = uint16_t(0);
    rc.outstanding_byte = uint8_t(0xFF);
}

#ifdef FULL_RENORM
/* Full renorm version that can handle outstanding_byte == 0xFF */
void renorm_encoder(void)
{
    if (rc.outstanding_byte == 0xFF) {
        rc.outstanding_byte = uint8_t(rc.low >> 8);
    } else if (rc.low <= 0xFF00) {
        slice_data[rc.bs_off++].v = rc.outstanding_byte;
        uint16_t cnt = rc.outstanding_count;
        for (; cnt > 0; cnt--)
            slice_data[rc.bs_off++].v = uint8_t(0xFF);
        rc.outstanding_count = uint16_t(0);
        rc.outstanding_byte = uint8_t(rc.low >> 8);
    } else if (rc.low >= 0x10000) {
        slice_data[rc.bs_off++].v = rc.outstanding_byte + uint8_t(1);
        uint16_t cnt = rc.outstanding_count;
        for (; cnt > 0; cnt--)
            slice_data[rc.bs_off++].v = uint8_t(0x00);
        rc.outstanding_count = uint16_t(0);
        rc.outstanding_byte = uint8_t(bitfieldExtract(rc.low, 8, 8));
    } else {
        rc.outstanding_count++;
    }

    rc.range <<= 8;
    rc.low = bitfieldInsert(0, rc.low, 8, 8);
}

#else

/* Cannot deal with outstanding_byte == -1 in the name of speed */
void renorm_encoder(void)
{
    uint16_t oc = rc.outstanding_count + uint16_t(1);
    uint low = rc.low;

    rc.range <<= 8;
    rc.low = bitfieldInsert(0, low, 8, 8);

    if (low > 0xFF00 && low < 0x10000) {
        rc.outstanding_count = oc;
        return;
    }

    uint8_t outstanding_byte = rc.outstanding_byte;

    rc.outstanding_count = uint16_t(0);
    rc.outstanding_byte  = uint8_t(low >> 8);

    uint8_t obs = uint8_t(low > 0xFF00);
    uint8_t fill = obs - uint8_t(1); /* unsigned underflow */

    slice_data[rc.bs_off++].v = outstanding_byte + obs;
    for (int i = 1; i < oc; i++)
        slice_data[rc.bs_off++].v = fill;
}
#endif

void put_rac_internal(const uint range1, bool bit)
{
#ifdef DEBUG
    if (range1 >= rc.range)
        debugPrintfEXT("Error: range1 >= range");
    if (range1 <= 0)
        debugPrintfEXT("Error: range1 <= 0");
#endif

    uint ranged = rc.range - range1;
    rc.low += bit ? ranged : 0;
    rc.range = bit ? range1 : ranged;

    if (expectEXT(rc.range < 0x100, false))
        renorm_encoder();
}

void put_rac(inout uint8_t state, bool bit)
{
    put_rac_internal((rc.range * state) >> 8, bit);
    state = zero_one_state[(uint(bit) << 8) + state];
}

void put_rac_equi(bool bit)
{
    put_rac_internal(rc.range >> 1, bit);
}

void put_rac_terminate(void)
{
    uint range1 = (rc.range * 129) >> 8;

#ifdef DEBUG
    if (range1 >= rc.range)
        debugPrintfEXT("Error: range1 >= c.range");
    if (range1 <= 0)
        debugPrintfEXT("Error: range1 <= 0");
#endif

    rc.range -= range1;
    if (expectEXT(rc.range < 0x100, false))
        renorm_encoder();
}

/* Return the number of bytes written. */
uint rac_terminate(void)
{
    put_rac_terminate();
    rc.range = uint16_t(0xFF);
    rc.low  += 0xFF;
    renorm_encoder();
    rc.range = uint16_t(0xFF);
    renorm_encoder();

#ifdef DEBUG
    if (rc.low != 0)
        debugPrintfEXT("Error: low != 0");
    if (rc.range < 0x100)
        debugPrintfEXT("Error: range < 0x100");
#endif

    return rc.bs_off - rc.bs_start;
}

void rac_init_dec(uint bs_start, uint bs_len)
{
    /* Skip priming bytes */
    rac_init(bs_start + 2, bs_len - 2);

    u8vec2 prime = u8vec2buf(slice_data + bs_start).v;
    /* Switch endianness of the priming bytes */
    rc.low = pack16(prime.yx);

    if (rc.low >= 0xFF00) {
        rc.low = 0xFF00;
        rc.bs_end = bs_start + 2;
    }
}

void refill(void)
{
    rc.range <<= 8;
    rc.low   <<= 8;
    if (expectEXT(rc.bs_off < rc.bs_end, true))
        rc.low |= slice_data[rc.bs_off].v;
    rc.bs_off++;
}

bool get_rac_internal(const uint range1)
{
    uint ranged = rc.range - range1;
    bool bit = rc.low >= ranged;
    rc.low -= bit ? ranged : 0;
    rc.range = (bit ? 0 : ranged) + (bit ? range1 : 0);

    if (expectEXT(rc.range < 0x100, false))
        refill();

    return bit;
}

bool get_rac(inout uint8_t state)
{
    bool bit = get_rac_internal(rc.range * state >> 8);
    state = zero_one_state[state + (bit ? 256 : 0)];
    return bit;
}

bool get_rac_state(uint idx)
{
    rc_dec[idx] = true;
    return (rc_data[idx] = get_rac_internal(rc.range * rc_state[idx] >> 8));
}

bool get_rac_equi(void)
{
    return get_rac_internal(rc.range >> 1);
}

#endif /* VULKAN_RANGECODER_H */
