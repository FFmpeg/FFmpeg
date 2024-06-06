/*
 * Intel MediaSDK QSV based HEVC encoder
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


#include <stdint.h>
#include <sys/types.h>

#include <mfxvideo.h>

#include "libavutil/common.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/mastering_display_metadata.h"

#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "get_bits.h"
#include "h2645_parse.h"
#include "qsv.h"
#include "qsvenc.h"

#include "hevc/hevc.h"
#include "hevc/ps.h"

enum LoadPlugin {
    LOAD_PLUGIN_NONE,
    LOAD_PLUGIN_HEVC_SW,
    LOAD_PLUGIN_HEVC_HW,
};

typedef struct QSVHEVCEncContext {
    AVClass *class;
    QSVEncContext qsv;
    int load_plugin;
} QSVHEVCEncContext;

static int generate_fake_vps(QSVEncContext *q, AVCodecContext *avctx)
{
    GetByteContext gbc;
    PutByteContext pbc;

    GetBitContext gb;
    H2645RBSP sps_rbsp = { NULL };
    H2645NAL sps_nal = { NULL };
    HEVCSPS sps = { 0 };
    HEVCVPS vps = { 0 };
    uint8_t vps_buf[128], vps_rbsp_buf[128];
    uint8_t *new_extradata;
    unsigned int sps_id;
    int ret, i, type, vps_size;

    if (!avctx->extradata_size) {
        av_log(avctx, AV_LOG_ERROR, "No extradata returned from libmfx\n");
        return AVERROR_UNKNOWN;
    }

    av_fast_padded_malloc(&sps_rbsp.rbsp_buffer, &sps_rbsp.rbsp_buffer_alloc_size, avctx->extradata_size);
    if (!sps_rbsp.rbsp_buffer)
        return AVERROR(ENOMEM);

    /* parse the SPS */
    ret = ff_h2645_extract_rbsp(avctx->extradata + 4, avctx->extradata_size - 4, &sps_rbsp, &sps_nal, 1);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Error unescaping the SPS buffer\n");
        return ret;
    }

    ret = init_get_bits8(&gb, sps_nal.data, sps_nal.size);
    if (ret < 0) {
        av_freep(&sps_rbsp.rbsp_buffer);
        return ret;
    }

    get_bits(&gb, 1);
    type = get_bits(&gb, 6);
    if (type != HEVC_NAL_SPS) {
        av_log(avctx, AV_LOG_ERROR, "Unexpected NAL type in the extradata: %d\n",
               type);
        av_freep(&sps_rbsp.rbsp_buffer);
        return AVERROR_INVALIDDATA;
    }
    get_bits(&gb, 9);

    ret = ff_hevc_parse_sps(&sps, &gb, &sps_id, 0, 0, NULL, avctx);
    av_freep(&sps_rbsp.rbsp_buffer);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Error parsing the SPS\n");
        return ret;
    }

    /* generate the VPS */
    vps.vps_max_layers     = 1;
    vps.vps_max_sub_layers = sps.max_sub_layers;
    vps.vps_temporal_id_nesting_flag = sps.temporal_id_nesting;
    memcpy(&vps.ptl, &sps.ptl, sizeof(vps.ptl));
    vps.vps_sub_layer_ordering_info_present_flag = 1;
    for (i = 0; i < HEVC_MAX_SUB_LAYERS; i++) {
        vps.vps_max_dec_pic_buffering[i] = sps.temporal_layer[i].max_dec_pic_buffering;
        vps.vps_num_reorder_pics[i]      = sps.temporal_layer[i].num_reorder_pics;
        vps.vps_max_latency_increase[i]  = sps.temporal_layer[i].max_latency_increase;
    }

    vps.vps_num_layer_sets                  = 1;
    vps.vps_timing_info_present_flag        = sps.vui.vui_timing_info_present_flag;
    vps.vps_num_units_in_tick               = sps.vui.vui_num_units_in_tick;
    vps.vps_time_scale                      = sps.vui.vui_time_scale;
    vps.vps_poc_proportional_to_timing_flag = sps.vui.vui_poc_proportional_to_timing_flag;
    vps.vps_num_ticks_poc_diff_one          = sps.vui.vui_num_ticks_poc_diff_one_minus1 + 1;
    vps.vps_num_hrd_parameters              = 0;

    /* generate the encoded RBSP form of the VPS */
    ret = ff_hevc_encode_nal_vps(&vps, sps.vps_id, vps_rbsp_buf, sizeof(vps_rbsp_buf));
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Error writing the VPS\n");
        return ret;
    }

    /* escape and add the startcode */
    bytestream2_init(&gbc, vps_rbsp_buf, ret);
    bytestream2_init_writer(&pbc, vps_buf, sizeof(vps_buf));

    bytestream2_put_be32(&pbc, 1);                 // startcode
    bytestream2_put_byte(&pbc, HEVC_NAL_VPS << 1); // NAL
    bytestream2_put_byte(&pbc, 1);                 // header

    while (bytestream2_get_bytes_left(&gbc)) {
        if (bytestream2_get_bytes_left(&gbc) >= 3 && bytestream2_peek_be24(&gbc) <= 3) {
            bytestream2_put_be24(&pbc, 3);
            bytestream2_skip(&gbc, 2);
        } else
            bytestream2_put_byte(&pbc, bytestream2_get_byte(&gbc));
    }

    vps_size = bytestream2_tell_p(&pbc);
    new_extradata = av_mallocz(vps_size + avctx->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!new_extradata)
        return AVERROR(ENOMEM);
    memcpy(new_extradata, vps_buf, vps_size);
    memcpy(new_extradata + vps_size, avctx->extradata, avctx->extradata_size);

    av_freep(&avctx->extradata);
    avctx->extradata       = new_extradata;
    avctx->extradata_size += vps_size;

    return 0;
}

static int qsv_hevc_set_encode_ctrl(AVCodecContext *avctx,
                                    const AVFrame *frame, mfxEncodeCtrl *enc_ctrl)
{
    QSVHEVCEncContext *q = avctx->priv_data;
    AVFrameSideData *sd;

    if (!frame || !QSV_RUNTIME_VERSION_ATLEAST(q->qsv.ver, 1, 25))
        return 0;

    sd = av_frame_get_side_data(frame, AV_FRAME_DATA_MASTERING_DISPLAY_METADATA);
    if (sd) {
        AVMasteringDisplayMetadata *mdm = (AVMasteringDisplayMetadata *)sd->data;

        // SEI is needed when both the primaries and luminance are set
        if (mdm->has_primaries && mdm->has_luminance) {
            const int mapping[3] = {1, 2, 0};
            const int chroma_den = 50000;
            const int luma_den   = 10000;
            int i;
            mfxExtMasteringDisplayColourVolume *mdcv = av_mallocz(sizeof(mfxExtMasteringDisplayColourVolume));

            if (!mdcv)
                return AVERROR(ENOMEM);

            mdcv->Header.BufferId = MFX_EXTBUFF_MASTERING_DISPLAY_COLOUR_VOLUME;
            mdcv->Header.BufferSz = sizeof(*mdcv);

            for (i = 0; i < 3; i++) {
                const int j = mapping[i];

                mdcv->DisplayPrimariesX[i] =
                    FFMIN(lrint(chroma_den *
                                av_q2d(mdm->display_primaries[j][0])),
                          chroma_den);
                mdcv->DisplayPrimariesY[i] =
                    FFMIN(lrint(chroma_den *
                                av_q2d(mdm->display_primaries[j][1])),
                          chroma_den);
            }

            mdcv->WhitePointX =
                FFMIN(lrint(chroma_den * av_q2d(mdm->white_point[0])),
                      chroma_den);
            mdcv->WhitePointY =
                FFMIN(lrint(chroma_den * av_q2d(mdm->white_point[1])),
                      chroma_den);

            mdcv->MaxDisplayMasteringLuminance =
                lrint(luma_den * av_q2d(mdm->max_luminance));
            mdcv->MinDisplayMasteringLuminance =
                FFMIN(lrint(luma_den * av_q2d(mdm->min_luminance)),
                      mdcv->MaxDisplayMasteringLuminance);

            enc_ctrl->ExtParam[enc_ctrl->NumExtParam++] = (mfxExtBuffer *)mdcv;
        }
    }

    sd = av_frame_get_side_data(frame, AV_FRAME_DATA_CONTENT_LIGHT_LEVEL);
    if (sd) {
        AVContentLightMetadata *clm = (AVContentLightMetadata *)sd->data;
        mfxExtContentLightLevelInfo * clli = av_mallocz(sizeof(mfxExtContentLightLevelInfo));

        if (!clli)
            return AVERROR(ENOMEM);

        clli->Header.BufferId = MFX_EXTBUFF_CONTENT_LIGHT_LEVEL_INFO;
        clli->Header.BufferSz = sizeof(*clli);

        clli->MaxContentLightLevel          = FFMIN(clm->MaxCLL,  65535);
        clli->MaxPicAverageLightLevel       = FFMIN(clm->MaxFALL, 65535);

        enc_ctrl->ExtParam[enc_ctrl->NumExtParam++] = (mfxExtBuffer *)clli;
    }

    return 0;
}

static av_cold int qsv_enc_init(AVCodecContext *avctx)
{
    QSVHEVCEncContext *q = avctx->priv_data;
    int ret;

    if (q->load_plugin != LOAD_PLUGIN_NONE) {
        static const char * const uid_hevcenc_sw = "2fca99749fdb49aeb121a5b63ef568f7";
        static const char * const uid_hevcenc_hw = "6fadc791a0c2eb479ab6dcd5ea9da347";

        if (q->qsv.load_plugins[0]) {
            av_log(avctx, AV_LOG_WARNING,
                   "load_plugins is not empty, but load_plugin is not set to 'none'."
                   "The load_plugin value will be ignored.\n");
        } else {
            av_freep(&q->qsv.load_plugins);

            if (q->load_plugin == LOAD_PLUGIN_HEVC_SW)
                q->qsv.load_plugins = av_strdup(uid_hevcenc_sw);
            else
                q->qsv.load_plugins = av_strdup(uid_hevcenc_hw);

            if (!q->qsv.load_plugins)
                return AVERROR(ENOMEM);
        }
    }

    // HEVC and H264 meaning of the value is shifted by 1, make it consistent
    q->qsv.idr_interval++;

    q->qsv.set_encode_ctrl_cb = qsv_hevc_set_encode_ctrl;

    ret = ff_qsv_enc_init(avctx, &q->qsv);
    if (ret < 0)
        return ret;

    if (!q->qsv.hevc_vps) {
        ret = generate_fake_vps(&q->qsv, avctx);
        if (ret < 0) {
            ff_qsv_enc_close(avctx, &q->qsv);
            return ret;
        }
    }

    return 0;
}

static int qsv_enc_frame(AVCodecContext *avctx, AVPacket *pkt,
                         const AVFrame *frame, int *got_packet)
{
    QSVHEVCEncContext *q = avctx->priv_data;

    return ff_qsv_encode(avctx, &q->qsv, pkt, frame, got_packet);
}

static av_cold int qsv_enc_close(AVCodecContext *avctx)
{
    QSVHEVCEncContext *q = avctx->priv_data;

    return ff_qsv_enc_close(avctx, &q->qsv);
}

#define OFFSET(x) offsetof(QSVHEVCEncContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    QSV_COMMON_OPTS
    QSV_OPTION_RDO
    QSV_OPTION_MAX_FRAME_SIZE
    QSV_OPTION_MAX_SLICE_SIZE
    QSV_OPTION_MBBRC
    QSV_OPTION_EXTBRC
    QSV_OPTION_P_STRATEGY
    QSV_OPTION_B_STRATEGY
    QSV_OPTION_DBLK_IDC
    QSV_OPTION_LOW_DELAY_BRC
    QSV_OPTION_MAX_MIN_QP
    QSV_OPTION_ADAPTIVE_I
    QSV_OPTION_ADAPTIVE_B
    QSV_OPTION_SCENARIO
    QSV_OPTION_AVBR
    QSV_OPTION_SKIP_FRAME
#if QSV_HAVE_HE
    QSV_HE_OPTIONS
#endif

    { "idr_interval", "Distance (in I-frames) between IDR frames", OFFSET(qsv.idr_interval), AV_OPT_TYPE_INT, { .i64 = 0 }, -1, INT_MAX, VE, .unit = "idr_interval" },
    { "begin_only", "Output an IDR-frame only at the beginning of the stream", 0, AV_OPT_TYPE_CONST, { .i64 = -1 }, 0, 0, VE, .unit = "idr_interval" },
    { "load_plugin", "A user plugin to load in an internal session", OFFSET(load_plugin), AV_OPT_TYPE_INT, { .i64 = LOAD_PLUGIN_HEVC_HW }, LOAD_PLUGIN_NONE, LOAD_PLUGIN_HEVC_HW, VE, .unit = "load_plugin" },
    { "none",     NULL, 0, AV_OPT_TYPE_CONST, { .i64 = LOAD_PLUGIN_NONE },    0, 0, VE, .unit = "load_plugin" },
    { "hevc_sw",  NULL, 0, AV_OPT_TYPE_CONST, { .i64 = LOAD_PLUGIN_HEVC_SW }, 0, 0, VE, .unit = "load_plugin" },
    { "hevc_hw",  NULL, 0, AV_OPT_TYPE_CONST, { .i64 = LOAD_PLUGIN_HEVC_HW }, 0, 0, VE, .unit = "load_plugin" },

    { "load_plugins", "A :-separate list of hexadecimal plugin UIDs to load in an internal session",
        OFFSET(qsv.load_plugins), AV_OPT_TYPE_STRING, { .str = "" }, 0, 0, VE },

    { "look_ahead_depth", "Depth of look ahead in number frames, available when extbrc option is enabled", OFFSET(qsv.look_ahead_depth), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 100, VE },
    { "profile", NULL, OFFSET(qsv.profile), AV_OPT_TYPE_INT, { .i64 = MFX_PROFILE_UNKNOWN }, 0, INT_MAX, VE, .unit = "profile" },
    { "unknown", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_PROFILE_UNKNOWN      }, INT_MIN, INT_MAX,     VE, .unit = "profile" },
    { "main",    NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_PROFILE_HEVC_MAIN    }, INT_MIN, INT_MAX,     VE, .unit = "profile" },
    { "main10",  NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_PROFILE_HEVC_MAIN10  }, INT_MIN, INT_MAX,     VE, .unit = "profile" },
    { "mainsp",  NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_PROFILE_HEVC_MAINSP  }, INT_MIN, INT_MAX,     VE, .unit = "profile" },
    { "rext",    NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_PROFILE_HEVC_REXT    }, INT_MIN, INT_MAX,     VE, .unit = "profile" },
#if QSV_VERSION_ATLEAST(1, 32)
    { "scc",     NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_PROFILE_HEVC_SCC     }, INT_MIN, INT_MAX,     VE, .unit = "profile" },
#endif
    { "tier",    "Set the encoding tier (only level >= 4 can support high tier)", OFFSET(qsv.tier), AV_OPT_TYPE_INT, { .i64 = MFX_TIER_HEVC_HIGH }, MFX_TIER_HEVC_MAIN, MFX_TIER_HEVC_HIGH, VE, .unit = "tier" },
    { "main",    NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_TIER_HEVC_MAIN       }, INT_MIN, INT_MAX,     VE, .unit = "tier" },
    { "high",    NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_TIER_HEVC_HIGH       }, INT_MIN, INT_MAX,     VE, .unit = "tier" },

    { "gpb", "1: GPB (generalized P/B frame); 0: regular P frame", OFFSET(qsv.gpb), AV_OPT_TYPE_BOOL, { .i64 = 1 }, 0, 1, VE},

    { "tile_cols",  "Number of columns for tiled encoding",   OFFSET(qsv.tile_cols),    AV_OPT_TYPE_INT, { .i64 = 0 }, 0, UINT16_MAX, VE },
    { "tile_rows",  "Number of rows for tiled encoding",      OFFSET(qsv.tile_rows),    AV_OPT_TYPE_INT, { .i64 = 0 }, 0, UINT16_MAX, VE },
    { "recovery_point_sei", "Insert recovery point SEI messages",       OFFSET(qsv.recovery_point_sei),      AV_OPT_TYPE_INT, { .i64 = -1 },               -1,          1, VE },
    { "aud", "Insert the Access Unit Delimiter NAL", OFFSET(qsv.aud), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, VE},
    { "pic_timing_sei",    "Insert picture timing SEI with pic_struct_syntax element", OFFSET(qsv.pic_timing_sei), AV_OPT_TYPE_BOOL, { .i64 = 1 }, 0, 1, VE },
    { "transform_skip", "Turn this option ON to enable transformskip",   OFFSET(qsv.transform_skip),          AV_OPT_TYPE_INT,    { .i64 = -1},   -1, 1,  VE},
    { "int_ref_type", "Intra refresh type. B frames should be set to 0",         OFFSET(qsv.int_ref_type),            AV_OPT_TYPE_INT, { .i64 = -1 }, -1, UINT16_MAX, VE, .unit = "int_ref_type" },
        { "none",     NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 0 }, .flags = VE, .unit = "int_ref_type" },
        { "vertical", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 1 }, .flags = VE, .unit = "int_ref_type" },
        { "horizontal", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 2 }, .flags = VE, .unit = "int_ref_type" },
        { "slice"     , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 3 }, .flags = VE, .unit = "int_ref_type" },
    { "int_ref_cycle_size", "Number of frames in the intra refresh cycle",       OFFSET(qsv.int_ref_cycle_size),      AV_OPT_TYPE_INT, { .i64 = -1 },               -1, UINT16_MAX, VE },
    { "int_ref_qp_delta",   "QP difference for the refresh MBs",                 OFFSET(qsv.int_ref_qp_delta),        AV_OPT_TYPE_INT, { .i64 = INT16_MIN }, INT16_MIN,  INT16_MAX, VE },
    { "int_ref_cycle_dist",   "Distance between the beginnings of the intra-refresh cycles in frames",  OFFSET(qsv.int_ref_cycle_dist),      AV_OPT_TYPE_INT, { .i64 = -1 }, -1, INT16_MAX, VE },

    { NULL },
};

static const AVClass class = {
    .class_name = "hevc_qsv encoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const FFCodecDefault qsv_enc_defaults[] = {
    { "b",         "0"     },
    { "refs",      "0"     },
    { "g",         "248"   },
    { "bf",        "-1"    },
    { "qmin",      "-1"    },
    { "qmax",      "-1"    },
    { "trellis",   "-1"    },
    { NULL },
};

const FFCodec ff_hevc_qsv_encoder = {
    .p.name         = "hevc_qsv",
    CODEC_LONG_NAME("HEVC (Intel Quick Sync Video acceleration)"),
    .priv_data_size = sizeof(QSVHEVCEncContext),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_HEVC,
    .init           = qsv_enc_init,
    FF_CODEC_ENCODE_CB(qsv_enc_frame),
    .close          = qsv_enc_close,
    .p.capabilities = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_HYBRID,
    .p.pix_fmts     = (const enum AVPixelFormat[]){ AV_PIX_FMT_NV12,
                                                    AV_PIX_FMT_P010,
                                                    AV_PIX_FMT_P012,
                                                    AV_PIX_FMT_YUYV422,
                                                    AV_PIX_FMT_Y210,
                                                    AV_PIX_FMT_QSV,
                                                    AV_PIX_FMT_BGRA,
                                                    AV_PIX_FMT_X2RGB10,
                                                    AV_PIX_FMT_VUYX,
                                                    AV_PIX_FMT_XV30,
                                                    AV_PIX_FMT_NONE },
    .color_ranges   = AVCOL_RANGE_MPEG | AVCOL_RANGE_JPEG,
    .p.priv_class   = &class,
    .defaults       = qsv_enc_defaults,
    .caps_internal  = FF_CODEC_CAP_NOT_INIT_THREADSAFE |
                      FF_CODEC_CAP_INIT_CLEANUP,
    .p.wrapper_name = "qsv",
    .hw_configs     = ff_qsv_enc_hw_configs,
};
