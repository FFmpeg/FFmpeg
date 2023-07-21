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

    // packet for receiving encoded output
    AVPacket *pkt;

    // combined size of all the packets received from the encoder
    uint64_t data_size;

    // number of packets received from the encoder
    uint64_t packets_encoded;

    uint64_t dup_warning;

    int opened;
};

void enc_free(Encoder **penc)
{
    Encoder *enc = *penc;

    if (!enc)
        return;

    av_frame_free(&enc->last_frame);
    av_frame_free(&enc->sq_frame);

    av_packet_free(&enc->pkt);

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

    enc->pkt = av_packet_alloc();
    if (!enc->pkt)
        goto fail;

    enc->dup_warning = 1000;

    *penc = enc;

    return 0;
fail:
    enc_free(&enc);
    return AVERROR(ENOMEM);
}

static int hw_device_setup_for_encode(OutputStream *ost, AVBufferRef *frames_ref)
{
    const AVCodecHWConfig *config;
    HWDevice *dev = NULL;
    int i;

    if (frames_ref &&
        ((AVHWFramesContext*)frames_ref->data)->format ==
        ost->enc_ctx->pix_fmt) {
        // Matching format, will try to use hw_frames_ctx.
    } else {
        frames_ref = NULL;
    }

    for (i = 0;; i++) {
        config = avcodec_get_hw_config(ost->enc_ctx->codec, i);
        if (!config)
            break;

        if (frames_ref &&
            config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_FRAMES_CTX &&
            (config->pix_fmt == AV_PIX_FMT_NONE ||
             config->pix_fmt == ost->enc_ctx->pix_fmt)) {
            av_log(ost->enc_ctx, AV_LOG_VERBOSE, "Using input "
                   "frames context (format %s) with %s encoder.\n",
                   av_get_pix_fmt_name(ost->enc_ctx->pix_fmt),
                   ost->enc_ctx->codec->name);
            ost->enc_ctx->hw_frames_ctx = av_buffer_ref(frames_ref);
            if (!ost->enc_ctx->hw_frames_ctx)
                return AVERROR(ENOMEM);
            return 0;
        }

        if (!dev &&
            config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)
            dev = hw_device_get_by_type(config->device_type);
    }

    if (dev) {
        av_log(ost->enc_ctx, AV_LOG_VERBOSE, "Using device %s "
               "(type %s) with %s encoder.\n", dev->name,
               av_hwdevice_get_type_name(dev->type), ost->enc_ctx->codec->name);
        ost->enc_ctx->hw_device_ctx = av_buffer_ref(dev->device_ref);
        if (!ost->enc_ctx->hw_device_ctx)
            return AVERROR(ENOMEM);
    } else {
        // No device required, or no device available.
    }
    return 0;
}

static int set_encoder_id(OutputFile *of, OutputStream *ost)
{
    const char *cname = ost->enc_ctx->codec->name;
    uint8_t *encoder_string;
    int encoder_string_len;

    if (av_dict_get(ost->st->metadata, "encoder",  NULL, 0))
        return 0;

    encoder_string_len = sizeof(LIBAVCODEC_IDENT) + strlen(cname) + 2;
    encoder_string     = av_mallocz(encoder_string_len);
    if (!encoder_string)
        return AVERROR(ENOMEM);

    if (!of->bitexact && !ost->bitexact)
        av_strlcpy(encoder_string, LIBAVCODEC_IDENT " ", encoder_string_len);
    else
        av_strlcpy(encoder_string, "Lavc ", encoder_string_len);
    av_strlcat(encoder_string, cname, encoder_string_len);
    av_dict_set(&ost->st->metadata, "encoder",  encoder_string,
                AV_DICT_DONT_STRDUP_VAL | AV_DICT_DONT_OVERWRITE);

    return 0;
}

static int enc_choose_timebase(OutputStream *ost, AVFrame *frame)
{
    const OutputFile *of = output_files[ost->file_index];
    AVCodecContext  *enc = ost->enc_ctx;
    AVRational        tb = (AVRational){ 0, 0 };
    AVRational fr;
    FrameData *fd;

    if (ost->type == AVMEDIA_TYPE_SUBTITLE) {
        if (ost->enc_timebase.num)
            av_log(ost, AV_LOG_WARNING,
                   "-enc_time_base not supported for subtitles, ignoring\n");
        enc->time_base = AV_TIME_BASE_Q;
        return 0;
    }

    fd = frame_data(frame);

    // apply -enc_time_base
    if (ost->enc_timebase.num == ENC_TIME_BASE_DEMUX &&
        (fd->dec.tb.num <= 0 || fd->dec.tb.den <= 0)) {
        av_log(ost, AV_LOG_ERROR,
               "Demuxing timebase not available - cannot use it for encoding\n");
        return AVERROR(EINVAL);
    }

    switch (ost->enc_timebase.num) {
    case 0:                                            break;
    case ENC_TIME_BASE_DEMUX:  tb = fd->dec.tb;        break;
    case ENC_TIME_BASE_FILTER: tb = frame->time_base;  break;
    default:                   tb = ost->enc_timebase; break;
    }

    if (ost->type == AVMEDIA_TYPE_AUDIO) {
        enc->time_base = tb.num ? tb : (AVRational){ 1, frame->sample_rate };
        return 0;
    }

    fr = ost->frame_rate;
    if (!fr.num)
        fr = fd->frame_rate_filter;

    if (ost->is_cfr) {
        if (!fr.num && !ost->max_frame_rate.num) {
            fr = (AVRational){25, 1};
            av_log(ost, AV_LOG_WARNING,
                   "No information "
                   "about the input framerate is available. Falling "
                   "back to a default value of 25fps. Use the -r option "
                   "if you want a different framerate.\n");
        }

        if (ost->max_frame_rate.num &&
            (av_q2d(fr) > av_q2d(ost->max_frame_rate) ||
            !fr.den))
            fr = ost->max_frame_rate;
    }

    if (fr.num > 0) {
        if (enc->codec->supported_framerates && !ost->force_fps) {
            int idx = av_find_nearest_q_idx(fr, enc->codec->supported_framerates);
            fr = enc->codec->supported_framerates[idx];
        }
        // reduce frame rate for mpeg4 to be within the spec limits
        if (enc->codec_id == AV_CODEC_ID_MPEG4) {
            av_reduce(&fr.num, &fr.den,
                      fr.num, fr.den, 65535);
        }
    }

    if (av_q2d(fr) > 1e3 && ost->vsync_method != VSYNC_PASSTHROUGH &&
        (ost->vsync_method == VSYNC_CFR || ost->vsync_method == VSYNC_VSCFR ||
        (ost->vsync_method == VSYNC_AUTO && !(of->format->flags & AVFMT_VARIABLE_FPS)))){
        av_log(ost, AV_LOG_WARNING, "Frame rate very high for a muxer not efficiently supporting it.\n"
                                    "Please consider specifying a lower framerate, a different muxer or "
                                    "setting vsync/fps_mode to vfr\n");
    }

    enc->framerate = fr;

    ost->st->avg_frame_rate = fr;

    if (!(tb.num > 0 && tb.den > 0))
        tb = av_inv_q(fr);
    if (!(tb.num > 0 && tb.den > 0))
        tb = frame->time_base;

    enc->time_base = tb;

    return 0;
}

int enc_open(OutputStream *ost, AVFrame *frame)
{
    InputStream *ist = ost->ist;
    Encoder              *e = ost->enc;
    AVCodecContext *enc_ctx = ost->enc_ctx;
    AVCodecContext *dec_ctx = NULL;
    const AVCodec      *enc = enc_ctx->codec;
    OutputFile      *of = output_files[ost->file_index];
    FrameData *fd;
    int ret;

    if (e->opened)
        return 0;

    // frame is always non-NULL for audio and video
    av_assert0(frame || (enc->type != AVMEDIA_TYPE_VIDEO && enc->type != AVMEDIA_TYPE_AUDIO));

    if (frame) {
        fd = frame_data(frame);
        if (!fd)
            return AVERROR(ENOMEM);
    }

    ret = set_encoder_id(output_files[ost->file_index], ost);
    if (ret < 0)
        return ret;

    if (ist) {
        dec_ctx = ist->dec_ctx;
    }

    ret = enc_choose_timebase(ost, frame);
    if (ret < 0) {
        av_log(ost, AV_LOG_ERROR, "Could not choose a time base for encoding\n");
        return AVERROR(EINVAL);
    }

    switch (enc_ctx->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        enc_ctx->sample_fmt     = frame->format;
        enc_ctx->sample_rate    = frame->sample_rate;
        ret = av_channel_layout_copy(&enc_ctx->ch_layout, &frame->ch_layout);
        if (ret < 0)
            return ret;

        if (ost->bits_per_raw_sample)
            enc_ctx->bits_per_raw_sample = ost->bits_per_raw_sample;
        else
            enc_ctx->bits_per_raw_sample = FFMIN(fd->bits_per_raw_sample,
                                                 av_get_bytes_per_sample(enc_ctx->sample_fmt) << 3);
        break;

    case AVMEDIA_TYPE_VIDEO: {
        enc_ctx->width  = frame->width;
        enc_ctx->height = frame->height;
        enc_ctx->sample_aspect_ratio = ost->st->sample_aspect_ratio =
            ost->frame_aspect_ratio.num ? // overridden by the -aspect cli option
            av_mul_q(ost->frame_aspect_ratio, (AVRational){ enc_ctx->height, enc_ctx->width }) :
            frame->sample_aspect_ratio;

        enc_ctx->pix_fmt = frame->format;

        if (ost->bits_per_raw_sample)
            enc_ctx->bits_per_raw_sample = ost->bits_per_raw_sample;
        else
            enc_ctx->bits_per_raw_sample = FFMIN(fd->bits_per_raw_sample,
                                                 av_pix_fmt_desc_get(enc_ctx->pix_fmt)->comp[0].depth);

        enc_ctx->color_range            = frame->color_range;
        enc_ctx->color_primaries        = frame->color_primaries;
        enc_ctx->color_trc              = frame->color_trc;
        enc_ctx->colorspace             = frame->colorspace;
        enc_ctx->chroma_sample_location = frame->chroma_location;

        if (enc_ctx->flags & (AV_CODEC_FLAG_INTERLACED_DCT | AV_CODEC_FLAG_INTERLACED_ME) ||
            (frame->flags & AV_FRAME_FLAG_INTERLACED)
#if FFMPEG_OPT_TOP
            || ost->top_field_first >= 0
#endif
            ) {
            int top_field_first =
#if FFMPEG_OPT_TOP
                ost->top_field_first >= 0 ?
                ost->top_field_first :
#endif
                !!(frame->flags & AV_FRAME_FLAG_TOP_FIELD_FIRST);

            if (enc->id == AV_CODEC_ID_MJPEG)
                enc_ctx->field_order = top_field_first ? AV_FIELD_TT : AV_FIELD_BB;
            else
                enc_ctx->field_order = top_field_first ? AV_FIELD_TB : AV_FIELD_BT;
        } else
            enc_ctx->field_order = AV_FIELD_PROGRESSIVE;

        break;
        }
    case AVMEDIA_TYPE_SUBTITLE:
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

    av_dict_set(&ost->encoder_opts, "flags", "+frame_duration", AV_DICT_MULTIKEY);

    ret = hw_device_setup_for_encode(ost, frame ? frame->hw_frames_ctx : NULL);
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

    e->opened = 1;

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

    ret = check_avoptions(ost->encoder_opts);
    if (ret < 0)
        return ret;

    if (ost->enc_ctx->bit_rate && ost->enc_ctx->bit_rate < 1000 &&
        ost->enc_ctx->codec_id != AV_CODEC_ID_CODEC2 /* don't complain about 700 bit/s modes */)
        av_log(ost, AV_LOG_WARNING, "The bitrate parameter is set too low."
                                    " It takes bits/s as argument, not kbits/s\n");

    ret = avcodec_parameters_from_context(ost->par_in, ost->enc_ctx);
    if (ret < 0) {
        av_log(ost, AV_LOG_FATAL,
               "Error initializing the output stream codec context.\n");
        return ret;
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

int enc_subtitle(OutputFile *of, OutputStream *ost, const AVSubtitle *sub)
{
    Encoder *e = ost->enc;
    int subtitle_out_max_size = 1024 * 1024;
    int subtitle_out_size, nb, i, ret;
    AVCodecContext *enc;
    AVPacket *pkt = e->pkt;
    int64_t pts;

    if (sub->pts == AV_NOPTS_VALUE) {
        av_log(ost, AV_LOG_ERROR, "Subtitle packets must have a pts\n");
        return exit_on_error ? AVERROR(EINVAL) : 0;
    }
    if (ost->finished ||
        (of->start_time != AV_NOPTS_VALUE && sub->pts < of->start_time))
        return 0;

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
            return 0;

        ret = av_new_packet(pkt, subtitle_out_max_size);
        if (ret < 0)
            return AVERROR(ENOMEM);

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
            return subtitle_out_size;
        }

        av_shrink_packet(pkt, subtitle_out_size);
        pkt->time_base = AV_TIME_BASE_Q;
        pkt->pts       = sub->pts;
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

        ret = of_output_packet(of, ost, pkt);
        if (ret < 0)
            return ret;
    }

    return 0;
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
        tbi  = fd->dec.tb;
        ptsi = fd->dec.pts;
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
        case ENC_STATS_FRAME_NUM_IN:    avio_printf(io, "%"PRIu64,  fd ? fd->dec.frame_num : -1);   continue;
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

static inline double psnr(double d)
{
    return -10.0 * log10(d);
}

static int update_video_stats(OutputStream *ost, const AVPacket *pkt, int write_vstats)
{
    Encoder        *e = ost->enc;
    const uint8_t *sd = av_packet_get_side_data(pkt, AV_PKT_DATA_QUALITY_STATS,
                                                NULL);
    AVCodecContext *enc = ost->enc_ctx;
    enum AVPictureType pict_type;
    int64_t frame_number;
    double ti1, bitrate, avg_bitrate;
    double psnr_val = -1;

    ost->quality   = sd ? AV_RL32(sd) : -1;
    pict_type      = sd ? sd[4] : AV_PICTURE_TYPE_NONE;

    if ((enc->flags & AV_CODEC_FLAG_PSNR) && sd && sd[5]) {
        // FIXME the scaling assumes 8bit
        double error = AV_RL64(sd + 8) / (enc->width * enc->height * 255.0 * 255.0);
        if (error >= 0 && error <= 1)
            psnr_val = psnr(error);
    }

    if (!write_vstats)
        return 0;

    /* this is executed just the first time update_video_stats is called */
    if (!vstats_file) {
        vstats_file = fopen(vstats_filename, "w");
        if (!vstats_file) {
            perror("fopen");
            return AVERROR(errno);
        }
    }

    frame_number = e->packets_encoded;
    if (vstats_version <= 1) {
        fprintf(vstats_file, "frame= %5"PRId64" q= %2.1f ", frame_number,
                ost->quality / (float)FF_QP2LAMBDA);
    } else  {
        fprintf(vstats_file, "out= %2d st= %2d frame= %5"PRId64" q= %2.1f ", ost->file_index, ost->index, frame_number,
                ost->quality / (float)FF_QP2LAMBDA);
    }

    if (psnr_val >= 0)
        fprintf(vstats_file, "PSNR= %6.2f ", psnr_val);

    fprintf(vstats_file,"f_size= %6d ", pkt->size);
    /* compute pts value */
    ti1 = pkt->dts * av_q2d(pkt->time_base);
    if (ti1 < 0.01)
        ti1 = 0.01;

    bitrate     = (pkt->size * 8) / av_q2d(enc->time_base) / 1000.0;
    avg_bitrate = (double)(e->data_size * 8) / ti1 / 1000.0;
    fprintf(vstats_file, "s_size= %8.0fkB time= %0.3f br= %7.1fkbits/s avg_br= %7.1fkbits/s ",
           (double)e->data_size / 1024, ti1, bitrate, avg_bitrate);
    fprintf(vstats_file, "type= %c\n", av_get_picture_type_char(pict_type));

    return 0;
}

static int encode_frame(OutputFile *of, OutputStream *ost, AVFrame *frame)
{
    Encoder            *e = ost->enc;
    AVCodecContext   *enc = ost->enc_ctx;
    AVPacket         *pkt = e->pkt;
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
        av_packet_unref(pkt);

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
            ret = of_output_packet(of, ost, NULL);
            return ret < 0 ? ret : AVERROR_EOF;
        } else if (ret < 0) {
            av_log(ost, AV_LOG_ERROR, "%s encoding failed\n", type_desc);
            return ret;
        }

        if (enc->codec_type == AVMEDIA_TYPE_VIDEO) {
            ret = update_video_stats(ost, pkt, !!vstats_filename);
            if (ret < 0)
                return ret;
        }

        if (ost->enc_stats_post.io)
            enc_stats_write(ost, &ost->enc_stats_post, NULL, pkt,
                            e->packets_encoded);

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
            return ret;
        }

        e->data_size += pkt->size;

        e->packets_encoded++;

        ret = of_output_packet(of, ost, pkt);
        if (ret < 0)
            return ret;
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

static int do_audio_out(OutputFile *of, OutputStream *ost,
                        AVFrame *frame)
{
    Encoder          *e = ost->enc;
    AVCodecContext *enc = ost->enc_ctx;
    int ret;

    if (!(enc->codec->capabilities & AV_CODEC_CAP_PARAM_CHANGE) &&
        enc->ch_layout.nb_channels != frame->ch_layout.nb_channels) {
        av_log(ost, AV_LOG_ERROR,
               "Audio channel count changed and encoder does not support parameter changes\n");
        return 0;
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
    frame->duration  = av_rescale_q(frame->nb_samples, (AVRational){1, frame->sample_rate},
                                    enc->time_base);

    if (!check_recording_time(ost, frame->pts, frame->time_base))
        return 0;

    e->next_pts = frame->pts + frame->nb_samples;

    ret = submit_encode_frame(of, ost, frame);
    return (ret < 0 && ret != AVERROR_EOF) ? ret : 0;
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
    // when float_pts is not exactly an integer,
    // avoid exact midpoints to reduce the chance of rounding differences, this
    // can be removed in case the fps code is changed to work with integers
    if (float_pts != llrint(float_pts))
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
                               AVFrame *frame, double duration,
                               int64_t *nb_frames, int64_t *nb_frames_prev)
{
    Encoder *e = ost->enc;
    double delta0, delta, sync_ipts;

    if (!frame) {
        *nb_frames_prev = *nb_frames = mid_pred(e->frames_prev_hist[0],
                                                e->frames_prev_hist[1],
                                                e->frames_prev_hist[2]);
        goto finish;
    }

    sync_ipts = adjust_frame_pts_to_encoder_tb(of, ost, frame);
    /* delta0 is the "drift" between the input frame and
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
        frame->duration = 1;
        break;
    case VSYNC_VFR:
        if (delta <= -0.6)
            *nb_frames = 0;
        else if (delta > 0.6)
            e->next_pts = llrint(sync_ipts);
        frame->duration = llrint(duration);
        break;
    case VSYNC_DROP:
    case VSYNC_PASSTHROUGH:
        frame->duration = llrint(duration);
        e->next_pts = llrint(sync_ipts);
        break;
    default:
        av_assert0(0);
    }

finish:
    memmove(e->frames_prev_hist + 1,
            e->frames_prev_hist,
            sizeof(e->frames_prev_hist[0]) * (FF_ARRAY_ELEMS(e->frames_prev_hist) - 1));
    e->frames_prev_hist[0] = *nb_frames_prev;
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
               (in_picture->flags & AV_FRAME_FLAG_KEY) && !dup_idx) {
            goto force_keyframe;
    } else if (kf->type == KF_FORCE_SOURCE_NO_DROP && !dup_idx) {
        kf->dropped_keyframe = 0;
        if ((in_picture->flags & AV_FRAME_FLAG_KEY) || kf->dropped_keyframe)
            goto force_keyframe;
    }

    return AV_PICTURE_TYPE_NONE;

force_keyframe:
    av_log(logctx, AV_LOG_DEBUG, "Forced keyframe at time %f\n", pts_time);
    return AV_PICTURE_TYPE_I;
}

/* May modify/reset frame */
static int do_video_out(OutputFile *of, OutputStream *ost, AVFrame *frame)
{
    int ret;
    Encoder *e = ost->enc;
    AVCodecContext *enc = ost->enc_ctx;
    int64_t nb_frames, nb_frames_prev, i;
    double duration = 0;

    if (frame)
        duration = frame->duration * av_q2d(frame->time_base) / av_q2d(enc->time_base);

    video_sync_process(of, ost, frame, duration,
                       &nb_frames, &nb_frames_prev);

    if (nb_frames_prev == 0 && ost->last_dropped) {
        ost->nb_frames_drop++;
        av_log(ost, AV_LOG_VERBOSE,
               "*** dropping frame %"PRId64" at ts %"PRId64"\n",
               e->vsync_frame_number, e->last_frame->pts);
    }
    if (nb_frames > (nb_frames_prev && ost->last_dropped) + (nb_frames > nb_frames_prev)) {
        if (nb_frames > dts_error_threshold * 30) {
            av_log(ost, AV_LOG_ERROR, "%"PRId64" frame duplication too large, skipping\n", nb_frames - 1);
            ost->nb_frames_drop++;
            return 0;
        }
        ost->nb_frames_dup += nb_frames - (nb_frames_prev && ost->last_dropped) - (nb_frames > nb_frames_prev);
        av_log(ost, AV_LOG_VERBOSE, "*** %"PRId64" dup!\n", nb_frames - 1);
        if (ost->nb_frames_dup > e->dup_warning) {
            av_log(ost, AV_LOG_WARNING, "More than %"PRIu64" frames duplicated\n", e->dup_warning);
            e->dup_warning *= 10;
        }
    }
    ost->last_dropped = nb_frames == nb_frames_prev && frame;
    ost->kf.dropped_keyframe = ost->last_dropped && frame && (frame->flags & AV_FRAME_FLAG_KEY);

    /* duplicates frame if needed */
    for (i = 0; i < nb_frames; i++) {
        AVFrame *in_picture;

        if (i < nb_frames_prev && e->last_frame->buf[0]) {
            in_picture = e->last_frame;
        } else
            in_picture = frame;

        if (!in_picture)
            return 0;

        in_picture->pts = e->next_pts;

        if (!check_recording_time(ost, in_picture->pts, ost->enc_ctx->time_base))
            return 0;

        in_picture->quality = enc->global_quality;
        in_picture->pict_type = forced_kf_apply(ost, &ost->kf, enc->time_base, in_picture, i);

#if FFMPEG_OPT_TOP
        if (ost->top_field_first >= 0) {
            in_picture->flags &= ~AV_FRAME_FLAG_TOP_FIELD_FIRST;
            in_picture->flags |= AV_FRAME_FLAG_TOP_FIELD_FIRST * (!!ost->top_field_first);
        }
#endif

        ret = submit_encode_frame(of, ost, in_picture);
        if (ret == AVERROR_EOF)
            break;
        else if (ret < 0)
            return ret;

        e->next_pts++;
        e->vsync_frame_number++;
    }

    av_frame_unref(e->last_frame);
    if (frame)
        av_frame_move_ref(e->last_frame, frame);

    return 0;
}

int enc_frame(OutputStream *ost, AVFrame *frame)
{
    OutputFile *of = output_files[ost->file_index];
    int ret;

    ret = enc_open(ost, frame);
    if (ret < 0)
        return ret;

    return ost->enc_ctx->codec_type == AVMEDIA_TYPE_VIDEO ?
           do_video_out(of, ost, frame) : do_audio_out(of, ost, frame);
}

int enc_flush(void)
{
    int ret;

    for (OutputStream *ost = ost_iter(NULL); ost; ost = ost_iter(ost)) {
        OutputFile      *of = output_files[ost->file_index];
        if (ost->sq_idx_encode >= 0)
            sq_send(of->sq_encode, ost->sq_idx_encode, SQFRAME(NULL));
    }

    for (OutputStream *ost = ost_iter(NULL); ost; ost = ost_iter(ost)) {
        Encoder          *e = ost->enc;
        AVCodecContext *enc = ost->enc_ctx;
        OutputFile      *of = output_files[ost->file_index];

        if (!enc || !e->opened ||
            (enc->codec_type != AVMEDIA_TYPE_VIDEO && enc->codec_type != AVMEDIA_TYPE_AUDIO))
            continue;

        ret = submit_encode_frame(of, ost, NULL);
        if (ret != AVERROR_EOF)
            return ret;
    }

    return 0;
}
