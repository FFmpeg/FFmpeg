/*
 * Copyright (c) 2026 Lynne <dev@lynne.ee>
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

#version 460
#pragma shader_stage(compute)
#extension GL_GOOGLE_include_directive : require
#extension GL_KHR_shader_subgroup_basic : require
#extension GL_KHR_shader_subgroup_arithmetic : require
#extension GL_KHR_shader_subgroup_rotate : require
#extension GL_EXT_maximal_reconvergence : require

#define PB_UNALIGNED
#include "common.glsl"

#define APV_BLK_COEFFS 64

/*
 * One workgroup encodes one tile-component. The workgroup size (set by the
 * host via spec constants 253/254/255) equals the number of transform blocks
 * in the tile-component, so there is exactly one invocation per block.
 *
 * The minimum APV tile is 16x8 MBs -> 16*8*4 = 512 blocks. The buffers below
 * are sized for 1024 (a 2x tile). With a guaranteed subgroup size of >= 32,
 * a 1024-invocation workgroup has at most 1024/32 = 32 subgroups.
 */
#define MAX_BLOCKS    1024
#define MAX_SUBGROUPS 32

/* ff_zigzag_direct, packed: each byte is the raster index (y*8 + x). */
const uint8_t zigzag[64] = {
    uint8_t( 0), uint8_t( 1), uint8_t( 8), uint8_t(16),
    uint8_t( 9), uint8_t( 2), uint8_t( 3), uint8_t(10),
    uint8_t(17), uint8_t(24), uint8_t(32), uint8_t(25),
    uint8_t(18), uint8_t(11), uint8_t( 4), uint8_t( 5),
    uint8_t(12), uint8_t(19), uint8_t(26), uint8_t(33),
    uint8_t(40), uint8_t(48), uint8_t(41), uint8_t(34),
    uint8_t(27), uint8_t(20), uint8_t(13), uint8_t( 6),
    uint8_t( 7), uint8_t(14), uint8_t(21), uint8_t(28),
    uint8_t(35), uint8_t(42), uint8_t(49), uint8_t(56),
    uint8_t(57), uint8_t(50), uint8_t(43), uint8_t(36),
    uint8_t(29), uint8_t(22), uint8_t(15), uint8_t(23),
    uint8_t(30), uint8_t(37), uint8_t(44), uint8_t(51),
    uint8_t(58), uint8_t(59), uint8_t(52), uint8_t(45),
    uint8_t(38), uint8_t(31), uint8_t(39), uint8_t(46),
    uint8_t(53), uint8_t(60), uint8_t(61), uint8_t(54),
    uint8_t(47), uint8_t(55), uint8_t(62), uint8_t(63),
};

/* Coefficients are int16, accessed through a packed u32 view: the mask
 * build streams whole words, and the few nonzero values are extracted
 * on demand. */
layout (set = 0, binding = 0, scalar) readonly buffer coeffs_buf {
    uint32_t coeffs32[];
};

int coeff_at(uint base16, uint i)
{
    uint idx = base16 + i;
    uint w   = coeffs32[idx >> 1];
    return (int(w << ((1u - (idx & 1u)) << 4))) >> 16;
}

/*
 * Inverse zig-zag: zz_inv[raster] = scan position. Indexed only by
 * unrolled-constant indices, so it folds into immediates.
 */
const uint8_t zz_inv[64] = {
    uint8_t( 0), uint8_t( 1), uint8_t( 5), uint8_t( 6),
    uint8_t(14), uint8_t(15), uint8_t(27), uint8_t(28),
    uint8_t( 2), uint8_t( 4), uint8_t( 7), uint8_t(13),
    uint8_t(16), uint8_t(26), uint8_t(29), uint8_t(42),
    uint8_t( 3), uint8_t( 8), uint8_t(12), uint8_t(17),
    uint8_t(25), uint8_t(30), uint8_t(41), uint8_t(43),
    uint8_t( 9), uint8_t(11), uint8_t(18), uint8_t(24),
    uint8_t(31), uint8_t(40), uint8_t(44), uint8_t(53),
    uint8_t(10), uint8_t(19), uint8_t(23), uint8_t(32),
    uint8_t(39), uint8_t(45), uint8_t(52), uint8_t(54),
    uint8_t(20), uint8_t(22), uint8_t(33), uint8_t(38),
    uint8_t(46), uint8_t(51), uint8_t(55), uint8_t(60),
    uint8_t(21), uint8_t(34), uint8_t(37), uint8_t(47),
    uint8_t(50), uint8_t(56), uint8_t(59), uint8_t(61),
    uint8_t(35), uint8_t(36), uint8_t(48), uint8_t(49),
    uint8_t(57), uint8_t(58), uint8_t(62), uint8_t(63),
};

/* Zig-zag-domain nonzero map of one block: bit s = coefficient at scan
 * position s is nonzero. 32 sequential word loads, constant bit targets. */
uint64_t nz_mask(uint base16)
{
    uint64_t mask = uint64_t(0);
    uint b32 = base16 >> 1;
    [[unroll]]
    for (uint w = 0u; w < 32u; w++) {
        uint v = coeffs32[b32 + w];
        if ((v & 0xFFFFu) != 0u)
            mask |= uint64_t(1) << zz_inv[2u * w];
        if ((v >> 16) != 0u)
            mask |= uint64_t(1) << zz_inv[2u * w + 1u];
    }
    return mask;
}

/* Index of the lowest set bit; mask must be nonzero. */
int findLSB64(uint64_t m)
{
    u32vec2 h = unpack32(m);
    return (h.x != 0u) ? findLSB(h.x) : 32 + findLSB(h.y);
}

layout (set = 0, binding = 1, scalar) writeonly buffer sizes_buf {
    uint32_t tile_comp_sizes[];
};

layout (push_constant, scalar) uniform pushConstants {
    u8buf bytestream;       /* device address of the bytestream buffer */
    ivec2 tile_count;       /* number of tile columns/rows */
    int   num_comp;
    uint  slot_size;        /* per-tile-component bytestream slot size */
    uint  comp_base;        /* component index of this dispatch's z = 0 */
    uint  blocks_per_tile;  /* uniform coeff stride, in blocks */
    ivec2 frame_mb;         /* frame size in MBs (luma basis) */
    ivec2 tile_mb_dim;      /* full-tile size in MBs */
    uint  blocks_per_mb;    /* blocks per MB of this dispatch's components */
};

shared uint32_t sg_lasttail[MAX_SUBGROUPS]; /* last block's tail, per subgroup */
shared uint32_t sg_scan    [MAX_SUBGROUPS]; /* per-subgroup scan totals        */

/*
 * Flush only the complete bytes held by the writer. The trailing partial
 * byte is intentionally left unwritten: it is the block's tail, and the next
 * block writes it (prepended to its own first byte). flush_put_bits() rounds
 * the byte count up; this rounds it down.
 */
void flush_whole_bytes(inout PutBitContext pb)
{
    if (pb.bit_left < BUF_BITS)
        pb.bit_buf <<= pb.bit_left;

    uint to_write = uint(BUF_BITS - pb.bit_left) >> 3;

    u8buf bs = u8buf(pb.buf);
    for (int i = 0; i < to_write; i++)
        bs[i].v = BYTE_EXTRACT(pb.bit_buf, BUF_BYTES - uint8_t(1) - i);
    pb.buf = uint64_t(bs) + to_write;
}

/*
 * A block coder bundles the output writer, a running bit count, a rolling
 * copy of the last emitted bits, and a flag selecting whether to actually
 * write. The count and encode passes share the exact same code path, so the
 * counted length always matches the written one.
 */
struct BlockCoder {
    PutBitContext pb;
    uint nbits;
    uint roll;
    bool wr;
};

void emit(inout BlockCoder bc, uint32_t n, uint32_t value)
{
    bc.nbits += n;
    bc.roll   = (bc.roll << n) | value;   /* keeps the last >= 8 emitted bits */
    if (bc.wr)
        put_bits(bc.pb, n, value);
}

/* Variable-length code from the APV spec, section 7.2.4. */
void write_vlc(inout BlockCoder bc, int kParam, int symbolVal)
{
    int threshold1 = 1 << kParam;
    int threshold2 = threshold1 << 1;

    if (symbolVal < threshold1) {
        emit(bc, 1u, 1u);
        if (kParam > 0)
            emit(bc, uint(kParam), uint(symbolVal));
    } else if (symbolVal < threshold2) {
        emit(bc, 2u, 0u);
        if (kParam > 0)
            emit(bc, uint(kParam), uint(symbolVal - threshold1));
    } else {
        emit(bc, 2u, 1u);
        int adjusted = symbolVal - threshold1;
        int n_plus_k = findMSB(adjusted);
        int n = n_plus_k - kParam;
        if (n > 0)
            emit(bc, uint(n), 0u);
        emit(bc, 1u, 1u);
        int residual = adjusted - (1 << n_plus_k);
        if (n_plus_k > 0)
            emit(bc, uint(n_plus_k), uint(residual));
    }
}

/* abs() of the first non-zero AC coefficient in zig-zag order, 0 if none. */
int first_ac_level(uint base16, uint64_t mask)
{
    uint64_t mac = mask >> 1;
    if (mac == uint64_t(0))
        return 0;
    int sp = 1 + findLSB64(mac);
    return abs(coeff_at(base16, uint(zigzag[sp])));
}

/* Entropy-code one transform block given its predicted context. The nonzero
 * map drives the zero-runs, so only the nonzero values are ever loaded. */
void process_block(inout BlockCoder bc, uint base16, uint64_t mask,
                   int prev_dc, int prev_k_dc, int prev_1st_ac)
{
    /* DC */
    int dc = coeff_at(base16, 0u);
    int dc_diff = dc - prev_dc;
    int abs_dc = abs(dc_diff);
    write_vlc(bc, prev_k_dc, abs_dc);
    if (abs_dc != 0)
        emit(bc, 1u, dc_diff < 0 ? 1u : 0u);

    /* AC, zig-zag from position 1. m's bit 0 = scan position scan_pos. */
    uint64_t m = mask >> 1;
    int scan_pos = 1;
    int prev_level = prev_1st_ac;
    int prev_run = 0;

    while (scan_pos < APV_BLK_COEFFS) {
        int run = (m == uint64_t(0)) ? APV_BLK_COEFFS - scan_pos
                                     : findLSB64(m);

        int k_run = clamp(prev_run >> 2, 0, 2);
        write_vlc(bc, k_run, run);

        scan_pos += run;
        prev_run = run;

        if (scan_pos < APV_BLK_COEFFS) {
            int level = coeff_at(base16, uint(zigzag[scan_pos]));
            int abs_level_m1 = abs(level) - 1;
            int k_level = clamp(prev_level >> 2, 0, 4);
            write_vlc(bc, k_level, abs_level_m1);
            emit(bc, 1u, level < 0 ? 1u : 0u);

            prev_level = abs_level_m1 + 1;
            scan_pos++;
            m >>= run + 1;
        }
    }
}

/* DC predictor context for block 'idx' -- pure function of preceding DCs. */
void dc_context(uint coeff_base, uint idx, out int prev_dc, out int prev_k_dc)
{
    if (idx == 0u) {
        prev_dc   = 0;
        prev_k_dc = 5;
        return;
    }
    uint base = coeff_base + idx * APV_BLK_COEFFS;
    int dc_m1 = coeff_at(base - APV_BLK_COEFFS, 0u);
    int dc_m2 = (idx == 1u) ? 0 : coeff_at(base - 2u * APV_BLK_COEFFS, 0u);
    prev_dc   = dc_m1;
    prev_k_dc = min(abs(dc_m1 - dc_m2) >> 1, 5);
}

void main(void) [[maximally_reconverges]]
{
    const uint b         = gl_LocalInvocationID.x;
    const uint comp      = comp_base + gl_WorkGroupID.z;
    const uint tile_idx  = gl_WorkGroupID.y * uint(tile_count.x) + gl_WorkGroupID.x;
    const uint tile_comp = tile_idx * uint(num_comp) + comp;

    /*
     * Remainder tiles: the rightmost column / bottom row of tiles may be
     * smaller than the full tile the workgroup was sized for. Invocations at
     * b >= nb stay for the barriers and scans (contributing zeros) but never
     * read coefficients or write bitstream. Active blocks are always the
     * contiguous prefix [0, nb), so the tail-handoff chain below never
     * crosses an inactive->active boundary.
     */
    const int actual_tw = min(tile_mb_dim.x,
                              frame_mb.x - int(gl_WorkGroupID.x) * tile_mb_dim.x);
    const int actual_th = min(tile_mb_dim.y,
                              frame_mb.y - int(gl_WorkGroupID.y) * tile_mb_dim.y);
    const uint nb = uint(actual_tw * actual_th) * blocks_per_mb;

    /*
     * This tile-component's coefficient region. The coeff buffer uses a
     * uniform per-tile-component stride (the luma block count); a chroma
     * tile-component simply reads fewer blocks from its region.
     */
    const uint coeff_base = tile_comp * blocks_per_tile * APV_BLK_COEFFS;
    const uint blk        = coeff_base + b * APV_BLK_COEFFS;

    int prev_dc = 0, prev_k_dc = 5;
    uint64_t mask = uint64_t(0);
    if (b < nb) {
        dc_context(coeff_base, b, prev_dc, prev_k_dc);
        mask = nz_mask(blk);
    }

    /*
     * Scan #1: prev_1st_ac_level is the first-AC level of the most recent
     * *non-empty* block before this one. Pack (blockIndex+1, level) into a
     * key (empty blocks -> 0) so a plain Max picks the highest-index prior
     * non-empty block. Two levels: subgroup scan, then across subgroups.
     */
    int my_first_ac = (b < nb) ? first_ac_level(blk, mask) : 0;
    uint key = (my_first_ac == 0) ? 0u
                                  : (((b + 1u) << 16) | uint(my_first_ac));

    uint key_excl  = subgroupExclusiveMax(key);
    uint key_total = subgroupMax(key);
    if (subgroupElect())
        sg_scan[gl_SubgroupID] = key_total;
    barrier();

    uint carry = key_excl;
    for (uint i = 0u; i < gl_NumSubgroups; i++) {
        if (i >= gl_SubgroupID)
            break;
        carry = max(carry, sg_scan[i]);
    }
    int prev_1st_ac = (carry == 0u) ? 0 : int(carry & 0xFFFFu);
    barrier();   /* sg_scan is reused by scan #2 */

    /*
     * Count pass: measure this block's coded length, and -- for free, since
     * we walk every emitted bit anyway -- keep a rolling copy of the last
     * bits in cnt.roll.
     */
    BlockCoder cnt;
    cnt.nbits = 0u;
    cnt.roll  = 0u;
    cnt.wr    = false;
    if (b < nb)
        process_block(cnt, blk, mask, prev_dc, prev_k_dc, prev_1st_ac);

    /*
     * Scan #2: exclusive prefix sum of the bit counts gives each block's
     * start offset (in bits) within the tile-component bitstream.
     */
    uint bits_excl  = subgroupExclusiveAdd(cnt.nbits);
    uint bits_total = subgroupAdd(cnt.nbits);
    if (subgroupElect())
        sg_scan[gl_SubgroupID] = bits_total;
    barrier();

    uint off = bits_excl;
    for (uint i = 0u; i < gl_NumSubgroups; i++) {
        if (i >= gl_SubgroupID)
            break;
        off += sg_scan[i];
    }

    /*
     * This block's tail = its last (end_bit & 7) bits. The next block
     * prepends these so its own writer starts on a byte boundary; that makes
     * every block write a disjoint run of whole bytes -- no atomics, no
     * shared bytes. end_bit is known only now (after scan #2).
     *
     * The predecessor's tail is just the left-neighbour lane, so a subgroup
     * rotate fetches it from a register; only at a subgroup boundary (lane 0)
     * does it fall back to shared memory -- the previous subgroup's last block.
     */
    uint end_bit = off + cnt.nbits;
    uint tail_n  = end_bit & 7u;
    uint my_tail = cnt.roll & ((1u << tail_n) - 1u);

    uint prev_tail = subgroupRotate(my_tail, gl_SubgroupSize - 1u);
    if (gl_SubgroupInvocationID == gl_SubgroupSize - 1u)
        sg_lasttail[gl_SubgroupID] = my_tail;
    barrier();
    if (gl_SubgroupInvocationID == 0u && gl_SubgroupID > 0u)
        prev_tail = sg_lasttail[gl_SubgroupID - 1u];

    /* Write pass: each block writes bytes [off>>3, end_bit>>3). Inactive
     * remainder-tile invocations write nothing. */
    if (b >= nb)
        return;

    BlockCoder enc;
    init_put_bits(enc.pb,
                  OFFBUF(u8buf, bytestream, tile_comp * slot_size + (off >> 3u)),
                  uint64_t(slot_size));
    enc.nbits = 0u;
    enc.roll  = 0u;
    enc.wr    = true;

    /* Prepend the predecessor's tail so this block's first byte comes out
     * complete. (off & 7 == 0 for block 0, which has no predecessor.) */
    if (b > 0u)
        put_bits(enc.pb, off & 7u, prev_tail);

    process_block(enc, blk, mask, prev_dc, prev_k_dc, prev_1st_ac);

    if (b == nb - 1u) {
        /* Last block: no successor, so flush the trailing partial byte too
         * (the tile-component is byte-aligned, zero-padded), and record the
         * total size. */
        flush_put_bits(enc.pb);
        tile_comp_sizes[tile_comp] = (end_bit + 7u) >> 3u;
    } else {
        /* Leave the trailing partial byte for the next block to prepend. */
        flush_whole_bytes(enc.pb);
    }
}
