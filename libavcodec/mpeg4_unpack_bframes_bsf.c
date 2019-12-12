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
#include "bsf.h"
#include "internal.h"
#include "mpeg4video.h"

typedef struct UnpackBFramesBSFContext {
    AVBufferRef *b_frame_ref;
} UnpackBFramesBSFContext;

/* determine the position of the packed marker in the userdata,
 * the number of VOPs and the position of the second VOP */
static void scan_buffer(const uint8_t *buf, int buf_size,
                        int *pos_p, int *nb_vop, int *pos_vop2) {
    uint32_t startcode;
    const uint8_t *end = buf + buf_size, *pos = buf;

    while (pos < end) {
        startcode = -1;
        pos = avpriv_find_start_code(pos, end, &startcode);

        if (startcode == USER_DATA_STARTCODE && pos_p) {
            /* check if the (DivX) userdata string ends with 'p' (packed) */
            for (int i = 0; i < 255 && pos + i + 1 < end; i++) {
                if (pos[i] == 'p' && pos[i + 1] == '\0') {
                    *pos_p = pos + i - buf;
                    break;
                }
            }
        } else if (startcode == VOP_STARTCODE && nb_vop) {
            *nb_vop += 1;
            if (*nb_vop == 2 && pos_vop2) {
                *pos_vop2 = pos - buf - 4; /* subtract 4 bytes startcode */
            }
        }
    }
}

static int mpeg4_unpack_bframes_filter(AVBSFContext *ctx, AVPacket *pkt)
{
    UnpackBFramesBSFContext *s = ctx->priv_data;
    int pos_p = -1, nb_vop = 0, pos_vop2 = -1, ret = 0;

    ret = ff_bsf_get_packet_ref(ctx, pkt);
    if (ret < 0)
        return ret;

    scan_buffer(pkt->data, pkt->size, &pos_p, &nb_vop, &pos_vop2);
    av_log(ctx, AV_LOG_DEBUG, "Found %d VOP startcode(s) in this packet.\n", nb_vop);

    if (pos_vop2 >= 0) {
        if (s->b_frame_ref) {
            av_log(ctx, AV_LOG_WARNING,
                   "Missing one N-VOP packet, discarding one B-frame.\n");
            av_buffer_unref(&s->b_frame_ref);
        }
        /* store a reference to the packed B-frame's data in the BSFContext */
        s->b_frame_ref = av_buffer_ref(pkt->buf);
        if (!s->b_frame_ref) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        s->b_frame_ref->data = pkt->data + pos_vop2;
        s->b_frame_ref->size = pkt->size - pos_vop2;
    }

    if (nb_vop > 2) {
        av_log(ctx, AV_LOG_WARNING,
       "Found %d VOP headers in one packet, only unpacking one.\n", nb_vop);
    }

    if (nb_vop == 1 && s->b_frame_ref) {
        AVBufferRef *tmp = pkt->buf;

        /* make tmp accurately reflect the packet's data */
        tmp->data = pkt->data;
        tmp->size = pkt->size;

        /* replace data in packet with stored data */
        pkt->buf  = s->b_frame_ref;
        pkt->data = s->b_frame_ref->data;
        pkt->size = s->b_frame_ref->size;

        /* store reference to data into BSFContext */
        s->b_frame_ref = tmp;

        if (s->b_frame_ref->size <= MAX_NVOP_SIZE) {
            /* N-VOP - discard stored data */
            av_log(ctx, AV_LOG_DEBUG, "Skipping N-VOP.\n");
            av_buffer_unref(&s->b_frame_ref);
        }
    } else if (nb_vop >= 2) {
        /* use first frame of the packet */
        pkt->size = pos_vop2;
    } else if (pos_p >= 0) {
        ret = av_packet_make_writable(pkt);
        if (ret < 0)
            goto fail;
        av_log(ctx, AV_LOG_DEBUG, "Updating DivX userdata (remove trailing 'p').\n");
        /* remove 'p' (packed) from the end of the (DivX) userdata string */
        pkt->data[pos_p] = '\0';
    } else {
        /* use packet as is */
    }

fail:
    if (ret < 0)
        av_packet_unref(pkt);

    return ret;
}

static int mpeg4_unpack_bframes_init(AVBSFContext *ctx)
{
    if (ctx->par_in->extradata) {
        int pos_p_ext = -1;
        scan_buffer(ctx->par_in->extradata, ctx->par_in->extradata_size, &pos_p_ext, NULL, NULL);
        if (pos_p_ext >= 0) {
            av_log(ctx, AV_LOG_DEBUG,
                   "Updating DivX userdata (remove trailing 'p') in extradata.\n");
            ctx->par_out->extradata[pos_p_ext] = '\0';
        }
    }

    return 0;
}

static void mpeg4_unpack_bframes_close_flush(AVBSFContext *bsfc)
{
    UnpackBFramesBSFContext *ctx = bsfc->priv_data;
    av_buffer_unref(&ctx->b_frame_ref);
}

static const enum AVCodecID codec_ids[] = {
    AV_CODEC_ID_MPEG4, AV_CODEC_ID_NONE,
};

const AVBitStreamFilter ff_mpeg4_unpack_bframes_bsf = {
    .name           = "mpeg4_unpack_bframes",
    .priv_data_size = sizeof(UnpackBFramesBSFContext),
    .init           = mpeg4_unpack_bframes_init,
    .filter         = mpeg4_unpack_bframes_filter,
    .flush          = mpeg4_unpack_bframes_close_flush,
    .close          = mpeg4_unpack_bframes_close_flush,
    .codec_ids      = codec_ids,
};
