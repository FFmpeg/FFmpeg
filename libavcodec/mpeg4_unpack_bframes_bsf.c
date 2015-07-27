/*
 * Bitstream filter for unpacking DivX-style packed B-frames in MPEG-4 (divx_packed)
 * Copyright (c) 2015 Andreas Cadhalpun <Andreas.Cadhalpun@googlemail.com>
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

#include "avcodec.h"
#include "mpeg4video.h"

typedef struct UnpackBFramesBSFContext {
    uint8_t *b_frame_buf;
    int      b_frame_buf_size;
    int      updated_extradata;
} UnpackBFramesBSFContext;

/* search next start code */
static unsigned int find_startcode(const uint8_t *buf, int buf_size, int *pos)
{
    unsigned int startcode = 0xFF;

    for (; *pos < buf_size;) {
        startcode = ((startcode << 8) | buf[*pos]) & 0xFFFFFFFF;
        *pos +=1;
        if ((startcode & 0xFFFFFF00) != 0x100)
            continue;  /* no startcode */
        return startcode;
    }

    return 0;
}

/* determine the position of the packed marker in the userdata,
 * the number of VOPs and the position of the second VOP */
static void scan_buffer(const uint8_t *buf, int buf_size,
                        int *pos_p, int *nb_vop, int *pos_vop2) {
    unsigned int startcode;
    int pos, i;

    for (pos = 0; pos < buf_size;) {
        startcode = find_startcode(buf, buf_size, &pos);

        if (startcode == USER_DATA_STARTCODE && pos_p) {
            /* check if the (DivX) userdata string ends with 'p' (packed) */
            for (i = 0; i < 255 && pos + i + 1 < buf_size; i++) {
                if (buf[pos + i] == 'p' && buf[pos + i + 1] == '\0') {
                    *pos_p = pos + i;
                    break;
                }
            }
        } else if (startcode == VOP_STARTCODE && nb_vop) {
            *nb_vop += 1;
            if (*nb_vop == 2 && pos_vop2) {
                *pos_vop2 = pos - 4; /* subtract 4 bytes startcode */
            }
        }
    }
}

/* allocate new buffer and copy size bytes from src */
static uint8_t *create_new_buffer(const uint8_t *src, int size) {
    uint8_t *dst = av_malloc(size + AV_INPUT_BUFFER_PADDING_SIZE);

    if (dst) {
        memcpy(dst, src, size);
        memset(dst + size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    }

    return dst;
}

static int mpeg4_unpack_bframes_filter(AVBitStreamFilterContext *bsfc,
                                       AVCodecContext *avctx, const char *args,
                                       uint8_t  **poutbuf, int *poutbuf_size,
                                       const uint8_t *buf, int      buf_size,
                                       int keyframe)
{
    UnpackBFramesBSFContext *ctx = bsfc->priv_data;
    int pos_p = -1, nb_vop = 0, pos_vop2 = -1, ret = 0;

    if (avctx->codec_id != AV_CODEC_ID_MPEG4) {
        av_log(avctx, AV_LOG_ERROR,
               "The mpeg4_unpack_bframes bitstream filter is only useful for mpeg4.\n");
        return AVERROR(EINVAL);
    }

    if (!ctx->updated_extradata && avctx->extradata) {
        int pos_p_ext = -1;
        scan_buffer(avctx->extradata, avctx->extradata_size, &pos_p_ext, NULL, NULL);
        if (pos_p_ext >= 0) {
            av_log(avctx, AV_LOG_DEBUG,
                   "Updating DivX userdata (remove trailing 'p') in extradata.\n");
            avctx->extradata[pos_p_ext] = '\0';
        }
        ctx->updated_extradata = 1;
    }

    scan_buffer(buf, buf_size, &pos_p, &nb_vop, &pos_vop2);
    av_log(avctx, AV_LOG_DEBUG, "Found %d VOP startcode(s) in this packet.\n", nb_vop);

    if (pos_vop2 >= 0) {
        if (ctx->b_frame_buf) {
            av_log(avctx, AV_LOG_WARNING,
                   "Missing one N-VOP packet, discarding one B-frame.\n");
            av_freep(&ctx->b_frame_buf);
            ctx->b_frame_buf_size = 0;
        }
        /* store the packed B-frame in the BSFContext */
        ctx->b_frame_buf_size = buf_size - pos_vop2;
        ctx->b_frame_buf      = create_new_buffer(buf + pos_vop2, ctx->b_frame_buf_size);
        if (!ctx->b_frame_buf) {
            ctx->b_frame_buf_size = 0;
            return AVERROR(ENOMEM);
        }
    }

    if (nb_vop > 2) {
        av_log(avctx, AV_LOG_WARNING,
       "Found %d VOP headers in one packet, only unpacking one.\n", nb_vop);
    }

    if (nb_vop == 1 && ctx->b_frame_buf) {
        /* use frame from BSFContext */
        *poutbuf      = ctx->b_frame_buf;
        *poutbuf_size = ctx->b_frame_buf_size;
        /* the output buffer is distinct from the input buffer */
        ret = 1;
        if (buf_size <= MAX_NVOP_SIZE) {
            /* N-VOP */
            av_log(avctx, AV_LOG_DEBUG, "Skipping N-VOP.\n");
            ctx->b_frame_buf      = NULL;
            ctx->b_frame_buf_size = 0;
        } else {
            /* copy packet into BSFContext */
            ctx->b_frame_buf_size = buf_size;
            ctx->b_frame_buf      = create_new_buffer(buf , buf_size);
            if (!ctx->b_frame_buf) {
                ctx->b_frame_buf_size = 0;
                av_freep(poutbuf);
                *poutbuf_size = 0;
                return AVERROR(ENOMEM);
            }
        }
    } else if (nb_vop >= 2) {
        /* use first frame of the packet */
        *poutbuf      = (uint8_t *) buf;
        *poutbuf_size = pos_vop2;
    } else if (pos_p >= 0) {
        av_log(avctx, AV_LOG_DEBUG, "Updating DivX userdata (remove trailing 'p').\n");
        *poutbuf_size = buf_size;
        *poutbuf      = create_new_buffer(buf, buf_size);
        if (!*poutbuf) {
            *poutbuf_size = 0;
            return AVERROR(ENOMEM);
        }
        /* remove 'p' (packed) from the end of the (DivX) userdata string */
        (*poutbuf)[pos_p] = '\0';
        /* the output buffer is distinct from the input buffer */
        ret = 1;
    } else {
        /* copy packet */
        *poutbuf      = (uint8_t *) buf;
        *poutbuf_size = buf_size;
    }

    return ret;
}

static void mpeg4_unpack_bframes_close(AVBitStreamFilterContext *bsfc)
{
    UnpackBFramesBSFContext *ctx = bsfc->priv_data;
    av_freep(&ctx->b_frame_buf);
}

AVBitStreamFilter ff_mpeg4_unpack_bframes_bsf = {
    .name           = "mpeg4_unpack_bframes",
    .priv_data_size = sizeof(UnpackBFramesBSFContext),
    .filter         = mpeg4_unpack_bframes_filter,
    .close          = mpeg4_unpack_bframes_close
};
