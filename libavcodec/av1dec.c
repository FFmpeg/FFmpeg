/*
 * AV1 video decoder
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

#include "libavutil/pixdesc.h"
#include "avcodec.h"
#include "av1dec.h"
#include "bytestream.h"
#include "hwconfig.h"
#include "internal.h"
#include "profiles.h"

static void setup_past_independence(AV1Frame *f)
{
    f->loop_filter_delta_enabled = 1;

    f->loop_filter_ref_deltas[AV1_REF_FRAME_INTRA] = 1;
    f->loop_filter_ref_deltas[AV1_REF_FRAME_LAST] = 0;
    f->loop_filter_ref_deltas[AV1_REF_FRAME_LAST2] = 0;
    f->loop_filter_ref_deltas[AV1_REF_FRAME_LAST3] = 0;
    f->loop_filter_ref_deltas[AV1_REF_FRAME_GOLDEN] = -1;
    f->loop_filter_ref_deltas[AV1_REF_FRAME_BWDREF] = 0;
    f->loop_filter_ref_deltas[AV1_REF_FRAME_ALTREF2] = -1;
    f->loop_filter_ref_deltas[AV1_REF_FRAME_ALTREF] = -1;

    f->loop_filter_mode_deltas[0] = 0;
    f->loop_filter_mode_deltas[1] = 0;
}

static void load_previous_and_update(AV1DecContext *s)
{
    uint8_t primary_frame, prev_frame;

    primary_frame = s->raw_frame_header->primary_ref_frame;
    prev_frame = s->raw_frame_header->ref_frame_idx[primary_frame];
    memcpy(s->cur_frame.loop_filter_ref_deltas,
           s->ref[prev_frame].loop_filter_ref_deltas,
           AV1_NUM_REF_FRAMES * sizeof(int8_t));
    memcpy(s->cur_frame.loop_filter_mode_deltas,
           s->ref[prev_frame].loop_filter_mode_deltas,
           2 * sizeof(int8_t));

    if (s->raw_frame_header->loop_filter_delta_update) {
        for (int i = 0; i < AV1_NUM_REF_FRAMES; i++) {
            if (s->raw_frame_header->update_ref_delta[i])
                s->cur_frame.loop_filter_ref_deltas[i] =
                    s->raw_frame_header->loop_filter_ref_deltas[i];
        }

        for (int i = 0; i < 2; i++) {
            if (s->raw_frame_header->update_mode_delta[i])
                s->cur_frame.loop_filter_mode_deltas[i] =
                    s->raw_frame_header->loop_filter_mode_deltas[i];
        }
    }

    s->cur_frame.loop_filter_delta_enabled =
        s->raw_frame_header->loop_filter_delta_enabled;
}

static uint32_t inverse_recenter(int r, uint32_t v)
{
    if (v > 2 * r)
        return v;
    else if (v & 1)
        return r - ((v + 1) >> 1);
    else
        return r + (v >> 1);
}

static uint32_t decode_unsigned_subexp_with_ref(uint32_t sub_exp,
                                                int mx, int r)
{
    if ((r << 1) <= mx) {
        return inverse_recenter(r, sub_exp);
    } else {
        return mx - 1 - inverse_recenter(mx - 1 - r, sub_exp);
    }
}

static int32_t decode_signed_subexp_with_ref(uint32_t sub_exp, int low,
                                             int high, int r)
{
    int32_t x = decode_unsigned_subexp_with_ref(sub_exp, high - low, r - low);
    return x + low;
}

static void read_global_param(AV1DecContext *s, int type, int ref, int idx)
{
    uint8_t primary_frame, prev_frame;
    uint32_t abs_bits, prec_bits, round, prec_diff, sub, mx;
    int32_t r;

    primary_frame = s->raw_frame_header->primary_ref_frame;
    prev_frame = s->raw_frame_header->ref_frame_idx[primary_frame];
    abs_bits = AV1_GM_ABS_ALPHA_BITS;
    prec_bits = AV1_GM_ALPHA_PREC_BITS;

    if (idx < 2) {
        if (type == AV1_WARP_MODEL_TRANSLATION) {
            abs_bits = AV1_GM_ABS_TRANS_ONLY_BITS -
                !s->raw_frame_header->allow_high_precision_mv;
            prec_bits = AV1_GM_TRANS_ONLY_PREC_BITS -
                !s->raw_frame_header->allow_high_precision_mv;
        } else {
            abs_bits = AV1_GM_ABS_TRANS_BITS;
            prec_bits = AV1_GM_TRANS_PREC_BITS;
        }
    }
    round = (idx % 3) == 2 ? (1 << AV1_WARPEDMODEL_PREC_BITS) : 0;
    prec_diff = AV1_WARPEDMODEL_PREC_BITS - prec_bits;
    sub = (idx % 3) == 2 ? (1 << prec_bits) : 0;
    mx = 1 << abs_bits;
    r = (s->ref[prev_frame].gm_params[ref][idx] >> prec_diff) - sub;

    s->cur_frame.gm_params[ref][idx] =
        (decode_signed_subexp_with_ref(s->raw_frame_header->gm_params[ref][idx],
                                       -mx, mx + 1, r) << prec_diff) + round;
}

/**
* update gm type/params, since cbs already implemented part of this funcation,
* so we don't need to full implement spec.
*/
static void global_motion_params(AV1DecContext *s)
{
    const AV1RawFrameHeader *header = s->raw_frame_header;
    int type, ref;

    for (ref = AV1_REF_FRAME_LAST; ref <= AV1_REF_FRAME_ALTREF; ref++) {
        s->cur_frame.gm_type[ref] = AV1_WARP_MODEL_IDENTITY;
        for (int i = 0; i < 6; i++)
            s->cur_frame.gm_params[ref][i] = (i % 3 == 2) ?
                                             1 << AV1_WARPEDMODEL_PREC_BITS : 0;
    }
    if (header->frame_type == AV1_FRAME_KEY ||
        header->frame_type == AV1_FRAME_INTRA_ONLY)
        return;

    for (ref = AV1_REF_FRAME_LAST; ref <= AV1_REF_FRAME_ALTREF; ref++) {
        if (header->is_global[ref]) {
            if (header->is_rot_zoom[ref]) {
                type = AV1_WARP_MODEL_ROTZOOM;
            } else {
                type = header->is_translation[ref] ? AV1_WARP_MODEL_TRANSLATION
                                                   : AV1_WARP_MODEL_AFFINE;
            }
        } else {
            type = AV1_WARP_MODEL_IDENTITY;
        }
        s->cur_frame.gm_type[ref] = type;

        if (type >= AV1_WARP_MODEL_ROTZOOM) {
            read_global_param(s, type, ref, 2);
            read_global_param(s, type, ref, 3);
            if (type == AV1_WARP_MODEL_AFFINE) {
                read_global_param(s, type, ref, 4);
                read_global_param(s, type, ref, 5);
            } else {
                s->cur_frame.gm_params[ref][4] = -s->cur_frame.gm_params[ref][3];
                s->cur_frame.gm_params[ref][5] = s->cur_frame.gm_params[ref][2];
            }
        }
        if (type >= AV1_WARP_MODEL_TRANSLATION) {
            read_global_param(s, type, ref, 0);
            read_global_param(s, type, ref, 1);
        }
    }
}

static int init_tile_data(AV1DecContext *s)

{
    int cur_tile_num =
        s->raw_frame_header->tile_cols * s->raw_frame_header->tile_rows;
    if (s->tile_num < cur_tile_num) {
        int ret = av_reallocp_array(&s->tile_group_info, cur_tile_num,
                                    sizeof(TileGroupInfo));
        if (ret < 0) {
            s->tile_num = 0;
            return ret;
        }
    }
    s->tile_num = cur_tile_num;

    return 0;
}

static int get_tiles_info(AVCodecContext *avctx, const AV1RawTileGroup *tile_group)
{
    AV1DecContext *s = avctx->priv_data;
    GetByteContext gb;
    uint16_t tile_num, tile_row, tile_col;
    uint32_t size = 0, size_bytes = 0;

    bytestream2_init(&gb, tile_group->tile_data.data,
                     tile_group->tile_data.data_size);
    s->tg_start = tile_group->tg_start;
    s->tg_end = tile_group->tg_end;

    for (tile_num = tile_group->tg_start; tile_num <= tile_group->tg_end; tile_num++) {
        tile_row = tile_num / s->raw_frame_header->tile_cols;
        tile_col = tile_num % s->raw_frame_header->tile_cols;

        if (tile_num == tile_group->tg_end) {
            s->tile_group_info[tile_num].tile_size = bytestream2_get_bytes_left(&gb);
            s->tile_group_info[tile_num].tile_offset = bytestream2_tell(&gb);
            s->tile_group_info[tile_num].tile_row = tile_row;
            s->tile_group_info[tile_num].tile_column = tile_col;
            return 0;
        }
        size_bytes = s->raw_frame_header->tile_size_bytes_minus1 + 1;
        if (bytestream2_get_bytes_left(&gb) < size_bytes)
            return AVERROR_INVALIDDATA;
        size = 0;
        for (int i = 0; i < size_bytes; i++)
            size |= bytestream2_get_byteu(&gb) << 8 * i;
        if (bytestream2_get_bytes_left(&gb) <= size)
            return AVERROR_INVALIDDATA;
        size++;

        s->tile_group_info[tile_num].tile_size = size;
        s->tile_group_info[tile_num].tile_offset = bytestream2_tell(&gb);
        s->tile_group_info[tile_num].tile_row = tile_row;
        s->tile_group_info[tile_num].tile_column = tile_col;

        bytestream2_skipu(&gb, size);
    }

    return 0;

}

static int get_pixel_format(AVCodecContext *avctx)
{
    AV1DecContext *s = avctx->priv_data;
    const AV1RawSequenceHeader *seq = s->raw_seq;
    uint8_t bit_depth;
    int ret;
    enum AVPixelFormat pix_fmt = AV_PIX_FMT_NONE;
#define HWACCEL_MAX (0)
    enum AVPixelFormat pix_fmts[HWACCEL_MAX + 2], *fmtp = pix_fmts;

    if (seq->seq_profile == 2 && seq->color_config.high_bitdepth)
        bit_depth = seq->color_config.twelve_bit ? 12 : 10;
    else if (seq->seq_profile <= 2)
        bit_depth = seq->color_config.high_bitdepth ? 10 : 8;
    else {
        av_log(avctx, AV_LOG_ERROR,
               "Unknown AV1 profile %d.\n", seq->seq_profile);
        return -1;
    }

    if (!seq->color_config.mono_chrome) {
        // 4:4:4 x:0 y:0, 4:2:2 x:1 y:0, 4:2:0 x:1 y:1
        if (seq->color_config.subsampling_x == 0 &&
            seq->color_config.subsampling_y == 0) {
            if (bit_depth == 8)
                pix_fmt = AV_PIX_FMT_YUV444P;
            else if (bit_depth == 10)
                pix_fmt = AV_PIX_FMT_YUV444P10;
            else if (bit_depth == 12)
                pix_fmt = AV_PIX_FMT_YUV444P12;
            else
                av_log(avctx, AV_LOG_WARNING, "Unknown AV1 pixel format.\n");
        } else if (seq->color_config.subsampling_x == 1 &&
                   seq->color_config.subsampling_y == 0) {
            if (bit_depth == 8)
                pix_fmt = AV_PIX_FMT_YUV422P;
            else if (bit_depth == 10)
                pix_fmt = AV_PIX_FMT_YUV422P10;
            else if (bit_depth == 12)
                pix_fmt = AV_PIX_FMT_YUV422P12;
            else
                av_log(avctx, AV_LOG_WARNING, "Unknown AV1 pixel format.\n");
        } else if (seq->color_config.subsampling_x == 1 &&
                   seq->color_config.subsampling_y == 1) {
            if (bit_depth == 8)
                pix_fmt = AV_PIX_FMT_YUV420P;
            else if (bit_depth == 10)
                pix_fmt = AV_PIX_FMT_YUV420P10;
            else if (bit_depth == 12)
                pix_fmt = AV_PIX_FMT_YUV420P12;
            else
                av_log(avctx, AV_LOG_WARNING, "Unknown AV1 pixel format.\n");
        }
    } else {
        if (seq->color_config.subsampling_x == 1 &&
            seq->color_config.subsampling_y == 1)
            pix_fmt = AV_PIX_FMT_YUV440P;
        else
            av_log(avctx, AV_LOG_WARNING, "Unknown AV1 pixel format.\n");
    }

    av_log(avctx, AV_LOG_DEBUG, "AV1 decode get format: %s.\n",
           av_get_pix_fmt_name(pix_fmt));

    if (pix_fmt == AV_PIX_FMT_NONE)
        return -1;
    s->pix_fmt = pix_fmt;

    *fmtp++ = s->pix_fmt;
    *fmtp = AV_PIX_FMT_NONE;

    ret = ff_thread_get_format(avctx, pix_fmts);
    if (ret < 0)
        return ret;

    /**
     * check if the HW accel is inited correctly. If not, return un-implemented.
     * Since now the av1 decoder doesn't support native decode, if it will be
     * implemented in the future, need remove this check.
     */
    if (!avctx->hwaccel) {
        av_log(avctx, AV_LOG_ERROR, "Your platform doesn't suppport"
               " hardware accelerated AV1 decoding.\n");
        return AVERROR(ENOSYS);
    }

    avctx->pix_fmt = ret;

    return 0;
}

static void av1_frame_unref(AVCodecContext *avctx, AV1Frame *f)
{
    ff_thread_release_buffer(avctx, &f->tf);
    av_buffer_unref(&f->hwaccel_priv_buf);
    f->hwaccel_picture_private = NULL;
    f->spatial_id = f->temporal_id = 0;
}

static int av1_frame_ref(AVCodecContext *avctx, AV1Frame *dst, const AV1Frame *src)
{
    int ret;

    ret = ff_thread_ref_frame(&dst->tf, &src->tf);
    if (ret < 0)
        return ret;

    if (src->hwaccel_picture_private) {
        dst->hwaccel_priv_buf = av_buffer_ref(src->hwaccel_priv_buf);
        if (!dst->hwaccel_priv_buf)
            goto fail;
        dst->hwaccel_picture_private = dst->hwaccel_priv_buf->data;
    }

    dst->spatial_id = src->spatial_id;
    dst->temporal_id = src->temporal_id;
    dst->loop_filter_delta_enabled = src->loop_filter_delta_enabled;
    memcpy(dst->loop_filter_ref_deltas,
           src->loop_filter_ref_deltas,
           AV1_NUM_REF_FRAMES * sizeof(int8_t));
    memcpy(dst->loop_filter_mode_deltas,
           src->loop_filter_mode_deltas,
           2 * sizeof(int8_t));
    memcpy(dst->gm_type,
           src->gm_type,
           AV1_NUM_REF_FRAMES * sizeof(uint8_t));
    memcpy(dst->gm_params,
           src->gm_params,
           AV1_NUM_REF_FRAMES * 6 * sizeof(int32_t));

    return 0;

fail:
    av1_frame_unref(avctx, dst);
    return AVERROR(ENOMEM);
}

static av_cold int av1_decode_free(AVCodecContext *avctx)
{
    AV1DecContext *s = avctx->priv_data;

    for (int i = 0; i < FF_ARRAY_ELEMS(s->ref); i++) {
        av1_frame_unref(avctx, &s->ref[i]);
        av_frame_free(&s->ref[i].tf.f);
    }
    av1_frame_unref(avctx, &s->cur_frame);
    av_frame_free(&s->cur_frame.tf.f);

    av_buffer_unref(&s->seq_ref);
    av_buffer_unref(&s->header_ref);
    av_freep(&s->tile_group_info);

    ff_cbs_fragment_free(&s->current_obu);
    ff_cbs_close(&s->cbc);

    return 0;
}

static int set_context_with_sequence(AVCodecContext *avctx,
                                     const AV1RawSequenceHeader *seq)
{
    int width = seq->max_frame_width_minus_1 + 1;
    int height = seq->max_frame_height_minus_1 + 1;

    avctx->profile = seq->seq_profile;
    avctx->level = seq->seq_level_idx[0];

    avctx->color_range =
        seq->color_config.color_range ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;
    avctx->color_primaries = seq->color_config.color_primaries;
    avctx->colorspace = seq->color_config.color_primaries;
    avctx->color_trc = seq->color_config.transfer_characteristics;

    switch (seq->color_config.chroma_sample_position) {
    case AV1_CSP_VERTICAL:
        avctx->chroma_sample_location = AVCHROMA_LOC_LEFT;
        break;
    case AV1_CSP_COLOCATED:
        avctx->chroma_sample_location = AVCHROMA_LOC_TOPLEFT;
        break;
    }

    if (avctx->width != width || avctx->height != height) {
        int ret = ff_set_dimensions(avctx, width, height);
        if (ret < 0)
            return ret;
    }
    avctx->sample_aspect_ratio = (AVRational) { 1, 1 };

    if (seq->timing_info.num_units_in_display_tick &&
        seq->timing_info.time_scale) {
        av_reduce(&avctx->framerate.den, &avctx->framerate.num,
                  seq->timing_info.num_units_in_display_tick,
                  seq->timing_info.time_scale,
                  INT_MAX);
        if (seq->timing_info.equal_picture_interval)
            avctx->ticks_per_frame = seq->timing_info.num_ticks_per_picture_minus_1 + 1;
    }

    return 0;
}

static int update_context_with_frame_header(AVCodecContext *avctx,
                                            const AV1RawFrameHeader *header)
{
    AVRational aspect_ratio;
    int width = header->frame_width_minus_1 + 1;
    int height = header->frame_height_minus_1 + 1;
    int r_width = header->render_width_minus_1 + 1;
    int r_height = header->render_height_minus_1 + 1;
    int ret;

    if (avctx->width != width || avctx->height != height) {
        ret = ff_set_dimensions(avctx, width, height);
        if (ret < 0)
            return ret;
    }

    av_reduce(&aspect_ratio.num, &aspect_ratio.den,
              (int64_t)height * r_width,
              (int64_t)width * r_height,
              INT_MAX);

    if (av_cmp_q(avctx->sample_aspect_ratio, aspect_ratio)) {
        ret = ff_set_sar(avctx, aspect_ratio);
        if (ret < 0)
            return ret;
    }

    return 0;
}

static av_cold int av1_decode_init(AVCodecContext *avctx)
{
    AV1DecContext *s = avctx->priv_data;
    AV1RawSequenceHeader *seq;
    int ret;

    s->avctx = avctx;
    s->pix_fmt = AV_PIX_FMT_NONE;

    for (int i = 0; i < FF_ARRAY_ELEMS(s->ref); i++) {
        s->ref[i].tf.f = av_frame_alloc();
        if (!s->ref[i].tf.f) {
            av_log(avctx, AV_LOG_ERROR,
                   "Failed to allocate reference frame buffer %d.\n", i);
            return AVERROR(ENOMEM);
        }
    }

    s->cur_frame.tf.f = av_frame_alloc();
    if (!s->cur_frame.tf.f) {
        av_log(avctx, AV_LOG_ERROR,
               "Failed to allocate current frame buffer.\n");
        return AVERROR(ENOMEM);
    }

    ret = ff_cbs_init(&s->cbc, AV_CODEC_ID_AV1, avctx);
    if (ret < 0)
        return ret;

    if (avctx->extradata && avctx->extradata_size) {
        ret = ff_cbs_read(s->cbc, &s->current_obu, avctx->extradata,
                          avctx->extradata_size);
        if (ret < 0) {
            av_log(avctx, AV_LOG_WARNING, "Failed to read extradata.\n");
            return ret;
        }

        seq = ((CodedBitstreamAV1Context *)(s->cbc->priv_data))->sequence_header;
        if (!seq) {
            av_log(avctx, AV_LOG_WARNING, "No sequence header available.\n");
            goto end;
        }

        ret = set_context_with_sequence(avctx, seq);
        if (ret < 0) {
            av_log(avctx, AV_LOG_WARNING, "Failed to set decoder context.\n");
            goto end;
        }

        end:
        ff_cbs_fragment_reset(&s->current_obu);
    }

    return ret;
}

static int av1_frame_alloc(AVCodecContext *avctx, AV1Frame *f)
{
    AV1DecContext *s = avctx->priv_data;
    AV1RawFrameHeader *header= s->raw_frame_header;
    AVFrame *frame;
    int ret;

    ret = update_context_with_frame_header(avctx, header);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to update context with frame header\n");
        return ret;
    }

    if ((ret = ff_thread_get_buffer(avctx, &f->tf, AV_GET_BUFFER_FLAG_REF)) < 0)
        return ret;

    frame = f->tf.f;
    frame->key_frame = header->frame_type == AV1_FRAME_KEY;

    switch (header->frame_type) {
    case AV1_FRAME_KEY:
    case AV1_FRAME_INTRA_ONLY:
        frame->pict_type = AV_PICTURE_TYPE_I;
        break;
    case AV1_FRAME_INTER:
        frame->pict_type = AV_PICTURE_TYPE_P;
        break;
    case AV1_FRAME_SWITCH:
        frame->pict_type = AV_PICTURE_TYPE_SP;
        break;
    }

    if (avctx->hwaccel) {
        const AVHWAccel *hwaccel = avctx->hwaccel;
        if (hwaccel->frame_priv_data_size) {
            f->hwaccel_priv_buf =
                av_buffer_allocz(hwaccel->frame_priv_data_size);
            if (!f->hwaccel_priv_buf)
                goto fail;
            f->hwaccel_picture_private = f->hwaccel_priv_buf->data;
        }
    }
    return 0;

fail:
    av1_frame_unref(avctx, f);
    return AVERROR(ENOMEM);
}

static int set_output_frame(AVCodecContext *avctx, AVFrame *frame,
                            const AVPacket *pkt, int *got_frame)
{
    AV1DecContext *s = avctx->priv_data;
    const AVFrame *srcframe = s->cur_frame.tf.f;
    int ret;

    ret = av_frame_ref(frame, srcframe);
    if (ret < 0)
        return ret;

    frame->pts = pkt->pts;
    frame->pkt_dts = pkt->dts;
    frame->pkt_size = pkt->size;

    *got_frame = 1;

    return 0;
}

static int update_reference_list(AVCodecContext *avctx)
{
    AV1DecContext *s = avctx->priv_data;
    const AV1RawFrameHeader *header = s->raw_frame_header;
    int ret;

    for (int i = 0; i < AV1_NUM_REF_FRAMES; i++) {
        if (header->refresh_frame_flags & (1 << i)) {
            if (s->ref[i].tf.f->buf[0])
                av1_frame_unref(avctx, &s->ref[i]);
            if ((ret = av1_frame_ref(avctx, &s->ref[i], &s->cur_frame)) < 0) {
                av_log(avctx, AV_LOG_ERROR,
                       "Failed to update frame %d in reference list\n", i);
                return ret;
            }
        }
    }
    return 0;
}

static int get_current_frame(AVCodecContext *avctx)
{
    AV1DecContext *s = avctx->priv_data;
    int ret;

    if (s->cur_frame.tf.f->buf[0])
        av1_frame_unref(avctx, &s->cur_frame);

    ret = av1_frame_alloc(avctx, &s->cur_frame);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR,
               "Failed to allocate space for current frame.\n");
        return ret;
    }

    ret = init_tile_data(s);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to init tile data.\n");
        return ret;
    }

    if (s->raw_frame_header->primary_ref_frame == AV1_PRIMARY_REF_NONE)
        setup_past_independence(&s->cur_frame);
    else
        load_previous_and_update(s);

    global_motion_params(s);

    return ret;
}

static int av1_decode_frame(AVCodecContext *avctx, void *frame,
                            int *got_frame, AVPacket *pkt)
{
    AV1DecContext *s = avctx->priv_data;
    AV1RawTileGroup *raw_tile_group = NULL;
    int ret;

    ret = ff_cbs_read_packet(s->cbc, &s->current_obu, pkt);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to read packet.\n");
        goto end;
    }
    av_log(avctx, AV_LOG_DEBUG, "Total obu for this frame:%d.\n",
           s->current_obu.nb_units);

    for (int i = 0; i < s->current_obu.nb_units; i++) {
        CodedBitstreamUnit *unit = &s->current_obu.units[i];
        AV1RawOBU *obu = unit->content;
        const AV1RawOBUHeader *header = &obu->header;
        av_log(avctx, AV_LOG_DEBUG, "Obu idx:%d, obu type:%d.\n", i, unit->type);

        switch (unit->type) {
        case AV1_OBU_SEQUENCE_HEADER:
            av_buffer_unref(&s->seq_ref);
            s->seq_ref = av_buffer_ref(unit->content_ref);
            if (!s->seq_ref) {
                ret = AVERROR(ENOMEM);
                goto end;
            }

            s->raw_seq = &obu->obu.sequence_header;

            ret = set_context_with_sequence(avctx, s->raw_seq);
            if (ret < 0) {
                av_log(avctx, AV_LOG_ERROR, "Failed to set context.\n");
                s->raw_seq = NULL;
                goto end;
            }

            if (s->pix_fmt == AV_PIX_FMT_NONE) {
                ret = get_pixel_format(avctx);
                if (ret < 0) {
                    av_log(avctx, AV_LOG_ERROR,
                           "Failed to get pixel format.\n");
                    s->raw_seq = NULL;
                    goto end;
                }
            }

            if (avctx->hwaccel && avctx->hwaccel->decode_params) {
                ret = avctx->hwaccel->decode_params(avctx, unit->type, unit->data,
                                                    unit->data_size);
                if (ret < 0) {
                    av_log(avctx, AV_LOG_ERROR, "HW accel decode params fail.\n");
                    s->raw_seq = NULL;
                    goto end;
                }
            }
            break;
        case AV1_OBU_REDUNDANT_FRAME_HEADER:
            if (s->raw_frame_header)
                break;
        // fall-through
        case AV1_OBU_FRAME:
        case AV1_OBU_FRAME_HEADER:
            if (!s->raw_seq) {
                av_log(avctx, AV_LOG_ERROR, "Missing Sequence Header.\n");
                ret = AVERROR_INVALIDDATA;
                goto end;
            }

            av_buffer_unref(&s->header_ref);
            s->header_ref = av_buffer_ref(unit->content_ref);
            if (!s->header_ref) {
                ret = AVERROR(ENOMEM);
                goto end;
            }

            if (unit->type == AV1_OBU_FRAME)
                s->raw_frame_header = &obu->obu.frame.header;
            else
                s->raw_frame_header = &obu->obu.frame_header;

            if (s->raw_frame_header->show_existing_frame) {
                if (s->cur_frame.tf.f->buf[0])
                    av1_frame_unref(avctx, &s->cur_frame);

                ret = av1_frame_ref(avctx, &s->cur_frame,
                                    &s->ref[s->raw_frame_header->frame_to_show_map_idx]);
                if (ret < 0) {
                    av_log(avctx, AV_LOG_ERROR, "Failed to get reference frame.\n");
                    goto end;
                }

                ret = update_reference_list(avctx);
                if (ret < 0) {
                    av_log(avctx, AV_LOG_ERROR, "Failed to update reference list.\n");
                    goto end;
                }

                ret = set_output_frame(avctx, frame, pkt, got_frame);
                if (ret < 0)
                    av_log(avctx, AV_LOG_ERROR, "Set output frame error.\n");

                s->raw_frame_header = NULL;

                goto end;
            }

            ret = get_current_frame(avctx);
            if (ret < 0) {
                av_log(avctx, AV_LOG_ERROR, "Get current frame error\n");
                goto end;
            }

            s->cur_frame.spatial_id  = header->spatial_id;
            s->cur_frame.temporal_id = header->temporal_id;

            if (avctx->hwaccel) {
                ret = avctx->hwaccel->start_frame(avctx, unit->data,
                                                  unit->data_size);
                if (ret < 0) {
                    av_log(avctx, AV_LOG_ERROR, "HW accel start frame fail.\n");
                    goto end;
                }
            }
            if (unit->type != AV1_OBU_FRAME)
                break;
        // fall-through
        case AV1_OBU_TILE_GROUP:
            if (!s->raw_frame_header) {
                av_log(avctx, AV_LOG_ERROR, "Missing Frame Header.\n");
                ret = AVERROR_INVALIDDATA;
                goto end;
            }

            if (unit->type == AV1_OBU_FRAME)
                raw_tile_group = &obu->obu.frame.tile_group;
            else
                raw_tile_group = &obu->obu.tile_group;

            ret = get_tiles_info(avctx, raw_tile_group);
            if (ret < 0)
                goto end;

            if (avctx->hwaccel) {
                ret = avctx->hwaccel->decode_slice(avctx,
                                                   raw_tile_group->tile_data.data,
                                                   raw_tile_group->tile_data.data_size);
                if (ret < 0) {
                    av_log(avctx, AV_LOG_ERROR,
                           "HW accel decode slice fail.\n");
                    goto end;
                }
            }
            break;
        case AV1_OBU_TILE_LIST:
        case AV1_OBU_TEMPORAL_DELIMITER:
        case AV1_OBU_PADDING:
        case AV1_OBU_METADATA:
            break;
        default:
            av_log(avctx, AV_LOG_DEBUG,
                   "Unknown obu type: %d (%"SIZE_SPECIFIER" bits).\n",
                   unit->type, unit->data_size);
        }

        if (raw_tile_group && (s->tile_num == raw_tile_group->tg_end + 1)) {
            if (avctx->hwaccel) {
                ret = avctx->hwaccel->end_frame(avctx);
                if (ret < 0) {
                    av_log(avctx, AV_LOG_ERROR, "HW accel end frame fail.\n");
                    goto end;
                }
            }

            ret = update_reference_list(avctx);
            if (ret < 0) {
                av_log(avctx, AV_LOG_ERROR, "Failed to update reference list.\n");
                goto end;
            }

            if (s->raw_frame_header->show_frame) {
                ret = set_output_frame(avctx, frame, pkt, got_frame);
                if (ret < 0) {
                    av_log(avctx, AV_LOG_ERROR, "Set output frame error\n");
                    goto end;
                }
            }
            raw_tile_group = NULL;
            s->raw_frame_header = NULL;
        }
    }

end:
    ff_cbs_fragment_reset(&s->current_obu);
    if (ret < 0)
        s->raw_frame_header = NULL;
    return ret;
}

static void av1_decode_flush(AVCodecContext *avctx)
{
    AV1DecContext *s = avctx->priv_data;

    for (int i = 0; i < FF_ARRAY_ELEMS(s->ref); i++)
        av1_frame_unref(avctx, &s->ref[i]);

    av1_frame_unref(avctx, &s->cur_frame);
    s->raw_frame_header = NULL;
    s->raw_seq = NULL;

    ff_cbs_flush(s->cbc);
}

AVCodec ff_av1_decoder = {
    .name                  = "av1",
    .long_name             = NULL_IF_CONFIG_SMALL("Alliance for Open Media AV1"),
    .type                  = AVMEDIA_TYPE_VIDEO,
    .id                    = AV_CODEC_ID_AV1,
    .priv_data_size        = sizeof(AV1DecContext),
    .init                  = av1_decode_init,
    .close                 = av1_decode_free,
    .decode                = av1_decode_frame,
    .capabilities          = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_AVOID_PROBING,
    .caps_internal         = FF_CODEC_CAP_INIT_THREADSAFE |
                             FF_CODEC_CAP_INIT_CLEANUP |
                             FF_CODEC_CAP_SETS_PKT_DTS,
    .flush                 = av1_decode_flush,
    .profiles              = NULL_IF_CONFIG_SMALL(ff_av1_profiles),
    .hw_configs            = (const AVCodecHWConfigInternal * []) {
        NULL
    },
};
