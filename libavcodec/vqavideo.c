/*
 * Westwood Studios VQA Video Decoder
 * Copyright (C) 2003 the ffmpeg project
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

/**
 * @file vqavideo.c
 * VQA Video Decoder by Mike Melanson (melanson@pcisys.net)
 * For more information about the RPZA format, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 *
 * The VQA video decoder outputs PAL8 colorspace data.
 *
 * This decoder needs the 42-byte VQHD header from the beginning
 * of the VQA file passed through the extradata field. The VQHD header
 * is laid out as:
 *
 *   bytes 0-3   chunk fourcc: 'VQHD'
 *   bytes 4-7   chunk size in big-endian format, should be 0x0000002A
 *   bytes 8-50  VQHD chunk data
 *
 * Bytes 8-50 are what this decoder expects to see.
 *
 * Briefly, VQA is a vector quantized animation format that operates in a
 * 6-bit VGA palettized colorspace. It operates on pixel vectors (blocks)
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
 * V1,2 VQA uses 12-bit codebook indices. If the 12-bit indices were
 * packed into bytes and then RLE compressed, bytewise, the results would
 * be poor. That is why the coding method divides each index into 2 parts,
 * the top 4 bits and the bottom 8 bits, the RL encodes the 4-bit pieces
 * together and the 8-bit pieces together. If most of the vectors are
 * clustered into one group of 256 vectors, most of the 4-bit index pieces
 * should be the same.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "avcodec.h"
#include "dsputil.h"

#define PALETTE_COUNT 256
#define VQA_HEADER_SIZE 0x2A
#define CHUNK_PREAMBLE_SIZE 8

/* v1, v2 files: each vector codebook entry is 4x2=8 pixels = 8 bytes */
#define V1_2_VECTOR_SIZE 8
#define V1_2_MAX_VECTORS 0xF00
/* v3 files: each vector codebook entry is 4x4=16 pixels = 16 bytes */
#define V3_VECTOR_SIZE 16
#define V3_MAX_VECTORS 0xFF00

#define LE_16(x)  ((((uint8_t*)(x))[1] << 8) | ((uint8_t*)(x))[0])
#define BE_16(x)  ((((uint8_t*)(x))[0] << 8) | ((uint8_t*)(x))[1])
#define BE_32(x)  ((((uint8_t*)(x))[0] << 24) | \
                   (((uint8_t*)(x))[1] << 16) | \
                   (((uint8_t*)(x))[2] << 8) | \
                    ((uint8_t*)(x))[3])

#define FOURCC_TAG( ch0, ch1, ch2, ch3 ) \
        ( (long)(unsigned char)(ch3) | \
        ( (long)(unsigned char)(ch2) << 8 ) | \
        ( (long)(unsigned char)(ch1) << 16 ) | \
        ( (long)(unsigned char)(ch0) << 24 ) )

#define CBF0_TAG FOURCC_TAG('C', 'B', 'F', '0')
#define CBFZ_TAG FOURCC_TAG('C', 'B', 'F', 'Z')
#define CBP0_TAG FOURCC_TAG('C', 'B', 'P', '0')
#define CBPZ_TAG FOURCC_TAG('C', 'B', 'P', 'Z')
#define CPL0_TAG FOURCC_TAG('C', 'P', 'L', '0')
#define CPLZ_TAG FOURCC_TAG('C', 'P', 'L', 'Z')
#define VPTZ_TAG FOURCC_TAG('V', 'P', 'T', 'Z')

#define VQA_DEBUG 0

#if VQA_DEBUG
#define vqa_debug printf
#else
static inline void vqa_debug(const char *format, ...) { }
#endif

typedef struct VqaContext {

    AVCodecContext *avctx;
    DSPContext dsp;
    AVFrame frame;

    unsigned char *buf;
    int size;

    unsigned char palette[PALETTE_COUNT * 4];

    int width;   /* width of a frame */
    int height;   /* height of a frame */
    int vector_width;  /* width of individual vector */
    int vector_height;  /* height of individual vector */
    int vqa_version;  /* this should be either 1, 2 or 3 */

    unsigned char *codebook;         /* the current codebook */
    unsigned char *next_codebook_buffer;  /* accumulator for next codebook */
    int next_codebook_buffer_index;

    unsigned char *decode_buffer;
    int decode_buffer_size;

    /* number of frames to go before replacing codebook */
    int partial_countdown;
    int partial_count;

} VqaContext;

static int vqa_decode_init(AVCodecContext *avctx)
{
    VqaContext *s = (VqaContext *)avctx->priv_data;
    unsigned char *vqa_header;

    s->avctx = avctx;
    avctx->pix_fmt = PIX_FMT_PAL8;
    avctx->has_b_frames = 0;
    dsputil_init(&s->dsp, avctx);

    /* make sure the extradata made it */
    if (s->avctx->extradata_size != VQA_HEADER_SIZE) {
        printf("  VQA video: expected extradata size of %d\n", VQA_HEADER_SIZE);
        return -1;
    }

    /* load up the VQA parameters from the header */
    vqa_header = (unsigned char *)s->avctx->extradata;
    s->vqa_version = vqa_header[0];
    s->width = LE_16(&vqa_header[6]);
    s->height = LE_16(&vqa_header[8]);
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
    if (s->vqa_version == 3) {
        s->codebook = av_malloc(V3_VECTOR_SIZE * V3_MAX_VECTORS);
        s->next_codebook_buffer = av_malloc(V3_VECTOR_SIZE * V3_MAX_VECTORS);
    } else {
        s->codebook = av_malloc(V1_2_VECTOR_SIZE * V1_2_MAX_VECTORS);
        s->next_codebook_buffer = av_malloc(V1_2_VECTOR_SIZE * V1_2_MAX_VECTORS);
    }
    s->next_codebook_buffer_index = 0;

    /* allocate decode buffer */
    s->decode_buffer_size = (s->width / s->vector_width) *
        (s->height / s->vector_height) * 2;
    s->decode_buffer = av_malloc(s->decode_buffer_size);

    s->frame.data[0] = NULL;

    return 0;
}

#define CHECK_COUNT() \
    if (dest_index + count > dest_size) { \
        printf ("vqavideo: decode_format80 problem: next op would overflow dest_index\n"); \
        printf ("vqavideo: current dest_index = %d, count = %d, dest_size = %d\n", \
            dest_index, count, dest_size); \
        return; \
    }

static void decode_format80(unsigned char *src, int src_size,
    unsigned char *dest, int dest_size) {

    int src_index = 0;
    int dest_index = 0;
    int count;
    int src_pos;
    unsigned char color;
    int i;

    while (src_index < src_size) {

        vqa_debug("      opcode %02X: ", src[src_index]);

        /* 0x80 means that frame is finished */
        if (src[src_index] == 0x80)
            return;

        if (dest_index >= dest_size) {
            printf ("vqavideo: decode_format80 problem: dest_index (%d) exceeded dest_size (%d)\n",
                dest_index, dest_size);
            return;
        }

        if (src[src_index] == 0xFF) {

            src_index++;
            count = LE_16(&src[src_index]);
            src_index += 2;
            src_pos = LE_16(&src[src_index]);
            src_index += 2;
            vqa_debug("(1) copy %X bytes from absolute pos %X\n", count, src_pos);
            CHECK_COUNT();
            for (i = 0; i < count; i++)
                dest[dest_index + i] = dest[src_pos + i];
            dest_index += count;

        } else if (src[src_index] == 0xFE) {

            src_index++;
            count = LE_16(&src[src_index]);
            src_index += 2;
            color = src[src_index++];
            vqa_debug("(2) set %X bytes to %02X\n", count, color);
            CHECK_COUNT();
            memset(&dest[dest_index], color, count);
            dest_index += count;

        } else if ((src[src_index] & 0xC0) == 0xC0) {

            count = (src[src_index++] & 0x3F) + 3;
            src_pos = LE_16(&src[src_index]);
            src_index += 2;
            vqa_debug("(3) copy %X bytes from absolute pos %X\n", count, src_pos);
            CHECK_COUNT();
            for (i = 0; i < count; i++)
                dest[dest_index + i] = dest[src_pos + i];
            dest_index += count;

        } else if (src[src_index] > 0x80) {

            count = src[src_index++] & 0x3F;
            vqa_debug("(4) copy %X bytes from source to dest\n", count);
            CHECK_COUNT();
            memcpy(&dest[dest_index], &src[src_index], count);
            src_index += count;
            dest_index += count;

        } else {

            count = ((src[src_index] & 0x70) >> 4) + 3;
            src_pos = BE_16(&src[src_index]) & 0x0FFF;
            src_index += 2;
            vqa_debug("(5) copy %X bytes from relpos %X\n", count, src_pos);
            CHECK_COUNT();
            for (i = 0; i < count; i++)
                dest[dest_index + i] = dest[dest_index - src_pos + i];
            dest_index += count;
        }
    }

    if (dest_index < dest_size)
        printf ("vqavideo: decode_format80 problem: decode finished with dest_index (%d) < dest_size (%d)\n",
            dest_index, dest_size);
}

static void vqa_decode_chunk(VqaContext *s)
{

//static int frame = 0;
    unsigned int chunk_type;
    unsigned int chunk_size;
    int byte_skip;
    unsigned int index = 0;
    int i;
    unsigned char r, g, b;
    unsigned int *palette32;

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

//printf (" **** decoding frame #%d, stride = %d\n", frame++, s->frame.linesize[0]);
    /* first, traverse through the frame and find the subchunks */
    while (index < s->size) {

        chunk_type = BE_32(&s->buf[index]);
        chunk_size = BE_32(&s->buf[index + 4]);

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
            printf ("  VQA video: Found unknown chunk type: %c%c%c%c (%08X)\n",
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
        printf ("  VQA video: problem: found both CPL0 and CPLZ chunks\n");
        return;
    }

    /* decompress the palette chunk */
    if (cplz_chunk != -1) {

/* yet to be handled */

    }

    /* convert the RGB palette into the machine's endian format */
    if (cpl0_chunk != -1) {

        chunk_size = BE_32(&s->buf[cpl0_chunk + 4]);
        /* sanity check the palette size */
        if (chunk_size / 3 > 256) {
            printf ("vqavideo: problem: found a palette chunk with %d colors\n",
                chunk_size / 3);
            return;
        }
        cpl0_chunk += CHUNK_PREAMBLE_SIZE;
        palette32 = (unsigned int *)s->palette;
        for (i = 0; i < chunk_size / 3; i++) {
            /* scale by 4 to transform 6-bit palette -> 8-bit */
            r = s->buf[cpl0_chunk++] * 4;
            g = s->buf[cpl0_chunk++] * 4;
            b = s->buf[cpl0_chunk++] * 4;
            palette32[i] = (r << 16) | (g << 8) | (b);
        }
    }

    /* next, look for a full codebook */
    if ((cbf0_chunk != -1) && (cbfz_chunk != -1)) {

        /* a chunk should not have both chunk types */
        printf ("  VQA video: problem: found both CBF0 and CBFZ chunks\n");
        return;
    }

    /* decompress the full codebook chunk into the codebook accumulation
     * buffer; this is safe since only the first frame is supposed to have 
     * a full codebook */
    if (cbfz_chunk != -1) {

/* yet to be handled */

    }

    /* copy a full codebook */
    if (cbf0_chunk != -1) {

        index = cbf0_chunk;

        chunk_size = BE_32(&s->buf[cbf0_chunk + 4]);
        /* sanity check the full codebook size */
        if (s->vqa_version == 3) {
            if (chunk_size / (V3_VECTOR_SIZE) > V3_MAX_VECTORS) {
                printf ("  VQA video: problem: CBF0 chunk too large (0x%X bytes)\n",
                    chunk_size);
                return;
            }
        } else {
            if (chunk_size / (V1_2_VECTOR_SIZE) > V1_2_MAX_VECTORS) {
                printf ("  VQA video: problem: CBF0 chunk too large (0x%X bytes)\n",
                    chunk_size);
                return;
            }
        }
        cbf0_chunk += CHUNK_PREAMBLE_SIZE;

        memcpy(s->codebook, &s->buf[cbf0_chunk], chunk_size);
    }

    /* decode the frame */
    if (vptz_chunk == -1) {

        /* something is wrong if there is no VPTZ chunk */
        printf ("  VQA video: problem: no VPTZ chunk found\n");
        return;
    }

    /* decode frame */
    chunk_size = BE_32(&s->buf[vptz_chunk + 4]);
    vptz_chunk += CHUNK_PREAMBLE_SIZE;
    decode_format80(&s->buf[vptz_chunk], chunk_size,
        s->decode_buffer, s->decode_buffer_size);


if (0)
{
int count = 0;
printf (" finished decoding frame...");
for (index = 8000; index < 16000; index++)
  if (s->decode_buffer[index] > 0x0F)
    count++;
printf ("  %d/8000 hibytes exceeded 0x0F\n", count);
}


    /* render the final PAL8 frame */
    for (y = 0; y < s->frame.linesize[0] * s->height; 
        y += s->frame.linesize[0] * s->vector_height) {

        for (x = y; x < y + s->width; x += 4, lobytes++, hibytes++) {
            pixel_ptr = x;

            /* get the vector index, the method for which varies according to
             * VQA file version */
            switch (s->vqa_version) {

            case 1:
/* still need sample media for this case (only one game, "Legend of 
 * Kyrandia III : Malcolm's Revenge", is known to use this version) */
                lines = 0;
                break;

            case 2:
                lobyte = s->decode_buffer[lobytes];
                hibyte = s->decode_buffer[hibytes];
                if (hibyte > 0x0F) {
                    printf ("  VQA video: problem: vector #%d/%d high byte out of range (0x%X >= 0x0F)\n",
                        lobytes, s->decode_buffer_size / 2, hibyte);
                    hibyte = lobyte = 0;
                } else if (hibyte == 0x0F)
                    hibyte = 0;
                vector_index = (hibyte << 8) | lobyte;
                vector_index *= V1_2_VECTOR_SIZE;
                lines = 2;
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
        printf ("  VQA video: problem: found both CBP0 and CBPZ chunks\n");
        return;
    }

    if (cbp0_chunk != -1) {

        chunk_size = BE_32(&s->buf[cbp0_chunk + 4]);
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

/* more partial codebook handling ... */

    }
}

static int vqa_decode_frame(AVCodecContext *avctx,
                            void *data, int *data_size,
                            uint8_t *buf, int buf_size)
{
    VqaContext *s = (VqaContext *)avctx->priv_data;

    s->buf = buf;
    s->size = buf_size;

    if (s->frame.data[0])
        avctx->release_buffer(avctx, &s->frame);

    if (avctx->get_buffer(avctx, &s->frame)) {
        printf ("  VQA Video: get_buffer() failed\n");
        return -1;
    }

    vqa_decode_chunk(s);

    /* make the palette available on the way out */
    memcpy(s->frame.data[1], s->palette, PALETTE_COUNT * 4);

    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = s->frame;

    /* report that the buffer was completely consumed */
    return buf_size;
}

static int vqa_decode_end(AVCodecContext *avctx)
{
    VqaContext *s = (VqaContext *)avctx->priv_data;

    av_free(s->codebook);
    av_free(s->next_codebook_buffer);
    av_free(s->decode_buffer);

    if (s->frame.data[0])
        avctx->release_buffer(avctx, &s->frame);

    return 0;
}

AVCodec vqa_decoder = {
    "vqavideo",
    CODEC_TYPE_VIDEO,
    CODEC_ID_WS_VQA,
    sizeof(VqaContext),
    vqa_decode_init,
    NULL,
    vqa_decode_end,
    vqa_decode_frame,
    CODEC_CAP_DR1,
};
