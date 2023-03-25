/*
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

#include <math.h>
#include <stdint.h>

#include "ffmpeg.h"

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/avutil.h"
#include "libavutil/dict.h"
#include "libavutil/display.h"
#include "libavutil/eval.h"
#include "libavutil/frame.h"
#include "libavutil/log.h"
#include "libavutil/pixdesc.h"
#include "libavutil/rational.h"

#include "libavfilter/buffersink.h"

#include "libavcodec/avcodec.h"

#include "libavformat/avformat.h"

static void set_encoder_id(OutputFile *of, OutputStream *ost)
{
    const char *cname = ost->enc_ctx->codec->name;
    uint8_t *encoder_string;
    int encoder_string_len;

    if (av_dict_get(ost->st->metadata, "encoder",  NULL, 0))
        return;

    encoder_string_len = sizeof(LIBAVCODEC_IDENT) + strlen(cname) + 2;
    encoder_string     = av_mallocz(encoder_string_len);
    if (!encoder_string)
        report_and_exit(AVERROR(ENOMEM));

    if (!of->bitexact && !ost->bitexact)
        av_strlcpy(encoder_string, LIBAVCODEC_IDENT " ", encoder_string_len);
    else
        av_strlcpy(encoder_string, "Lavc ", encoder_string_len);
    av_strlcat(encoder_string, cname, encoder_string_len);
    av_dict_set(&ost->st->metadata, "encoder",  encoder_string,
                AV_DICT_DONT_STRDUP_VAL | AV_DICT_DONT_OVERWRITE);
}

static void init_encoder_time_base(OutputStream *ost, AVRational default_time_base)
{
    InputStream *ist = ost->ist;
    AVCodecContext *enc_ctx = ost->enc_ctx;

    if (ost->enc_timebase.num > 0) {
        enc_ctx->time_base = ost->enc_timebase;
        return;
    }

    if (ost->enc_timebase.num < 0) {
        if (ist) {
            enc_ctx->time_base = ist->st->time_base;
            return;
        }

        av_log(ost, AV_LOG_WARNING,
               "Input stream data not available, using default time base\n");
    }

    enc_ctx->time_base = default_time_base;
}

int enc_open(OutputStream *ost, AVFrame *frame)
{
    InputStream *ist = ost->ist;
    AVCodecContext *enc_ctx = ost->enc_ctx;
    AVCodecContext *dec_ctx = NULL;
    const AVCodec      *enc = enc_ctx->codec;
    OutputFile      *of = output_files[ost->file_index];
    int ret;

    if (ost->initialized)
        return 0;

    set_encoder_id(output_files[ost->file_index], ost);

    if (ist) {
        dec_ctx = ist->dec_ctx;
    }

    if (enc_ctx->codec_type == AVMEDIA_TYPE_VIDEO) {
        if (!ost->frame_rate.num)
            ost->frame_rate = av_buffersink_get_frame_rate(ost->filter->filter);
        if (!ost->frame_rate.num && !ost->max_frame_rate.num) {
            ost->frame_rate = (AVRational){25, 1};
            av_log(ost, AV_LOG_WARNING,
                   "No information "
                   "about the input framerate is available. Falling "
                   "back to a default value of 25fps. Use the -r option "
                   "if you want a different framerate.\n");
        }

        if (ost->max_frame_rate.num &&
            (av_q2d(ost->frame_rate) > av_q2d(ost->max_frame_rate) ||
            !ost->frame_rate.den))
            ost->frame_rate = ost->max_frame_rate;

        if (enc->supported_framerates && !ost->force_fps) {
            int idx = av_find_nearest_q_idx(ost->frame_rate, enc->supported_framerates);
            ost->frame_rate = enc->supported_framerates[idx];
        }
        // reduce frame rate for mpeg4 to be within the spec limits
        if (enc_ctx->codec_id == AV_CODEC_ID_MPEG4) {
            av_reduce(&ost->frame_rate.num, &ost->frame_rate.den,
                      ost->frame_rate.num, ost->frame_rate.den, 65535);
        }
    }

    switch (enc_ctx->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        enc_ctx->sample_fmt     = av_buffersink_get_format(ost->filter->filter);
        enc_ctx->sample_rate    = av_buffersink_get_sample_rate(ost->filter->filter);
        ret = av_buffersink_get_ch_layout(ost->filter->filter, &enc_ctx->ch_layout);
        if (ret < 0)
            return ret;

        if (ost->bits_per_raw_sample)
            enc_ctx->bits_per_raw_sample = ost->bits_per_raw_sample;
        else if (dec_ctx && ost->filter->graph->is_meta)
            enc_ctx->bits_per_raw_sample = FFMIN(dec_ctx->bits_per_raw_sample,
                                                 av_get_bytes_per_sample(enc_ctx->sample_fmt) << 3);

        init_encoder_time_base(ost, av_make_q(1, enc_ctx->sample_rate));
        break;

    case AVMEDIA_TYPE_VIDEO:
        init_encoder_time_base(ost, av_inv_q(ost->frame_rate));

        if (!(enc_ctx->time_base.num && enc_ctx->time_base.den))
            enc_ctx->time_base = av_buffersink_get_time_base(ost->filter->filter);
        if (   av_q2d(enc_ctx->time_base) < 0.001 && ost->vsync_method != VSYNC_PASSTHROUGH
           && (ost->vsync_method == VSYNC_CFR || ost->vsync_method == VSYNC_VSCFR ||
               (ost->vsync_method == VSYNC_AUTO && !(of->format->flags & AVFMT_VARIABLE_FPS)))){
            av_log(ost, AV_LOG_WARNING, "Frame rate very high for a muxer not efficiently supporting it.\n"
                                        "Please consider specifying a lower framerate, a different muxer or "
                                        "setting vsync/fps_mode to vfr\n");
        }

        enc_ctx->width  = av_buffersink_get_w(ost->filter->filter);
        enc_ctx->height = av_buffersink_get_h(ost->filter->filter);
        enc_ctx->sample_aspect_ratio = ost->st->sample_aspect_ratio =
            ost->frame_aspect_ratio.num ? // overridden by the -aspect cli option
            av_mul_q(ost->frame_aspect_ratio, (AVRational){ enc_ctx->height, enc_ctx->width }) :
            av_buffersink_get_sample_aspect_ratio(ost->filter->filter);

        enc_ctx->pix_fmt = av_buffersink_get_format(ost->filter->filter);

        if (ost->bits_per_raw_sample)
            enc_ctx->bits_per_raw_sample = ost->bits_per_raw_sample;
        else if (dec_ctx && ost->filter->graph->is_meta)
            enc_ctx->bits_per_raw_sample = FFMIN(dec_ctx->bits_per_raw_sample,
                                                 av_pix_fmt_desc_get(enc_ctx->pix_fmt)->comp[0].depth);

        if (frame) {
            enc_ctx->color_range            = frame->color_range;
            enc_ctx->color_primaries        = frame->color_primaries;
            enc_ctx->color_trc              = frame->color_trc;
            enc_ctx->colorspace             = frame->colorspace;
            enc_ctx->chroma_sample_location = frame->chroma_location;
        }

        enc_ctx->framerate = ost->frame_rate;

        ost->st->avg_frame_rate = ost->frame_rate;

        // Field order: autodetection
        if (frame) {
            if (enc_ctx->flags & (AV_CODEC_FLAG_INTERLACED_DCT | AV_CODEC_FLAG_INTERLACED_ME) &&
                ost->top_field_first >= 0)
                frame->top_field_first = !!ost->top_field_first;

            if (frame->interlaced_frame) {
                if (enc->id == AV_CODEC_ID_MJPEG)
                    enc_ctx->field_order = frame->top_field_first ? AV_FIELD_TT:AV_FIELD_BB;
                else
                    enc_ctx->field_order = frame->top_field_first ? AV_FIELD_TB:AV_FIELD_BT;
            } else
                enc_ctx->field_order = AV_FIELD_PROGRESSIVE;
        }

        // Field order: override
        if (ost->top_field_first == 0) {
            enc_ctx->field_order = AV_FIELD_BB;
        } else if (ost->top_field_first == 1) {
            enc_ctx->field_order = AV_FIELD_TT;
        }

        break;
    case AVMEDIA_TYPE_SUBTITLE:
        enc_ctx->time_base = AV_TIME_BASE_Q;
        if (!enc_ctx->width) {
            enc_ctx->width     = ost->ist->par->width;
            enc_ctx->height    = ost->ist->par->height;
        }
        if (dec_ctx && dec_ctx->subtitle_header) {
            /* ASS code assumes this buffer is null terminated so add extra byte. */
            enc_ctx->subtitle_header = av_mallocz(dec_ctx->subtitle_header_size + 1);
            if (!enc_ctx->subtitle_header)
                return AVERROR(ENOMEM);
            memcpy(enc_ctx->subtitle_header, dec_ctx->subtitle_header,
                   dec_ctx->subtitle_header_size);
            enc_ctx->subtitle_header_size = dec_ctx->subtitle_header_size;
        }
        if (ist && ist->dec->type == AVMEDIA_TYPE_SUBTITLE &&
            enc_ctx->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            int input_props = 0, output_props = 0;
            AVCodecDescriptor const *input_descriptor =
                avcodec_descriptor_get(ist->dec->id);
            AVCodecDescriptor const *output_descriptor =
                avcodec_descriptor_get(enc_ctx->codec_id);
            if (input_descriptor)
                input_props = input_descriptor->props & (AV_CODEC_PROP_TEXT_SUB | AV_CODEC_PROP_BITMAP_SUB);
            if (output_descriptor)
                output_props = output_descriptor->props & (AV_CODEC_PROP_TEXT_SUB | AV_CODEC_PROP_BITMAP_SUB);
            if (input_props && output_props && input_props != output_props) {
                av_log(ost, AV_LOG_ERROR,
                       "Subtitle encoding currently only possible from text to text "
                       "or bitmap to bitmap");
                return AVERROR_INVALIDDATA;
            }
        }

        break;
    default:
        abort();
        break;
    }

    if (ost->bitexact)
        enc_ctx->flags |= AV_CODEC_FLAG_BITEXACT;

    if (!av_dict_get(ost->encoder_opts, "threads", NULL, 0))
        av_dict_set(&ost->encoder_opts, "threads", "auto", 0);

    if (enc->capabilities & AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE) {
        ret = av_dict_set(&ost->encoder_opts, "flags", "+copy_opaque", AV_DICT_MULTIKEY);
        if (ret < 0)
            return ret;
    }

    ret = hw_device_setup_for_encode(ost);
    if (ret < 0) {
        av_log(ost, AV_LOG_ERROR,
               "Encoding hardware device setup failed: %s\n", av_err2str(ret));
        return ret;
    }

    if ((ret = avcodec_open2(ost->enc_ctx, enc, &ost->encoder_opts)) < 0) {
        if (ret != AVERROR_EXPERIMENTAL)
            av_log(ost, AV_LOG_ERROR, "Error while opening encoder - maybe "
                   "incorrect parameters such as bit_rate, rate, width or height.\n");
        return ret;
    }

    if (ost->enc_ctx->frame_size) {
        av_assert0(ost->sq_idx_encode >= 0);
        sq_frame_samples(output_files[ost->file_index]->sq_encode,
                         ost->sq_idx_encode, ost->enc_ctx->frame_size);
    }

    assert_avoptions(ost->encoder_opts);
    if (ost->enc_ctx->bit_rate && ost->enc_ctx->bit_rate < 1000 &&
        ost->enc_ctx->codec_id != AV_CODEC_ID_CODEC2 /* don't complain about 700 bit/s modes */)
        av_log(ost, AV_LOG_WARNING, "The bitrate parameter is set too low."
                                    " It takes bits/s as argument, not kbits/s\n");

    ret = avcodec_parameters_from_context(ost->st->codecpar, ost->enc_ctx);
    if (ret < 0) {
        av_log(ost, AV_LOG_FATAL,
               "Error initializing the output stream codec context.\n");
        exit_program(1);
    }

    if (ost->enc_ctx->nb_coded_side_data) {
        int i;

        for (i = 0; i < ost->enc_ctx->nb_coded_side_data; i++) {
            const AVPacketSideData *sd_src = &ost->enc_ctx->coded_side_data[i];
            uint8_t *dst_data;

            dst_data = av_stream_new_side_data(ost->st, sd_src->type, sd_src->size);
            if (!dst_data)
                return AVERROR(ENOMEM);
            memcpy(dst_data, sd_src->data, sd_src->size);
        }
    }

    /*
     * Add global input side data. For now this is naive, and copies it
     * from the input stream's global side data. All side data should
     * really be funneled over AVFrame and libavfilter, then added back to
     * packet side data, and then potentially using the first packet for
     * global side data.
     */
    if (ist) {
        int i;
        for (i = 0; i < ist->st->nb_side_data; i++) {
            AVPacketSideData *sd = &ist->st->side_data[i];
            if (sd->type != AV_PKT_DATA_CPB_PROPERTIES) {
                uint8_t *dst = av_stream_new_side_data(ost->st, sd->type, sd->size);
                if (!dst)
                    return AVERROR(ENOMEM);
                memcpy(dst, sd->data, sd->size);
                if (ist->autorotate && sd->type == AV_PKT_DATA_DISPLAYMATRIX)
                    av_display_rotation_set((int32_t *)dst, 0);
            }
        }
    }

    // copy timebase while removing common factors
    if (ost->st->time_base.num <= 0 || ost->st->time_base.den <= 0)
        ost->st->time_base = av_add_q(ost->enc_ctx->time_base, (AVRational){0, 1});

    // copy estimated duration as a hint to the muxer
    if (ost->st->duration <= 0 && ist && ist->st->duration > 0)
        ost->st->duration = av_rescale_q(ist->st->duration, ist->st->time_base, ost->st->time_base);

    ost->mux_timebase = enc_ctx->time_base;

    ret = of_stream_init(of, ost);
    if (ret < 0)
        return ret;

    return 0;
}

void enc_subtitle(OutputFile *of, OutputStream *ost, AVSubtitle *sub)
{
    int subtitle_out_max_size = 1024 * 1024;
    int subtitle_out_size, nb, i, ret;
    AVCodecContext *enc;
    AVPacket *pkt = ost->pkt;
    int64_t pts;

    if (sub->pts == AV_NOPTS_VALUE) {
        av_log(ost, AV_LOG_ERROR, "Subtitle packets must have a pts\n");
        if (exit_on_error)
            exit_program(1);
        return;
    }

    enc = ost->enc_ctx;

    /* Note: DVB subtitle need one packet to draw them and one other
       packet to clear them */
    /* XXX: signal it in the codec context ? */
    if (enc->codec_id == AV_CODEC_ID_DVB_SUBTITLE)
        nb = 2;
    else if (enc->codec_id == AV_CODEC_ID_ASS)
        nb = FFMAX(sub->num_rects, 1);
    else
        nb = 1;

    /* shift timestamp to honor -ss and make check_recording_time() work with -t */
    pts = sub->pts;
    if (output_files[ost->file_index]->start_time != AV_NOPTS_VALUE)
        pts -= output_files[ost->file_index]->start_time;
    for (i = 0; i < nb; i++) {
        AVSubtitle local_sub = *sub;

        if (!check_recording_time(ost, pts, AV_TIME_BASE_Q))
            return;

        ret = av_new_packet(pkt, subtitle_out_max_size);
        if (ret < 0)
            report_and_exit(AVERROR(ENOMEM));

        local_sub.pts = pts;
        // start_display_time is required to be 0
        local_sub.pts               += av_rescale_q(sub->start_display_time, (AVRational){ 1, 1000 }, AV_TIME_BASE_Q);
        local_sub.end_display_time  -= sub->start_display_time;
        local_sub.start_display_time = 0;

        if (enc->codec_id == AV_CODEC_ID_DVB_SUBTITLE && i == 1)
            local_sub.num_rects = 0;
        else if (enc->codec_id == AV_CODEC_ID_ASS && sub->num_rects > 0) {
            local_sub.num_rects = 1;
            local_sub.rects += i;
        }

        ost->frames_encoded++;

        subtitle_out_size = avcodec_encode_subtitle(enc, pkt->data, pkt->size, &local_sub);
        if (subtitle_out_size < 0) {
            av_log(ost, AV_LOG_FATAL, "Subtitle encoding failed\n");
            exit_program(1);
        }

        av_shrink_packet(pkt, subtitle_out_size);
        pkt->time_base = ost->mux_timebase;
        pkt->pts  = av_rescale_q(sub->pts, AV_TIME_BASE_Q, pkt->time_base);
        pkt->duration = av_rescale_q(sub->end_display_time, (AVRational){ 1, 1000 }, pkt->time_base);
        if (enc->codec_id == AV_CODEC_ID_DVB_SUBTITLE) {
            /* XXX: the pts correction is handled here. Maybe handling
               it in the codec would be better */
            if (i == 0)
                pkt->pts += av_rescale_q(sub->start_display_time, (AVRational){ 1, 1000 }, pkt->time_base);
            else
                pkt->pts += av_rescale_q(sub->end_display_time, (AVRational){ 1, 1000 }, pkt->time_base);
        }
        pkt->dts = pkt->pts;

        of_output_packet(of, pkt, ost, 0);
    }
}
