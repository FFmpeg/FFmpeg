/*
 * Westwood Studios VQA Video Decoder
 * Copyright (C) 2003 the ffmpeg project
 *
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

/**
 * @file
 * VQA Video Decoder
 * @author Mike Melanson (melanson@pcisys.net)
 * @see http://wiki.multimedia.cx/index.php?title=VQA
 *
 * The VQA video decoder outputs PAL8 or RGB555 colorspace data, depending
 * on the type of data in the file.
 *
 * This decoder needs the 42-byte VQHD header from the beginning
 * of the VQA file passed through the extradata field. The VQHD header
 * is laid out as:
 *
 *   bytes 0-3   chunk fourcc: 'VQHD'
 *   bytes 4-7   chunk size in big-endian format, should be 0x0000002A
 *   bytes 8-49  VQHD chunk data
 *
 * Bytes 8-49 are what this decoder expects to see.
 *
 * Briefly, VQA is a vector quantized animation format that operates in a
 * VGA palettized colorspace. It operates on pixel vectors (blocks)
 * of either 4x2 or 4x4 in size. Compressed VQA chunks can contain vector
 * codebooks, palette information, and code maps for rendering vectors onto
 * frames. Any of these components can also be compressed with a run-length
 * encoding (RLE) algorithm commonly referred to as "format80".
 *
 * VQA takes a novel approach to rate control. Each group of n frames
 * (usually, n = 8) relies on a different vector codebook. Rather than
 * transporting an entire codebook every 8th frame, the new codebook is
 * broken up into 8 pieces and sent along with the compressed video chunks
 * for each of the 8 frames preceding the 8 frames which require the
 * codebook. A full codebook is also sent on the very first frame of a
 * file. This is an interesting technique, although it makes random file
 * seeking difficult despite the fact that the frames are all intracoded.
 *
 * V1,2 VQA uses 12-bit codebook indexes. If the 12-bit indexes were
 * packed into bytes and then RLE compressed, bytewise, the results would
 * be poor. That is why the coding method divides each index into 2 parts,
 * the top 4 bits and the bottom 8 bits, then RL encodes the 4-bit pieces
 * together and the 8-bit pieces together. If most of the vectors are
 * clustered into one group of 256 vectors, most of the 4-bit index pieces
 * should be the same.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libavutil/intreadwrite.h"
#include "libavutil/imgutils.h"
#include "avcodec.h"
#include "bytestream.h"
#include "internal.h"

#define PALETTE_COUNT 256
#define VQA_HEADER_SIZE 0x2A

/* allocate the maximum vector space, regardless of the file version:
 * (0xFF00 codebook vectors + 0x100 solid pixel vectors) * (4x4 pixels/block) */
#define MAX_CODEBOOK_VECTORS 0xFF00
#define SOLID_PIXEL_VECTORS 0x100
#define MAX_VECTORS (MAX_CODEBOOK_VECTORS + SOLID_PIXEL_VECTORS)
#define MAX_CODEBOOK_SIZE (MAX_VECTORS * 4 * 4)

#define CBF0_TAG MKBETAG('C', 'B', 'F', '0')
#define CBFZ_TAG MKBETAG('C', 'B', 'F', 'Z')
#define CBP0_TAG MKBETAG('C', 'B', 'P', '0')
#define CBPZ_TAG MKBETAG('C', 'B', 'P', 'Z')
#define CPL0_TAG MKBETAG('C', 'P', 'L', '0')
#define CPLZ_TAG MKBETAG('C', 'P', 'L', 'Z')
#define VPTZ_TAG MKBETAG('V', 'P', 'T', 'Z')

typedef struct VqaContext {

    AVCodecContext *avctx;
    GetByteContext gb;

    uint32_t palette[PALETTE_COUNT];

    int width;   /* width of a frame */
    int height;   /* height of a frame */
    int vector_width;  /* width of individual vector */
    int vector_height;  /* height of individual vector */
    int vqa_version;  /* this should be either 1, 2 or 3 */

    unsigned char *codebook;         /* the current codebook */
    int codebook_size;
    unsigned char *next_codebook_buffer;  /* accumulator for next codebook */
    int next_codebook_buffer_index;

    unsigned char *decode_buffer;
    int decode_buffer_size;

    /* number of frames to go before replacing codebook */
    int partial_countdown;
    int partial_count;

} VqaContext;

static av_cold int vqa_decode_init(AVCodecContext *avctx)
{
    VqaContext *s = avctx->priv_data;
    int i, j, codebook_index, ret;

    s->avctx = avctx;
    avctx->pix_fmt = AV_PIX_FMT_PAL8;

    /* make sure the extradata made it */
    if (s->avctx->extradata_size != VQA_HEADER_SIZE) {
        av_log(s->avctx, AV_LOG_ERROR, "  VQA video: expected extradata size of %d\n", VQA_HEADER_SIZE);
        return AVERROR(EINVAL);
    }

    /* load up the VQA parameters from the header */
    s->vqa_version = s->avctx->extradata[0];
    switch (s->vqa_version) {
    case 1:
    case 2:
        break;
    case 3:
        avpriv_report_missing_feature(avctx, "VQA Version %d", s->vqa_version);
        return AVERROR_PATCHWELCOME;
    default:
        avpriv_request_sample(avctx, "VQA Version %i", s->vqa_version);
        return AVERROR_PATCHWELCOME;
    }
    s->width = AV_RL16(&s->avctx->extradata[6]);
    s->height = AV_RL16(&s->avctx->extradata[8]);
    if ((ret = av_image_check_size(s->width, s->height, 0, avctx)) < 0) {
        s->width= s->height= 0;
        return ret;
    }
    s->vector_width = s->avctx->extradata[10];
    s->vector_height = s->avctx->extradata[11];
    s->partial_count = s->partial_countdown = s->avctx->extradata[13];

    /* the vector dimensions have to meet very stringent requirements */
    if ((s->vector_width != 4) ||
        ((s->vector_height != 2) && (s->vector_height != 4))) {
        /* return without further initialization */
        return AVERROR_INVALIDDATA;
    }

    if (s->width  & (s->vector_width  - 1) ||
        s->height & (s->vector_height - 1)) {
        av_log(avctx, AV_LOG_ERROR, "Image size not multiple of block size\n");
        return AVERROR_INVALIDDATA;
    }

    /* allocate codebooks */
    s->codebook_size = MAX_CODEBOOK_SIZE;
    s->codebook = av_malloc(s->codebook_size);
    if (!s->codebook)
        goto fail;
    s->next_codebook_buffer = av_malloc(s->codebook_size);
    if (!s->next_codebook_buffer)
        goto fail;

    /* allocate decode buffer */
    s->decode_buffer_size = (s->width / s->vector_width) *
        (s->height / s->vector_height) * 2;
    s->decode_buffer = av_malloc(s->decode_buffer_size);
    if (!s->decode_buffer)
        goto fail;

    /* initialize the solid-color vectors */
    if (s->vector_height == 4) {
        codebook_index = 0xFF00 * 16;
        for (i = 0; i < 256; i++)
            for (j = 0; j < 16; j++)
                s->codebook[codebook_index++] = i;
    } else {
        codebook_index = 0xF00 * 8;
        for (i = 0; i < 256; i++)
            for (j = 0; j < 8; j++)
                s->codebook[codebook_index++] = i;
    }
    s->next_codebook_buffer_index = 0;

    return 0;
fail:
    av_freep(&s->codebook);
    av_freep(&s->next_codebook_buffer);
    av_freep(&s->decode_buffer);
    return AVERROR(ENOMEM);
}

#define CHECK_COUNT() \
    if (dest_index + count > dest_size) { \
        av_log(NULL, AV_LOG_ERROR, "  VQA video: decode_format80 problem: next op would overflow dest_index\n"); \
        av_log(NULL, AV_LOG_ERROR, "  VQA video: current dest_index = %d, count = %d, dest_size = %d\n", \
            dest_index, count, dest_size); \
        return AVERROR_INVALIDDATA; \
    }

#define CHECK_COPY(idx) \
    if (idx < 0 || idx + count > dest_size) { \
        av_log(NULL, AV_LOG_ERROR, "  VQA video: decode_format80 problem: next op would overflow dest_index\n"); \
        av_log(NULL, AV_LOG_ERROR, "  VQA video: current src_pos = %d, count = %d, dest_size = %d\n", \
            src_pos, count, dest_size); \
        return AVERROR_INVALIDDATA; \
    }


static int decode_format80(GetByteContext *gb, int src_size,
    unsigned char *dest, int dest_size, int check_size) {

    int dest_index = 0;
    int count, opcode, start;
    int src_pos;
    unsigned char color;
    int i;

    start = bytestream2_tell(gb);
    while (bytestream2_tell(gb) - start < src_size) {
        opcode = bytestream2_get_byte(gb);
        av_dlog(NULL, "      opcode %02X: ", opcode);

        /* 0x80 means that frame is finished */
        if (opcode == 0x80)
            return 0;

        if (dest_index >= dest_size) {
            av_log(NULL, AV_LOG_ERROR, "  VQA video: decode_format80 problem: dest_index (%d) exceeded dest_size (%d)\n",
                dest_index, dest_size);
            return AVERROR_INVALIDDATA;
        }

        if (opcode == 0xFF) {

            count   = bytestream2_get_le16(gb);
            src_pos = bytestream2_get_le16(gb);
            av_dlog(NULL, "(1) copy %X bytes from absolute pos %X\n", count, src_pos);
            CHECK_COUNT();
            CHECK_COPY(src_pos);
            for (i = 0; i < count; i++)
                dest[dest_index + i] = dest[src_pos + i];
            dest_index += count;

        } else if (opcode == 0xFE) {

            count = bytestream2_get_le16(gb);
            color = bytestream2_get_byte(gb);
            av_dlog(NULL, "(2) set %X bytes to %02X\n", count, color);
            CHECK_COUNT();
            memset(&dest[dest_index], color, count);
            dest_index += count;

        } else if ((opcode & 0xC0) == 0xC0) {

            count = (opcode & 0x3F) + 3;
            src_pos = bytestream2_get_le16(gb);
            av_dlog(NULL, "(3) copy %X bytes from absolute pos %X\n", count, src_pos);
            CHECK_COUNT();
            CHECK_COPY(src_pos);
            for (i = 0; i < count; i++)
                dest[dest_index + i] = dest[src_pos + i];
            dest_index += count;

        } else if (opcode > 0x80) {

            count = opcode & 0x3F;
            av_dlog(NULL, "(4) copy %X bytes from source to dest\n", count);
            CHECK_COUNT();
            bytestream2_get_buffer(gb, &dest[dest_index], count);
            dest_index += count;

        } else {

            count = ((opcode & 0x70) >> 4) + 3;
            src_pos = bytestream2_get_byte(gb) | ((opcode & 0x0F) << 8);
            av_dlog(NULL, "(5) copy %X bytes from relpos %X\n", count, src_pos);
            CHECK_COUNT();
            CHECK_COPY(dest_index - src_pos);
            for (i = 0; i < count; i++)
                dest[dest_index + i] = dest[dest_index - src_pos + i];
            dest_index += count;
        }
    }

    /* validate that the entire destination buffer was filled; this is
     * important for decoding frame maps since each vector needs to have a
     * codebook entry; it is not important for compressed codebooks because
     * not every entry needs to be filled */
    if (check_size)
        if (dest_index < dest_size)
            av_log(NULL, AV_LOG_ERROR, "  VQA video: decode_format80 problem: decode finished with dest_index (%d) < dest_size (%d)\n",
                dest_index, dest_size);

    return 0; // let's display what we decoded anyway
}

static int vqa_decode_chunk(VqaContext *s, AVFrame *frame)
{
    unsigned int chunk_type;
    unsigned int chunk_size;
    int byte_skip;
    unsigned int index = 0;
    int i;
    unsigned char r, g, b;
    int index_shift;
    int res;

    int cbf0_chunk = -1;
    int cbfz_chunk = -1;
    int cbp0_chunk = -1;
    int cbpz_chunk = -1;
    int cpl0_chunk = -1;
    int cplz_chunk = -1;
    int vptz_chunk = -1;

    int x, y;
    int lines = 0;
    int pixel_ptr;
    int vector_index = 0;
    int lobyte = 0;
    int hibyte = 0;
    int lobytes = 0;
    int hibytes = s->decode_buffer_size / 2;

    /* first, traverse through the frame and find the subchunks */
    while (bytestream2_get_bytes_left(&s->gb) >= 8) {

        chunk_type = bytestream2_get_be32u(&s->gb);
        index      = bytestream2_tell(&s->gb);
        chunk_size = bytestream2_get_be32u(&s->gb);

        switch (chunk_type) {

        case CBF0_TAG:
            cbf0_chunk = index;
            break;

        case CBFZ_TAG:
            cbfz_chunk = index;
            break;

        case CBP0_TAG:
            cbp0_chunk = index;
            break;

        case CBPZ_TAG:
            cbpz_chunk = index;
            break;

        case CPL0_TAG:
            cpl0_chunk = index;
            break;

        case CPLZ_TAG:
            cplz_chunk = index;
            break;

        case VPTZ_TAG:
            vptz_chunk = index;
            break;

        default:
            av_log(s->avctx, AV_LOG_ERROR, "  VQA video: Found unknown chunk type: %c%c%c%c (%08X)\n",
            (chunk_type >> 24) & 0xFF,
            (chunk_type >> 16) & 0xFF,
            (chunk_type >>  8) & 0xFF,
            (chunk_type >>  0) & 0xFF,
            chunk_type);
            break;
        }

        byte_skip = chunk_size & 0x01;
        bytestream2_skip(&s->gb, chunk_size + byte_skip);
    }

    /* next, deal with the palette */
    if ((cpl0_chunk != -1) && (cplz_chunk != -1)) {

        /* a chunk should not have both chunk types */
        av_log(s->avctx, AV_LOG_ERROR, "  VQA video: problem: found both CPL0 and CPLZ chunks\n");
        return AVERROR_INVALIDDATA;
    }

    /* decompress the palette chunk */
    if (cplz_chunk != -1) {

/* yet to be handled */

    }

    /* convert the RGB palette into the machine's endian format */
    if (cpl0_chunk != -1) {

        bytestream2_seek(&s->gb, cpl0_chunk, SEEK_SET);
        chunk_size = bytestream2_get_be32(&s->gb);
        /* sanity check the palette size */
        if (chunk_size / 3 > 256 || chunk_size > bytestream2_get_bytes_left(&s->gb)) {
            av_log(s->avctx, AV_LOG_ERROR, "  VQA video: problem: found a palette chunk with %d colors\n",
                chunk_size / 3);
            return AVERROR_INVALIDDATA;
        }
        for (i = 0; i < chunk_size / 3; i++) {
            /* scale by 4 to transform 6-bit palette -> 8-bit */
            r = bytestream2_get_byteu(&s->gb) * 4;
            g = bytestream2_get_byteu(&s->gb) * 4;
            b = bytestream2_get_byteu(&s->gb) * 4;
            s->palette[i] = (r << 16) | (g << 8) | (b);
        }
    }

    /* next, look for a full codebook */
    if ((cbf0_chunk != -1) && (cbfz_chunk != -1)) {

        /* a chunk should not have both chunk types */
        av_log(s->avctx, AV_LOG_ERROR, "  VQA video: problem: found both CBF0 and CBFZ chunks\n");
        return AVERROR_INVALIDDATA;
    }

    /* decompress the full codebook chunk */
    if (cbfz_chunk != -1) {

        bytestream2_seek(&s->gb, cbfz_chunk, SEEK_SET);
        chunk_size = bytestream2_get_be32(&s->gb);
        if ((res = decode_format80(&s->gb, chunk_size, s->codebook,
                                   s->codebook_size, 0)) < 0)
            return res;
    }

    /* copy a full codebook */
    if (cbf0_chunk != -1) {

        bytestream2_seek(&s->gb, cbf0_chunk, SEEK_SET);
        chunk_size = bytestream2_get_be32(&s->gb);
        /* sanity check the full codebook size */
        if (chunk_size > MAX_CODEBOOK_SIZE) {
            av_log(s->avctx, AV_LOG_ERROR, "  VQA video: problem: CBF0 chunk too large (0x%X bytes)\n",
                chunk_size);
            return AVERROR_INVALIDDATA;
        }

        bytestream2_get_buffer(&s->gb, s->codebook, chunk_size);
    }

    /* decode the frame */
    if (vptz_chunk == -1) {

        /* something is wrong if there is no VPTZ chunk */
        av_log(s->avctx, AV_LOG_ERROR, "  VQA video: problem: no VPTZ chunk found\n");
        return AVERROR_INVALIDDATA;
    }

    bytestream2_seek(&s->gb, vptz_chunk, SEEK_SET);
    chunk_size = bytestream2_get_be32(&s->gb);
    if ((res = decode_format80(&s->gb, chunk_size,
                               s->decode_buffer, s->decode_buffer_size, 1)) < 0)
        return res;

    /* render the final PAL8 frame */
    if (s->vector_height == 4)
        index_shift = 4;
    else
        index_shift = 3;
    for (y = 0; y < s->height; y += s->vector_height) {
        for (x = 0; x < s->width; x += 4, lobytes++, hibytes++) {
            pixel_ptr = y * frame->linesize[0] + x;

            /* get the vector index, the method for which varies according to
             * VQA file version */
            switch (s->vqa_version) {

            case 1:
                lobyte = s->decode_buffer[lobytes * 2];
                hibyte = s->decode_buffer[(lobytes * 2) + 1];
                vector_index = ((hibyte << 8) | lobyte) >> 3;
                vector_index <<= index_shift;
                lines = s->vector_height;
                /* uniform color fill - a quick hack */
                if (hibyte == 0xFF) {
                    while (lines--) {
                        frame->data[0][pixel_ptr + 0] = 255 - lobyte;
                        frame->data[0][pixel_ptr + 1] = 255 - lobyte;
                        frame->data[0][pixel_ptr + 2] = 255 - lobyte;
                        frame->data[0][pixel_ptr + 3] = 255 - lobyte;
                        pixel_ptr += frame->linesize[0];
                    }
                    lines=0;
                }
                break;

            case 2:
                lobyte = s->decode_buffer[lobytes];
                hibyte = s->decode_buffer[hibytes];
                vector_index = (hibyte << 8) | lobyte;
                vector_index <<= index_shift;
                lines = s->vector_height;
                break;

            case 3:
/* not implemented yet */
                lines = 0;
                break;
            }

            while (lines--) {
                frame->data[0][pixel_ptr + 0] = s->codebook[vector_index++];
                frame->data[0][pixel_ptr + 1] = s->codebook[vector_index++];
                frame->data[0][pixel_ptr + 2] = s->codebook[vector_index++];
                frame->data[0][pixel_ptr + 3] = s->codebook[vector_index++];
                pixel_ptr += frame->linesize[0];
            }
        }
    }

    /* handle partial codebook */
    if ((cbp0_chunk != -1) && (cbpz_chunk != -1)) {
        /* a chunk should not have both chunk types */
        av_log(s->avctx, AV_LOG_ERROR, "  VQA video: problem: found both CBP0 and CBPZ chunks\n");
        return AVERROR_INVALIDDATA;
    }

    if (cbp0_chunk != -1) {

        bytestream2_seek(&s->gb, cbp0_chunk, SEEK_SET);
        chunk_size = bytestream2_get_be32(&s->gb);

        if (chunk_size > MAX_CODEBOOK_SIZE - s->next_codebook_buffer_index) {
            av_log(s->avctx, AV_LOG_ERROR, "cbp0 chunk too large (%u bytes)\n",
                   chunk_size);
            return AVERROR_INVALIDDATA;
        }

        /* accumulate partial codebook */
        bytestream2_get_buffer(&s->gb, &s->next_codebook_buffer[s->next_codebook_buffer_index],
                               chunk_size);
        s->next_codebook_buffer_index += chunk_size;

        s->partial_countdown--;
        if (s->partial_countdown == 0) {

            /* time to replace codebook */
            memcpy(s->codebook, s->next_codebook_buffer,
                s->next_codebook_buffer_index);

            /* reset accounting */
            s->next_codebook_buffer_index = 0;
            s->partial_countdown = s->partial_count;
        }
    }

    if (cbpz_chunk != -1) {

        bytestream2_seek(&s->gb, cbpz_chunk, SEEK_SET);
        chunk_size = bytestream2_get_be32(&s->gb);

        if (chunk_size > MAX_CODEBOOK_SIZE - s->next_codebook_buffer_index) {
            av_log(s->avctx, AV_LOG_ERROR, "cbpz chunk too large (%u bytes)\n",
                   chunk_size);
            return AVERROR_INVALIDDATA;
        }

        /* accumulate partial codebook */
        bytestream2_get_buffer(&s->gb, &s->next_codebook_buffer[s->next_codebook_buffer_index],
                               chunk_size);
        s->next_codebook_buffer_index += chunk_size;

        s->partial_countdown--;
        if (s->partial_countdown == 0) {
            GetByteContext gb;

            bytestream2_init(&gb, s->next_codebook_buffer, s->next_codebook_buffer_index);
            /* decompress codebook */
            if ((res = decode_format80(&gb, s->next_codebook_buffer_index,
                                       s->codebook, s->codebook_size, 0)) < 0)
                return res;

            /* reset accounting */
            s->next_codebook_buffer_index = 0;
            s->partial_countdown = s->partial_count;
        }
    }

    return 0;
}

static int vqa_decode_frame(AVCodecContext *avctx,
                            void *data, int *got_frame,
                            AVPacket *avpkt)
{
    VqaContext *s = avctx->priv_data;
    AVFrame *frame = data;
    int res;

    if ((res = ff_get_buffer(avctx, frame, 0)) < 0) {
        av_log(s->avctx, AV_LOG_ERROR, "  VQA Video: get_buffer() failed\n");
        return res;
    }

    bytestream2_init(&s->gb, avpkt->data, avpkt->size);
    if ((res = vqa_decode_chunk(s, frame)) < 0)
        return res;

    /* make the palette available on the way out */
    memcpy(frame->data[1], s->palette, PALETTE_COUNT * 4);
    frame->palette_has_changed = 1;

    *got_frame      = 1;

    /* report that the buffer was completely consumed */
    return avpkt->size;
}

static av_cold int vqa_decode_end(AVCodecContext *avctx)
{
    VqaContext *s = avctx->priv_data;

    av_freep(&s->codebook);
    av_freep(&s->next_codebook_buffer);
    av_freep(&s->decode_buffer);

    return 0;
}

AVCodec ff_vqa_decoder = {
    .name           = "vqavideo",
    .long_name      = NULL_IF_CONFIG_SMALL("Westwood Studios VQA (Vector Quantized Animation) video"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_WS_VQA,
    .priv_data_size = sizeof(VqaContext),
    .init           = vqa_decode_init,
    .close          = vqa_decode_end,
    .decode         = vqa_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
};
