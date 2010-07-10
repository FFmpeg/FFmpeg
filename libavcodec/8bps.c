/*
 * Quicktime Planar RGB (8BPS) Video Decoder
 * Copyright (C) 2003 Roberto Togni
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
 * QT 8BPS Video Decoder by Roberto Togni
 * For more information about the 8BPS format, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 *
 * Supports: PAL8 (RGB 8bpp, paletted)
 *         : BGR24 (RGB 24bpp) (can also output it as RGB32)
 *         : RGB32 (RGB 32bpp, 4th plane is probably alpha and it's ignored)
 *
 */

#include <stdio.h>
#include <stdlib.h>

#include "libavutil/intreadwrite.h"
#include "avcodec.h"


static const enum PixelFormat pixfmt_rgb24[] = {PIX_FMT_BGR24, PIX_FMT_RGB32, PIX_FMT_NONE};

/*
 * Decoder context
 */
typedef struct EightBpsContext {

        AVCodecContext *avctx;
        AVFrame pic;

        unsigned char planes;
        unsigned char planemap[4];
} EightBpsContext;


/*
 *
 * Decode a frame
 *
 */
static int decode_frame(AVCodecContext *avctx, void *data, int *data_size, AVPacket *avpkt)
{
        const uint8_t *buf = avpkt->data;
        int buf_size = avpkt->size;
        EightBpsContext * const c = avctx->priv_data;
        const unsigned char *encoded = buf;
        unsigned char *pixptr, *pixptr_end;
        unsigned int height = avctx->height; // Real image height
        unsigned int dlen, p, row;
        const unsigned char *lp, *dp;
        unsigned char count;
        unsigned int px_inc;
        unsigned int planes = c->planes;
        unsigned char *planemap = c->planemap;

        if(c->pic.data[0])
                avctx->release_buffer(avctx, &c->pic);

        c->pic.reference = 0;
        c->pic.buffer_hints = FF_BUFFER_HINTS_VALID;
        if(avctx->get_buffer(avctx, &c->pic) < 0){
                av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
                return -1;
        }

        /* Set data pointer after line lengths */
        dp = encoded + planes * (height << 1);

        /* Ignore alpha plane, don't know what to do with it */
        if (planes == 4)
                planes--;

        px_inc = planes + (avctx->pix_fmt == PIX_FMT_RGB32);

        for (p = 0; p < planes; p++) {
                /* Lines length pointer for this plane */
                lp = encoded + p * (height << 1);

                /* Decode a plane */
                for(row = 0; row < height; row++) {
                        pixptr = c->pic.data[0] + row * c->pic.linesize[0] + planemap[p];
                        pixptr_end = pixptr + c->pic.linesize[0];
                        dlen = av_be2ne16(*(const unsigned short *)(lp+row*2));
                        /* Decode a row of this plane */
                        while(dlen > 0) {
                                if(dp + 1 >= buf+buf_size) return -1;
                                if ((count = *dp++) <= 127) {
                                        count++;
                                        dlen -= count + 1;
                                        if (pixptr + count * px_inc > pixptr_end)
                                            break;
                                        if(dp + count > buf+buf_size) return -1;
                                        while(count--) {
                                                *pixptr = *dp++;
                                                pixptr += px_inc;
                                        }
                                } else {
                                        count = 257 - count;
                                        if (pixptr + count * px_inc > pixptr_end)
                                            break;
                                        while(count--) {
                                                *pixptr = *dp;
                                                pixptr += px_inc;
                                        }
                                        dp++;
                                        dlen -= 2;
                                }
                        }
                }
        }

        if (avctx->palctrl) {
                memcpy (c->pic.data[1], avctx->palctrl->palette, AVPALETTE_SIZE);
                if (avctx->palctrl->palette_changed) {
                        c->pic.palette_has_changed = 1;
                        avctx->palctrl->palette_changed = 0;
                } else
                        c->pic.palette_has_changed = 0;
        }

        *data_size = sizeof(AVFrame);
        *(AVFrame*)data = c->pic;

        /* always report that the buffer was completely consumed */
        return buf_size;
}


/*
 *
 * Init 8BPS decoder
 *
 */
static av_cold int decode_init(AVCodecContext *avctx)
{
        EightBpsContext * const c = avctx->priv_data;

        c->avctx = avctx;

        c->pic.data[0] = NULL;

        switch (avctx->bits_per_coded_sample) {
                case 8:
                        avctx->pix_fmt = PIX_FMT_PAL8;
                        c->planes = 1;
                        c->planemap[0] = 0; // 1st plane is palette indexes
                        if (avctx->palctrl == NULL) {
                                av_log(avctx, AV_LOG_ERROR, "Error: PAL8 format but no palette from demuxer.\n");
                                return -1;
                        }
                        break;
                case 24:
                        avctx->pix_fmt = avctx->get_format(avctx, pixfmt_rgb24);
                        c->planes = 3;
                        c->planemap[0] = 2; // 1st plane is red
                        c->planemap[1] = 1; // 2nd plane is green
                        c->planemap[2] = 0; // 3rd plane is blue
                        break;
                case 32:
                        avctx->pix_fmt = PIX_FMT_RGB32;
                        c->planes = 4;
#if HAVE_BIGENDIAN
                        c->planemap[0] = 1; // 1st plane is red
                        c->planemap[1] = 2; // 2nd plane is green
                        c->planemap[2] = 3; // 3rd plane is blue
                        c->planemap[3] = 0; // 4th plane is alpha???
#else
                        c->planemap[0] = 2; // 1st plane is red
                        c->planemap[1] = 1; // 2nd plane is green
                        c->planemap[2] = 0; // 3rd plane is blue
                        c->planemap[3] = 3; // 4th plane is alpha???
#endif
                        break;
                default:
                        av_log(avctx, AV_LOG_ERROR, "Error: Unsupported color depth: %u.\n", avctx->bits_per_coded_sample);
                        return -1;
        }

  return 0;
}




/*
 *
 * Uninit 8BPS decoder
 *
 */
static av_cold int decode_end(AVCodecContext *avctx)
{
        EightBpsContext * const c = avctx->priv_data;

        if (c->pic.data[0])
                avctx->release_buffer(avctx, &c->pic);

        return 0;
}



AVCodec eightbps_decoder = {
        "8bps",
        AVMEDIA_TYPE_VIDEO,
        CODEC_ID_8BPS,
        sizeof(EightBpsContext),
        decode_init,
        NULL,
        decode_end,
        decode_frame,
        CODEC_CAP_DR1,
        .long_name = NULL_IF_CONFIG_SMALL("QuickTime 8BPS video"),
};
