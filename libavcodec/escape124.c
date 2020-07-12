/*
 * Escape 124 Video Decoder
 * Copyright (C) 2008 Eli Friedman (eli.friedman@gmail.com)
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

#define BITSTREAM_READER_LE
#include "avcodec.h"
#include "get_bits.h"
#include "internal.h"

typedef union MacroBlock {
    uint16_t pixels[4];
    uint32_t pixels32[2];
} MacroBlock;

typedef union SuperBlock {
    uint16_t pixels[64];
    uint32_t pixels32[32];
} SuperBlock;

typedef struct CodeBook {
    unsigned depth;
    unsigned size;
    MacroBlock* blocks;
} CodeBook;

typedef struct Escape124Context {
    AVFrame *frame;

    unsigned num_superblocks;

    CodeBook codebooks[3];
} Escape124Context;

/**
 * Initialize the decoder
 * @param avctx decoder context
 * @return 0 success, negative on error
 */
static av_cold int escape124_decode_init(AVCodecContext *avctx)
{
    Escape124Context *s = avctx->priv_data;

    avctx->pix_fmt = AV_PIX_FMT_RGB555;

    s->num_superblocks = ((unsigned)avctx->width / 8) *
                         ((unsigned)avctx->height / 8);

    s->frame = av_frame_alloc();
    if (!s->frame)
        return AVERROR(ENOMEM);

    return 0;
}

static av_cold int escape124_decode_close(AVCodecContext *avctx)
{
    unsigned i;
    Escape124Context *s = avctx->priv_data;

    for (i = 0; i < 3; i++)
        av_freep(&s->codebooks[i].blocks);

    av_frame_free(&s->frame);

    return 0;
}

static CodeBook unpack_codebook(GetBitContext* gb, unsigned depth,
                                 unsigned size)
{
    unsigned i, j;
    CodeBook cb = { 0 };

    if (size >= INT_MAX / 34 || get_bits_left(gb) < size * 34)
        return cb;

    if (size >= INT_MAX / sizeof(MacroBlock))
        return cb;
    cb.blocks = av_malloc(size ? size * sizeof(MacroBlock) : 1);
    if (!cb.blocks)
        return cb;

    cb.depth = depth;
    cb.size = size;
    for (i = 0; i < size; i++) {
        unsigned mask_bits = get_bits(gb, 4);
        unsigned color0 = get_bits(gb, 15);
        unsigned color1 = get_bits(gb, 15);

        for (j = 0; j < 4; j++) {
            if (mask_bits & (1 << j))
                cb.blocks[i].pixels[j] = color1;
            else
                cb.blocks[i].pixels[j] = color0;
        }
    }
    return cb;
}

static unsigned decode_skip_count(GetBitContext* gb)
{
    unsigned value;
    // This function reads a maximum of 23 bits,
    // which is within the padding space
    if (get_bits_left(gb) < 1)
        return -1;
    value = get_bits1(gb);
    if (!value)
        return value;

    value += get_bits(gb, 3);
    if (value != (1 + ((1 << 3) - 1)))
        return value;

    value += get_bits(gb, 7);
    if (value != (1 + ((1 << 3) - 1)) + ((1 << 7) - 1))
        return value;

    return value + get_bits(gb, 12);
}

static MacroBlock decode_macroblock(Escape124Context* s, GetBitContext* gb,
                                    int* codebook_index, int superblock_index)
{
    // This function reads a maximum of 22 bits; the callers
    // guard this function appropriately
    unsigned block_index, depth;
    int value = get_bits1(gb);
    if (value) {
        static const int8_t transitions[3][2] = { {2, 1}, {0, 2}, {1, 0} };
        value = get_bits1(gb);
        *codebook_index = transitions[*codebook_index][value];
    }

    depth = s->codebooks[*codebook_index].depth;

    // depth = 0 means that this shouldn't read any bits;
    // in theory, this is the same as get_bits(gb, 0), but
    // that doesn't actually work.
    block_index = get_bitsz(gb, depth);

    if (*codebook_index == 1) {
        block_index += superblock_index << s->codebooks[1].depth;
    }

    // This condition can occur with invalid bitstreams and
    // *codebook_index == 2
    if (block_index >= s->codebooks[*codebook_index].size)
        return (MacroBlock) { { 0 } };

    return s->codebooks[*codebook_index].blocks[block_index];
}

static void insert_mb_into_sb(SuperBlock* sb, MacroBlock mb, unsigned index) {
   // Formula: ((index / 4) * 16 + (index % 4) * 2) / 2
   uint32_t *dst = sb->pixels32 + index + (index & -4);

   // This technically violates C99 aliasing rules, but it should be safe.
   dst[0] = mb.pixels32[0];
   dst[4] = mb.pixels32[1];
}

static void copy_superblock(uint16_t* dest, unsigned dest_stride,
                            uint16_t* src, unsigned src_stride)
{
    unsigned y;
    if (src)
        for (y = 0; y < 8; y++)
            memcpy(dest + y * dest_stride, src + y * src_stride,
                   sizeof(uint16_t) * 8);
    else
        for (y = 0; y < 8; y++)
            memset(dest + y * dest_stride, 0, sizeof(uint16_t) * 8);
}

static const uint16_t mask_matrix[] = {0x1,   0x2,   0x10,   0x20,
                                       0x4,   0x8,   0x40,   0x80,
                                       0x100, 0x200, 0x1000, 0x2000,
                                       0x400, 0x800, 0x4000, 0x8000};

static int escape124_decode_frame(AVCodecContext *avctx,
                                  void *data, int *got_frame,
                                  AVPacket *avpkt)
{
    int buf_size = avpkt->size;
    Escape124Context *s = avctx->priv_data;
    AVFrame *frame = data;

    GetBitContext gb;
    unsigned frame_flags, frame_size;
    unsigned i;

    unsigned superblock_index, cb_index = 1,
             superblock_col_index = 0,
             superblocks_per_row = avctx->width / 8, skip = -1;

    uint16_t* old_frame_data, *new_frame_data;
    unsigned old_stride, new_stride;

    int ret;

    if ((ret = init_get_bits8(&gb, avpkt->data, avpkt->size)) < 0)
        return ret;

    // This call also guards the potential depth reads for the
    // codebook unpacking.
    // Check if the amount we will read minimally is available on input.
    // The 64 represent the immediately next 2 frame_* elements read, the 23/4320
    // represent a lower bound of the space needed for skipped superblocks. Non
    // skipped SBs need more space.
    if (get_bits_left(&gb) < 64 + s->num_superblocks * 23LL / 4320)
        return -1;

    frame_flags = get_bits_long(&gb, 32);
    frame_size  = get_bits_long(&gb, 32);

    // Leave last frame unchanged
    // FIXME: Is this necessary?  I haven't seen it in any real samples
    if (!(frame_flags & 0x114) || !(frame_flags & 0x7800000)) {
        if (!s->frame->data[0])
            return AVERROR_INVALIDDATA;

        av_log(avctx, AV_LOG_DEBUG, "Skipping frame\n");

        *got_frame = 1;
        if ((ret = av_frame_ref(frame, s->frame)) < 0)
            return ret;

        return frame_size;
    }

    for (i = 0; i < 3; i++) {
        if (frame_flags & (1 << (17 + i))) {
            unsigned cb_depth, cb_size;
            if (i == 2) {
                // This codebook can be cut off at places other than
                // powers of 2, leaving some of the entries undefined.
                cb_size = get_bits(&gb, 20);
                if (!cb_size) {
                    av_log(avctx, AV_LOG_ERROR, "Invalid codebook size 0.\n");
                    return AVERROR_INVALIDDATA;
                }
                cb_depth = av_log2(cb_size - 1) + 1;
            } else {
                cb_depth = get_bits(&gb, 4);
                if (i == 0) {
                    // This is the most basic codebook: pow(2,depth) entries
                    // for a depth-length key
                    cb_size = 1 << cb_depth;
                } else {
                    // This codebook varies per superblock
                    // FIXME: I don't think this handles integer overflow
                    // properly
                    cb_size = s->num_superblocks << cb_depth;
                }
            }
            if (s->num_superblocks >= INT_MAX >> cb_depth) {
                av_log(avctx, AV_LOG_ERROR, "Depth or num_superblocks are too large\n");
                return AVERROR_INVALIDDATA;
            }

            av_freep(&s->codebooks[i].blocks);
            s->codebooks[i] = unpack_codebook(&gb, cb_depth, cb_size);
            if (!s->codebooks[i].blocks)
                return -1;
        }
    }

    if ((ret = ff_get_buffer(avctx, frame, AV_GET_BUFFER_FLAG_REF)) < 0)
        return ret;

    new_frame_data = (uint16_t*)frame->data[0];
    new_stride = frame->linesize[0] / 2;
    old_frame_data = (uint16_t*)s->frame->data[0];
    old_stride = s->frame->linesize[0] / 2;

    for (superblock_index = 0; superblock_index < s->num_superblocks;
         superblock_index++) {
        MacroBlock mb;
        SuperBlock sb;
        unsigned multi_mask = 0;

        if (skip == -1) {
            // Note that this call will make us skip the rest of the blocks
            // if the frame prematurely ends
            skip = decode_skip_count(&gb);
        }

        if (skip) {
            copy_superblock(new_frame_data, new_stride,
                            old_frame_data, old_stride);
        } else {
            copy_superblock(sb.pixels, 8,
                            old_frame_data, old_stride);

            while (get_bits_left(&gb) >= 1 && !get_bits1(&gb)) {
                unsigned mask;
                mb = decode_macroblock(s, &gb, &cb_index, superblock_index);
                mask = get_bits(&gb, 16);
                multi_mask |= mask;
                for (i = 0; i < 16; i++) {
                    if (mask & mask_matrix[i]) {
                        insert_mb_into_sb(&sb, mb, i);
                    }
                }
            }

            if (!get_bits1(&gb)) {
                unsigned inv_mask = get_bits(&gb, 4);
                for (i = 0; i < 4; i++) {
                    if (inv_mask & (1 << i)) {
                        multi_mask ^= 0xF << i*4;
                    } else {
                        multi_mask ^= get_bits(&gb, 4) << i*4;
                    }
                }

                for (i = 0; i < 16; i++) {
                    if (multi_mask & mask_matrix[i]) {
                        mb = decode_macroblock(s, &gb, &cb_index,
                                               superblock_index);
                        insert_mb_into_sb(&sb, mb, i);
                    }
                }
            } else if (frame_flags & (1 << 16)) {
                while (get_bits_left(&gb) >= 1 && !get_bits1(&gb)) {
                    mb = decode_macroblock(s, &gb, &cb_index, superblock_index);
                    insert_mb_into_sb(&sb, mb, get_bits(&gb, 4));
                }
            }

            copy_superblock(new_frame_data, new_stride, sb.pixels, 8);
        }

        superblock_col_index++;
        new_frame_data += 8;
        if (old_frame_data)
            old_frame_data += 8;
        if (superblock_col_index == superblocks_per_row) {
            new_frame_data += new_stride * 8 - superblocks_per_row * 8;
            if (old_frame_data)
                old_frame_data += old_stride * 8 - superblocks_per_row * 8;
            superblock_col_index = 0;
        }
        skip--;
    }

    av_log(avctx, AV_LOG_DEBUG,
           "Escape sizes: %i, %i, %i\n",
           frame_size, buf_size, get_bits_count(&gb) / 8);

    av_frame_unref(s->frame);
    if ((ret = av_frame_ref(s->frame, frame)) < 0)
        return ret;

    *got_frame = 1;

    return frame_size;
}


AVCodec ff_escape124_decoder = {
    .name           = "escape124",
    .long_name      = NULL_IF_CONFIG_SMALL("Escape 124"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_ESCAPE124,
    .priv_data_size = sizeof(Escape124Context),
    .init           = escape124_decode_init,
    .close          = escape124_decode_close,
    .decode         = escape124_decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1,
};
