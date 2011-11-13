/*
 * Westwood Studios VQA Video Decoder
 * Copyright (C) 2003 the ffmpeg project
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

#define PALETTE_COUNT 256
#define VQA_HEADER_SIZE 0x2A
#define CHUNK_PREAMBLE_SIZE 8

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
    AVFrame frame;

    const unsigned char *buf;
    int size;

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
    unsigned char *vqa_header;
    int i, j, codebook_index;

    s->avctx = avctx;
    avctx->pix_fmt = PIX_FMT_PAL8;

    /* make sure the extradata made it */
    if (s->avctx->extradata_size != VQA_HEADER_SIZE) {
        av_log(s->avctx, AV_LOG_ERROR, "  VQA video: expected extradata size of %d\n", VQA_HEADER_SIZE);
        return -1;
    }

    /* load up the VQA parameters from the header */
    vqa_header = (unsigned char *)s->avctx->extradata;
    s->vqa_version = vqa_header[0];
    if (s->vqa_version < 1 || s->vqa_version > 3) {
        av_log(s->avctx, AV_LOG_ERROR, "  VQA video: unsupported version %d\n", s->vqa_version);
        return -1;
    }
    s->width = AV_RL16(&vqa_header[6]);
    s->height = AV_RL16(&vqa_header[8]);
    if(av_image_check_size(s->width, s->height, 0, avctx)){
        s->width= s->height= 0;
        return -1;
    }
    s->vector_width = vqa_header[10];
    s->vector_height = vqa_header[11];
    s->partial_count = s->partial_countdown = vqa_header[13];

    /* the vector dimensions have to meet very stringent requirements */
    if ((s->vector_width != 4) ||
        ((s->vector_height != 2) && (s->vector_height != 4))) {
        /* return without further initialization */
        return -1;
    }

    /* allocate codebooks */
    s->codebook_size = MAX_CODEBOOK_SIZE;
    s->codebook = av_malloc(s->codebook_size);
    s->next_codebook_buffer = av_malloc(s->codebook_size);

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

    /* allocate decode buffer */
    s->decode_buffer_size = (s->width / s->vector_width) *
        (s->height / s->vector_height) * 2;
    s->decode_buffer = av_malloc(s->decode_buffer_size);

    avcodec_get_frame_defaults(&s->frame);
    s->frame.data[0] = NULL;

    return 0;
}

#define CHECK_COUNT() \
    if (dest_index + count > dest_size) { \
        av_log(NULL, AV_LOG_ERROR, "  VQA video: decode_format80 problem: next op would overflow dest_index\n"); \
        av_log(NULL, AV_LOG_ERROR, "  VQA video: current dest_index = %d, count = %d, dest_size = %d\n", \
            dest_index, count, dest_size); \
        return; \
    }

static void decode_format80(const unsigned char *src, int src_size,
    unsigned char *dest, int dest_size, int check_size) {

    int src_index = 0;
    int dest_index = 0;
    int count;
    int src_pos;
    unsigned char color;
    int i;

    while (src_index < src_size) {

        av_dlog(NULL, "      opcode %02X: ", src[src_index]);

        /* 0x80 means that frame is finished */
        if (src[src_index] == 0x80)
            return;

        if (dest_index >= dest_size) {
            av_log(NULL, AV_LOG_ERROR, "  VQA video: decode_format80 problem: dest_index (%d) exceeded dest_size (%d)\n",
                dest_index, dest_size);
            return;
        }

        if (src[src_index] == 0xFF) {

            src_index++;
            count = AV_RL16(&src[src_index]);
            src_index += 2;
            src_pos = AV_RL16(&src[src_index]);
            src_index += 2;
            av_dlog(NULL, "(1) copy %X bytes from absolute pos %X\n", count, src_pos);
            CHECK_COUNT();
            if (src_pos + count > dest_size)
                return;
            for (i = 0; i < count; i++)
                dest[dest_index + i] = dest[src_pos + i];
            dest_index += count;

        } else if (src[src_index] == 0xFE) {

            src_index++;
            count = AV_RL16(&src[src_index]);
            src_index += 2;
            color = src[src_index++];
            av_dlog(NULL, "(2) set %X bytes to %02X\n", count, color);
            CHECK_COUNT();
            memset(&dest[dest_index], color, count);
            dest_index += count;

        } else if ((src[src_index] & 0xC0) == 0xC0) {

            count = (src[src_index++] & 0x3F) + 3;
            src_pos = AV_RL16(&src[src_index]);
            src_index += 2;
            av_dlog(NULL, "(3) copy %X bytes from absolute pos %X\n", count, src_pos);
            CHECK_COUNT();
            if (src_pos + count > dest_size)
                return;
            for (i = 0; i < count; i++)
                dest[dest_index + i] = dest[src_pos + i];
            dest_index += count;

        } else if (src[src_index] > 0x80) {

            count = src[src_index++] & 0x3F;
            av_dlog(NULL, "(4) copy %X bytes from source to dest\n", count);
            CHECK_COUNT();
            memcpy(&dest[dest_index], &src[src_index], count);
            src_index += count;
            dest_index += count;

        } else {

            count = ((src[src_index] & 0x70) >> 4) + 3;
            src_pos = AV_RB16(&src[src_index]) & 0x0FFF;
            src_index += 2;
            av_dlog(NULL, "(5) copy %X bytes from relpos %X\n", count, src_pos);
            CHECK_COUNT();
            if (dest_index < src_pos)
                return;
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
}

static void vqa_decode_chunk(VqaContext *s)
{
    unsigned int chunk_type;
    unsigned int chunk_size;
    int byte_skip;
    unsigned int index = 0;
    int i;
    unsigned char r, g, b;
    int index_shift;

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
    while (index < s->size) {

        chunk_type = AV_RB32(&s->buf[index]);
        chunk_size = AV_RB32(&s->buf[index + 4]);

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
        index += (CHUNK_PREAMBLE_SIZE + chunk_size + byte_skip);
    }

    /* next, deal with the palette */
    if ((cpl0_chunk != -1) && (cplz_chunk != -1)) {

        /* a chunk should not have both chunk types */
        av_log(s->avctx, AV_LOG_ERROR, "  VQA video: problem: found both CPL0 and CPLZ chunks\n");
        return;
    }

    /* decompress the palette chunk */
    if (cplz_chunk != -1) {

/* yet to be handled */

    }

    /* convert the RGB palette into the machine's endian format */
    if (cpl0_chunk != -1) {

        chunk_size = AV_RB32(&s->buf[cpl0_chunk + 4]);
        /* sanity check the palette size */
        if (chunk_size / 3 > 256) {
            av_log(s->avctx, AV_LOG_ERROR, "  VQA video: problem: found a palette chunk with %d colors\n",
                chunk_size / 3);
            return;
        }
        cpl0_chunk += CHUNK_PREAMBLE_SIZE;
        for (i = 0; i < chunk_size / 3; i++) {
            /* scale by 4 to transform 6-bit palette -> 8-bit */
            r = s->buf[cpl0_chunk++] * 4;
            g = s->buf[cpl0_chunk++] * 4;
            b = s->buf[cpl0_chunk++] * 4;
            s->palette[i] = 0xFF << 24 | r << 16 | g << 8 | b;
            s->palette[i] |= s->palette[i] >> 6 & 0x30303;
        }
    }

    /* next, look for a full codebook */
    if ((cbf0_chunk != -1) && (cbfz_chunk != -1)) {

        /* a chunk should not have both chunk types */
        av_log(s->avctx, AV_LOG_ERROR, "  VQA video: problem: found both CBF0 and CBFZ chunks\n");
        return;
    }

    /* decompress the full codebook chunk */
    if (cbfz_chunk != -1) {

        chunk_size = AV_RB32(&s->buf[cbfz_chunk + 4]);
        cbfz_chunk += CHUNK_PREAMBLE_SIZE;
        decode_format80(&s->buf[cbfz_chunk], chunk_size,
            s->codebook, s->codebook_size, 0);
    }

    /* copy a full codebook */
    if (cbf0_chunk != -1) {

        chunk_size = AV_RB32(&s->buf[cbf0_chunk + 4]);
        /* sanity check the full codebook size */
        if (chunk_size > MAX_CODEBOOK_SIZE) {
            av_log(s->avctx, AV_LOG_ERROR, "  VQA video: problem: CBF0 chunk too large (0x%X bytes)\n",
                chunk_size);
            return;
        }
        cbf0_chunk += CHUNK_PREAMBLE_SIZE;

        memcpy(s->codebook, &s->buf[cbf0_chunk], chunk_size);
    }

    /* decode the frame */
    if (vptz_chunk == -1) {

        /* something is wrong if there is no VPTZ chunk */
        av_log(s->avctx, AV_LOG_ERROR, "  VQA video: problem: no VPTZ chunk found\n");
        return;
    }

    chunk_size = AV_RB32(&s->buf[vptz_chunk + 4]);
    vptz_chunk += CHUNK_PREAMBLE_SIZE;
    decode_format80(&s->buf[vptz_chunk], chunk_size,
        s->decode_buffer, s->decode_buffer_size, 1);

    /* render the final PAL8 frame */
    if (s->vector_height == 4)
        index_shift = 4;
    else
        index_shift = 3;
    for (y = 0; y < s->frame.linesize[0] * s->height;
        y += s->frame.linesize[0] * s->vector_height) {

        for (x = y; x < y + s->width; x += 4, lobytes++, hibytes++) {
            pixel_ptr = x;

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
                        s->frame.data[0][pixel_ptr + 0] = 255 - lobyte;
                        s->frame.data[0][pixel_ptr + 1] = 255 - lobyte;
                        s->frame.data[0][pixel_ptr + 2] = 255 - lobyte;
                        s->frame.data[0][pixel_ptr + 3] = 255 - lobyte;
                        pixel_ptr += s->frame.linesize[0];
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
                s->frame.data[0][pixel_ptr + 0] = s->codebook[vector_index++];
                s->frame.data[0][pixel_ptr + 1] = s->codebook[vector_index++];
                s->frame.data[0][pixel_ptr + 2] = s->codebook[vector_index++];
                s->frame.data[0][pixel_ptr + 3] = s->codebook[vector_index++];
                pixel_ptr += s->frame.linesize[0];
            }
        }
    }

    /* handle partial codebook */
    if ((cbp0_chunk != -1) && (cbpz_chunk != -1)) {
        /* a chunk should not have both chunk types */
        av_log(s->avctx, AV_LOG_ERROR, "  VQA video: problem: found both CBP0 and CBPZ chunks\n");
        return;
    }

    if (cbp0_chunk != -1) {

        chunk_size = AV_RB32(&s->buf[cbp0_chunk + 4]);
        cbp0_chunk += CHUNK_PREAMBLE_SIZE;

        /* accumulate partial codebook */
        memcpy(&s->next_codebook_buffer[s->next_codebook_buffer_index],
            &s->buf[cbp0_chunk], chunk_size);
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

        chunk_size = AV_RB32(&s->buf[cbpz_chunk + 4]);
        cbpz_chunk += CHUNK_PREAMBLE_SIZE;

        /* accumulate partial codebook */
        memcpy(&s->next_codebook_buffer[s->next_codebook_buffer_index],
            &s->buf[cbpz_chunk], chunk_size);
        s->next_codebook_buffer_index += chunk_size;

        s->partial_countdown--;
        if (s->partial_countdown == 0) {

            /* decompress codebook */
            decode_format80(s->next_codebook_buffer,
                s->next_codebook_buffer_index,
                s->codebook, s->codebook_size, 0);

            /* reset accounting */
            s->next_codebook_buffer_index = 0;
            s->partial_countdown = s->partial_count;
        }
    }
}

static int vqa_decode_frame(AVCodecContext *avctx,
                            void *data, int *data_size,
                            AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    VqaContext *s = avctx->priv_data;

    s->buf = buf;
    s->size = buf_size;

    if (s->frame.data[0])
        avctx->release_buffer(avctx, &s->frame);

    if (avctx->get_buffer(avctx, &s->frame)) {
        av_log(s->avctx, AV_LOG_ERROR, "  VQA Video: get_buffer() failed\n");
        return -1;
    }

    vqa_decode_chunk(s);

    /* make the palette available on the way out */
    memcpy(s->frame.data[1], s->palette, PALETTE_COUNT * 4);
    s->frame.palette_has_changed = 1;

    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = s->frame;

    /* report that the buffer was completely consumed */
    return buf_size;
}

static av_cold int vqa_decode_end(AVCodecContext *avctx)
{
    VqaContext *s = avctx->priv_data;

    av_free(s->codebook);
    av_free(s->next_codebook_buffer);
    av_free(s->decode_buffer);

    if (s->frame.data[0])
        avctx->release_buffer(avctx, &s->frame);

    return 0;
}

AVCodec ff_vqa_decoder = {
    .name           = "vqavideo",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_WS_VQA,
    .priv_data_size = sizeof(VqaContext),
    .init           = vqa_decode_init,
    .close          = vqa_decode_end,
    .decode         = vqa_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name = NULL_IF_CONFIG_SMALL("Westwood Studios VQA (Vector Quantized Animation) video"),
};
