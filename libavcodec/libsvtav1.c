/*
 * Scalable Video Technology for AV1 encoder library plugin
 *
 * Copyright (c) 2018 Intel Corporation
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
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdint.h>
#include <inttypes.h>
#include <EbSvtAv1ErrorCodes.h>
#include <EbSvtAv1Enc.h>
#include <EbSvtAv1Metadata.h>

#include "libavutil/attributes.h"
#include "libavutil/common.h"
#include "libavutil/frame.h"
#include "libavutil/imgutils.h"
#include "libavutil/base64.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mastering_display_metadata.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/avassert.h"

#include "codec_internal.h"
#include "dovi_rpu.h"
#include "encode.h"
#include "avcodec.h"
#include "profiles.h"
#include "qpfile_parser.h"

typedef enum eos_status {
    EOS_NOT_REACHED = 0,
    EOS_SENT,
    EOS_RECEIVED
}EOS_STATUS;

typedef struct SvtContext {
    const AVClass *class;

    EbSvtAv1EncConfiguration    enc_params;
    EbComponentType            *svt_handle;

    EbBufferHeaderType         *in_buf;
    int                         raw_size;
    int                         max_tu_size;

    AVFrame *frame;

    AVBufferPool *pool;

    EOS_STATUS eos_flag;

    DOVIContext dovi;

    uint8_t *stats_buf;

    // User options.
    AVDictionary *svtav1_opts;
    int enc_mode;
    int crf;
    int qp;
    char *qpfile_path;

    /* qpfile state. Populated by ff_qpfile_load() in
     * eb_enc_init() when qpfile_path is set, consumed per-frame in
     * eb_send_frame(), released in eb_enc_close().
     * The ROI map requires the versioned ROI ABI and is
     * gated on the SVT-AV1 1.6.0+ ROI ABI (enable_roi_map flag and
     * the ROI_MAP_EVENT priv-data type); older releases reject qpfile.
     *
     * roi_evts / roi_seg_maps are heap-owned arrays parallel to
     * qpfile.frames; SVT-AV1 keeps the ROI-map pointer alive across
     * the asynchronous encode pipeline (copy_private_data_list shallow-
     * copies ROI_MAP_EVENT data), so we must preserve these until
     * eb_enc_close().
     */
    FFQpFile  qpfile;
    int             qpfile_loaded;
#if SVT_AV1_CHECK_VERSION(1, 6, 0)
    SvtAv1RoiMapEvt *roi_evts;
    uint8_t        **roi_seg_maps;
    unsigned int     roi_evt_count;
    unsigned int     roi_b64_cols;
    unsigned int     roi_b64_rows;
    unsigned int     roi_input_frame;  /* input frames seen: the qpfile's frame index */
    const SvtAv1RoiMapEvt *roi_evt_sent; /* last event SVT-AV1 accepted; it stays in force */
#endif
} SvtContext;

static const struct {
    EbErrorType    eb_err;
    int            av_err;
    const char     *desc;
} svt_errors[] = {
    { EB_ErrorNone,                             0,              "success"                   },
    { EB_ErrorInsufficientResources,      AVERROR(ENOMEM),      "insufficient resources"    },
    { EB_ErrorUndefined,                  AVERROR(EINVAL),      "undefined error"           },
    { EB_ErrorInvalidComponent,           AVERROR(EINVAL),      "invalid component"         },
    { EB_ErrorBadParameter,               AVERROR(EINVAL),      "bad parameter"             },
    { EB_ErrorDestroyThreadFailed,        AVERROR_EXTERNAL,     "failed to destroy thread"  },
    { EB_ErrorSemaphoreUnresponsive,      AVERROR_EXTERNAL,     "semaphore unresponsive"    },
    { EB_ErrorDestroySemaphoreFailed,     AVERROR_EXTERNAL,     "failed to destroy semaphore"},
    { EB_ErrorCreateMutexFailed,          AVERROR_EXTERNAL,     "failed to create mutex"    },
    { EB_ErrorMutexUnresponsive,          AVERROR_EXTERNAL,     "mutex unresponsive"        },
    { EB_ErrorDestroyMutexFailed,         AVERROR_EXTERNAL,     "failed to destroy mutex"   },
    { EB_NoErrorEmptyQueue,               AVERROR(EAGAIN),      "empty queue"               },
};

static int svt_map_error(EbErrorType eb_err, const char **desc)
{
    int i;

    av_assert0(desc);
    for (i = 0; i < FF_ARRAY_ELEMS(svt_errors); i++) {
        if (svt_errors[i].eb_err == eb_err) {
            *desc = svt_errors[i].desc;
            return svt_errors[i].av_err;
        }
    }
    *desc = "unknown error";
    return AVERROR_UNKNOWN;
}

static int svt_print_error(void *log_ctx, EbErrorType err,
                           const char *error_string)
{
    const char *desc;
    int ret = svt_map_error(err, &desc);

    av_log(log_ctx, AV_LOG_ERROR, "%s: %s (0x%x)\n", error_string, desc, err);

    return ret;
}

static int alloc_buffer(EbSvtAv1EncConfiguration *config, SvtContext *svt_enc)
{
    const size_t luma_size = config->source_width * config->source_height *
        (config->encoder_bit_depth > 8 ? 2 : 1);

    EbSvtIOFormat *in_data;

    svt_enc->raw_size = luma_size * 3 / 2;

    // allocate buffer for in and out
    svt_enc->in_buf           = av_mallocz(sizeof(*svt_enc->in_buf));
    if (!svt_enc->in_buf)
        return AVERROR(ENOMEM);

    svt_enc->in_buf->p_buffer = av_mallocz(sizeof(*in_data));
    if (!svt_enc->in_buf->p_buffer)
        return AVERROR(ENOMEM);

    svt_enc->in_buf->size     = sizeof(*svt_enc->in_buf);

    return 0;

}

static void handle_mdcv(struct EbSvtAv1MasteringDisplayInfo *dst,
                        const AVMasteringDisplayMetadata *mdcv)
{
    if (mdcv->has_primaries) {
        const struct EbSvtAv1ChromaPoints *const points[] = {
            &dst->r,
            &dst->g,
            &dst->b,
        };

        for (int i = 0; i < 3; i++) {
            const struct EbSvtAv1ChromaPoints *dst = points[i];
            const AVRational *src = mdcv->display_primaries[i];

            AV_WB16(&dst->x,
                    av_rescale_q(1, src[0], (AVRational){ 1, (1 << 16) }));
            AV_WB16(&dst->y,
                    av_rescale_q(1, src[1], (AVRational){ 1, (1 << 16) }));
        }

        AV_WB16(&dst->white_point.x,
                av_rescale_q(1, mdcv->white_point[0],
                             (AVRational){ 1, (1 << 16) }));
        AV_WB16(&dst->white_point.y,
                av_rescale_q(1, mdcv->white_point[1],
                             (AVRational){ 1, (1 << 16) }));
    }

    if (mdcv->has_luminance) {
        AV_WB32(&dst->max_luma,
                av_rescale_q(1, mdcv->max_luminance,
                             (AVRational){ 1, (1 << 8) }));
        AV_WB32(&dst->min_luma,
                av_rescale_q(1, mdcv->min_luminance,
                             (AVRational){ 1, (1 << 14) }));
    }
}

static void handle_side_data(AVCodecContext *avctx,
                             EbSvtAv1EncConfiguration *param)
{
    const AVFrameSideData *cll_sd =
        av_frame_side_data_get(avctx->decoded_side_data,
            avctx->nb_decoded_side_data, AV_FRAME_DATA_CONTENT_LIGHT_LEVEL);
    const AVFrameSideData *mdcv_sd =
        av_frame_side_data_get(avctx->decoded_side_data,
            avctx->nb_decoded_side_data,
            AV_FRAME_DATA_MASTERING_DISPLAY_METADATA);

    if (cll_sd) {
        const AVContentLightMetadata *cll =
            (AVContentLightMetadata *)cll_sd->data;

        AV_WB16(&param->content_light_level.max_cll, cll->MaxCLL);
        AV_WB16(&param->content_light_level.max_fall, cll->MaxFALL);
    }

    if (mdcv_sd) {
        handle_mdcv(&param->mastering_display,
                    (AVMasteringDisplayMetadata *)mdcv_sd->data);
    }
}

static int config_enc_params(EbSvtAv1EncConfiguration *param,
                             AVCodecContext *avctx)
{
    SvtContext *svt_enc = avctx->priv_data;
    const AVPixFmtDescriptor *desc;
    av_unused const AVDictionaryEntry *en = NULL;

#if !SVT_AV1_CHECK_VERSION(3, 0, 0)
    // SVT-AV1 < 3.0.0 requires input dimensions of at least 64x64. Reject
    // smaller inputs explicitly here to produce a clear error rather than
    // relying on the library's internal validation, which may silently fail
    // to produce output and cause the caller to hang.
    // Sub-64px inputs were enabled upstream in MR !2356 (first released in
    // v3.0.0); for those versions the library validates the dimensions
    // itself.
    if (avctx->width < 64 || avctx->height < 64) {
        av_log(avctx, AV_LOG_ERROR,
               "Input dimensions %dx%d are smaller than the minimum 64x64 "
               "supported by SVT-AV1 < 3.0.0.\n",
               avctx->width, avctx->height);
        return AVERROR(EINVAL);
    }
#endif

    // Update param from options
    if (svt_enc->enc_mode >= -1)
        param->enc_mode             = svt_enc->enc_mode;

    if (avctx->bit_rate) {
        param->target_bit_rate      = avctx->bit_rate;
        if (avctx->rc_max_rate != avctx->bit_rate)
            param->rate_control_mode = 1;
        else
            param->rate_control_mode = 2;

        param->max_qp_allowed       = avctx->qmax;
        param->min_qp_allowed       = avctx->qmin;
    }
    param->max_bit_rate             = avctx->rc_max_rate;
    if ((avctx->bit_rate > 0 || avctx->rc_max_rate > 0) && avctx->rc_buffer_size)
        param->maximum_buffer_size_ms =
            avctx->rc_buffer_size * 1000LL /
            FFMAX(avctx->bit_rate, avctx->rc_max_rate);

    if (svt_enc->crf > 0) {
        param->qp                   = svt_enc->crf;
        param->rate_control_mode    = 0;
    } else if (svt_enc->qp > 0) {
        param->qp                   = svt_enc->qp;
        param->rate_control_mode    = 0;
#if SVT_AV1_CHECK_VERSION(4, 0, 0)
        param->aq_mode = 0;
#else
        param->enable_adaptive_quantization = 0;
#endif
    }

    desc = av_pix_fmt_desc_get(avctx->pix_fmt);
    param->color_primaries          = (enum EbColorPrimaries)avctx->color_primaries;
    param->matrix_coefficients      = (enum EbMatrixCoefficients)((desc->flags & AV_PIX_FMT_FLAG_RGB) ?
                                      AVCOL_SPC_RGB : avctx->colorspace);
    param->transfer_characteristics = (enum EbTransferCharacteristics)avctx->color_trc;

    if (avctx->color_range != AVCOL_RANGE_UNSPECIFIED)
        param->color_range = avctx->color_range == AVCOL_RANGE_JPEG;
    else
        param->color_range = !!(desc->flags & AV_PIX_FMT_FLAG_RGB);

#if SVT_AV1_CHECK_VERSION(1, 0, 0)
    if (avctx->chroma_sample_location != AVCHROMA_LOC_UNSPECIFIED) {
        const char *name =
            av_chroma_location_name(avctx->chroma_sample_location);

        switch (avctx->chroma_sample_location) {
        case AVCHROMA_LOC_LEFT:
            param->chroma_sample_position = EB_CSP_VERTICAL;
            break;
        case AVCHROMA_LOC_TOPLEFT:
            param->chroma_sample_position = EB_CSP_COLOCATED;
            break;
        default:
            if (!name)
                break;

            av_log(avctx, AV_LOG_WARNING,
                   "Specified chroma sample location %s is unsupported "
                   "on the AV1 bit stream level. Usage of a container that "
                   "allows passing this information - such as Matroska - "
                   "is recommended.\n",
                   name);
            break;
        }
    }
#endif

    if (avctx->profile != AV_PROFILE_UNKNOWN)
        param->profile = avctx->profile;

    if (avctx->level != AV_LEVEL_UNKNOWN)
        param->level = avctx->level;

    // gop_size == 1 case is handled when encoding each frame by setting
    // pic_type to EB_AV1_KEY_PICTURE. For gop_size > 1, set the
    // intra_period_length. Even though setting intra_period_length to 0 should
    // work in this case, it does not.
    // See: https://gitlab.com/AOMediaCodec/SVT-AV1/-/issues/2076
    if (avctx->gop_size > 1)
        param->intra_period_length  = avctx->gop_size - 1;

#if SVT_AV1_CHECK_VERSION(1, 1, 0)
    // In order for SVT-AV1 to force keyframes by setting pic_type to
    // EB_AV1_KEY_PICTURE on any frame, force_key_frames has to be set. Note
    // that this does not force all frames to be keyframes (it only forces a
    // keyframe with pic_type is set to EB_AV1_KEY_PICTURE). As of now, SVT-AV1
    // does not support arbitrary keyframe requests by setting pic_type to
    // EB_AV1_KEY_PICTURE, so it is done only when gop_size == 1.
    // FIXME: When SVT-AV1 supports arbitrary keyframe requests, this code needs
    // to be updated to set force_key_frames accordingly.
    if (avctx->gop_size == 1)
        param->force_key_frames = 1;
#endif

    if (avctx->framerate.num > 0 && avctx->framerate.den > 0) {
        param->frame_rate_numerator   = avctx->framerate.num;
        param->frame_rate_denominator = avctx->framerate.den;
    } else {
        param->frame_rate_numerator   = avctx->time_base.den;
        param->frame_rate_denominator = avctx->time_base.num;
    }

    /* 2 = IDR, closed GOP, 1 = CRA, open GOP */
    param->intra_refresh_type = avctx->flags & AV_CODEC_FLAG_CLOSED_GOP ? 2 : 1;

    handle_side_data(avctx, param);

#if SVT_AV1_CHECK_VERSION(0, 9, 1)
    while ((en = av_dict_iterate(svt_enc->svtav1_opts, en))) {
        EbErrorType ret = svt_av1_enc_parse_parameter(param, en->key, en->value);
        if (ret != EB_ErrorNone) {
            int level = (avctx->err_recognition & AV_EF_EXPLODE) ? AV_LOG_ERROR : AV_LOG_WARNING;
            av_log(avctx, level, "Error parsing option %s: %s.\n", en->key, en->value);
            if (avctx->err_recognition & AV_EF_EXPLODE)
                return AVERROR(EINVAL);
        }
    }
#else
    if (av_dict_count(svt_enc->svtav1_opts)) {
        int level = (avctx->err_recognition & AV_EF_EXPLODE) ? AV_LOG_ERROR : AV_LOG_WARNING;
        av_log(avctx, level, "svt-params needs libavcodec to be compiled with SVT-AV1 "
                             "headers >= 0.9.1.\n");
        if (avctx->err_recognition & AV_EF_EXPLODE)
            return AVERROR(ENOSYS);
    }
#endif
    if (avctx->flags & AV_CODEC_FLAG_PASS2) {
        int stats_sz;

        if (!avctx->stats_in) {
            av_log(avctx, AV_LOG_ERROR, "No stats file for second pass\n");
            return AVERROR(EINVAL);
        }

        stats_sz = AV_BASE64_DECODE_SIZE(strlen(avctx->stats_in));
        if (stats_sz <= 0) {
            av_log(avctx, AV_LOG_ERROR, "Invalid stats file size\n");
            return AVERROR(EINVAL);
        }

        svt_enc->stats_buf = av_malloc(stats_sz);
        if (!svt_enc->stats_buf) {
            av_log(avctx, AV_LOG_ERROR, "Failed to allocate stats buffer\n");
            return AVERROR(ENOMEM);
        }

        stats_sz = av_base64_decode(svt_enc->stats_buf, avctx->stats_in, stats_sz);
        if (stats_sz < 0) {
            av_log(avctx, AV_LOG_ERROR, "Failed to decode stats file\n");
            av_freep(&svt_enc->stats_buf);
            return AVERROR(EINVAL);
        }

        param->rc_stats_buffer.buf = svt_enc->stats_buf;
        param->rc_stats_buffer.sz  = stats_sz;
        param->pass = 2;

        av_log(avctx, AV_LOG_VERBOSE, "Using %d bytes of 2-pass stats\n", stats_sz);
    } else if (avctx->flags & AV_CODEC_FLAG_PASS1) {
        param->pass = 1;
        av_log(avctx, AV_LOG_VERBOSE, "Starting first pass\n");
    }

    param->source_width     = avctx->width;
    param->source_height    = avctx->height;

    param->encoder_bit_depth = desc->comp[0].depth;

    if (desc->log2_chroma_w == 1 && desc->log2_chroma_h == 1)
        param->encoder_color_format   = EB_YUV420;
    else if (desc->log2_chroma_w == 1 && desc->log2_chroma_h == 0)
        param->encoder_color_format   = EB_YUV422;
    else if (!desc->log2_chroma_w && !desc->log2_chroma_h)
        param->encoder_color_format   = EB_YUV444;
    else {
        av_log(avctx, AV_LOG_ERROR , "Unsupported pixel format\n");
        return AVERROR(EINVAL);
    }

    if ((param->encoder_color_format == EB_YUV422 || param->encoder_bit_depth > 10)
         && param->profile != AV_PROFILE_AV1_PROFESSIONAL ) {
        av_log(avctx, AV_LOG_WARNING, "Forcing Professional profile\n");
        param->profile = AV_PROFILE_AV1_PROFESSIONAL;
    } else if (param->encoder_color_format == EB_YUV444 && param->profile != AV_PROFILE_AV1_HIGH) {
        av_log(avctx, AV_LOG_WARNING, "Forcing High profile\n");
        param->profile = AV_PROFILE_AV1_HIGH;
    }

    avctx->bit_rate       = param->rate_control_mode > 0 ?
                            param->target_bit_rate : 0;
    avctx->rc_max_rate    = param->max_bit_rate;
    avctx->rc_buffer_size = param->maximum_buffer_size_ms *
                            FFMAX(avctx->bit_rate, avctx->rc_max_rate) / 1000LL;

    if (avctx->bit_rate || avctx->rc_max_rate || avctx->rc_buffer_size) {
        AVCPBProperties *cpb_props = ff_encode_add_cpb_side_data(avctx);
        if (!cpb_props)
            return AVERROR(ENOMEM);

        cpb_props->buffer_size = avctx->rc_buffer_size;
        cpb_props->max_bitrate = avctx->rc_max_rate;
        cpb_props->avg_bitrate = avctx->bit_rate;
    }

    return 0;
}

static int read_in_data(EbSvtAv1EncConfiguration *param, const AVFrame *frame,
                         EbBufferHeaderType *header_ptr)
{
    EbSvtIOFormat *in_data = (EbSvtIOFormat *)header_ptr->p_buffer;
    ptrdiff_t linesizes[4];
    size_t sizes[4];
    int bytes_shift = param->encoder_bit_depth > 8 ? 1 : 0;
    int ret, frame_size;

    for (int i = 0; i < 4; i++)
        linesizes[i] = frame->linesize[i];

    ret = av_image_fill_plane_sizes(sizes, frame->format, frame->height,
                                    linesizes);
    if (ret < 0)
        return ret;

    frame_size = 0;
    for (int i = 0; i < 4; i++) {
        if (sizes[i] > INT_MAX - frame_size)
            return AVERROR(EINVAL);
        frame_size += sizes[i];
    }

    in_data->luma = frame->data[0];
    in_data->cb   = frame->data[1];
    in_data->cr   = frame->data[2];

    in_data->y_stride  = AV_CEIL_RSHIFT(frame->linesize[0], bytes_shift);
    in_data->cb_stride = AV_CEIL_RSHIFT(frame->linesize[1], bytes_shift);
    in_data->cr_stride = AV_CEIL_RSHIFT(frame->linesize[2], bytes_shift);

    header_ptr->n_filled_len = frame_size;
    svt_metadata_array_free(&header_ptr->metadata);

    return 0;
}

#if SVT_AV1_CHECK_VERSION(1, 6, 0)
/* Translate a per-frame QP-offset file into an array of per-frame
 * SvtAv1RoiMapEvt records.
 *
 * Strategy:
 *   1. Average the 16x16 block offsets of each 64x64 superblock.
 *   2. Pick up to QPFILE_SVTAV1_MAX_SEGMENTS segment values from the
 *      averages (each distinct value when they fit, else uniform binning),
 *      ordered descending with a nonnegative first entry: SVT-AV1 before
 *      4.3.0 searches lower segment ids for a positive quantizer index and
 *      has no fallback below segment 0.
 *   3. Give each superblock the nearest segment through a lookup table.
 *   4. Size b64_seg_map at the encoder's actual SB grid stride
 *      ((max_input_luma_width+63)/64) so segmentation.c can index
 *      it correctly even if the qpfile was cut against a smaller
 *      preview.
 *
 * Per-SB rectangles fall out naturally -- SVT-AV1 consumes a dense
 * per-SB segment map, so explicit rectangle decomposition would
 * be redundant. Trade-off: per-MB qpfile resolution is rounded
 * to per-SB granularity, losing spatial detail finer than 64x64
 * px (a salient face smaller than that gets averaged with its
 * surroundings). Suitable for object-sized ROIs generated by
 * an external tool; finer granularity would require an SVT-AV1 ABI
 * change.
 *
 * Lifetime: the library shallow-copies ROI_MAP_EVENT pointers
 * (copy_private_data_list in enc_handle.c) and reads them later
 * on async pipeline threads, so the SvtAv1RoiMapEvt and its
 * b64_seg_map must outlive svt_av1_enc_send_picture() -- they
 * are owned by SvtContext and freed in eb_enc_close().
 */
#define QPFILE_SVTAV1_MAX_SEGMENTS 8
#define QPFILE_SVTAV1_SB_SIDE      4   /* 4 MBs (16 px each) per SB (64 px) */

/* Average macroblock QP offsets into 64x64 superblock averages. */
static void roi_average_map(const FFQpFrame *qf, unsigned dest_b64_cols,
                            unsigned dest_b64_rows, int8_t *avg)
{
    unsigned mb_cols = qf->block_cols, mb_rows = qf->block_rows;
    unsigned sb_y, sb_x;

    for (sb_y = 0; sb_y < dest_b64_rows; sb_y++) {
        for (sb_x = 0; sb_x < dest_b64_cols; sb_x++) {
            long sum = 0;
            unsigned count = 0;
            unsigned mb_y0 = sb_y * QPFILE_SVTAV1_SB_SIDE;
            unsigned mb_x0 = sb_x * QPFILE_SVTAV1_SB_SIDE;
            unsigned mb_y1 = FFMIN(mb_y0 + QPFILE_SVTAV1_SB_SIDE, mb_rows);
            unsigned mb_x1 = FFMIN(mb_x0 + QPFILE_SVTAV1_SB_SIDE, mb_cols);
            unsigned i, j;

            if (mb_x0 < mb_cols && mb_y0 < mb_rows) {
                for (i = mb_y0; i < mb_y1; i++) {
                    for (j = mb_x0; j < mb_x1; j++) {
                        sum += qf->block_offsets[i * mb_cols + j];
                        count++;
                    }
                }
            }
            avg[sb_y * dest_b64_cols + sb_x] =
                count ? (int8_t)(int)(sum / (long)count) : 0;
        }
    }
}

/* Build a descending segment palette for SVT-AV1 with nonnegative segment 0. */
static unsigned roi_palette(const int8_t *avg, size_t n_sb, int *seg_values)
{
    int max_avg = QPFILE_DELTA_MIN;
    unsigned max_seg, n;
    size_t i;

    for (i = 0; i < n_sb; i++)
        max_avg = FFMAX(max_avg, avg[i]);

    max_seg = max_avg >= 0 ? QPFILE_SVTAV1_MAX_SEGMENTS
                           : QPFILE_SVTAV1_MAX_SEGMENTS - 1;
    n = ff_qpfile_palette(avg, n_sb, seg_values, max_seg);
    if (max_avg < 0)
        seg_values[n++] = 0;

    for (unsigned k = 0; k < n / 2; k++)
        FFSWAP(int, seg_values[k], seg_values[n - 1 - k]);

    return n;
}

/* Build an SVT-AV1 ROI event and map for a frame, returning 0 or an error. */
static int build_one_roi_evt(const FFQpFrame *qf,
                             unsigned dest_b64_cols,
                             unsigned dest_b64_rows,
                             int8_t *avg,
                             SvtAv1RoiMapEvt *out_evt,
                             uint8_t **out_seg_map)
{
    int seg_values[QPFILE_SVTAV1_MAX_SEGMENTS];
    uint8_t lut[QPFILE_LUT_SIZE];
    uint8_t *seg_map = NULL;
    size_t n_sb;
    unsigned n_seg, i;

    if (!dest_b64_cols || !dest_b64_rows)
        return AVERROR(EINVAL);

    n_sb = (size_t)dest_b64_rows * dest_b64_cols;
    seg_map = av_calloc(n_sb, sizeof(*seg_map));
    if (!seg_map)
        return AVERROR(ENOMEM);

    memset(out_evt, 0, sizeof(*out_evt));
    out_evt->start_picture_number = qf->frame_index;
    out_evt->b64_seg_map          = seg_map;
    out_evt->max_seg_id           = 0;
    out_evt->next                 = NULL;
    *out_seg_map = seg_map;

    if (!qf->block_offsets)
        return 0;

    roi_average_map(qf, dest_b64_cols, dest_b64_rows, avg);
    n_seg = roi_palette(avg, n_sb, seg_values);
    ff_qpfile_segment_lut(lut, seg_values, n_seg);

    for (i = 0; i < n_sb; i++)
        seg_map[i] = lut[avg[i] - QPFILE_DELTA_MIN];

    out_evt->max_seg_id = (int8_t)(n_seg - 1u);
    for (i = 0; i < n_seg; i++)
        out_evt->seg_qp[i] = (int16_t)seg_values[i];

    return 0;
}

/* Build ROI events for all qpfile frames, returning 0 or a negative error. */
static int build_all_roi_evts(AVCodecContext *avctx, SvtContext *svt_enc)
{
    unsigned b64_cols = ((unsigned)avctx->width  + 63u) / 64u;
    unsigned b64_rows = ((unsigned)avctx->height + 63u) / 64u;
    unsigned n = svt_enc->qpfile.frame_count;
    unsigned i;
    int8_t *avg = NULL;
    int ret = 0;

    if (!n || !b64_cols || !b64_rows)
        return AVERROR(EINVAL);

    avg = av_malloc_array((size_t)b64_cols * b64_rows, sizeof(*avg));
    if (!avg)
        return AVERROR(ENOMEM);

    svt_enc->roi_evts = av_calloc(n, sizeof(*svt_enc->roi_evts));
    svt_enc->roi_seg_maps = av_calloc(n, sizeof(*svt_enc->roi_seg_maps));
    if (!svt_enc->roi_evts || !svt_enc->roi_seg_maps) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    svt_enc->roi_evt_count = n;
    svt_enc->roi_b64_cols  = b64_cols;
    svt_enc->roi_b64_rows  = b64_rows;

    for (i = 0; i < n; i++) {
        ret = build_one_roi_evt(&svt_enc->qpfile.frames[i],
                                b64_cols, b64_rows, avg,
                                &svt_enc->roi_evts[i],
                                &svt_enc->roi_seg_maps[i]);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR,
                   "failed to build ROI map for qpfile frame %u (%d)\n",
                   i, ret);
            goto end;
        }
        av_freep(&svt_enc->qpfile.frames[i].block_offsets);
    }

end:
    av_free(avg);
    return ret;
}

/* Look up the ROI map event of the record a frame uses, returning it, or
 * NULL when there is none or SVT-AV1 already holds it. SVT-AV1 keeps the
 * last event it accepted for all later frames, so an event is sent whenever
 * the record in force changes: normally on the frame of the record, or on
 * the next frame that reaches SVT-AV1 when that frame was dropped. */
static SvtAv1RoiMapEvt *roi_evt_for_index(SvtContext *svt_enc, unsigned frame_index)
{
    const FFQpFrame *qf;
    ptrdiff_t index;

    if (!svt_enc->roi_evt_count)
        return NULL;
    qf = ff_qpfile_frame_for_index(&svt_enc->qpfile, frame_index);
    if (!qf)
        return NULL;
    index = qf - svt_enc->qpfile.frames;
    if ((unsigned)index >= svt_enc->roi_evt_count ||
        &svt_enc->roi_evts[index] == svt_enc->roi_evt_sent)
        return NULL;
    return &svt_enc->roi_evts[index];
}
#endif /* SVT_AV1_CHECK_VERSION(1, 6, 0) */

/* Load the QP file and check its grid, returning 0 or a negative error code. */
static av_cold int load_qpfile(AVCodecContext *avctx)
{
    SvtContext *svt_enc = avctx->priv_data;
    int ret = ff_qpfile_load(avctx, svt_enc->qpfile_path, &svt_enc->qpfile);

    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "cannot read qpfile %s: %s\n",
               svt_enc->qpfile_path, av_err2str(ret));
        return ret;
    }
    svt_enc->qpfile_loaded = 1;
    return ff_qpfile_check_grid(avctx, "libsvtav1", &svt_enc->qpfile,
                                avctx->width, avctx->height);
}

static av_cold int eb_enc_init(AVCodecContext *avctx)
{
    SvtContext   *svt_enc = avctx->priv_data;
    EbErrorType svt_ret;
    int ret;

    svt_enc->eos_flag = EOS_NOT_REACHED;

#if SVT_AV1_CHECK_VERSION(3, 0, 0)
    svt_ret = svt_av1_enc_init_handle(&svt_enc->svt_handle, &svt_enc->enc_params);
#else
    svt_ret = svt_av1_enc_init_handle(&svt_enc->svt_handle, svt_enc, &svt_enc->enc_params);
#endif
    if (svt_ret != EB_ErrorNone) {
        return svt_print_error(avctx, svt_ret, "Error initializing encoder handle");
    }

    ret = config_enc_params(&svt_enc->enc_params, avctx);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Error configuring encoder parameters\n");
        return ret;
    }

    if (svt_enc->qpfile_path && *svt_enc->qpfile_path) {
        ret = load_qpfile(avctx);
        if (ret < 0)
            return ret;
#if SVT_AV1_CHECK_VERSION(1, 6, 0)
        if (svt_enc->enc_params.rate_control_mode != 0) {
            av_log(avctx, AV_LOG_ERROR,
                   "qpfile with libsvtav1 needs CRF or constant-QP rate "
                   "control (rc 0); SVT-AV1 applies ROI maps only there.\n");
            return AVERROR(EINVAL);
        }
        /* SVT-AV1 reads the ROI map with superblock positions of the
         * scaled frame but the stride of the full-size map, so internal
         * scaling would move the offsets to other superblocks. */
        if (svt_enc->enc_params.superres_mode != SUPERRES_NONE ||
            svt_enc->enc_params.resize_mode != RESIZE_NONE) {
            av_log(avctx, AV_LOG_ERROR,
                   "qpfile with libsvtav1 does not support internal "
                   "scaling (superres-mode or resize-mode other than 0).\n");
            return AVERROR(EINVAL);
        }
        /* SVT-AV1 can encode the first pass at a reduced size and then
         * reads the ROI map with the stride of that size, so the first
         * pass is refused even where it would not be downsampled. */
        if (svt_enc->enc_params.pass == 1) {
            av_log(avctx, AV_LOG_ERROR,
                   "qpfile with libsvtav1 does not support the first pass.\n");
            return AVERROR(EINVAL);
        }
        /* Build all per-frame ROI map events upfront. SVT-AV1 reads
         * ROI_MAP_EVENT data via shallow-copied pointers on async
         * pipeline threads, so the events and their b64_seg_maps must
         * live for the whole encode session. Then flip enable_roi_map
         * before set_parameter so the encoder allocates segmentation
         * state.
         */
        ret = build_all_roi_evts(avctx, svt_enc);
        if (ret < 0)
            return ret;
        svt_enc->enc_params.enable_roi_map = 1;
        av_log(avctx, AV_LOG_VERBOSE,
               "qpfile=%s loaded (frames=%u, %ux%u qpfile blocks "
               "-> %ux%u SB ROI grid).\n",
               svt_enc->qpfile_path, svt_enc->qpfile.frame_count,
               svt_enc->qpfile.block_cols, svt_enc->qpfile.block_rows,
               svt_enc->roi_b64_cols, svt_enc->roi_b64_rows);
#else
        av_log(avctx, AV_LOG_ERROR,
               "qpfile needs SVT-AV1 1.6.0 or newer.\n");
        return AVERROR(ENOSYS);
#endif
    }

    svt_ret = svt_av1_enc_set_parameter(svt_enc->svt_handle, &svt_enc->enc_params);
    if (svt_ret != EB_ErrorNone) {
        return svt_print_error(avctx, svt_ret, "Error setting encoder parameters");
    }


    svt_ret = svt_av1_enc_init(svt_enc->svt_handle);
    if (svt_ret != EB_ErrorNone) {
        return svt_print_error(avctx, svt_ret, "Error initializing encoder");
    }

    svt_enc->dovi.logctx = avctx;
    ret = ff_dovi_configure(&svt_enc->dovi, avctx);
    if (ret < 0)
        return ret;

    if (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER) {
        EbBufferHeaderType *headerPtr = NULL;

        svt_ret = svt_av1_enc_stream_header(svt_enc->svt_handle, &headerPtr);
        if (svt_ret != EB_ErrorNone) {
            return svt_print_error(avctx, svt_ret, "Error building stream header");
        }

        avctx->extradata_size = headerPtr->n_filled_len;
        avctx->extradata = av_mallocz(avctx->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
        if (!avctx->extradata) {
            av_log(avctx, AV_LOG_ERROR,
                   "Cannot allocate AV1 header of size %d.\n", avctx->extradata_size);
            return AVERROR(ENOMEM);
        }

        memcpy(avctx->extradata, headerPtr->p_buffer, avctx->extradata_size);

        svt_ret = svt_av1_enc_stream_header_release(headerPtr);
        if (svt_ret != EB_ErrorNone) {
            return svt_print_error(avctx, svt_ret, "Error freeing stream header");
        }
    }

    svt_enc->frame = av_frame_alloc();
    if (!svt_enc->frame)
        return AVERROR(ENOMEM);

    return alloc_buffer(&svt_enc->enc_params, svt_enc);
}

static int eb_send_frame(AVCodecContext *avctx, const AVFrame *frame)
{
    SvtContext           *svt_enc = avctx->priv_data;
    EbBufferHeaderType  *headerPtr = svt_enc->in_buf;
    AVFrameSideData *sd;
    EbErrorType svt_ret;
    int ret;
#if SVT_AV1_CHECK_VERSION(1, 6, 0)
    unsigned roi_frame = 0;
#endif

    if (!frame) {
        EbBufferHeaderType headerPtrLast;

        if (svt_enc->eos_flag == EOS_SENT)
            return 0;

        memset(&headerPtrLast, 0, sizeof(headerPtrLast));
        headerPtrLast.pic_type      = EB_AV1_INVALID_PICTURE;
        headerPtrLast.flags         = EB_BUFFERFLAG_EOS;

        svt_av1_enc_send_picture(svt_enc->svt_handle, &headerPtrLast);
        svt_enc->eos_flag = EOS_SENT;
        return 0;
    }

#if SVT_AV1_CHECK_VERSION(1, 6, 0)
    /* Count every input frame before any step that can fail, so that the
     * frames after a dropped one keep their own qpfile records. */
    if (svt_enc->qpfile_loaded)
        roi_frame = svt_enc->roi_input_frame++;
#endif

    ret = read_in_data(&svt_enc->enc_params, frame, headerPtr);
    if (ret < 0)
        return ret;

    headerPtr->flags         = 0;
    headerPtr->p_app_private = NULL;
    headerPtr->pts           = frame->pts;

    switch (frame->pict_type) {
    case AV_PICTURE_TYPE_I:
        headerPtr->pic_type = EB_AV1_KEY_PICTURE;
        break;
    default:
        // Actually means auto, or default.
        headerPtr->pic_type = EB_AV1_INVALID_PICTURE;
        break;
    }

    if (avctx->gop_size == 1)
        headerPtr->pic_type = EB_AV1_KEY_PICTURE;

    sd = av_frame_get_side_data(frame, AV_FRAME_DATA_DOVI_METADATA);
    if (svt_enc->dovi.cfg.dv_profile && sd) {
        const AVDOVIMetadata *metadata = (const AVDOVIMetadata *)sd->data;
        uint8_t *t35;
        int size;
        if ((ret = ff_dovi_rpu_generate(&svt_enc->dovi, metadata, FF_DOVI_WRAP_T35,
                                        &t35, &size)) < 0)
            return ret;
        ret = svt_add_metadata(headerPtr, EB_AV1_METADATA_TYPE_ITUT_T35, t35, size);
        av_free(t35);
        if (ret < 0)
            return AVERROR(ENOMEM);
    } else if (svt_enc->dovi.cfg.dv_profile) {
        av_log(avctx, AV_LOG_ERROR, "Dolby Vision enabled, but received frame "
               "without AV_FRAME_DATA_DOVI_METADATA\n");
        return AVERROR_INVALIDDATA;
    }

#if SVT_AV1_CHECK_VERSION(1, 6, 0)
    /* qpfile -> SVT-AV1 ROI map: attach the (already-built)
     * per-frame ROI map event as a stack-local priv-data node. The
     * encoder's resource_coordination_process.c expects
     *   node->size == sizeof(SvtAv1RoiMapEvt*)
     * and uses node->data as a pointer that must outlive the async
     * pipeline -- so the SvtAv1RoiMapEvt + b64_seg_map live in
     * svt_enc->roi_evts/roi_seg_maps until eb_enc_close(). The
     * EbPrivDataNode itself is shallow-copied by copy_private_data_list
     * in enc_handle.c, so a stack-local node is safe.
     */
    EbPrivDataNode roi_node;
    SvtAv1RoiMapEvt *roi_evt_ptr = NULL;
    if (svt_enc->qpfile_loaded && svt_enc->roi_evt_count > 0)
        roi_evt_ptr = roi_evt_for_index(svt_enc, roi_frame);
    if (roi_evt_ptr) {
        memset(&roi_node, 0, sizeof(roi_node));
        roi_node.node_type = ROI_MAP_EVENT;
        roi_node.data      = roi_evt_ptr;
        roi_node.size      = (uint32_t)sizeof(SvtAv1RoiMapEvt *);
        roi_node.next      = NULL;
        headerPtr->p_app_private = &roi_node;
    }
#endif

    svt_ret = svt_av1_enc_send_picture(svt_enc->svt_handle, headerPtr);

#if SVT_AV1_CHECK_VERSION(1, 6, 0)
    /* Restore the slot for the next frame's send-path. The library
     * already shallow-copied the priv-data list. */
    if (roi_evt_ptr) {
        headerPtr->p_app_private = NULL;
        /* After a failed send the event goes out again with the next frame. */
        if (svt_ret == EB_ErrorNone)
            svt_enc->roi_evt_sent = roi_evt_ptr;
    }
#endif

    if (svt_ret != EB_ErrorNone)
        return svt_print_error(avctx, svt_ret, "Error sending a frame to encoder");

    return 0;
}

static AVBufferRef *get_output_ref(AVCodecContext *avctx, SvtContext *svt_enc, int filled_len)
{
    if (filled_len > svt_enc->max_tu_size) {
        const int max_frames = 8;
        int max_tu_size;

        if (filled_len > svt_enc->raw_size * max_frames) {
            av_log(avctx, AV_LOG_ERROR, "TU size > %d raw frame size.\n", max_frames);
            return NULL;
        }

        max_tu_size = 1 << av_ceil_log2(filled_len);
        av_buffer_pool_uninit(&svt_enc->pool);
        svt_enc->pool = av_buffer_pool_init(max_tu_size + AV_INPUT_BUFFER_PADDING_SIZE, NULL);
        if (!svt_enc->pool)
            return NULL;

        svt_enc->max_tu_size = max_tu_size;
    }
    av_assert0(svt_enc->pool);

    return av_buffer_pool_get(svt_enc->pool);
}

static int eb_receive_packet(AVCodecContext *avctx, AVPacket *pkt)
{
    SvtContext  *svt_enc = avctx->priv_data;
    EbBufferHeaderType *headerPtr;
    AVFrame *frame = svt_enc->frame;
    EbErrorType svt_ret;
    AVBufferRef *ref;
    int ret = 0;

    if (svt_enc->eos_flag == EOS_RECEIVED)
        return AVERROR_EOF;

    ret = ff_encode_get_frame(avctx, frame);
    if (ret < 0 && ret != AVERROR_EOF)
        return ret;
    if (ret == AVERROR_EOF)
        frame = NULL;

    ret = eb_send_frame(avctx, frame);
    av_frame_unref(svt_enc->frame);
    if (ret < 0)
        return ret;

    svt_ret = svt_av1_enc_get_packet(svt_enc->svt_handle, &headerPtr, svt_enc->eos_flag);
    if (svt_ret == EB_NoErrorEmptyQueue)
        return AVERROR(EAGAIN);
    else if (svt_ret != EB_ErrorNone)
        return svt_print_error(avctx, svt_ret, "Error getting an output packet from encoder");

#if SVT_AV1_CHECK_VERSION(2, 0, 0)
    if (headerPtr->flags & EB_BUFFERFLAG_EOS) {
        if (avctx->flags & AV_CODEC_FLAG_PASS1) {
            SvtAv1FixedBuf first_pass_stats = { 0 };
            EbErrorType svt_ret_stats;
            int b64_size;

            svt_ret_stats = svt_av1_enc_get_stream_info(
                svt_enc->svt_handle,
                SVT_AV1_STREAM_INFO_FIRST_PASS_STATS_OUT,
                &first_pass_stats);

            if (svt_ret_stats != EB_ErrorNone) {
                av_log(avctx, AV_LOG_ERROR,
                       "Failed to get first pass stats\n");
                svt_av1_enc_release_out_buffer(&headerPtr);
                return AVERROR_EXTERNAL;
            }

            if (first_pass_stats.sz > 0 && first_pass_stats.buf) {
                b64_size = AV_BASE64_SIZE(first_pass_stats.sz);
                avctx->stats_out = av_malloc(b64_size);
                if (!avctx->stats_out) {
                    av_log(avctx, AV_LOG_ERROR,
                           "Failed to allocate stats output buffer\n");
                    svt_av1_enc_release_out_buffer(&headerPtr);
                    return AVERROR(ENOMEM);
                }

                av_base64_encode(avctx->stats_out, b64_size,
                                 first_pass_stats.buf, first_pass_stats.sz);

                av_log(avctx, AV_LOG_VERBOSE,
                       "First pass stats: %"PRIu64" bytes, encoded to %d bytes\n",
                       first_pass_stats.sz, b64_size);
            }
        }

        svt_enc->eos_flag = EOS_RECEIVED;
        svt_av1_enc_release_out_buffer(&headerPtr);
        return AVERROR_EOF;
    }
#endif

    ref = get_output_ref(avctx, svt_enc, headerPtr->n_filled_len);
    if (!ref) {
        av_log(avctx, AV_LOG_ERROR, "Failed to allocate output packet.\n");
        svt_av1_enc_release_out_buffer(&headerPtr);
        return AVERROR(ENOMEM);
    }
    pkt->buf = ref;
    pkt->data = ref->data;

    memcpy(pkt->data, headerPtr->p_buffer, headerPtr->n_filled_len);
    memset(pkt->data + headerPtr->n_filled_len, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    pkt->size = headerPtr->n_filled_len;
    pkt->pts  = headerPtr->pts;
    pkt->dts  = headerPtr->dts;

    enum AVPictureType pict_type;
    switch (headerPtr->pic_type) {
    case EB_AV1_KEY_PICTURE:
        pkt->flags |= AV_PKT_FLAG_KEY;
        av_fallthrough;
    case EB_AV1_INTRA_ONLY_PICTURE:
        pict_type = AV_PICTURE_TYPE_I;
        break;
    case EB_AV1_INVALID_PICTURE:
        pict_type = AV_PICTURE_TYPE_NONE;
        break;
    default:
        pict_type = AV_PICTURE_TYPE_P;
        break;
    }

    if (headerPtr->pic_type == EB_AV1_NON_REF_PICTURE)
        pkt->flags |= AV_PKT_FLAG_DISPOSABLE;

#if !(SVT_AV1_CHECK_VERSION(2, 0, 0))
    if (headerPtr->flags & EB_BUFFERFLAG_EOS)
        svt_enc->eos_flag = EOS_RECEIVED;
#endif

    ff_encode_add_stats_side_data(pkt, headerPtr->qp * FF_QP2LAMBDA, NULL, 0, pict_type);

    svt_av1_enc_release_out_buffer(&headerPtr);

    return 0;
}

static av_cold int eb_enc_close(AVCodecContext *avctx)
{
    SvtContext *svt_enc = avctx->priv_data;

    if (svt_enc->svt_handle) {
        svt_av1_enc_deinit(svt_enc->svt_handle);
        svt_av1_enc_deinit_handle(svt_enc->svt_handle);
    }
    if (svt_enc->in_buf) {
        av_free(svt_enc->in_buf->p_buffer);
        svt_metadata_array_free(&svt_enc->in_buf->metadata);
        av_freep(&svt_enc->in_buf);
    }

    av_buffer_pool_uninit(&svt_enc->pool);
    av_frame_free(&svt_enc->frame);
    ff_dovi_ctx_unref(&svt_enc->dovi);
    av_freep(&svt_enc->stats_buf);

    if (svt_enc->qpfile_loaded) {
#if SVT_AV1_CHECK_VERSION(1, 6, 0)
        if (svt_enc->roi_seg_maps) {
            for (unsigned i = 0; i < svt_enc->roi_evt_count; i++)
                av_freep(&svt_enc->roi_seg_maps[i]);
            av_freep(&svt_enc->roi_seg_maps);
        }
        av_freep(&svt_enc->roi_evts);
        svt_enc->roi_evt_count = 0;
        svt_enc->roi_b64_cols  = 0;
        svt_enc->roi_b64_rows  = 0;
        svt_enc->roi_input_frame = 0;
        svt_enc->roi_evt_sent = NULL;
#endif
        ff_qpfile_free(&svt_enc->qpfile);
        svt_enc->qpfile_loaded = 0;
    }

    return 0;
}

#define OFFSET(x) offsetof(SvtContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "preset", "Encoding preset",
      OFFSET(enc_mode), AV_OPT_TYPE_INT, { .i64 = -2 }, -2, MAX_ENC_PRESET, VE },

    FF_AV1_PROFILE_OPTS

#define LEVEL(name, value) name, NULL, 0, AV_OPT_TYPE_CONST, \
      { .i64 = value }, 0, 0, VE, .unit = "avctx.level"
        { LEVEL("2.0", 20) },
        { LEVEL("2.1", 21) },
        { LEVEL("2.2", 22) },
        { LEVEL("2.3", 23) },
        { LEVEL("3.0", 30) },
        { LEVEL("3.1", 31) },
        { LEVEL("3.2", 32) },
        { LEVEL("3.3", 33) },
        { LEVEL("4.0", 40) },
        { LEVEL("4.1", 41) },
        { LEVEL("4.2", 42) },
        { LEVEL("4.3", 43) },
        { LEVEL("5.0", 50) },
        { LEVEL("5.1", 51) },
        { LEVEL("5.2", 52) },
        { LEVEL("5.3", 53) },
        { LEVEL("6.0", 60) },
        { LEVEL("6.1", 61) },
        { LEVEL("6.2", 62) },
        { LEVEL("6.3", 63) },
        { LEVEL("7.0", 70) },
        { LEVEL("7.1", 71) },
        { LEVEL("7.2", 72) },
        { LEVEL("7.3", 73) },
#undef LEVEL

    { "crf", "Constant Rate Factor value", OFFSET(crf),
      AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 63, VE },
    { "qp", "Initial Quantizer level value", OFFSET(qp),
      AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 63, VE },
    { "svtav1-params", "Set the SVT-AV1 configuration using a :-separated list of key=value parameters", OFFSET(svtav1_opts), AV_OPT_TYPE_DICT, { 0 }, 0, 0, VE },
    { "qpfile",        "Read per-frame QP offsets from a file (needs SVT-AV1 1.6.0 or newer)", OFFSET(qpfile_path), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, VE },

    { "dolbyvision", "Enable Dolby Vision RPU coding", OFFSET(dovi.enable), AV_OPT_TYPE_BOOL, {.i64 = FF_DOVI_AUTOMATIC }, -1, 1, VE, .unit = "dovi" },
    {   "auto", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = FF_DOVI_AUTOMATIC}, .flags = VE, .unit = "dovi" },

    {NULL},
};

static const AVClass class = {
    .class_name = "libsvtav1",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const FFCodecDefault eb_enc_defaults[] = {
    { "b",         "0"    },
    { "flags",     "+cgop" },
    { "g",         "-1"    },
    { "qmin",      "1"     },
    { "qmax",      "63"    },
    { NULL },
};

const FFCodec ff_libsvtav1_encoder = {
    .p.name         = "libsvtav1",
    CODEC_LONG_NAME("SVT-AV1(Scalable Video Technology for AV1) encoder"),
    .priv_data_size = sizeof(SvtContext),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_AV1,
    .init           = eb_enc_init,
    FF_CODEC_RECEIVE_PACKET_CB(eb_receive_packet),
    .close          = eb_enc_close,
    .p.capabilities = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_OTHER_THREADS,
    .caps_internal  = FF_CODEC_CAP_NOT_INIT_THREADSAFE |
                      FF_CODEC_CAP_AUTO_THREADS | FF_CODEC_CAP_INIT_CLEANUP,
    CODEC_PIXFMTS(AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV420P10),
    .color_ranges   = AVCOL_RANGE_MPEG | AVCOL_RANGE_JPEG,
    .p.priv_class   = &class,
    .defaults       = eb_enc_defaults,
    .p.wrapper_name = "libsvtav1",
};
