/*
 * copyright (c) 2006 Michael Niedermayer <michaelni@gmx.at>
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

#include "libavutil/log.h"
#include "libavutil/opt.h"

#include "av1_parse.h"
#include "bsf.h"
#include "bsf_internal.h"
#include "h264.h"
#include "hevc.h"
#include "startcode.h"
#include "vc1_common.h"

enum RemoveFreq {
    REMOVE_FREQ_KEYFRAME,
    REMOVE_FREQ_ALL,
    REMOVE_FREQ_NONKEYFRAME,
};

#define START_CODE 0x000001

typedef struct RemoveExtradataContext {
    const AVClass *class;
    int freq;
} RemoveExtradataContext;

static int av1_split(const uint8_t *buf, int buf_size, void *logctx)
{
    AV1OBU obu;
    const uint8_t *ptr = buf, *end = buf + buf_size;

    while (ptr < end) {
        int len = ff_av1_extract_obu(&obu, ptr, buf_size, logctx);
        if (len < 0)
            break;

        if (obu.type == AV1_OBU_FRAME_HEADER ||
            obu.type == AV1_OBU_FRAME) {
            return ptr - buf;
        }
        ptr      += len;
        buf_size -= len;
    }

    return 0;
}

static int h264_split(const uint8_t *buf, int buf_size)
{
    const uint8_t *ptr = buf, *end = buf + buf_size;
    uint32_t state = -1;
    int has_sps    = 0;
    int has_pps    = 0;
    int nalu_type;

    while (ptr < end) {
        ptr = avpriv_find_start_code(ptr, end, &state);
        if ((state & 0xFFFFFF00) != 0x100)
            break;
        nalu_type = state & 0x1F;
        if (nalu_type == H264_NAL_SPS) {
            has_sps = 1;
        } else if (nalu_type == H264_NAL_PPS)
            has_pps = 1;
        /* else if (nalu_type == 0x01 ||
         *     nalu_type == 0x02 ||
         *     nalu_type == 0x05) {
         *  }
         */
        else if ((nalu_type != H264_NAL_SEI || has_pps) &&
                  nalu_type != H264_NAL_AUD && nalu_type != H264_NAL_SPS_EXT &&
                  nalu_type != 0x0f) {
            if (has_sps) {
                while (ptr - 4 > buf && ptr[-5] == 0)
                    ptr--;
                return ptr - 4 - buf;
            }
        }
    }

    return 0;
}

// Split after the parameter sets at the beginning of the stream if they exist.
static int hevc_split(const uint8_t *buf, int buf_size)
{
    const uint8_t *ptr = buf, *end = buf + buf_size;
    uint32_t state = -1;
    int has_vps = 0;
    int has_sps = 0;
    int has_pps = 0;
    int nut;

    while (ptr < end) {
        ptr = avpriv_find_start_code(ptr, end, &state);
        if ((state >> 8) != START_CODE)
            break;
        nut = (state >> 1) & 0x3F;
        if (nut == HEVC_NAL_VPS)
            has_vps = 1;
        else if (nut == HEVC_NAL_SPS)
            has_sps = 1;
        else if (nut == HEVC_NAL_PPS)
            has_pps = 1;
        else if ((nut != HEVC_NAL_SEI_PREFIX || has_pps) &&
                  nut != HEVC_NAL_AUD) {
            if (has_vps && has_sps) {
                while (ptr - 4 > buf && ptr[-5] == 0)
                    ptr--;
                return ptr - 4 - buf;
            }
        }
    }
    return 0;
}

static int mpegvideo_split(const uint8_t *buf, int buf_size)
{
    uint32_t state = -1;
    int found = 0;

    for (int i = 0; i < buf_size; i++) {
        state = (state << 8) | buf[i];
        if (state == 0x1B3) {
            found = 1;
        } else if (found && state != 0x1B5 && state < 0x200 && state >= 0x100)
            return i - 3;
    }
    return 0;
}

static int mpeg4video_split(const uint8_t *buf, int buf_size)
{
    const uint8_t *ptr = buf, *end = buf + buf_size;
    uint32_t state = -1;

    while (ptr < end) {
        ptr = avpriv_find_start_code(ptr, end, &state);
        if (state == 0x1B3 || state == 0x1B6)
            return ptr - 4 - buf;
    }

    return 0;
}

static int vc1_split(const uint8_t *buf, int buf_size)
{
    const uint8_t *ptr = buf, *end = buf + buf_size;
    uint32_t state = -1;
    int charged = 0;

    while (ptr < end) {
        ptr = avpriv_find_start_code(ptr, end, &state);
        if (state == VC1_CODE_SEQHDR || state == VC1_CODE_ENTRYPOINT) {
            charged = 1;
        } else if (charged && IS_MARKER(state))
            return ptr - 4 - buf;
    }

    return 0;
}

static int remove_extradata(AVBSFContext *ctx, AVPacket *pkt)
{
    RemoveExtradataContext *s = ctx->priv_data;

    int ret;

    ret = ff_bsf_get_packet_ref(ctx, pkt);
    if (ret < 0)
        return ret;

    if (s->freq == REMOVE_FREQ_ALL ||
        (s->freq == REMOVE_FREQ_NONKEYFRAME && !(pkt->flags & AV_PKT_FLAG_KEY)) ||
        (s->freq == REMOVE_FREQ_KEYFRAME && pkt->flags & AV_PKT_FLAG_KEY)) {
        int i;

        switch (ctx->par_in->codec_id) {
        case AV_CODEC_ID_AV1:
            i = av1_split(pkt->data, pkt->size, ctx);
            break;
        case AV_CODEC_ID_AVS2:
        case AV_CODEC_ID_AVS3:
        case AV_CODEC_ID_CAVS:
        case AV_CODEC_ID_MPEG4:
            i = mpeg4video_split(pkt->data, pkt->size);
            break;
        case AV_CODEC_ID_H264:
            i = h264_split(pkt->data, pkt->size);
            break;
        case AV_CODEC_ID_HEVC:
            i = hevc_split(pkt->data, pkt->size);
            break;
        case AV_CODEC_ID_MPEG1VIDEO:
        case AV_CODEC_ID_MPEG2VIDEO:
            i = mpegvideo_split(pkt->data, pkt->size);
            break;
        case AV_CODEC_ID_VC1:
            i = vc1_split(pkt->data, pkt->size);
            break;
        default:
            i = 0;
        }

        pkt->data += i;
        pkt->size -= i;
    }

    return 0;
}

#define OFFSET(x) offsetof(RemoveExtradataContext, x)
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_BSF_PARAM)
static const AVOption options[] = {
    { "freq", NULL, OFFSET(freq), AV_OPT_TYPE_INT, { .i64 = REMOVE_FREQ_KEYFRAME }, REMOVE_FREQ_KEYFRAME, REMOVE_FREQ_NONKEYFRAME, FLAGS, "freq" },
        { "k",        NULL, 0, AV_OPT_TYPE_CONST, { .i64 = REMOVE_FREQ_NONKEYFRAME }, .flags = FLAGS, .unit = "freq" },
        { "keyframe", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = REMOVE_FREQ_KEYFRAME }, .flags = FLAGS, .unit = "freq" },
        { "e",        NULL, 0, AV_OPT_TYPE_CONST, { .i64 = REMOVE_FREQ_ALL      }, .flags = FLAGS, .unit = "freq" },
        { "all",      NULL, 0, AV_OPT_TYPE_CONST, { .i64 = REMOVE_FREQ_ALL      }, .flags = FLAGS, .unit = "freq" },
    { NULL },
};

static const AVClass remove_extradata_class = {
    .class_name = "remove_extradata",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFBitStreamFilter ff_remove_extradata_bsf = {
    .p.name         = "remove_extra",
    .p.priv_class   = &remove_extradata_class,
    .priv_data_size = sizeof(RemoveExtradataContext),
    .filter         = remove_extradata,
};
