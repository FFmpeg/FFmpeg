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
#include "libavutil/intreadwrite.h"
#include "libavutil/log.h"
#include "libavutil/pixdesc.h"
#include "libavutil/rational.h"
#include "libavutil/timestamp.h"

#include "libavfilter/buffersink.h"

#include "libavcodec/avcodec.h"

// FIXME private header, used for mid_pred()
#include "libavcodec/mathops.h"

#include "libavformat/avformat.h"

struct Encoder {
    /* predicted pts of the next frame to be encoded */
    int64_t next_pts;

    AVFrame *last_frame;
    /* number of frames emitted by the video-encoding sync code */
    int64_t vsync_frame_number;
    /* history of nb_frames_prev, i.e. the number of times the
     * previous frame was duplicated by vsync code in recent
     * do_video_out() calls */
    int64_t frames_prev_hist[3];

    AVFrame *sq_frame;

    // combined size of all the packets received from the encoder
    uint64_t data_size;
};

static uint64_t dup_warning = 1000;

void enc_free(Encoder **penc)
{
    Encoder *enc = *penc;

    if (!enc)
        return;

    av_frame_free(&enc->last_frame);
    av_frame_free(&enc->sq_frame);

    av_freep(penc);
}

int enc_alloc(Encoder **penc, const AVCodec *codec)
{
    Encoder *enc;

    *penc = NULL;

    enc = av_mallocz(sizeof(*enc));
    if (!enc)
        return AVERROR(ENOMEM);

    if (codec->type == AVMEDIA_TYPE_VIDEO) {
        enc->last_frame = av_frame_alloc();
        if (!enc->last_frame)
            goto fail;
    }

    *penc = enc;

    return 0;
fail:
    enc_free(&enc);
    return AVERROR(ENOMEM);
}

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
    Encoder              *e = ost->enc;
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
        av_assert0(0);
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

    if (ost->sq_idx_encode >= 0) {
        e->sq_frame = av_frame_alloc();
        if (!e->sq_frame)
            return AVERROR(ENOMEM);
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

    ret = avcodec_parameters_from_context(ost->par_in, ost->enc_ctx);
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

static int check_recording_time(OutputStream *ost, int64_t ts, AVRational tb)
{
    OutputFile *of = output_files[ost->file_index];

    if (of->recording_time != INT64_MAX &&
        av_compare_ts(ts, tb, of->recording_time, AV_TIME_BASE_Q) >= 0) {
        close_output_stream(ost);
        return 0;
    }
    return 1;
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
    if (ost->finished ||
        (of->start_time != AV_NOPTS_VALUE && sub->pts < of->start_time))
        return;

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

void enc_stats_write(OutputStream *ost, EncStats *es,
                     const AVFrame *frame, const AVPacket *pkt,
                     uint64_t frame_num)
{
    Encoder      *e = ost->enc;
    AVIOContext *io = es->io;
    AVRational   tb = frame ? frame->time_base : pkt->time_base;
    int64_t     pts = frame ? frame->pts : pkt->pts;

    AVRational  tbi = (AVRational){ 0, 1};
    int64_t    ptsi = INT64_MAX;

    const FrameData *fd;

    if ((frame && frame->opaque_ref) || (pkt && pkt->opaque_ref)) {
        fd   = (const FrameData*)(frame ? frame->opaque_ref->data : pkt->opaque_ref->data);
        tbi  = fd->tb;
        ptsi = fd->pts;
    }

    for (size_t i = 0; i < es->nb_components; i++) {
        const EncStatsComponent *c = &es->components[i];

        switch (c->type) {
        case ENC_STATS_LITERAL:         avio_write (io, c->str,     c->str_len);                    continue;
        case ENC_STATS_FILE_IDX:        avio_printf(io, "%d",       ost->file_index);               continue;
        case ENC_STATS_STREAM_IDX:      avio_printf(io, "%d",       ost->index);                    continue;
        case ENC_STATS_TIMEBASE:        avio_printf(io, "%d/%d",    tb.num, tb.den);                continue;
        case ENC_STATS_TIMEBASE_IN:     avio_printf(io, "%d/%d",    tbi.num, tbi.den);              continue;
        case ENC_STATS_PTS:             avio_printf(io, "%"PRId64,  pts);                           continue;
        case ENC_STATS_PTS_IN:          avio_printf(io, "%"PRId64,  ptsi);                          continue;
        case ENC_STATS_PTS_TIME:        avio_printf(io, "%g",       pts * av_q2d(tb));              continue;
        case ENC_STATS_PTS_TIME_IN:     avio_printf(io, "%g",       ptsi == INT64_MAX ?
                                                                    INFINITY : ptsi * av_q2d(tbi)); continue;
        case ENC_STATS_FRAME_NUM:       avio_printf(io, "%"PRIu64,  frame_num);                     continue;
        case ENC_STATS_FRAME_NUM_IN:    avio_printf(io, "%"PRIu64,  fd ? fd->idx : -1);             continue;
        }

        if (frame) {
            switch (c->type) {
            case ENC_STATS_SAMPLE_NUM:  avio_printf(io, "%"PRIu64,  ost->samples_encoded);          continue;
            case ENC_STATS_NB_SAMPLES:  avio_printf(io, "%d",       frame->nb_samples);             continue;
            default: av_assert0(0);
            }
        } else {
            switch (c->type) {
            case ENC_STATS_DTS:         avio_printf(io, "%"PRId64,  pkt->dts);                      continue;
            case ENC_STATS_DTS_TIME:    avio_printf(io, "%g",       pkt->dts * av_q2d(tb));         continue;
            case ENC_STATS_PKT_SIZE:    avio_printf(io, "%d",       pkt->size);                     continue;
            case ENC_STATS_BITRATE: {
                double duration = FFMAX(pkt->duration, 1) * av_q2d(tb);
                avio_printf(io, "%g",  8.0 * pkt->size / duration);
                continue;
            }
            case ENC_STATS_AVG_BITRATE: {
                double duration = pkt->dts * av_q2d(tb);
                avio_printf(io, "%g",  duration > 0 ? 8.0 * e->data_size / duration : -1.);
                continue;
            }
            default: av_assert0(0);
            }
        }
    }
    avio_w8(io, '\n');
    avio_flush(io);
}

static void update_video_stats(OutputStream *ost, const AVPacket *pkt, int write_vstats)
{
    Encoder        *e = ost->enc;
    const uint8_t *sd = av_packet_get_side_data(pkt, AV_PKT_DATA_QUALITY_STATS,
                                                NULL);
    AVCodecContext *enc = ost->enc_ctx;
    int64_t frame_number;
    double ti1, bitrate, avg_bitrate;

    ost->quality   = sd ? AV_RL32(sd) : -1;
    ost->pict_type = sd ? sd[4] : AV_PICTURE_TYPE_NONE;

    for (int i = 0; i<FF_ARRAY_ELEMS(ost->error); i++) {
        if (sd && i < sd[5])
            ost->error[i] = AV_RL64(sd + 8 + 8*i);
        else
            ost->error[i] = -1;
    }

    if (!write_vstats)
        return;

    /* this is executed just the first time update_video_stats is called */
    if (!vstats_file) {
        vstats_file = fopen(vstats_filename, "w");
        if (!vstats_file) {
            perror("fopen");
            exit_program(1);
        }
    }

    frame_number = ost->packets_encoded;
    if (vstats_version <= 1) {
        fprintf(vstats_file, "frame= %5"PRId64" q= %2.1f ", frame_number,
                ost->quality / (float)FF_QP2LAMBDA);
    } else  {
        fprintf(vstats_file, "out= %2d st= %2d frame= %5"PRId64" q= %2.1f ", ost->file_index, ost->index, frame_number,
                ost->quality / (float)FF_QP2LAMBDA);
    }

    if (ost->error[0]>=0 && (enc->flags & AV_CODEC_FLAG_PSNR))
        fprintf(vstats_file, "PSNR= %6.2f ", psnr(ost->error[0] / (enc->width * enc->height * 255.0 * 255.0)));

    fprintf(vstats_file,"f_size= %6d ", pkt->size);
    /* compute pts value */
    ti1 = pkt->dts * av_q2d(pkt->time_base);
    if (ti1 < 0.01)
        ti1 = 0.01;

    bitrate     = (pkt->size * 8) / av_q2d(enc->time_base) / 1000.0;
    avg_bitrate = (double)(e->data_size * 8) / ti1 / 1000.0;
    fprintf(vstats_file, "s_size= %8.0fkB time= %0.3f br= %7.1fkbits/s avg_br= %7.1fkbits/s ",
           (double)e->data_size / 1024, ti1, bitrate, avg_bitrate);
    fprintf(vstats_file, "type= %c\n", av_get_picture_type_char(ost->pict_type));
}

static int encode_frame(OutputFile *of, OutputStream *ost, AVFrame *frame)
{
    Encoder            *e = ost->enc;
    AVCodecContext   *enc = ost->enc_ctx;
    AVPacket         *pkt = ost->pkt;
    const char *type_desc = av_get_media_type_string(enc->codec_type);
    const char    *action = frame ? "encode" : "flush";
    int ret;

    if (frame) {
        if (ost->enc_stats_pre.io)
            enc_stats_write(ost, &ost->enc_stats_pre, frame, NULL,
                            ost->frames_encoded);

        ost->frames_encoded++;
        ost->samples_encoded += frame->nb_samples;

        if (debug_ts) {
            av_log(ost, AV_LOG_INFO, "encoder <- type:%s "
                   "frame_pts:%s frame_pts_time:%s time_base:%d/%d\n",
                   type_desc,
                   av_ts2str(frame->pts), av_ts2timestr(frame->pts, &enc->time_base),
                   enc->time_base.num, enc->time_base.den);
        }

        if (frame->sample_aspect_ratio.num && !ost->frame_aspect_ratio.num)
            enc->sample_aspect_ratio = frame->sample_aspect_ratio;
    }

    update_benchmark(NULL);

    ret = avcodec_send_frame(enc, frame);
    if (ret < 0 && !(ret == AVERROR_EOF && !frame)) {
        av_log(ost, AV_LOG_ERROR, "Error submitting %s frame to the encoder\n",
               type_desc);
        return ret;
    }

    while (1) {
        ret = avcodec_receive_packet(enc, pkt);
        update_benchmark("%s_%s %d.%d", action, type_desc,
                         ost->file_index, ost->index);

        pkt->time_base = enc->time_base;

        /* if two pass, output log on success and EOF */
        if ((ret >= 0 || ret == AVERROR_EOF) && ost->logfile && enc->stats_out)
            fprintf(ost->logfile, "%s", enc->stats_out);

        if (ret == AVERROR(EAGAIN)) {
            av_assert0(frame); // should never happen during flushing
            return 0;
        } else if (ret == AVERROR_EOF) {
            of_output_packet(of, pkt, ost, 1);
            return ret;
        } else if (ret < 0) {
            av_log(ost, AV_LOG_ERROR, "%s encoding failed\n", type_desc);
            return ret;
        }

        if (enc->codec_type == AVMEDIA_TYPE_VIDEO)
            update_video_stats(ost, pkt, !!vstats_filename);
        if (ost->enc_stats_post.io)
            enc_stats_write(ost, &ost->enc_stats_post, NULL, pkt,
                            ost->packets_encoded);

        if (debug_ts) {
            av_log(ost, AV_LOG_INFO, "encoder -> type:%s "
                   "pkt_pts:%s pkt_pts_time:%s pkt_dts:%s pkt_dts_time:%s "
                   "duration:%s duration_time:%s\n",
                   type_desc,
                   av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, &enc->time_base),
                   av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, &enc->time_base),
                   av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, &enc->time_base));
        }

        av_packet_rescale_ts(pkt, pkt->time_base, ost->mux_timebase);
        pkt->time_base = ost->mux_timebase;

        if (debug_ts) {
            av_log(ost, AV_LOG_INFO, "encoder -> type:%s "
                   "pkt_pts:%s pkt_pts_time:%s pkt_dts:%s pkt_dts_time:%s "
                   "duration:%s duration_time:%s\n",
                   type_desc,
                   av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, &enc->time_base),
                   av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, &enc->time_base),
                   av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, &enc->time_base));
        }

        if ((ret = trigger_fix_sub_duration_heartbeat(ost, pkt)) < 0) {
            av_log(NULL, AV_LOG_ERROR,
                   "Subtitle heartbeat logic failed in %s! (%s)\n",
                   __func__, av_err2str(ret));
            exit_program(1);
        }

        e->data_size += pkt->size;

        ost->packets_encoded++;

        of_output_packet(of, pkt, ost, 0);
    }

    av_assert0(0);
}

static int submit_encode_frame(OutputFile *of, OutputStream *ost,
                               AVFrame *frame)
{
    Encoder *e = ost->enc;
    int ret;

    if (ost->sq_idx_encode < 0)
        return encode_frame(of, ost, frame);

    if (frame) {
        ret = av_frame_ref(e->sq_frame, frame);
        if (ret < 0)
            return ret;
        frame = e->sq_frame;
    }

    ret = sq_send(of->sq_encode, ost->sq_idx_encode,
                  SQFRAME(frame));
    if (ret < 0) {
        if (frame)
            av_frame_unref(frame);
        if (ret != AVERROR_EOF)
            return ret;
    }

    while (1) {
        AVFrame *enc_frame = e->sq_frame;

        ret = sq_receive(of->sq_encode, ost->sq_idx_encode,
                               SQFRAME(enc_frame));
        if (ret == AVERROR_EOF) {
            enc_frame = NULL;
        } else if (ret < 0) {
            return (ret == AVERROR(EAGAIN)) ? 0 : ret;
        }

        ret = encode_frame(of, ost, enc_frame);
        if (enc_frame)
            av_frame_unref(enc_frame);
        if (ret < 0) {
            if (ret == AVERROR_EOF)
                close_output_stream(ost);
            return ret;
        }
    }
}

static void do_audio_out(OutputFile *of, OutputStream *ost,
                         AVFrame *frame)
{
    Encoder          *e = ost->enc;
    AVCodecContext *enc = ost->enc_ctx;
    int ret;

    if (!(enc->codec->capabilities & AV_CODEC_CAP_PARAM_CHANGE) &&
        enc->ch_layout.nb_channels != frame->ch_layout.nb_channels) {
        av_log(ost, AV_LOG_ERROR,
               "Audio channel count changed and encoder does not support parameter changes\n");
        return;
    }

    if (frame->pts == AV_NOPTS_VALUE)
        frame->pts = e->next_pts;
    else {
        int64_t start_time = (of->start_time == AV_NOPTS_VALUE) ? 0 : of->start_time;
        frame->pts =
            av_rescale_q(frame->pts, frame->time_base, enc->time_base) -
            av_rescale_q(start_time, AV_TIME_BASE_Q,   enc->time_base);
    }
    frame->time_base = enc->time_base;

    if (!check_recording_time(ost, frame->pts, frame->time_base))
        return;

    e->next_pts = frame->pts + frame->nb_samples;

    ret = submit_encode_frame(of, ost, frame);
    if (ret < 0 && ret != AVERROR_EOF)
        exit_program(1);
}

static double adjust_frame_pts_to_encoder_tb(OutputFile *of, OutputStream *ost,
                                             AVFrame *frame)
{
    double float_pts = AV_NOPTS_VALUE; // this is identical to frame.pts but with higher precision
    const int64_t start_time = (of->start_time == AV_NOPTS_VALUE) ?
                               0 : of->start_time;

    AVCodecContext *const enc = ost->enc_ctx;

    AVRational        tb = enc->time_base;
    AVRational filter_tb = frame->time_base;
    const int extra_bits = av_clip(29 - av_log2(tb.den), 0, 16);

    if (frame->pts == AV_NOPTS_VALUE)
        goto early_exit;

    tb.den <<= extra_bits;
    float_pts = av_rescale_q(frame->pts, filter_tb, tb) -
                av_rescale_q(start_time, AV_TIME_BASE_Q, tb);
    float_pts /= 1 << extra_bits;
    // avoid exact midoints to reduce the chance of rounding differences, this
    // can be removed in case the fps code is changed to work with integers
    float_pts += FFSIGN(float_pts) * 1.0 / (1<<17);

    frame->pts = av_rescale_q(frame->pts, filter_tb, enc->time_base) -
                 av_rescale_q(start_time, AV_TIME_BASE_Q, enc->time_base);
    frame->time_base = enc->time_base;

early_exit:

    if (debug_ts) {
        av_log(NULL, AV_LOG_INFO, "filter -> pts:%s pts_time:%s exact:%f time_base:%d/%d\n",
               frame ? av_ts2str(frame->pts) : "NULL",
               (enc && frame) ? av_ts2timestr(frame->pts, &enc->time_base) : "NULL",
               float_pts,
               enc ? enc->time_base.num : -1,
               enc ? enc->time_base.den : -1);
    }

    return float_pts;
}

/* Convert frame timestamps to the encoder timebase and decide how many times
 * should this (and possibly previous) frame be repeated in order to conform to
 * desired target framerate (if any).
 */
static void video_sync_process(OutputFile *of, OutputStream *ost,
                               AVFrame *next_picture, double duration,
                               int64_t *nb_frames, int64_t *nb_frames_prev)
{
    Encoder *e = ost->enc;
    double delta0, delta;

    double sync_ipts = adjust_frame_pts_to_encoder_tb(of, ost, next_picture);
    /* delta0 is the "drift" between the input frame (next_picture) and
     * where it would fall in the output. */
    delta0 = sync_ipts - e->next_pts;
    delta  = delta0 + duration;

    // tracks the number of times the PREVIOUS frame should be duplicated,
    // mostly for variable framerate (VFR)
    *nb_frames_prev = 0;
    /* by default, we output a single frame */
    *nb_frames = 1;

    if (delta0 < 0 &&
        delta > 0 &&
        ost->vsync_method != VSYNC_PASSTHROUGH &&
        ost->vsync_method != VSYNC_DROP) {
        if (delta0 < -0.6) {
            av_log(ost, AV_LOG_VERBOSE, "Past duration %f too large\n", -delta0);
        } else
            av_log(ost, AV_LOG_DEBUG, "Clipping frame in rate conversion by %f\n", -delta0);
        sync_ipts = e->next_pts;
        duration += delta0;
        delta0 = 0;
    }

    switch (ost->vsync_method) {
    case VSYNC_VSCFR:
        if (e->vsync_frame_number == 0 && delta0 >= 0.5) {
            av_log(ost, AV_LOG_DEBUG, "Not duplicating %d initial frames\n", (int)lrintf(delta0));
            delta = duration;
            delta0 = 0;
            e->next_pts = llrint(sync_ipts);
        }
    case VSYNC_CFR:
        // FIXME set to 0.5 after we fix some dts/pts bugs like in avidec.c
        if (frame_drop_threshold && delta < frame_drop_threshold && e->vsync_frame_number) {
            *nb_frames = 0;
        } else if (delta < -1.1)
            *nb_frames = 0;
        else if (delta > 1.1) {
            *nb_frames = llrintf(delta);
            if (delta0 > 1.1)
                *nb_frames_prev = llrintf(delta0 - 0.6);
        }
        next_picture->duration = 1;
        break;
    case VSYNC_VFR:
        if (delta <= -0.6)
            *nb_frames = 0;
        else if (delta > 0.6)
            e->next_pts = llrint(sync_ipts);
        next_picture->duration = duration;
        break;
    case VSYNC_DROP:
    case VSYNC_PASSTHROUGH:
        next_picture->duration = duration;
        e->next_pts = llrint(sync_ipts);
        break;
    default:
        av_assert0(0);
    }
}

static enum AVPictureType forced_kf_apply(void *logctx, KeyframeForceCtx *kf,
                                          AVRational tb, const AVFrame *in_picture,
                                          int dup_idx)
{
    double pts_time;

    if (kf->ref_pts == AV_NOPTS_VALUE)
        kf->ref_pts = in_picture->pts;

    pts_time = (in_picture->pts - kf->ref_pts) * av_q2d(tb);
    if (kf->index < kf->nb_pts &&
        av_compare_ts(in_picture->pts, tb, kf->pts[kf->index], AV_TIME_BASE_Q) >= 0) {
        kf->index++;
        goto force_keyframe;
    } else if (kf->pexpr) {
        double res;
        kf->expr_const_values[FKF_T] = pts_time;
        res = av_expr_eval(kf->pexpr,
                           kf->expr_const_values, NULL);
        av_log(logctx, AV_LOG_TRACE,
               "force_key_frame: n:%f n_forced:%f prev_forced_n:%f t:%f prev_forced_t:%f -> res:%f\n",
               kf->expr_const_values[FKF_N],
               kf->expr_const_values[FKF_N_FORCED],
               kf->expr_const_values[FKF_PREV_FORCED_N],
               kf->expr_const_values[FKF_T],
               kf->expr_const_values[FKF_PREV_FORCED_T],
               res);

        kf->expr_const_values[FKF_N] += 1;

        if (res) {
            kf->expr_const_values[FKF_PREV_FORCED_N] = kf->expr_const_values[FKF_N] - 1;
            kf->expr_const_values[FKF_PREV_FORCED_T] = kf->expr_const_values[FKF_T];
            kf->expr_const_values[FKF_N_FORCED]     += 1;
            goto force_keyframe;
        }
    } else if (kf->type == KF_FORCE_SOURCE &&
               in_picture->key_frame == 1 && !dup_idx) {
            goto force_keyframe;
    } else if (kf->type == KF_FORCE_SOURCE_NO_DROP && !dup_idx) {
        kf->dropped_keyframe = 0;
        if ((in_picture->key_frame == 1) || kf->dropped_keyframe)
            goto force_keyframe;
    }

    return AV_PICTURE_TYPE_NONE;

force_keyframe:
    av_log(logctx, AV_LOG_DEBUG, "Forced keyframe at time %f\n", pts_time);
    return AV_PICTURE_TYPE_I;
}

/* May modify/reset next_picture */
static void do_video_out(OutputFile *of,
                         OutputStream *ost,
                         AVFrame *next_picture)
{
    int ret;
    Encoder *e = ost->enc;
    AVCodecContext *enc = ost->enc_ctx;
    AVRational frame_rate;
    int64_t nb_frames, nb_frames_prev, i;
    double duration = 0;
    InputStream *ist = ost->ist;
    AVFilterContext *filter = ost->filter->filter;

    frame_rate = av_buffersink_get_frame_rate(filter);
    if (frame_rate.num > 0 && frame_rate.den > 0)
        duration = 1/(av_q2d(frame_rate) * av_q2d(enc->time_base));

    if(ist && ist->st->start_time != AV_NOPTS_VALUE && ist->first_dts != AV_NOPTS_VALUE && ost->frame_rate.num)
        duration = FFMIN(duration, 1/(av_q2d(ost->frame_rate) * av_q2d(enc->time_base)));

    if (!ost->filters_script &&
        !ost->filters &&
        (nb_filtergraphs == 0 || !filtergraphs[0]->graph_desc) &&
        next_picture &&
        ist &&
        lrintf(next_picture->duration * av_q2d(ist->st->time_base) / av_q2d(enc->time_base)) > 0) {
        duration = lrintf(next_picture->duration * av_q2d(ist->st->time_base) / av_q2d(enc->time_base));
    }

    if (!next_picture) {
        //end, flushing
        nb_frames_prev = nb_frames = mid_pred(e->frames_prev_hist[0],
                                              e->frames_prev_hist[1],
                                              e->frames_prev_hist[2]);
    } else {
        video_sync_process(of, ost, next_picture, duration,
                           &nb_frames, &nb_frames_prev);
    }

    memmove(e->frames_prev_hist + 1,
            e->frames_prev_hist,
            sizeof(e->frames_prev_hist[0]) * (FF_ARRAY_ELEMS(e->frames_prev_hist) - 1));
    e->frames_prev_hist[0] = nb_frames_prev;

    if (nb_frames_prev == 0 && ost->last_dropped) {
        nb_frames_drop++;
        av_log(ost, AV_LOG_VERBOSE,
               "*** dropping frame %"PRId64" at ts %"PRId64"\n",
               e->vsync_frame_number, e->last_frame->pts);
    }
    if (nb_frames > (nb_frames_prev && ost->last_dropped) + (nb_frames > nb_frames_prev)) {
        if (nb_frames > dts_error_threshold * 30) {
            av_log(ost, AV_LOG_ERROR, "%"PRId64" frame duplication too large, skipping\n", nb_frames - 1);
            nb_frames_drop++;
            return;
        }
        nb_frames_dup += nb_frames - (nb_frames_prev && ost->last_dropped) - (nb_frames > nb_frames_prev);
        av_log(ost, AV_LOG_VERBOSE, "*** %"PRId64" dup!\n", nb_frames - 1);
        if (nb_frames_dup > dup_warning) {
            av_log(ost, AV_LOG_WARNING, "More than %"PRIu64" frames duplicated\n", dup_warning);
            dup_warning *= 10;
        }
    }
    ost->last_dropped = nb_frames == nb_frames_prev && next_picture;
    ost->kf.dropped_keyframe = ost->last_dropped && next_picture && next_picture->key_frame;

    /* duplicates frame if needed */
    for (i = 0; i < nb_frames; i++) {
        AVFrame *in_picture;

        if (i < nb_frames_prev && e->last_frame->buf[0]) {
            in_picture = e->last_frame;
        } else
            in_picture = next_picture;

        if (!in_picture)
            return;

        in_picture->pts = e->next_pts;

        if (!check_recording_time(ost, in_picture->pts, ost->enc_ctx->time_base))
            return;

        in_picture->quality = enc->global_quality;
        in_picture->pict_type = forced_kf_apply(ost, &ost->kf, enc->time_base, in_picture, i);

        ret = submit_encode_frame(of, ost, in_picture);
        if (ret == AVERROR_EOF)
            break;
        else if (ret < 0)
            exit_program(1);

        e->next_pts++;
        e->vsync_frame_number++;
    }

    av_frame_unref(e->last_frame);
    if (next_picture)
        av_frame_move_ref(e->last_frame, next_picture);
}

void enc_frame(OutputStream *ost, AVFrame *frame)
{
    OutputFile *of = output_files[ost->file_index];
    int ret;

    ret = enc_open(ost, frame);
    if (ret < 0)
        exit_program(1);

    if (ost->enc_ctx->codec_type == AVMEDIA_TYPE_VIDEO) do_video_out(of, ost, frame);
    else                                                do_audio_out(of, ost, frame);
}

void enc_flush(void)
{
    int ret;

    for (OutputStream *ost = ost_iter(NULL); ost; ost = ost_iter(ost)) {
        OutputFile      *of = output_files[ost->file_index];
        if (ost->sq_idx_encode >= 0)
            sq_send(of->sq_encode, ost->sq_idx_encode, SQFRAME(NULL));
    }

    for (OutputStream *ost = ost_iter(NULL); ost; ost = ost_iter(ost)) {
        AVCodecContext *enc = ost->enc_ctx;
        OutputFile      *of = output_files[ost->file_index];

        if (!enc)
            continue;

        // Try to enable encoding with no input frames.
        // Maybe we should just let encoding fail instead.
        if (!ost->initialized) {
            FilterGraph *fg = ost->filter->graph;

            av_log(ost, AV_LOG_WARNING,
                   "Finishing stream without any data written to it.\n");

            if (ost->filter && !fg->graph) {
                int x;
                for (x = 0; x < fg->nb_inputs; x++) {
                    InputFilter *ifilter = fg->inputs[x];
                    if (ifilter->format < 0 &&
                        ifilter_parameters_from_codecpar(ifilter, ifilter->ist->par) < 0) {
                        av_log(ost, AV_LOG_ERROR, "Error copying paramerets from input stream\n");
                        exit_program(1);
                    }
                }

                if (!ifilter_has_all_input_formats(fg))
                    continue;

                ret = configure_filtergraph(fg);
                if (ret < 0) {
                    av_log(ost, AV_LOG_ERROR, "Error configuring filter graph\n");
                    exit_program(1);
                }

                of_output_packet(of, ost->pkt, ost, 1);
            }

            ret = enc_open(ost, NULL);
            if (ret < 0)
                exit_program(1);
        }

        if (enc->codec_type != AVMEDIA_TYPE_VIDEO && enc->codec_type != AVMEDIA_TYPE_AUDIO)
            continue;

        ret = submit_encode_frame(of, ost, NULL);
        if (ret != AVERROR_EOF)
            exit_program(1);
    }
}
