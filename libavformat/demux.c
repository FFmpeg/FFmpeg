/*
 * Core demuxing component
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard
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

#include "config.h"

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/dict.h"
#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/pixfmt.h"
#include "libavutil/time.h"
#include "libavutil/timestamp.h"

#include "libavcodec/bsf.h"
#include "libavcodec/internal.h"
#include "libavcodec/packet_internal.h"
#include "libavcodec/raw.h"

#include "avformat.h"
#include "avio_internal.h"
#include "id3v2.h"
#include "internal.h"
#include "url.h"

static int64_t wrap_timestamp(const AVStream *st, int64_t timestamp)
{
    const FFStream *const sti = cffstream(st);
    if (sti->pts_wrap_behavior != AV_PTS_WRAP_IGNORE && st->pts_wrap_bits < 64 &&
        sti->pts_wrap_reference != AV_NOPTS_VALUE && timestamp != AV_NOPTS_VALUE) {
        if (sti->pts_wrap_behavior == AV_PTS_WRAP_ADD_OFFSET &&
            timestamp < sti->pts_wrap_reference)
            return timestamp + (1ULL << st->pts_wrap_bits);
        else if (sti->pts_wrap_behavior == AV_PTS_WRAP_SUB_OFFSET &&
            timestamp >= sti->pts_wrap_reference)
            return timestamp - (1ULL << st->pts_wrap_bits);
    }
    return timestamp;
}

int64_t ff_wrap_timestamp(const AVStream *st, int64_t timestamp)
{
    return wrap_timestamp(st, timestamp);
}

static const AVCodec *find_probe_decoder(AVFormatContext *s, const AVStream *st, enum AVCodecID codec_id)
{
    const AVCodec *codec;

#if CONFIG_H264_DECODER
    /* Other parts of the code assume this decoder to be used for h264,
     * so force it if possible. */
    if (codec_id == AV_CODEC_ID_H264)
        return avcodec_find_decoder_by_name("h264");
#endif

    codec = ff_find_decoder(s, st, codec_id);
    if (!codec)
        return NULL;

    if (codec->capabilities & AV_CODEC_CAP_AVOID_PROBING) {
        const AVCodec *probe_codec = NULL;
        void *iter = NULL;
        while ((probe_codec = av_codec_iterate(&iter))) {
            if (probe_codec->id == codec->id &&
                    av_codec_is_decoder(probe_codec) &&
                    !(probe_codec->capabilities & (AV_CODEC_CAP_AVOID_PROBING | AV_CODEC_CAP_EXPERIMENTAL))) {
                return probe_codec;
            }
        }
    }

    return codec;
}

static int set_codec_from_probe_data(AVFormatContext *s, AVStream *st,
                                     AVProbeData *pd)
{
    static const struct {
        const char *name;
        enum AVCodecID id;
        enum AVMediaType type;
    } fmt_id_type[] = {
        { "aac",        AV_CODEC_ID_AAC,          AVMEDIA_TYPE_AUDIO    },
        { "ac3",        AV_CODEC_ID_AC3,          AVMEDIA_TYPE_AUDIO    },
        { "aptx",       AV_CODEC_ID_APTX,         AVMEDIA_TYPE_AUDIO    },
        { "dts",        AV_CODEC_ID_DTS,          AVMEDIA_TYPE_AUDIO    },
        { "dvbsub",     AV_CODEC_ID_DVB_SUBTITLE, AVMEDIA_TYPE_SUBTITLE },
        { "dvbtxt",     AV_CODEC_ID_DVB_TELETEXT, AVMEDIA_TYPE_SUBTITLE },
        { "eac3",       AV_CODEC_ID_EAC3,         AVMEDIA_TYPE_AUDIO    },
        { "h264",       AV_CODEC_ID_H264,         AVMEDIA_TYPE_VIDEO    },
        { "hevc",       AV_CODEC_ID_HEVC,         AVMEDIA_TYPE_VIDEO    },
        { "loas",       AV_CODEC_ID_AAC_LATM,     AVMEDIA_TYPE_AUDIO    },
        { "m4v",        AV_CODEC_ID_MPEG4,        AVMEDIA_TYPE_VIDEO    },
        { "mjpeg_2000", AV_CODEC_ID_JPEG2000,     AVMEDIA_TYPE_VIDEO    },
        { "mp3",        AV_CODEC_ID_MP3,          AVMEDIA_TYPE_AUDIO    },
        { "mpegvideo",  AV_CODEC_ID_MPEG2VIDEO,   AVMEDIA_TYPE_VIDEO    },
        { "truehd",     AV_CODEC_ID_TRUEHD,       AVMEDIA_TYPE_AUDIO    },
        { 0 }
    };
    int score;
    const AVInputFormat *fmt = av_probe_input_format3(pd, 1, &score);
    FFStream *const sti = ffstream(st);

    if (fmt) {
        av_log(s, AV_LOG_DEBUG,
               "Probe with size=%d, packets=%d detected %s with score=%d\n",
               pd->buf_size, s->max_probe_packets - sti->probe_packets,
               fmt->name, score);
        for (int i = 0; fmt_id_type[i].name; i++) {
            if (!strcmp(fmt->name, fmt_id_type[i].name)) {
                if (fmt_id_type[i].type != AVMEDIA_TYPE_AUDIO &&
                    st->codecpar->sample_rate)
                    continue;
                if (sti->request_probe > score &&
                    st->codecpar->codec_id != fmt_id_type[i].id)
                    continue;
                st->codecpar->codec_id   = fmt_id_type[i].id;
                st->codecpar->codec_type = fmt_id_type[i].type;
                sti->need_context_update = 1;
                return score;
            }
        }
    }
    return 0;
}

static int init_input(AVFormatContext *s, const char *filename,
                      AVDictionary **options)
{
    int ret;
    AVProbeData pd = { filename, NULL, 0 };
    int score = AVPROBE_SCORE_RETRY;

    if (s->pb) {
        s->flags |= AVFMT_FLAG_CUSTOM_IO;
        if (!s->iformat)
            return av_probe_input_buffer2(s->pb, &s->iformat, filename,
                                          s, 0, s->format_probesize);
        else if (s->iformat->flags & AVFMT_NOFILE)
            av_log(s, AV_LOG_WARNING, "Custom AVIOContext makes no sense and "
                                      "will be ignored with AVFMT_NOFILE format.\n");
        return 0;
    }

    if ((s->iformat && s->iformat->flags & AVFMT_NOFILE) ||
        (!s->iformat && (s->iformat = av_probe_input_format2(&pd, 0, &score))))
        return score;

    if ((ret = s->io_open(s, &s->pb, filename, AVIO_FLAG_READ | s->avio_flags, options)) < 0)
        return ret;

    if (s->iformat)
        return 0;
    return av_probe_input_buffer2(s->pb, &s->iformat, filename,
                                  s, 0, s->format_probesize);
}

static int update_stream_avctx(AVFormatContext *s)
{
    int ret;
    for (unsigned i = 0; i < s->nb_streams; i++) {
        AVStream *const st  = s->streams[i];
        FFStream *const sti = ffstream(st);

        if (!sti->need_context_update)
            continue;

        /* close parser, because it depends on the codec */
        if (sti->parser && sti->avctx->codec_id != st->codecpar->codec_id) {
            av_parser_close(sti->parser);
            sti->parser = NULL;
        }

        /* update internal codec context, for the parser */
        ret = avcodec_parameters_to_context(sti->avctx, st->codecpar);
        if (ret < 0)
            return ret;

        sti->need_context_update = 0;
    }
    return 0;
}

int avformat_open_input(AVFormatContext **ps, const char *filename,
                        const AVInputFormat *fmt, AVDictionary **options)
{
    AVFormatContext *s = *ps;
    FFFormatContext *si;
    AVDictionary *tmp = NULL;
    ID3v2ExtraMeta *id3v2_extra_meta = NULL;
    int ret = 0;

    if (!s && !(s = avformat_alloc_context()))
        return AVERROR(ENOMEM);
    si = ffformatcontext(s);
    if (!s->av_class) {
        av_log(NULL, AV_LOG_ERROR, "Input context has not been properly allocated by avformat_alloc_context() and is not NULL either\n");
        return AVERROR(EINVAL);
    }
    if (fmt)
        s->iformat = fmt;

    if (options)
        av_dict_copy(&tmp, *options, 0);

    if (s->pb) // must be before any goto fail
        s->flags |= AVFMT_FLAG_CUSTOM_IO;

    if ((ret = av_opt_set_dict(s, &tmp)) < 0)
        goto fail;

    if (!(s->url = av_strdup(filename ? filename : ""))) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    if ((ret = init_input(s, filename, &tmp)) < 0)
        goto fail;
    s->probe_score = ret;

    if (!s->protocol_whitelist && s->pb && s->pb->protocol_whitelist) {
        s->protocol_whitelist = av_strdup(s->pb->protocol_whitelist);
        if (!s->protocol_whitelist) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
    }

    if (!s->protocol_blacklist && s->pb && s->pb->protocol_blacklist) {
        s->protocol_blacklist = av_strdup(s->pb->protocol_blacklist);
        if (!s->protocol_blacklist) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
    }

    if (s->format_whitelist && av_match_list(s->iformat->name, s->format_whitelist, ',') <= 0) {
        av_log(s, AV_LOG_ERROR, "Format not on whitelist \'%s\'\n", s->format_whitelist);
        ret = AVERROR(EINVAL);
        goto fail;
    }

    avio_skip(s->pb, s->skip_initial_bytes);

    /* Check filename in case an image number is expected. */
    if (s->iformat->flags & AVFMT_NEEDNUMBER) {
        if (!av_filename_number_test(filename)) {
            ret = AVERROR(EINVAL);
            goto fail;
        }
    }

    s->duration = s->start_time = AV_NOPTS_VALUE;

    /* Allocate private data. */
    if (s->iformat->priv_data_size > 0) {
        if (!(s->priv_data = av_mallocz(s->iformat->priv_data_size))) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        if (s->iformat->priv_class) {
            *(const AVClass **) s->priv_data = s->iformat->priv_class;
            av_opt_set_defaults(s->priv_data);
            if ((ret = av_opt_set_dict(s->priv_data, &tmp)) < 0)
                goto fail;
        }
    }

    /* e.g. AVFMT_NOFILE formats will not have an AVIOContext */
    if (s->pb)
        ff_id3v2_read_dict(s->pb, &si->id3v2_meta, ID3v2_DEFAULT_MAGIC, &id3v2_extra_meta);

    if (s->iformat->read_header)
        if ((ret = s->iformat->read_header(s)) < 0) {
            if (s->iformat->flags_internal & FF_FMT_INIT_CLEANUP)
                goto close;
            goto fail;
        }

    if (!s->metadata) {
        s->metadata    = si->id3v2_meta;
        si->id3v2_meta = NULL;
    } else if (si->id3v2_meta) {
        av_log(s, AV_LOG_WARNING, "Discarding ID3 tags because more suitable tags were found.\n");
        av_dict_free(&si->id3v2_meta);
    }

    if (id3v2_extra_meta) {
        if (!strcmp(s->iformat->name, "mp3") || !strcmp(s->iformat->name, "aac") ||
            !strcmp(s->iformat->name, "tta") || !strcmp(s->iformat->name, "wav")) {
            if ((ret = ff_id3v2_parse_apic(s, id3v2_extra_meta)) < 0)
                goto close;
            if ((ret = ff_id3v2_parse_chapters(s, id3v2_extra_meta)) < 0)
                goto close;
            if ((ret = ff_id3v2_parse_priv(s, id3v2_extra_meta)) < 0)
                goto close;
        } else
            av_log(s, AV_LOG_DEBUG, "demuxer does not support additional id3 data, skipping\n");
        ff_id3v2_free_extra_meta(&id3v2_extra_meta);
    }

    if ((ret = avformat_queue_attached_pictures(s)) < 0)
        goto close;

    if (s->pb && !si->data_offset)
        si->data_offset = avio_tell(s->pb);

    si->raw_packet_buffer_size = 0;

    update_stream_avctx(s);

    if (options) {
        av_dict_free(options);
        *options = tmp;
    }
    *ps = s;
    return 0;

close:
    if (s->iformat->read_close)
        s->iformat->read_close(s);
fail:
    ff_id3v2_free_extra_meta(&id3v2_extra_meta);
    av_dict_free(&tmp);
    if (s->pb && !(s->flags & AVFMT_FLAG_CUSTOM_IO))
        avio_closep(&s->pb);
    avformat_free_context(s);
    *ps = NULL;
    return ret;
}

void avformat_close_input(AVFormatContext **ps)
{
    AVFormatContext *s;
    AVIOContext *pb;

    if (!ps || !*ps)
        return;

    s  = *ps;
    pb = s->pb;

    if ((s->iformat && strcmp(s->iformat->name, "image2") && s->iformat->flags & AVFMT_NOFILE) ||
        (s->flags & AVFMT_FLAG_CUSTOM_IO))
        pb = NULL;

    if (s->iformat)
        if (s->iformat->read_close)
            s->iformat->read_close(s);

    avformat_free_context(s);

    *ps = NULL;

    avio_close(pb);
}

static void force_codec_ids(AVFormatContext *s, AVStream *st)
{
    switch (st->codecpar->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
        if (s->video_codec_id)
            st->codecpar->codec_id = s->video_codec_id;
        break;
    case AVMEDIA_TYPE_AUDIO:
        if (s->audio_codec_id)
            st->codecpar->codec_id = s->audio_codec_id;
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        if (s->subtitle_codec_id)
            st->codecpar->codec_id = s->subtitle_codec_id;
        break;
    case AVMEDIA_TYPE_DATA:
        if (s->data_codec_id)
            st->codecpar->codec_id = s->data_codec_id;
        break;
    }
}

static int probe_codec(AVFormatContext *s, AVStream *st, const AVPacket *pkt)
{
    FFFormatContext *const si = ffformatcontext(s);
    FFStream *const sti = ffstream(st);

    if (sti->request_probe > 0) {
        AVProbeData *const pd = &sti->probe_data;
        int end;
        av_log(s, AV_LOG_DEBUG, "probing stream %d pp:%d\n", st->index, sti->probe_packets);
        --sti->probe_packets;

        if (pkt) {
            uint8_t *new_buf = av_realloc(pd->buf, pd->buf_size+pkt->size+AVPROBE_PADDING_SIZE);
            if (!new_buf) {
                av_log(s, AV_LOG_WARNING,
                       "Failed to reallocate probe buffer for stream %d\n",
                       st->index);
                goto no_packet;
            }
            pd->buf = new_buf;
            memcpy(pd->buf + pd->buf_size, pkt->data, pkt->size);
            pd->buf_size += pkt->size;
            memset(pd->buf + pd->buf_size, 0, AVPROBE_PADDING_SIZE);
        } else {
no_packet:
            sti->probe_packets = 0;
            if (!pd->buf_size) {
                av_log(s, AV_LOG_WARNING,
                       "nothing to probe for stream %d\n", st->index);
            }
        }

        end = si->raw_packet_buffer_size >= s->probesize
                || sti->probe_packets <= 0;

        if (end || av_log2(pd->buf_size) != av_log2(pd->buf_size - pkt->size)) {
            int score = set_codec_from_probe_data(s, st, pd);
            if (    (st->codecpar->codec_id != AV_CODEC_ID_NONE && score > AVPROBE_SCORE_STREAM_RETRY)
                || end) {
                pd->buf_size = 0;
                av_freep(&pd->buf);
                sti->request_probe = -1;
                if (st->codecpar->codec_id != AV_CODEC_ID_NONE) {
                    av_log(s, AV_LOG_DEBUG, "probed stream %d\n", st->index);
                } else
                    av_log(s, AV_LOG_WARNING, "probed stream %d failed\n", st->index);
            }
            force_codec_ids(s, st);
        }
    }
    return 0;
}

static int update_wrap_reference(AVFormatContext *s, AVStream *st, int stream_index, AVPacket *pkt)
{
    FFStream *const sti = ffstream(st);
    int64_t ref = pkt->dts;
    int pts_wrap_behavior;
    int64_t pts_wrap_reference;
    AVProgram *first_program;

    if (ref == AV_NOPTS_VALUE)
        ref = pkt->pts;
    if (sti->pts_wrap_reference != AV_NOPTS_VALUE || st->pts_wrap_bits >= 63 || ref == AV_NOPTS_VALUE || !s->correct_ts_overflow)
        return 0;
    ref &= (1LL << st->pts_wrap_bits)-1;

    // reference time stamp should be 60 s before first time stamp
    pts_wrap_reference = ref - av_rescale(60, st->time_base.den, st->time_base.num);
    // if first time stamp is not more than 1/8 and 60s before the wrap point, subtract rather than add wrap offset
    pts_wrap_behavior = (ref < (1LL << st->pts_wrap_bits) - (1LL << st->pts_wrap_bits-3)) ||
        (ref < (1LL << st->pts_wrap_bits) - av_rescale(60, st->time_base.den, st->time_base.num)) ?
        AV_PTS_WRAP_ADD_OFFSET : AV_PTS_WRAP_SUB_OFFSET;

    first_program = av_find_program_from_stream(s, NULL, stream_index);

    if (!first_program) {
        int default_stream_index = av_find_default_stream_index(s);
        FFStream *const default_sti = ffstream(s->streams[default_stream_index]);
        if (default_sti->pts_wrap_reference == AV_NOPTS_VALUE) {
            for (unsigned i = 0; i < s->nb_streams; i++) {
                FFStream *const sti = ffstream(s->streams[i]);
                if (av_find_program_from_stream(s, NULL, i))
                    continue;
                sti->pts_wrap_reference = pts_wrap_reference;
                sti->pts_wrap_behavior  = pts_wrap_behavior;
            }
        } else {
            sti->pts_wrap_reference = default_sti->pts_wrap_reference;
            sti->pts_wrap_behavior  = default_sti->pts_wrap_behavior;
        }
    } else {
        AVProgram *program = first_program;
        while (program) {
            if (program->pts_wrap_reference != AV_NOPTS_VALUE) {
                pts_wrap_reference = program->pts_wrap_reference;
                pts_wrap_behavior = program->pts_wrap_behavior;
                break;
            }
            program = av_find_program_from_stream(s, program, stream_index);
        }

        // update every program with differing pts_wrap_reference
        program = first_program;
        while (program) {
            if (program->pts_wrap_reference != pts_wrap_reference) {
                for (unsigned i = 0; i < program->nb_stream_indexes; i++) {
                    FFStream *const sti = ffstream(s->streams[program->stream_index[i]]);
                    sti->pts_wrap_reference = pts_wrap_reference;
                    sti->pts_wrap_behavior  = pts_wrap_behavior;
                }

                program->pts_wrap_reference = pts_wrap_reference;
                program->pts_wrap_behavior = pts_wrap_behavior;
            }
            program = av_find_program_from_stream(s, program, stream_index);
        }
    }
    return 1;
}

int ff_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    FFFormatContext *const si = ffformatcontext(s);
    int err;

#if FF_API_INIT_PACKET
FF_DISABLE_DEPRECATION_WARNINGS
    pkt->data = NULL;
    pkt->size = 0;
    av_init_packet(pkt);
FF_ENABLE_DEPRECATION_WARNINGS
#else
    av_packet_unref(pkt);
#endif

    for (;;) {
        PacketListEntry *pktl = si->raw_packet_buffer.head;
        AVStream *st;
        FFStream *sti;
        const AVPacket *pkt1;

        if (pktl) {
            AVStream *const st = s->streams[pktl->pkt.stream_index];
            if (si->raw_packet_buffer_size >= s->probesize)
                if ((err = probe_codec(s, st, NULL)) < 0)
                    return err;
            if (ffstream(st)->request_probe <= 0) {
                avpriv_packet_list_get(&si->raw_packet_buffer, pkt);
                si->raw_packet_buffer_size -= pkt->size;
                return 0;
            }
        }

        err = s->iformat->read_packet(s, pkt);
        if (err < 0) {
            av_packet_unref(pkt);

            /* Some demuxers return FFERROR_REDO when they consume
               data and discard it (ignored streams, junk, extradata).
               We must re-call the demuxer to get the real packet. */
            if (err == FFERROR_REDO)
                continue;
            if (!pktl || err == AVERROR(EAGAIN))
                return err;
            for (unsigned i = 0; i < s->nb_streams; i++) {
                AVStream *const st  = s->streams[i];
                FFStream *const sti = ffstream(st);
                if (sti->probe_packets || sti->request_probe > 0)
                    if ((err = probe_codec(s, st, NULL)) < 0)
                        return err;
                av_assert0(sti->request_probe <= 0);
            }
            continue;
        }

        err = av_packet_make_refcounted(pkt);
        if (err < 0) {
            av_packet_unref(pkt);
            return err;
        }

        if (pkt->flags & AV_PKT_FLAG_CORRUPT) {
            av_log(s, AV_LOG_WARNING,
                   "Packet corrupt (stream = %d, dts = %s)",
                   pkt->stream_index, av_ts2str(pkt->dts));
            if (s->flags & AVFMT_FLAG_DISCARD_CORRUPT) {
                av_log(s, AV_LOG_WARNING, ", dropping it.\n");
                av_packet_unref(pkt);
                continue;
            }
            av_log(s, AV_LOG_WARNING, ".\n");
        }

        av_assert0(pkt->stream_index < (unsigned)s->nb_streams &&
                   "Invalid stream index.\n");

        st  = s->streams[pkt->stream_index];
        sti = ffstream(st);

        if (update_wrap_reference(s, st, pkt->stream_index, pkt) && sti->pts_wrap_behavior == AV_PTS_WRAP_SUB_OFFSET) {
            // correct first time stamps to negative values
            if (!is_relative(sti->first_dts))
                sti->first_dts = wrap_timestamp(st, sti->first_dts);
            if (!is_relative(st->start_time))
                st->start_time = wrap_timestamp(st, st->start_time);
            if (!is_relative(sti->cur_dts))
                sti->cur_dts = wrap_timestamp(st, sti->cur_dts);
        }

        pkt->dts = wrap_timestamp(st, pkt->dts);
        pkt->pts = wrap_timestamp(st, pkt->pts);

        force_codec_ids(s, st);

        /* TODO: audio: time filter; video: frame reordering (pts != dts) */
        if (s->use_wallclock_as_timestamps)
            pkt->dts = pkt->pts = av_rescale_q(av_gettime(), AV_TIME_BASE_Q, st->time_base);

        if (!pktl && sti->request_probe <= 0)
            return 0;

        err = avpriv_packet_list_put(&si->raw_packet_buffer,
                                     pkt, NULL, 0);
        if (err < 0) {
            av_packet_unref(pkt);
            return err;
        }
        pkt1 = &si->raw_packet_buffer.tail->pkt;
        si->raw_packet_buffer_size += pkt1->size;

        if ((err = probe_codec(s, st, pkt1)) < 0)
            return err;
    }
}

/**
 * Return the frame duration in seconds. Return 0 if not available.
 */
static void compute_frame_duration(AVFormatContext *s, int *pnum, int *pden,
                                   AVStream *st, AVCodecParserContext *pc,
                                   AVPacket *pkt)
{
    FFStream *const sti = ffstream(st);
    AVRational codec_framerate = sti->avctx->framerate;
    int frame_size, sample_rate;

    *pnum = 0;
    *pden = 0;
    switch (st->codecpar->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
        if (st->r_frame_rate.num && (!pc || !codec_framerate.num)) {
            *pnum = st->r_frame_rate.den;
            *pden = st->r_frame_rate.num;
        } else if (st->time_base.num * 1000LL > st->time_base.den) {
            *pnum = st->time_base.num;
            *pden = st->time_base.den;
        } else if (codec_framerate.den * 1000LL > codec_framerate.num) {
            av_assert0(sti->avctx->ticks_per_frame);
            av_reduce(pnum, pden,
                      codec_framerate.den,
                      codec_framerate.num * (int64_t)sti->avctx->ticks_per_frame,
                      INT_MAX);

            if (pc && pc->repeat_pict) {
                av_reduce(pnum, pden,
                          (*pnum) * (1LL + pc->repeat_pict),
                          (*pden),
                          INT_MAX);
            }
            /* If this codec can be interlaced or progressive then we need
             * a parser to compute duration of a packet. Thus if we have
             * no parser in such case leave duration undefined. */
            if (sti->avctx->ticks_per_frame > 1 && !pc)
                *pnum = *pden = 0;
        }
        break;
    case AVMEDIA_TYPE_AUDIO:
        if (sti->avctx_inited) {
            frame_size  = av_get_audio_frame_duration(sti->avctx, pkt->size);
            sample_rate = sti->avctx->sample_rate;
        } else {
            frame_size  = av_get_audio_frame_duration2(st->codecpar, pkt->size);
            sample_rate = st->codecpar->sample_rate;
        }
        if (frame_size <= 0 || sample_rate <= 0)
            break;
        *pnum = frame_size;
        *pden = sample_rate;
        break;
    default:
        break;
    }
}

static int has_decode_delay_been_guessed(AVStream *st)
{
    FFStream *const sti = ffstream(st);
    if (st->codecpar->codec_id != AV_CODEC_ID_H264) return 1;
    if (!sti->info) // if we have left find_stream_info then nb_decoded_frames won't increase anymore for stream copy
        return 1;
#if CONFIG_H264_DECODER
    if (sti->avctx->has_b_frames &&
        avpriv_h264_has_num_reorder_frames(sti->avctx) == sti->avctx->has_b_frames)
        return 1;
#endif
    if (sti->avctx->has_b_frames < 3)
        return sti->nb_decoded_frames >= 7;
    else if (sti->avctx->has_b_frames < 4)
        return sti->nb_decoded_frames >= 18;
    else
        return sti->nb_decoded_frames >= 20;
}

static PacketListEntry *get_next_pkt(AVFormatContext *s, AVStream *st,
                                     PacketListEntry *pktl)
{
    FFFormatContext *const si = ffformatcontext(s);
    if (pktl->next)
        return pktl->next;
    if (pktl == si->packet_buffer.tail)
        return si->parse_queue.head;
    return NULL;
}

static int64_t select_from_pts_buffer(AVStream *st, int64_t *pts_buffer, int64_t dts)
{
    FFStream *const sti = ffstream(st);
    int onein_oneout = st->codecpar->codec_id != AV_CODEC_ID_H264 &&
                       st->codecpar->codec_id != AV_CODEC_ID_HEVC;

    if (!onein_oneout) {
        int delay = sti->avctx->has_b_frames;

        if (dts == AV_NOPTS_VALUE) {
            int64_t best_score = INT64_MAX;
            for (int i = 0; i < delay; i++) {
                if (sti->pts_reorder_error_count[i]) {
                    int64_t score = sti->pts_reorder_error[i] / sti->pts_reorder_error_count[i];
                    if (score < best_score) {
                        best_score = score;
                        dts = pts_buffer[i];
                    }
                }
            }
        } else {
            for (int i = 0; i < delay; i++) {
                if (pts_buffer[i] != AV_NOPTS_VALUE) {
                    int64_t diff = FFABS(pts_buffer[i] - dts)
                                   + (uint64_t)sti->pts_reorder_error[i];
                    diff = FFMAX(diff, sti->pts_reorder_error[i]);
                    sti->pts_reorder_error[i] = diff;
                    sti->pts_reorder_error_count[i]++;
                    if (sti->pts_reorder_error_count[i] > 250) {
                        sti->pts_reorder_error[i] >>= 1;
                        sti->pts_reorder_error_count[i] >>= 1;
                    }
                }
            }
        }
    }

    if (dts == AV_NOPTS_VALUE)
        dts = pts_buffer[0];

    return dts;
}

/**
 * Updates the dts of packets of a stream in pkt_buffer, by re-ordering the pts
 * of the packets in a window.
 */
static void update_dts_from_pts(AVFormatContext *s, int stream_index,
                                PacketListEntry *pkt_buffer)
{
    AVStream *const st = s->streams[stream_index];
    int delay = ffstream(st)->avctx->has_b_frames;

    int64_t pts_buffer[MAX_REORDER_DELAY+1];

    for (int i = 0; i < MAX_REORDER_DELAY + 1; i++)
        pts_buffer[i] = AV_NOPTS_VALUE;

    for (; pkt_buffer; pkt_buffer = get_next_pkt(s, st, pkt_buffer)) {
        if (pkt_buffer->pkt.stream_index != stream_index)
            continue;

        if (pkt_buffer->pkt.pts != AV_NOPTS_VALUE && delay <= MAX_REORDER_DELAY) {
            pts_buffer[0] = pkt_buffer->pkt.pts;
            for (int i = 0; i < delay && pts_buffer[i] > pts_buffer[i + 1]; i++)
                FFSWAP(int64_t, pts_buffer[i], pts_buffer[i + 1]);

            pkt_buffer->pkt.dts = select_from_pts_buffer(st, pts_buffer, pkt_buffer->pkt.dts);
        }
    }
}

static void update_initial_timestamps(AVFormatContext *s, int stream_index,
                                      int64_t dts, int64_t pts, AVPacket *pkt)
{
    FFFormatContext *const si = ffformatcontext(s);
    AVStream *const st  = s->streams[stream_index];
    FFStream *const sti = ffstream(st);
    PacketListEntry *pktl = si->packet_buffer.head ? si->packet_buffer.head : si->parse_queue.head;

    uint64_t shift;

    if (sti->first_dts != AV_NOPTS_VALUE ||
        dts           == AV_NOPTS_VALUE ||
        sti->cur_dts   == AV_NOPTS_VALUE ||
        sti->cur_dts < INT_MIN + RELATIVE_TS_BASE ||
        dts  < INT_MIN + (sti->cur_dts - RELATIVE_TS_BASE) ||
        is_relative(dts))
        return;

    sti->first_dts = dts - (sti->cur_dts - RELATIVE_TS_BASE);
    sti->cur_dts   = dts;
    shift          = (uint64_t)sti->first_dts - RELATIVE_TS_BASE;

    if (is_relative(pts))
        pts += shift;

    for (PacketListEntry *pktl_it = pktl; pktl_it; pktl_it = get_next_pkt(s, st, pktl_it)) {
        if (pktl_it->pkt.stream_index != stream_index)
            continue;
        if (is_relative(pktl_it->pkt.pts))
            pktl_it->pkt.pts += shift;

        if (is_relative(pktl_it->pkt.dts))
            pktl_it->pkt.dts += shift;

        if (st->start_time == AV_NOPTS_VALUE && pktl_it->pkt.pts != AV_NOPTS_VALUE) {
            st->start_time = pktl_it->pkt.pts;
            if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && st->codecpar->sample_rate)
                st->start_time = av_sat_add64(st->start_time, av_rescale_q(sti->skip_samples, (AVRational){1, st->codecpar->sample_rate}, st->time_base));
        }
    }

    if (has_decode_delay_been_guessed(st))
        update_dts_from_pts(s, stream_index, pktl);

    if (st->start_time == AV_NOPTS_VALUE) {
        if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO || !(pkt->flags & AV_PKT_FLAG_DISCARD)) {
            st->start_time = pts;
        }
        if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && st->codecpar->sample_rate)
            st->start_time = av_sat_add64(st->start_time, av_rescale_q(sti->skip_samples, (AVRational){1, st->codecpar->sample_rate}, st->time_base));
    }
}

static void update_initial_durations(AVFormatContext *s, AVStream *st,
                                     int stream_index, int64_t duration)
{
    FFFormatContext *const si = ffformatcontext(s);
    FFStream *const sti = ffstream(st);
    PacketListEntry *pktl = si->packet_buffer.head ? si->packet_buffer.head : si->parse_queue.head;
    int64_t cur_dts = RELATIVE_TS_BASE;

    if (sti->first_dts != AV_NOPTS_VALUE) {
        if (sti->update_initial_durations_done)
            return;
        sti->update_initial_durations_done = 1;
        cur_dts = sti->first_dts;
        for (; pktl; pktl = get_next_pkt(s, st, pktl)) {
            if (pktl->pkt.stream_index == stream_index) {
                if (pktl->pkt.pts != pktl->pkt.dts  ||
                    pktl->pkt.dts != AV_NOPTS_VALUE ||
                    pktl->pkt.duration)
                    break;
                cur_dts -= duration;
            }
        }
        if (pktl && pktl->pkt.dts != sti->first_dts) {
            av_log(s, AV_LOG_DEBUG, "first_dts %s not matching first dts %s (pts %s, duration %"PRId64") in the queue\n",
                   av_ts2str(sti->first_dts), av_ts2str(pktl->pkt.dts), av_ts2str(pktl->pkt.pts), pktl->pkt.duration);
            return;
        }
        if (!pktl) {
            av_log(s, AV_LOG_DEBUG, "first_dts %s but no packet with dts in the queue\n", av_ts2str(sti->first_dts));
            return;
        }
        pktl = si->packet_buffer.head ? si->packet_buffer.head : si->parse_queue.head;
        sti->first_dts = cur_dts;
    } else if (sti->cur_dts != RELATIVE_TS_BASE)
        return;

    for (; pktl; pktl = get_next_pkt(s, st, pktl)) {
        if (pktl->pkt.stream_index != stream_index)
            continue;
        if ((pktl->pkt.pts == pktl->pkt.dts ||
             pktl->pkt.pts == AV_NOPTS_VALUE) &&
            (pktl->pkt.dts == AV_NOPTS_VALUE ||
             pktl->pkt.dts == sti->first_dts ||
             pktl->pkt.dts == RELATIVE_TS_BASE) &&
            !pktl->pkt.duration &&
            av_sat_add64(cur_dts, duration) == cur_dts + (uint64_t)duration
        ) {
            pktl->pkt.dts = cur_dts;
            if (!sti->avctx->has_b_frames)
                pktl->pkt.pts = cur_dts;
            pktl->pkt.duration = duration;
        } else
            break;
        cur_dts = pktl->pkt.dts + pktl->pkt.duration;
    }
    if (!pktl)
        sti->cur_dts = cur_dts;
}

static void compute_pkt_fields(AVFormatContext *s, AVStream *st,
                               AVCodecParserContext *pc, AVPacket *pkt,
                               int64_t next_dts, int64_t next_pts)
{
    FFFormatContext *const si = ffformatcontext(s);
    FFStream *const sti = ffstream(st);
    int num, den, presentation_delayed, delay;
    int64_t offset;
    AVRational duration;
    int onein_oneout = st->codecpar->codec_id != AV_CODEC_ID_H264 &&
                       st->codecpar->codec_id != AV_CODEC_ID_HEVC;

    if (s->flags & AVFMT_FLAG_NOFILLIN)
        return;

    if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && pkt->dts != AV_NOPTS_VALUE) {
        if (pkt->dts == pkt->pts && sti->last_dts_for_order_check != AV_NOPTS_VALUE) {
            if (sti->last_dts_for_order_check <= pkt->dts) {
                sti->dts_ordered++;
            } else {
                av_log(s, sti->dts_misordered ? AV_LOG_DEBUG : AV_LOG_WARNING,
                       "DTS %"PRIi64" < %"PRIi64" out of order\n",
                       pkt->dts,
                       sti->last_dts_for_order_check);
                sti->dts_misordered++;
            }
            if (sti->dts_ordered + sti->dts_misordered > 250) {
                sti->dts_ordered    >>= 1;
                sti->dts_misordered >>= 1;
            }
        }

        sti->last_dts_for_order_check = pkt->dts;
        if (sti->dts_ordered < 8 * sti->dts_misordered && pkt->dts == pkt->pts)
            pkt->dts = AV_NOPTS_VALUE;
    }

    if ((s->flags & AVFMT_FLAG_IGNDTS) && pkt->pts != AV_NOPTS_VALUE)
        pkt->dts = AV_NOPTS_VALUE;

    if (pc && pc->pict_type == AV_PICTURE_TYPE_B
        && !sti->avctx->has_b_frames)
        //FIXME Set low_delay = 0 when has_b_frames = 1
        sti->avctx->has_b_frames = 1;

    /* do we have a video B-frame ? */
    delay = sti->avctx->has_b_frames;
    presentation_delayed = 0;

    /* XXX: need has_b_frame, but cannot get it if the codec is
     *  not initialized */
    if (delay &&
        pc && pc->pict_type != AV_PICTURE_TYPE_B)
        presentation_delayed = 1;

    if (pkt->pts != AV_NOPTS_VALUE && pkt->dts != AV_NOPTS_VALUE &&
        st->pts_wrap_bits < 63 && pkt->dts > INT64_MIN + (1LL << st->pts_wrap_bits) &&
        pkt->dts - (1LL << (st->pts_wrap_bits - 1)) > pkt->pts) {
        if (is_relative(sti->cur_dts) || pkt->dts - (1LL<<(st->pts_wrap_bits - 1)) > sti->cur_dts) {
            pkt->dts -= 1LL << st->pts_wrap_bits;
        } else
            pkt->pts += 1LL << st->pts_wrap_bits;
    }

    /* Some MPEG-2 in MPEG-PS lack dts (issue #171 / input_file.mpg).
     * We take the conservative approach and discard both.
     * Note: If this is misbehaving for an H.264 file, then possibly
     * presentation_delayed is not set correctly. */
    if (delay == 1 && pkt->dts == pkt->pts &&
        pkt->dts != AV_NOPTS_VALUE && presentation_delayed) {
        av_log(s, AV_LOG_DEBUG, "invalid dts/pts combination %"PRIi64"\n", pkt->dts);
        if (    strcmp(s->iformat->name, "mov,mp4,m4a,3gp,3g2,mj2")
             && strcmp(s->iformat->name, "flv")) // otherwise we discard correct timestamps for vc1-wmapro.ism
            pkt->dts = AV_NOPTS_VALUE;
    }

    duration = av_mul_q((AVRational) {pkt->duration, 1}, st->time_base);
    if (pkt->duration <= 0) {
        compute_frame_duration(s, &num, &den, st, pc, pkt);
        if (den && num) {
            duration = (AVRational) {num, den};
            pkt->duration = av_rescale_rnd(1,
                                           num * (int64_t) st->time_base.den,
                                           den * (int64_t) st->time_base.num,
                                           AV_ROUND_DOWN);
        }
    }

    if (pkt->duration > 0 && (si->packet_buffer.head || si->parse_queue.head))
        update_initial_durations(s, st, pkt->stream_index, pkt->duration);

    /* Correct timestamps with byte offset if demuxers only have timestamps
     * on packet boundaries */
    if (pc && sti->need_parsing == AVSTREAM_PARSE_TIMESTAMPS && pkt->size) {
        /* this will estimate bitrate based on this frame's duration and size */
        offset = av_rescale(pc->offset, pkt->duration, pkt->size);
        if (pkt->pts != AV_NOPTS_VALUE)
            pkt->pts += offset;
        if (pkt->dts != AV_NOPTS_VALUE)
            pkt->dts += offset;
    }

    /* This may be redundant, but it should not hurt. */
    if (pkt->dts != AV_NOPTS_VALUE &&
        pkt->pts != AV_NOPTS_VALUE &&
        pkt->pts > pkt->dts)
        presentation_delayed = 1;

    if (s->debug & FF_FDEBUG_TS)
        av_log(s, AV_LOG_DEBUG,
            "IN delayed:%d pts:%s, dts:%s cur_dts:%s st:%d pc:%p duration:%"PRId64" delay:%d onein_oneout:%d\n",
            presentation_delayed, av_ts2str(pkt->pts), av_ts2str(pkt->dts), av_ts2str(sti->cur_dts),
            pkt->stream_index, pc, pkt->duration, delay, onein_oneout);

    /* Interpolate PTS and DTS if they are not present. We skip H264
     * currently because delay and has_b_frames are not reliably set. */
    if ((delay == 0 || (delay == 1 && pc)) &&
        onein_oneout) {
        if (presentation_delayed) {
            /* DTS = decompression timestamp */
            /* PTS = presentation timestamp */
            if (pkt->dts == AV_NOPTS_VALUE)
                pkt->dts = sti->last_IP_pts;
            update_initial_timestamps(s, pkt->stream_index, pkt->dts, pkt->pts, pkt);
            if (pkt->dts == AV_NOPTS_VALUE)
                pkt->dts = sti->cur_dts;

            /* This is tricky: the dts must be incremented by the duration
             * of the frame we are displaying, i.e. the last I- or P-frame. */
            if (sti->last_IP_duration == 0 && (uint64_t)pkt->duration <= INT32_MAX)
                sti->last_IP_duration = pkt->duration;
            if (pkt->dts != AV_NOPTS_VALUE)
                sti->cur_dts = av_sat_add64(pkt->dts, sti->last_IP_duration);
            if (pkt->dts != AV_NOPTS_VALUE &&
                pkt->pts == AV_NOPTS_VALUE &&
                sti->last_IP_duration > 0 &&
                ((uint64_t)sti->cur_dts - (uint64_t)next_dts + 1) <= 2 &&
                next_dts != next_pts &&
                next_pts != AV_NOPTS_VALUE)
                pkt->pts = next_dts;

            if ((uint64_t)pkt->duration <= INT32_MAX)
                sti->last_IP_duration = pkt->duration;
            sti->last_IP_pts      = pkt->pts;
            /* Cannot compute PTS if not present (we can compute it only
             * by knowing the future. */
        } else if (pkt->pts != AV_NOPTS_VALUE ||
                   pkt->dts != AV_NOPTS_VALUE ||
                   pkt->duration > 0             ) {

            /* presentation is not delayed : PTS and DTS are the same */
            if (pkt->pts == AV_NOPTS_VALUE)
                pkt->pts = pkt->dts;
            update_initial_timestamps(s, pkt->stream_index, pkt->pts,
                                      pkt->pts, pkt);
            if (pkt->pts == AV_NOPTS_VALUE)
                pkt->pts = sti->cur_dts;
            pkt->dts = pkt->pts;
            if (pkt->pts != AV_NOPTS_VALUE && duration.num >= 0)
                sti->cur_dts = av_add_stable(st->time_base, pkt->pts, duration, 1);
        }
    }

    if (pkt->pts != AV_NOPTS_VALUE && delay <= MAX_REORDER_DELAY) {
        sti->pts_buffer[0] = pkt->pts;
        for (int i = 0; i < delay && sti->pts_buffer[i] > sti->pts_buffer[i + 1]; i++)
            FFSWAP(int64_t, sti->pts_buffer[i], sti->pts_buffer[i + 1]);

        if (has_decode_delay_been_guessed(st))
            pkt->dts = select_from_pts_buffer(st, sti->pts_buffer, pkt->dts);
    }
    // We skipped it above so we try here.
    if (!onein_oneout)
        // This should happen on the first packet
        update_initial_timestamps(s, pkt->stream_index, pkt->dts, pkt->pts, pkt);
    if (pkt->dts > sti->cur_dts)
        sti->cur_dts = pkt->dts;

    if (s->debug & FF_FDEBUG_TS)
        av_log(s, AV_LOG_DEBUG, "OUTdelayed:%d/%d pts:%s, dts:%s cur_dts:%s st:%d (%d)\n",
            presentation_delayed, delay, av_ts2str(pkt->pts), av_ts2str(pkt->dts), av_ts2str(sti->cur_dts), st->index, st->id);

    /* update flags */
    if (st->codecpar->codec_type == AVMEDIA_TYPE_DATA || ff_is_intra_only(st->codecpar->codec_id))
        pkt->flags |= AV_PKT_FLAG_KEY;
}

/**
 * Parse a packet, add all split parts to parse_queue.
 *
 * @param pkt   Packet to parse; must not be NULL.
 * @param flush Indicates whether to flush. If set, pkt must be blank.
 */
static int parse_packet(AVFormatContext *s, AVPacket *pkt,
                        int stream_index, int flush)
{
    FFFormatContext *const si = ffformatcontext(s);
    AVPacket *out_pkt = si->parse_pkt;
    AVStream *st = s->streams[stream_index];
    FFStream *const sti = ffstream(st);
    const uint8_t *data = pkt->data;
    int size = pkt->size;
    int ret = 0, got_output = flush;

    if (!size && !flush && sti->parser->flags & PARSER_FLAG_COMPLETE_FRAMES) {
        // preserve 0-size sync packets
        compute_pkt_fields(s, st, sti->parser, pkt, AV_NOPTS_VALUE, AV_NOPTS_VALUE);
    }

    while (size > 0 || (flush && got_output)) {
        int64_t next_pts = pkt->pts;
        int64_t next_dts = pkt->dts;
        int len;

        len = av_parser_parse2(sti->parser, sti->avctx,
                               &out_pkt->data, &out_pkt->size, data, size,
                               pkt->pts, pkt->dts, pkt->pos);

        pkt->pts = pkt->dts = AV_NOPTS_VALUE;
        pkt->pos = -1;
        /* increment read pointer */
        av_assert1(data || !len);
        data  = len ? data + len : data;
        size -= len;

        got_output = !!out_pkt->size;

        if (!out_pkt->size)
            continue;

        if (pkt->buf && out_pkt->data == pkt->data) {
            /* reference pkt->buf only when out_pkt->data is guaranteed to point
             * to data in it and not in the parser's internal buffer. */
            /* XXX: Ensure this is the case with all parsers when sti->parser->flags
             * is PARSER_FLAG_COMPLETE_FRAMES and check for that instead? */
            out_pkt->buf = av_buffer_ref(pkt->buf);
            if (!out_pkt->buf) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }
        } else {
            ret = av_packet_make_refcounted(out_pkt);
            if (ret < 0)
                goto fail;
        }

        if (pkt->side_data) {
            out_pkt->side_data       = pkt->side_data;
            out_pkt->side_data_elems = pkt->side_data_elems;
            pkt->side_data          = NULL;
            pkt->side_data_elems    = 0;
        }

        /* set the duration */
        out_pkt->duration = (sti->parser->flags & PARSER_FLAG_COMPLETE_FRAMES) ? pkt->duration : 0;
        if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (sti->avctx->sample_rate > 0) {
                out_pkt->duration =
                    av_rescale_q_rnd(sti->parser->duration,
                                     (AVRational) { 1, sti->avctx->sample_rate },
                                     st->time_base,
                                     AV_ROUND_DOWN);
            }
        }

        out_pkt->stream_index = st->index;
        out_pkt->pts          = sti->parser->pts;
        out_pkt->dts          = sti->parser->dts;
        out_pkt->pos          = sti->parser->pos;
        out_pkt->flags       |= pkt->flags & (AV_PKT_FLAG_DISCARD | AV_PKT_FLAG_CORRUPT);

        if (sti->need_parsing == AVSTREAM_PARSE_FULL_RAW)
            out_pkt->pos = sti->parser->frame_offset;

        if (sti->parser->key_frame == 1 ||
            (sti->parser->key_frame == -1 &&
             sti->parser->pict_type == AV_PICTURE_TYPE_I))
            out_pkt->flags |= AV_PKT_FLAG_KEY;

        if (sti->parser->key_frame == -1 && sti->parser->pict_type ==AV_PICTURE_TYPE_NONE && (pkt->flags&AV_PKT_FLAG_KEY))
            out_pkt->flags |= AV_PKT_FLAG_KEY;

        compute_pkt_fields(s, st, sti->parser, out_pkt, next_dts, next_pts);

        ret = avpriv_packet_list_put(&si->parse_queue,
                                     out_pkt, NULL, 0);
        if (ret < 0)
            goto fail;
    }

    /* end of the stream => close and free the parser */
    if (flush) {
        av_parser_close(sti->parser);
        sti->parser = NULL;
    }

fail:
    if (ret < 0)
        av_packet_unref(out_pkt);
    av_packet_unref(pkt);
    return ret;
}

static int64_t ts_to_samples(AVStream *st, int64_t ts)
{
    return av_rescale(ts, st->time_base.num * st->codecpar->sample_rate, st->time_base.den);
}

static int read_frame_internal(AVFormatContext *s, AVPacket *pkt)
{
    FFFormatContext *const si = ffformatcontext(s);
    int ret, got_packet = 0;
    AVDictionary *metadata = NULL;

    while (!got_packet && !si->parse_queue.head) {
        AVStream *st;
        FFStream *sti;

        /* read next packet */
        ret = ff_read_packet(s, pkt);
        if (ret < 0) {
            if (ret == AVERROR(EAGAIN))
                return ret;
            /* flush the parsers */
            for (unsigned i = 0; i < s->nb_streams; i++) {
                AVStream *const st  = s->streams[i];
                FFStream *const sti = ffstream(st);
                if (sti->parser && sti->need_parsing)
                    parse_packet(s, pkt, st->index, 1);
            }
            /* all remaining packets are now in parse_queue =>
             * really terminate parsing */
            break;
        }
        ret = 0;
        st  = s->streams[pkt->stream_index];
        sti = ffstream(st);

        st->event_flags |= AVSTREAM_EVENT_FLAG_NEW_PACKETS;

        /* update context if required */
        if (sti->need_context_update) {
            if (avcodec_is_open(sti->avctx)) {
                av_log(s, AV_LOG_DEBUG, "Demuxer context update while decoder is open, closing and trying to re-open\n");
                avcodec_close(sti->avctx);
                sti->info->found_decoder = 0;
            }

            /* close parser, because it depends on the codec */
            if (sti->parser && sti->avctx->codec_id != st->codecpar->codec_id) {
                av_parser_close(sti->parser);
                sti->parser = NULL;
            }

            ret = avcodec_parameters_to_context(sti->avctx, st->codecpar);
            if (ret < 0) {
                av_packet_unref(pkt);
                return ret;
            }

            sti->need_context_update = 0;
        }

        if (pkt->pts != AV_NOPTS_VALUE &&
            pkt->dts != AV_NOPTS_VALUE &&
            pkt->pts < pkt->dts) {
            av_log(s, AV_LOG_WARNING,
                   "Invalid timestamps stream=%d, pts=%s, dts=%s, size=%d\n",
                   pkt->stream_index,
                   av_ts2str(pkt->pts),
                   av_ts2str(pkt->dts),
                   pkt->size);
        }
        if (s->debug & FF_FDEBUG_TS)
            av_log(s, AV_LOG_DEBUG,
                   "ff_read_packet stream=%d, pts=%s, dts=%s, size=%d, duration=%"PRId64", flags=%d\n",
                   pkt->stream_index,
                   av_ts2str(pkt->pts),
                   av_ts2str(pkt->dts),
                   pkt->size, pkt->duration, pkt->flags);

        if (sti->need_parsing && !sti->parser && !(s->flags & AVFMT_FLAG_NOPARSE)) {
            sti->parser = av_parser_init(st->codecpar->codec_id);
            if (!sti->parser) {
                av_log(s, AV_LOG_VERBOSE, "parser not found for codec "
                       "%s, packets or times may be invalid.\n",
                       avcodec_get_name(st->codecpar->codec_id));
                /* no parser available: just output the raw packets */
                sti->need_parsing = AVSTREAM_PARSE_NONE;
            } else if (sti->need_parsing == AVSTREAM_PARSE_HEADERS)
                sti->parser->flags |= PARSER_FLAG_COMPLETE_FRAMES;
            else if (sti->need_parsing == AVSTREAM_PARSE_FULL_ONCE)
                sti->parser->flags |= PARSER_FLAG_ONCE;
            else if (sti->need_parsing == AVSTREAM_PARSE_FULL_RAW)
                sti->parser->flags |= PARSER_FLAG_USE_CODEC_TS;
        }

        if (!sti->need_parsing || !sti->parser) {
            /* no parsing needed: we just output the packet as is */
            compute_pkt_fields(s, st, NULL, pkt, AV_NOPTS_VALUE, AV_NOPTS_VALUE);
            if ((s->iformat->flags & AVFMT_GENERIC_INDEX) &&
                (pkt->flags & AV_PKT_FLAG_KEY) && pkt->dts != AV_NOPTS_VALUE) {
                ff_reduce_index(s, st->index);
                av_add_index_entry(st, pkt->pos, pkt->dts,
                                   0, 0, AVINDEX_KEYFRAME);
            }
            got_packet = 1;
        } else if (st->discard < AVDISCARD_ALL) {
            if ((ret = parse_packet(s, pkt, pkt->stream_index, 0)) < 0)
                return ret;
            st->codecpar->sample_rate = sti->avctx->sample_rate;
            st->codecpar->bit_rate = sti->avctx->bit_rate;
            st->codecpar->channels = sti->avctx->channels;
            st->codecpar->channel_layout = sti->avctx->channel_layout;
            st->codecpar->codec_id = sti->avctx->codec_id;
        } else {
            /* free packet */
            av_packet_unref(pkt);
        }
        if (pkt->flags & AV_PKT_FLAG_KEY)
            sti->skip_to_keyframe = 0;
        if (sti->skip_to_keyframe) {
            av_packet_unref(pkt);
            got_packet = 0;
        }
    }

    if (!got_packet && si->parse_queue.head)
        ret = avpriv_packet_list_get(&si->parse_queue, pkt);

    if (ret >= 0) {
        AVStream *const st  = s->streams[pkt->stream_index];
        FFStream *const sti = ffstream(st);
        int discard_padding = 0;
        if (sti->first_discard_sample && pkt->pts != AV_NOPTS_VALUE) {
            int64_t pts = pkt->pts - (is_relative(pkt->pts) ? RELATIVE_TS_BASE : 0);
            int64_t sample = ts_to_samples(st, pts);
            int duration = ts_to_samples(st, pkt->duration);
            int64_t end_sample = sample + duration;
            if (duration > 0 && end_sample >= sti->first_discard_sample &&
                sample < sti->last_discard_sample)
                discard_padding = FFMIN(end_sample - sti->first_discard_sample, duration);
        }
        if (sti->start_skip_samples && (pkt->pts == 0 || pkt->pts == RELATIVE_TS_BASE))
            sti->skip_samples = sti->start_skip_samples;
        if (sti->skip_samples || discard_padding) {
            uint8_t *p = av_packet_new_side_data(pkt, AV_PKT_DATA_SKIP_SAMPLES, 10);
            if (p) {
                AV_WL32(p, sti->skip_samples);
                AV_WL32(p + 4, discard_padding);
                av_log(s, AV_LOG_DEBUG, "demuxer injecting skip %d / discard %d\n", sti->skip_samples, discard_padding);
            }
            sti->skip_samples = 0;
        }

        if (sti->inject_global_side_data) {
            for (int i = 0; i < st->nb_side_data; i++) {
                const AVPacketSideData *const src_sd = &st->side_data[i];
                uint8_t *dst_data;

                if (av_packet_get_side_data(pkt, src_sd->type, NULL))
                    continue;

                dst_data = av_packet_new_side_data(pkt, src_sd->type, src_sd->size);
                if (!dst_data) {
                    av_log(s, AV_LOG_WARNING, "Could not inject global side data\n");
                    continue;
                }

                memcpy(dst_data, src_sd->data, src_sd->size);
            }
            sti->inject_global_side_data = 0;
        }
    }

    av_opt_get_dict_val(s, "metadata", AV_OPT_SEARCH_CHILDREN, &metadata);
    if (metadata) {
        s->event_flags |= AVFMT_EVENT_FLAG_METADATA_UPDATED;
        av_dict_copy(&s->metadata, metadata, 0);
        av_dict_free(&metadata);
        av_opt_set_dict_val(s, "metadata", NULL, AV_OPT_SEARCH_CHILDREN);
    }

    if (s->debug & FF_FDEBUG_TS)
        av_log(s, AV_LOG_DEBUG,
               "read_frame_internal stream=%d, pts=%s, dts=%s, "
               "size=%d, duration=%"PRId64", flags=%d\n",
               pkt->stream_index,
               av_ts2str(pkt->pts),
               av_ts2str(pkt->dts),
               pkt->size, pkt->duration, pkt->flags);

    /* A demuxer might have returned EOF because of an IO error, let's
     * propagate this back to the user. */
    if (ret == AVERROR_EOF && s->pb && s->pb->error < 0 && s->pb->error != AVERROR(EAGAIN))
        ret = s->pb->error;

    return ret;
}

int av_read_frame(AVFormatContext *s, AVPacket *pkt)
{
    FFFormatContext *const si = ffformatcontext(s);
    const int genpts = s->flags & AVFMT_FLAG_GENPTS;
    int eof = 0;
    int ret;
    AVStream *st;

    if (!genpts) {
        ret = si->packet_buffer.head
              ? avpriv_packet_list_get(&si->packet_buffer, pkt)
              : read_frame_internal(s, pkt);
        if (ret < 0)
            return ret;
        goto return_packet;
    }

    for (;;) {
        PacketListEntry *pktl = si->packet_buffer.head;

        if (pktl) {
            AVPacket *next_pkt = &pktl->pkt;

            if (next_pkt->dts != AV_NOPTS_VALUE) {
                int wrap_bits = s->streams[next_pkt->stream_index]->pts_wrap_bits;
                // last dts seen for this stream. if any of packets following
                // current one had no dts, we will set this to AV_NOPTS_VALUE.
                int64_t last_dts = next_pkt->dts;
                av_assert2(wrap_bits <= 64);
                while (pktl && next_pkt->pts == AV_NOPTS_VALUE) {
                    if (pktl->pkt.stream_index == next_pkt->stream_index &&
                        av_compare_mod(next_pkt->dts, pktl->pkt.dts, 2ULL << (wrap_bits - 1)) < 0) {
                        if (av_compare_mod(pktl->pkt.pts, pktl->pkt.dts, 2ULL << (wrap_bits - 1))) {
                            // not B-frame
                            next_pkt->pts = pktl->pkt.dts;
                        }
                        if (last_dts != AV_NOPTS_VALUE) {
                            // Once last dts was set to AV_NOPTS_VALUE, we don't change it.
                            last_dts = pktl->pkt.dts;
                        }
                    }
                    pktl = pktl->next;
                }
                if (eof && next_pkt->pts == AV_NOPTS_VALUE && last_dts != AV_NOPTS_VALUE) {
                    // Fixing the last reference frame had none pts issue (For MXF etc).
                    // We only do this when
                    // 1. eof.
                    // 2. we are not able to resolve a pts value for current packet.
                    // 3. the packets for this stream at the end of the files had valid dts.
                    next_pkt->pts = last_dts + next_pkt->duration;
                }
                pktl = si->packet_buffer.head;
            }

            /* read packet from packet buffer, if there is data */
            st = s->streams[next_pkt->stream_index];
            if (!(next_pkt->pts == AV_NOPTS_VALUE && st->discard < AVDISCARD_ALL &&
                  next_pkt->dts != AV_NOPTS_VALUE && !eof)) {
                ret = avpriv_packet_list_get(&si->packet_buffer, pkt);
                goto return_packet;
            }
        }

        ret = read_frame_internal(s, pkt);
        if (ret < 0) {
            if (pktl && ret != AVERROR(EAGAIN)) {
                eof = 1;
                continue;
            } else
                return ret;
        }

        ret = avpriv_packet_list_put(&si->packet_buffer,
                                     pkt, NULL, 0);
        if (ret < 0) {
            av_packet_unref(pkt);
            return ret;
        }
    }

return_packet:
    st = s->streams[pkt->stream_index];
    if ((s->iformat->flags & AVFMT_GENERIC_INDEX) && pkt->flags & AV_PKT_FLAG_KEY) {
        ff_reduce_index(s, st->index);
        av_add_index_entry(st, pkt->pos, pkt->dts, 0, 0, AVINDEX_KEYFRAME);
    }

    if (is_relative(pkt->dts))
        pkt->dts -= RELATIVE_TS_BASE;
    if (is_relative(pkt->pts))
        pkt->pts -= RELATIVE_TS_BASE;

    return ret;
}

/**
 * Return TRUE if the stream has accurate duration in any stream.
 *
 * @return TRUE if the stream has accurate duration for at least one component.
 */
static int has_duration(AVFormatContext *ic)
{
    for (unsigned i = 0; i < ic->nb_streams; i++) {
        const AVStream *const st = ic->streams[i];
        if (st->duration != AV_NOPTS_VALUE)
            return 1;
    }
    if (ic->duration != AV_NOPTS_VALUE)
        return 1;
    return 0;
}

/**
 * Estimate the stream timings from the one of each components.
 *
 * Also computes the global bitrate if possible.
 */
static void update_stream_timings(AVFormatContext *ic)
{
    int64_t start_time, start_time1, start_time_text, end_time, end_time1, end_time_text;
    int64_t duration, duration1, duration_text, filesize;

    start_time = INT64_MAX;
    start_time_text = INT64_MAX;
    end_time   = INT64_MIN;
    end_time_text   = INT64_MIN;
    duration   = INT64_MIN;
    duration_text = INT64_MIN;

    for (unsigned i = 0; i < ic->nb_streams; i++) {
        AVStream *const st = ic->streams[i];
        int is_text = st->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE ||
                      st->codecpar->codec_type == AVMEDIA_TYPE_DATA;

        if (st->start_time != AV_NOPTS_VALUE && st->time_base.den) {
            start_time1 = av_rescale_q(st->start_time, st->time_base,
                                       AV_TIME_BASE_Q);
            if (is_text)
                start_time_text = FFMIN(start_time_text, start_time1);
            else
                start_time = FFMIN(start_time, start_time1);
            end_time1 = av_rescale_q_rnd(st->duration, st->time_base,
                                         AV_TIME_BASE_Q,
                                         AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
            if (end_time1 != AV_NOPTS_VALUE && (end_time1 > 0 ? start_time1 <= INT64_MAX - end_time1 : start_time1 >= INT64_MIN - end_time1)) {
                end_time1 += start_time1;
                if (is_text)
                    end_time_text = FFMAX(end_time_text, end_time1);
                else
                    end_time = FFMAX(end_time, end_time1);
            }
            for (AVProgram *p = NULL; (p = av_find_program_from_stream(ic, p, i)); ) {
                if (p->start_time == AV_NOPTS_VALUE || p->start_time > start_time1)
                    p->start_time = start_time1;
                if (p->end_time < end_time1)
                    p->end_time = end_time1;
            }
        }
        if (st->duration != AV_NOPTS_VALUE) {
            duration1 = av_rescale_q(st->duration, st->time_base,
                                     AV_TIME_BASE_Q);
            if (is_text)
                duration_text = FFMAX(duration_text, duration1);
            else
                duration = FFMAX(duration, duration1);
        }
    }
    if (start_time == INT64_MAX || (start_time > start_time_text && start_time - (uint64_t)start_time_text < AV_TIME_BASE))
        start_time = start_time_text;
    else if (start_time > start_time_text)
        av_log(ic, AV_LOG_VERBOSE, "Ignoring outlier non primary stream starttime %f\n", start_time_text / (float)AV_TIME_BASE);

    if (end_time == INT64_MIN || (end_time < end_time_text && end_time_text - (uint64_t)end_time < AV_TIME_BASE))
        end_time = end_time_text;
    else if (end_time < end_time_text)
        av_log(ic, AV_LOG_VERBOSE, "Ignoring outlier non primary stream endtime %f\n", end_time_text / (float)AV_TIME_BASE);

     if (duration == INT64_MIN || (duration < duration_text && duration_text - duration < AV_TIME_BASE))
         duration = duration_text;
     else if (duration < duration_text)
         av_log(ic, AV_LOG_VERBOSE, "Ignoring outlier non primary stream duration %f\n", duration_text / (float)AV_TIME_BASE);

    if (start_time != INT64_MAX) {
        ic->start_time = start_time;
        if (end_time != INT64_MIN) {
            if (ic->nb_programs > 1) {
                for (unsigned i = 0; i < ic->nb_programs; i++) {
                    AVProgram *const p = ic->programs[i];

                    if (p->start_time != AV_NOPTS_VALUE &&
                        p->end_time > p->start_time &&
                        p->end_time - (uint64_t)p->start_time <= INT64_MAX)
                        duration = FFMAX(duration, p->end_time - p->start_time);
                }
            } else if (end_time >= start_time && end_time - (uint64_t)start_time <= INT64_MAX) {
                duration = FFMAX(duration, end_time - start_time);
            }
        }
    }
    if (duration != INT64_MIN && duration > 0 && ic->duration == AV_NOPTS_VALUE) {
        ic->duration = duration;
    }
    if (ic->pb && (filesize = avio_size(ic->pb)) > 0 && ic->duration > 0) {
        /* compute the bitrate */
        double bitrate = (double) filesize * 8.0 * AV_TIME_BASE /
                         (double) ic->duration;
        if (bitrate >= 0 && bitrate <= INT64_MAX)
            ic->bit_rate = bitrate;
    }
}

static void fill_all_stream_timings(AVFormatContext *ic)
{
    update_stream_timings(ic);
    for (unsigned i = 0; i < ic->nb_streams; i++) {
        AVStream *const st = ic->streams[i];

        if (st->start_time == AV_NOPTS_VALUE) {
            if (ic->start_time != AV_NOPTS_VALUE)
                st->start_time = av_rescale_q(ic->start_time, AV_TIME_BASE_Q,
                                              st->time_base);
            if (ic->duration != AV_NOPTS_VALUE)
                st->duration = av_rescale_q(ic->duration, AV_TIME_BASE_Q,
                                            st->time_base);
        }
    }
}

static void estimate_timings_from_bit_rate(AVFormatContext *ic)
{
    FFFormatContext *const si = ffformatcontext(ic);
    int show_warning = 0;

    /* if bit_rate is already set, we believe it */
    if (ic->bit_rate <= 0) {
        int64_t bit_rate = 0;
        for (unsigned i = 0; i < ic->nb_streams; i++) {
            const AVStream *const st  = ic->streams[i];
            const FFStream *const sti = cffstream(st);
            if (st->codecpar->bit_rate <= 0 && sti->avctx->bit_rate > 0)
                st->codecpar->bit_rate = sti->avctx->bit_rate;
            if (st->codecpar->bit_rate > 0) {
                if (INT64_MAX - st->codecpar->bit_rate < bit_rate) {
                    bit_rate = 0;
                    break;
                }
                bit_rate += st->codecpar->bit_rate;
            } else if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && sti->codec_info_nb_frames > 1) {
                // If we have a videostream with packets but without a bitrate
                // then consider the sum not known
                bit_rate = 0;
                break;
            }
        }
        ic->bit_rate = bit_rate;
    }

    /* if duration is already set, we believe it */
    if (ic->duration == AV_NOPTS_VALUE &&
        ic->bit_rate != 0) {
        int64_t filesize = ic->pb ? avio_size(ic->pb) : 0;
        if (filesize > si->data_offset) {
            filesize -= si->data_offset;
            for (unsigned i = 0; i < ic->nb_streams; i++) {
                AVStream *const st = ic->streams[i];

                if (   st->time_base.num <= INT64_MAX / ic->bit_rate
                    && st->duration == AV_NOPTS_VALUE) {
                    st->duration = av_rescale(filesize, 8LL * st->time_base.den,
                                          ic->bit_rate *
                                          (int64_t) st->time_base.num);
                    show_warning = 1;
                }
            }
        }
    }
    if (show_warning)
        av_log(ic, AV_LOG_WARNING,
               "Estimating duration from bitrate, this may be inaccurate\n");
}

#define DURATION_MAX_READ_SIZE 250000LL
#define DURATION_MAX_RETRY 6

/* only usable for MPEG-PS streams */
static void estimate_timings_from_pts(AVFormatContext *ic, int64_t old_offset)
{
    FFFormatContext *const si = ffformatcontext(ic);
    AVPacket *const pkt = si->pkt;
    int num, den, read_size, ret;
    int found_duration = 0;
    int is_end;
    int64_t filesize, offset, duration;
    int retry = 0;

    /* flush packet queue */
    ff_flush_packet_queue(ic);

    for (unsigned i = 0; i < ic->nb_streams; i++) {
        AVStream *const st  = ic->streams[i];
        FFStream *const sti = ffstream(st);

        if (st->start_time == AV_NOPTS_VALUE &&
            sti->first_dts == AV_NOPTS_VALUE &&
            st->codecpar->codec_type != AVMEDIA_TYPE_UNKNOWN)
            av_log(ic, AV_LOG_WARNING,
                   "start time for stream %d is not set in estimate_timings_from_pts\n", i);

        if (sti->parser) {
            av_parser_close(sti->parser);
            sti->parser = NULL;
        }
    }

    if (ic->skip_estimate_duration_from_pts) {
        av_log(ic, AV_LOG_INFO, "Skipping duration calculation in estimate_timings_from_pts\n");
        goto skip_duration_calc;
    }

    av_opt_set_int(ic, "skip_changes", 1, AV_OPT_SEARCH_CHILDREN);
    /* estimate the end time (duration) */
    /* XXX: may need to support wrapping */
    filesize = ic->pb ? avio_size(ic->pb) : 0;
    do {
        is_end = found_duration;
        offset = filesize - (DURATION_MAX_READ_SIZE << retry);
        if (offset < 0)
            offset = 0;

        avio_seek(ic->pb, offset, SEEK_SET);
        read_size = 0;
        for (;;) {
            AVStream *st;
            FFStream *sti;
            if (read_size >= DURATION_MAX_READ_SIZE << (FFMAX(retry - 1, 0)))
                break;

            do {
                ret = ff_read_packet(ic, pkt);
            } while (ret == AVERROR(EAGAIN));
            if (ret != 0)
                break;
            read_size += pkt->size;
            st         = ic->streams[pkt->stream_index];
            sti        = ffstream(st);
            if (pkt->pts != AV_NOPTS_VALUE &&
                (st->start_time != AV_NOPTS_VALUE ||
                 sti->first_dts != AV_NOPTS_VALUE)) {
                if (pkt->duration == 0) {
                    compute_frame_duration(ic, &num, &den, st, sti->parser, pkt);
                    if (den && num) {
                        pkt->duration = av_rescale_rnd(1,
                                           num * (int64_t) st->time_base.den,
                                           den * (int64_t) st->time_base.num,
                                           AV_ROUND_DOWN);
                    }
                }
                duration = pkt->pts + pkt->duration;
                found_duration = 1;
                if (st->start_time != AV_NOPTS_VALUE)
                    duration -= st->start_time;
                else
                    duration -= sti->first_dts;
                if (duration > 0) {
                    if (st->duration == AV_NOPTS_VALUE || sti->info->last_duration<= 0 ||
                        (st->duration < duration && FFABS(duration - sti->info->last_duration) < 60LL*st->time_base.den / st->time_base.num))
                        st->duration = duration;
                    sti->info->last_duration = duration;
                }
            }
            av_packet_unref(pkt);
        }

        /* check if all audio/video streams have valid duration */
        if (!is_end) {
            is_end = 1;
            for (unsigned i = 0; i < ic->nb_streams; i++) {
                const AVStream *const st = ic->streams[i];
                switch (st->codecpar->codec_type) {
                    case AVMEDIA_TYPE_VIDEO:
                    case AVMEDIA_TYPE_AUDIO:
                        if (st->duration == AV_NOPTS_VALUE)
                            is_end = 0;
                }
            }
        }
    } while (!is_end &&
             offset &&
             ++retry <= DURATION_MAX_RETRY);

    av_opt_set_int(ic, "skip_changes", 0, AV_OPT_SEARCH_CHILDREN);

    /* warn about audio/video streams which duration could not be estimated */
    for (unsigned i = 0; i < ic->nb_streams; i++) {
        const AVStream *const st  = ic->streams[i];
        const FFStream *const sti = cffstream(st);

        if (st->duration == AV_NOPTS_VALUE) {
            switch (st->codecpar->codec_type) {
            case AVMEDIA_TYPE_VIDEO:
            case AVMEDIA_TYPE_AUDIO:
                if (st->start_time != AV_NOPTS_VALUE || sti->first_dts != AV_NOPTS_VALUE) {
                    av_log(ic, AV_LOG_WARNING, "stream %d : no PTS found at end of file, duration not set\n", i);
                } else
                    av_log(ic, AV_LOG_WARNING, "stream %d : no TS found at start of file, duration not set\n", i);
            }
        }
    }
skip_duration_calc:
    fill_all_stream_timings(ic);

    avio_seek(ic->pb, old_offset, SEEK_SET);
    for (unsigned i = 0; i < ic->nb_streams; i++) {
        AVStream *const st  = ic->streams[i];
        FFStream *const sti = ffstream(st);

        sti->cur_dts     = sti->first_dts;
        sti->last_IP_pts = AV_NOPTS_VALUE;
        sti->last_dts_for_order_check = AV_NOPTS_VALUE;
        for (int j = 0; j < MAX_REORDER_DELAY + 1; j++)
            sti->pts_buffer[j] = AV_NOPTS_VALUE;
    }
}

/* 1:1 map to AVDurationEstimationMethod */
static const char *const duration_name[] = {
    [AVFMT_DURATION_FROM_PTS]     = "pts",
    [AVFMT_DURATION_FROM_STREAM]  = "stream",
    [AVFMT_DURATION_FROM_BITRATE] = "bit rate",
};

static const char *duration_estimate_name(enum AVDurationEstimationMethod method)
{
    return duration_name[method];
}

static void estimate_timings(AVFormatContext *ic, int64_t old_offset)
{
    int64_t file_size;

    /* get the file size, if possible */
    if (ic->iformat->flags & AVFMT_NOFILE) {
        file_size = 0;
    } else {
        file_size = avio_size(ic->pb);
        file_size = FFMAX(0, file_size);
    }

    if ((!strcmp(ic->iformat->name, "mpeg") ||
         !strcmp(ic->iformat->name, "mpegts")) &&
        file_size && (ic->pb->seekable & AVIO_SEEKABLE_NORMAL)) {
        /* get accurate estimate from the PTSes */
        estimate_timings_from_pts(ic, old_offset);
        ic->duration_estimation_method = AVFMT_DURATION_FROM_PTS;
    } else if (has_duration(ic)) {
        /* at least one component has timings - we use them for all
         * the components */
        fill_all_stream_timings(ic);
        /* nut demuxer estimate the duration from PTS */
        if (!strcmp(ic->iformat->name, "nut"))
            ic->duration_estimation_method = AVFMT_DURATION_FROM_PTS;
        else
            ic->duration_estimation_method = AVFMT_DURATION_FROM_STREAM;
    } else {
        /* less precise: use bitrate info */
        estimate_timings_from_bit_rate(ic);
        ic->duration_estimation_method = AVFMT_DURATION_FROM_BITRATE;
    }
    update_stream_timings(ic);

    for (unsigned i = 0; i < ic->nb_streams; i++) {
        AVStream *const st = ic->streams[i];
        if (st->time_base.den)
            av_log(ic, AV_LOG_TRACE, "stream %u: start_time: %s duration: %s\n", i,
                   av_ts2timestr(st->start_time, &st->time_base),
                   av_ts2timestr(st->duration, &st->time_base));
    }
    av_log(ic, AV_LOG_TRACE,
           "format: start_time: %s duration: %s (estimate from %s) bitrate=%"PRId64" kb/s\n",
           av_ts2timestr(ic->start_time, &AV_TIME_BASE_Q),
           av_ts2timestr(ic->duration, &AV_TIME_BASE_Q),
           duration_estimate_name(ic->duration_estimation_method),
           (int64_t)ic->bit_rate / 1000);
}

static int determinable_frame_size(const AVCodecContext *avctx)
{
    switch(avctx->codec_id) {
    case AV_CODEC_ID_MP1:
    case AV_CODEC_ID_MP2:
    case AV_CODEC_ID_MP3:
    case AV_CODEC_ID_CODEC2:
        return 1;
    }

    return 0;
}

static int has_codec_parameters(const AVStream *st, const char **errmsg_ptr)
{
    const FFStream *const sti = cffstream(st);
    const AVCodecContext *const avctx = sti->avctx;

#define FAIL(errmsg) do {                                         \
        if (errmsg_ptr)                                           \
            *errmsg_ptr = errmsg;                                 \
        return 0;                                                 \
    } while (0)

    if (   avctx->codec_id == AV_CODEC_ID_NONE
        && avctx->codec_type != AVMEDIA_TYPE_DATA)
        FAIL("unknown codec");
    switch (avctx->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        if (!avctx->frame_size && determinable_frame_size(avctx))
            FAIL("unspecified frame size");
        if (sti->info->found_decoder >= 0 &&
            avctx->sample_fmt == AV_SAMPLE_FMT_NONE)
            FAIL("unspecified sample format");
        if (!avctx->sample_rate)
            FAIL("unspecified sample rate");
        if (!avctx->channels)
            FAIL("unspecified number of channels");
        if (sti->info->found_decoder >= 0 && !sti->nb_decoded_frames && avctx->codec_id == AV_CODEC_ID_DTS)
            FAIL("no decodable DTS frames");
        break;
    case AVMEDIA_TYPE_VIDEO:
        if (!avctx->width)
            FAIL("unspecified size");
        if (sti->info->found_decoder >= 0 && avctx->pix_fmt == AV_PIX_FMT_NONE)
            FAIL("unspecified pixel format");
        if (st->codecpar->codec_id == AV_CODEC_ID_RV30 || st->codecpar->codec_id == AV_CODEC_ID_RV40)
            if (!st->sample_aspect_ratio.num && !st->codecpar->sample_aspect_ratio.num && !sti->codec_info_nb_frames)
                FAIL("no frame in rv30/40 and no sar");
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        if (avctx->codec_id == AV_CODEC_ID_HDMV_PGS_SUBTITLE && !avctx->width)
            FAIL("unspecified size");
        break;
    case AVMEDIA_TYPE_DATA:
        if (avctx->codec_id == AV_CODEC_ID_NONE) return 1;
    }

    return 1;
}

/* returns 1 or 0 if or if not decoded data was returned, or a negative error */
static int try_decode_frame(AVFormatContext *s, AVStream *st,
                            const AVPacket *avpkt, AVDictionary **options)
{
    FFStream *const sti = ffstream(st);
    AVCodecContext *const avctx = sti->avctx;
    const AVCodec *codec;
    int got_picture = 1, ret = 0;
    AVFrame *frame = av_frame_alloc();
    AVSubtitle subtitle;
    AVPacket pkt = *avpkt;
    int do_skip_frame = 0;
    enum AVDiscard skip_frame;

    if (!frame)
        return AVERROR(ENOMEM);

    if (!avcodec_is_open(avctx) &&
        sti->info->found_decoder <= 0 &&
        (st->codecpar->codec_id != -sti->info->found_decoder || !st->codecpar->codec_id)) {
        AVDictionary *thread_opt = NULL;

        codec = find_probe_decoder(s, st, st->codecpar->codec_id);

        if (!codec) {
            sti->info->found_decoder = -st->codecpar->codec_id;
            ret                     = -1;
            goto fail;
        }

        /* Force thread count to 1 since the H.264 decoder will not extract
         * SPS and PPS to extradata during multi-threaded decoding. */
        av_dict_set(options ? options : &thread_opt, "threads", "1", 0);
        /* Force lowres to 0. The decoder might reduce the video size by the
         * lowres factor, and we don't want that propagated to the stream's
         * codecpar */
        av_dict_set(options ? options : &thread_opt, "lowres", "0", 0);
        if (s->codec_whitelist)
            av_dict_set(options ? options : &thread_opt, "codec_whitelist", s->codec_whitelist, 0);
        ret = avcodec_open2(avctx, codec, options ? options : &thread_opt);
        if (!options)
            av_dict_free(&thread_opt);
        if (ret < 0) {
            sti->info->found_decoder = -avctx->codec_id;
            goto fail;
        }
        sti->info->found_decoder = 1;
    } else if (!sti->info->found_decoder)
        sti->info->found_decoder = 1;

    if (sti->info->found_decoder < 0) {
        ret = -1;
        goto fail;
    }

    if (avpriv_codec_get_cap_skip_frame_fill_param(avctx->codec)) {
        do_skip_frame = 1;
        skip_frame = avctx->skip_frame;
        avctx->skip_frame = AVDISCARD_ALL;
    }

    while ((pkt.size > 0 || (!pkt.data && got_picture)) &&
           ret >= 0 &&
           (!has_codec_parameters(st, NULL) || !has_decode_delay_been_guessed(st) ||
            (!sti->codec_info_nb_frames &&
             (avctx->codec->capabilities & AV_CODEC_CAP_CHANNEL_CONF)))) {
        got_picture = 0;
        if (avctx->codec_type == AVMEDIA_TYPE_VIDEO ||
            avctx->codec_type == AVMEDIA_TYPE_AUDIO) {
            ret = avcodec_send_packet(avctx, &pkt);
            if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
                break;
            if (ret >= 0)
                pkt.size = 0;
            ret = avcodec_receive_frame(avctx, frame);
            if (ret >= 0)
                got_picture = 1;
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                ret = 0;
        } else if (avctx->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            ret = avcodec_decode_subtitle2(avctx, &subtitle,
                                           &got_picture, &pkt);
            if (got_picture)
                avsubtitle_free(&subtitle);
            if (ret >= 0)
                pkt.size = 0;
        }
        if (ret >= 0) {
            if (got_picture)
                sti->nb_decoded_frames++;
            ret       = got_picture;
        }
    }

fail:
    if (do_skip_frame) {
        avctx->skip_frame = skip_frame;
    }

    av_frame_free(&frame);
    return ret;
}

static int chapter_start_cmp(const void *p1, const void *p2)
{
    const AVChapter *const ch1 = *(AVChapter**)p1;
    const AVChapter *const ch2 = *(AVChapter**)p2;
    int delta = av_compare_ts(ch1->start, ch1->time_base, ch2->start, ch2->time_base);
    if (delta)
        return delta;
    return FFDIFFSIGN(ch1->id, ch2->id);
}

static int compute_chapters_end(AVFormatContext *s)
{
    int64_t max_time = 0;
    AVChapter **timetable;

    if (!s->nb_chapters)
        return 0;

    if (s->duration > 0 && s->start_time < INT64_MAX - s->duration)
        max_time = s->duration +
                       ((s->start_time == AV_NOPTS_VALUE) ? 0 : s->start_time);

    timetable = av_memdup(s->chapters, s->nb_chapters * sizeof(*timetable));
    if (!timetable)
        return AVERROR(ENOMEM);
    qsort(timetable, s->nb_chapters, sizeof(*timetable), chapter_start_cmp);

    for (unsigned i = 0; i < s->nb_chapters; i++)
        if (timetable[i]->end == AV_NOPTS_VALUE) {
            AVChapter *const ch = timetable[i];
            int64_t end = max_time ? av_rescale_q(max_time, AV_TIME_BASE_Q,
                                                  ch->time_base)
                                   : INT64_MAX;

            if (i + 1 < s->nb_chapters) {
                const AVChapter *const ch1 = timetable[i + 1];
                int64_t next_start = av_rescale_q(ch1->start, ch1->time_base,
                                                  ch->time_base);
                if (next_start > ch->start && next_start < end)
                    end = next_start;
            }
            ch->end = (end == INT64_MAX || end < ch->start) ? ch->start : end;
        }
    av_free(timetable);
    return 0;
}

static int get_std_framerate(int i)
{
    if (i < 30*12)
        return (i + 1) * 1001;
    i -= 30*12;

    if (i < 30)
        return (i + 31) * 1001 * 12;
    i -= 30;

    if (i < 3)
        return ((const int[]) { 80, 120, 240})[i] * 1001 * 12;

    i -= 3;

    return ((const int[]) { 24, 30, 60, 12, 15, 48 })[i] * 1000 * 12;
}

/* Is the time base unreliable?
 * This is a heuristic to balance between quick acceptance of the values in
 * the headers vs. some extra checks.
 * Old DivX and Xvid often have nonsense timebases like 1fps or 2fps.
 * MPEG-2 commonly misuses field repeat flags to store different framerates.
 * And there are "variable" fps files this needs to detect as well. */
static int tb_unreliable(AVCodecContext *c)
{
    if (c->time_base.den >= 101LL * c->time_base.num ||
        c->time_base.den <    5LL * c->time_base.num ||
        // c->codec_tag == AV_RL32("DIVX") ||
        // c->codec_tag == AV_RL32("XVID") ||
        c->codec_tag == AV_RL32("mp4v") ||
        c->codec_id == AV_CODEC_ID_MPEG2VIDEO ||
        c->codec_id == AV_CODEC_ID_GIF ||
        c->codec_id == AV_CODEC_ID_HEVC ||
        c->codec_id == AV_CODEC_ID_H264)
        return 1;
    return 0;
}

int ff_rfps_add_frame(AVFormatContext *ic, AVStream *st, int64_t ts)
{
    FFStream *const sti = ffstream(st);
    int64_t last = sti->info->last_dts;

    if (   ts != AV_NOPTS_VALUE && last != AV_NOPTS_VALUE && ts > last
       && ts - (uint64_t)last < INT64_MAX) {
        double dts = (is_relative(ts) ?  ts - RELATIVE_TS_BASE : ts) * av_q2d(st->time_base);
        int64_t duration = ts - last;

        if (!sti->info->duration_error)
            sti->info->duration_error = av_mallocz(sizeof(sti->info->duration_error[0])*2);
        if (!sti->info->duration_error)
            return AVERROR(ENOMEM);

//         if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO)
//             av_log(NULL, AV_LOG_ERROR, "%f\n", dts);
        for (int i = 0; i < MAX_STD_TIMEBASES; i++) {
            if (sti->info->duration_error[0][1][i] < 1e10) {
                int framerate = get_std_framerate(i);
                double sdts = dts*framerate/(1001*12);
                for (int j = 0; j < 2; j++) {
                    int64_t ticks = llrint(sdts+j*0.5);
                    double error = sdts - ticks + j*0.5;
                    sti->info->duration_error[j][0][i] += error;
                    sti->info->duration_error[j][1][i] += error*error;
                }
            }
        }
        if (sti->info->rfps_duration_sum <= INT64_MAX - duration) {
            sti->info->duration_count++;
            sti->info->rfps_duration_sum += duration;
        }

        if (sti->info->duration_count % 10 == 0) {
            int n = sti->info->duration_count;
            for (int i = 0; i < MAX_STD_TIMEBASES; i++) {
                if (sti->info->duration_error[0][1][i] < 1e10) {
                    double a0     = sti->info->duration_error[0][0][i] / n;
                    double error0 = sti->info->duration_error[0][1][i] / n - a0*a0;
                    double a1     = sti->info->duration_error[1][0][i] / n;
                    double error1 = sti->info->duration_error[1][1][i] / n - a1*a1;
                    if (error0 > 0.04 && error1 > 0.04) {
                        sti->info->duration_error[0][1][i] = 2e10;
                        sti->info->duration_error[1][1][i] = 2e10;
                    }
                }
            }
        }

        // ignore the first 4 values, they might have some random jitter
        if (sti->info->duration_count > 3 && is_relative(ts) == is_relative(last))
            sti->info->duration_gcd = av_gcd(sti->info->duration_gcd, duration);
    }
    if (ts != AV_NOPTS_VALUE)
        sti->info->last_dts = ts;

    return 0;
}

void ff_rfps_calculate(AVFormatContext *ic)
{
    for (unsigned i = 0; i < ic->nb_streams; i++) {
        AVStream *const st  = ic->streams[i];
        FFStream *const sti = ffstream(st);

        if (st->codecpar->codec_type != AVMEDIA_TYPE_VIDEO)
            continue;
        // the check for tb_unreliable() is not completely correct, since this is not about handling
        // an unreliable/inexact time base, but a time base that is finer than necessary, as e.g.
        // ipmovie.c produces.
        if (tb_unreliable(sti->avctx) && sti->info->duration_count > 15 && sti->info->duration_gcd > FFMAX(1, st->time_base.den/(500LL*st->time_base.num)) && !st->r_frame_rate.num &&
            sti->info->duration_gcd < INT64_MAX / st->time_base.num)
            av_reduce(&st->r_frame_rate.num, &st->r_frame_rate.den, st->time_base.den, st->time_base.num * sti->info->duration_gcd, INT_MAX);
        if (sti->info->duration_count > 1 && !st->r_frame_rate.num
            && tb_unreliable(sti->avctx)) {
            int num = 0;
            double best_error = 0.01;
            AVRational ref_rate = st->r_frame_rate.num ? st->r_frame_rate : av_inv_q(st->time_base);

            for (int j = 0; j < MAX_STD_TIMEBASES; j++) {
                if (sti->info->codec_info_duration &&
                    sti->info->codec_info_duration*av_q2d(st->time_base) < (1001*11.5)/get_std_framerate(j))
                    continue;
                if (!sti->info->codec_info_duration && get_std_framerate(j) < 1001*12)
                    continue;

                if (av_q2d(st->time_base) * sti->info->rfps_duration_sum / sti->info->duration_count < (1001*12.0 * 0.8)/get_std_framerate(j))
                    continue;

                for (int k = 0; k < 2; k++) {
                    int n = sti->info->duration_count;
                    double a = sti->info->duration_error[k][0][j] / n;
                    double error = sti->info->duration_error[k][1][j]/n - a*a;

                    if (error < best_error && best_error> 0.000000001) {
                        best_error= error;
                        num = get_std_framerate(j);
                    }
                    if (error < 0.02)
                        av_log(ic, AV_LOG_DEBUG, "rfps: %f %f\n", get_std_framerate(j) / 12.0/1001, error);
                }
            }
            // do not increase frame rate by more than 1 % in order to match a standard rate.
            if (num && (!ref_rate.num || (double)num/(12*1001) < 1.01 * av_q2d(ref_rate)))
                av_reduce(&st->r_frame_rate.num, &st->r_frame_rate.den, num, 12*1001, INT_MAX);
        }
        if (   !st->avg_frame_rate.num
            && st->r_frame_rate.num && sti->info->rfps_duration_sum
            && sti->info->codec_info_duration <= 0
            && sti->info->duration_count > 2
            && fabs(1.0 / (av_q2d(st->r_frame_rate) * av_q2d(st->time_base)) - sti->info->rfps_duration_sum / (double)sti->info->duration_count) <= 1.0
            ) {
            av_log(ic, AV_LOG_DEBUG, "Setting avg frame rate based on r frame rate\n");
            st->avg_frame_rate = st->r_frame_rate;
        }

        av_freep(&sti->info->duration_error);
        sti->info->last_dts = AV_NOPTS_VALUE;
        sti->info->duration_count = 0;
        sti->info->rfps_duration_sum = 0;
    }
}

static int extract_extradata_check(AVStream *st)
{
    const AVBitStreamFilter *const f = av_bsf_get_by_name("extract_extradata");
    if (!f)
        return 0;

    if (f->codec_ids) {
        const enum AVCodecID *ids;
        for (ids = f->codec_ids; *ids != AV_CODEC_ID_NONE; ids++)
            if (*ids == st->codecpar->codec_id)
                return 1;
    }

    return 0;
}

static int extract_extradata_init(AVStream *st)
{
    FFStream *const sti = ffstream(st);
    const AVBitStreamFilter *f;
    int ret;

    f = av_bsf_get_by_name("extract_extradata");
    if (!f)
        goto finish;

    /* check that the codec id is supported */
    ret = extract_extradata_check(st);
    if (!ret)
        goto finish;

    ret = av_bsf_alloc(f, &sti->extract_extradata.bsf);
    if (ret < 0)
        return ret;

    ret = avcodec_parameters_copy(sti->extract_extradata.bsf->par_in,
                                  st->codecpar);
    if (ret < 0)
        goto fail;

    sti->extract_extradata.bsf->time_base_in = st->time_base;

    ret = av_bsf_init(sti->extract_extradata.bsf);
    if (ret < 0)
        goto fail;

finish:
    sti->extract_extradata.inited = 1;

    return 0;
fail:
    av_bsf_free(&sti->extract_extradata.bsf);
    return ret;
}

static int extract_extradata(FFFormatContext *si, AVStream *st, const AVPacket *pkt)
{
    FFStream *const sti = ffstream(st);
    AVPacket *const pkt_ref = si->parse_pkt;
    int ret;

    if (!sti->extract_extradata.inited) {
        ret = extract_extradata_init(st);
        if (ret < 0)
            return ret;
    }

    if (sti->extract_extradata.inited && !sti->extract_extradata.bsf)
        return 0;

    ret = av_packet_ref(pkt_ref, pkt);
    if (ret < 0)
        return ret;

    ret = av_bsf_send_packet(sti->extract_extradata.bsf, pkt_ref);
    if (ret < 0) {
        av_packet_unref(pkt_ref);
        return ret;
    }

    while (ret >= 0 && !sti->avctx->extradata) {
        ret = av_bsf_receive_packet(sti->extract_extradata.bsf, pkt_ref);
        if (ret < 0) {
            if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
                return ret;
            continue;
        }

        for (int i = 0; i < pkt_ref->side_data_elems; i++) {
            AVPacketSideData *const side_data = &pkt_ref->side_data[i];
            if (side_data->type == AV_PKT_DATA_NEW_EXTRADATA) {
                sti->avctx->extradata      = side_data->data;
                sti->avctx->extradata_size = side_data->size;
                side_data->data = NULL;
                side_data->size = 0;
                break;
            }
        }
        av_packet_unref(pkt_ref);
    }

    return 0;
}

static int add_coded_side_data(AVStream *st, AVCodecContext *avctx)
{
    for (int i = 0; i < avctx->nb_coded_side_data; i++) {
        const AVPacketSideData *const sd_src = &avctx->coded_side_data[i];
        uint8_t *dst_data;
        dst_data = av_stream_new_side_data(st, sd_src->type, sd_src->size);
        if (!dst_data)
            return AVERROR(ENOMEM);
        memcpy(dst_data, sd_src->data, sd_src->size);
    }
    return 0;
}

int avformat_find_stream_info(AVFormatContext *ic, AVDictionary **options)
{
    FFFormatContext *const si = ffformatcontext(ic);
    int count = 0, ret = 0;
    int64_t read_size;
    AVPacket *pkt1 = si->pkt;
    int64_t old_offset  = avio_tell(ic->pb);
    // new streams might appear, no options for those
    int orig_nb_streams = ic->nb_streams;
    int flush_codecs;
    int64_t max_analyze_duration = ic->max_analyze_duration;
    int64_t max_stream_analyze_duration;
    int64_t max_subtitle_analyze_duration;
    int64_t probesize = ic->probesize;
    int eof_reached = 0;
    int *missing_streams = av_opt_ptr(ic->iformat->priv_class, ic->priv_data, "missing_streams");

    flush_codecs = probesize > 0;

    av_opt_set_int(ic, "skip_clear", 1, AV_OPT_SEARCH_CHILDREN);

    max_stream_analyze_duration = max_analyze_duration;
    max_subtitle_analyze_duration = max_analyze_duration;
    if (!max_analyze_duration) {
        max_stream_analyze_duration =
        max_analyze_duration        = 5*AV_TIME_BASE;
        max_subtitle_analyze_duration = 30*AV_TIME_BASE;
        if (!strcmp(ic->iformat->name, "flv"))
            max_stream_analyze_duration = 90*AV_TIME_BASE;
        if (!strcmp(ic->iformat->name, "mpeg") || !strcmp(ic->iformat->name, "mpegts"))
            max_stream_analyze_duration = 7*AV_TIME_BASE;
    }

    if (ic->pb) {
        FFIOContext *const ctx = ffiocontext(ic->pb);
        av_log(ic, AV_LOG_DEBUG, "Before avformat_find_stream_info() pos: %"PRId64" bytes read:%"PRId64" seeks:%d nb_streams:%d\n",
               avio_tell(ic->pb), ctx->bytes_read, ctx->seek_count, ic->nb_streams);
    }

    for (unsigned i = 0; i < ic->nb_streams; i++) {
        const AVCodec *codec;
        AVDictionary *thread_opt = NULL;
        AVStream *const st  = ic->streams[i];
        FFStream *const sti = ffstream(st);
        AVCodecContext *const avctx = sti->avctx;

        if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO ||
            st->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) {
/*            if (!st->time_base.num)
                st->time_base = */
            if (!avctx->time_base.num)
                avctx->time_base = st->time_base;
        }

        /* check if the caller has overridden the codec id */
        // only for the split stuff
        if (!sti->parser && !(ic->flags & AVFMT_FLAG_NOPARSE) && sti->request_probe <= 0) {
            sti->parser = av_parser_init(st->codecpar->codec_id);
            if (sti->parser) {
                if (sti->need_parsing == AVSTREAM_PARSE_HEADERS) {
                    sti->parser->flags |= PARSER_FLAG_COMPLETE_FRAMES;
                } else if (sti->need_parsing == AVSTREAM_PARSE_FULL_RAW) {
                    sti->parser->flags |= PARSER_FLAG_USE_CODEC_TS;
                }
            } else if (sti->need_parsing) {
                av_log(ic, AV_LOG_VERBOSE, "parser not found for codec "
                       "%s, packets or times may be invalid.\n",
                       avcodec_get_name(st->codecpar->codec_id));
            }
        }

        ret = avcodec_parameters_to_context(avctx, st->codecpar);
        if (ret < 0)
            goto find_stream_info_err;
        if (sti->request_probe <= 0)
            sti->avctx_inited = 1;

        codec = find_probe_decoder(ic, st, st->codecpar->codec_id);

        /* Force thread count to 1 since the H.264 decoder will not extract
         * SPS and PPS to extradata during multi-threaded decoding. */
        av_dict_set(options ? &options[i] : &thread_opt, "threads", "1", 0);
        /* Force lowres to 0. The decoder might reduce the video size by the
         * lowres factor, and we don't want that propagated to the stream's
         * codecpar */
        av_dict_set(options ? &options[i] : &thread_opt, "lowres", "0", 0);

        if (ic->codec_whitelist)
            av_dict_set(options ? &options[i] : &thread_opt, "codec_whitelist", ic->codec_whitelist, 0);

        // Try to just open decoders, in case this is enough to get parameters.
        // Also ensure that subtitle_header is properly set.
        if (!has_codec_parameters(st, NULL) && sti->request_probe <= 0 ||
            st->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            if (codec && !avctx->codec)
                if (avcodec_open2(avctx, codec, options ? &options[i] : &thread_opt) < 0)
                    av_log(ic, AV_LOG_WARNING,
                           "Failed to open codec in %s\n",__FUNCTION__);
        }
        if (!options)
            av_dict_free(&thread_opt);
    }

    read_size = 0;
    for (;;) {
        const AVPacket *pkt;
        AVStream *st;
        FFStream *sti;
        AVCodecContext *avctx;
        int analyzed_all_streams;
        unsigned i;
        if (ff_check_interrupt(&ic->interrupt_callback)) {
            ret = AVERROR_EXIT;
            av_log(ic, AV_LOG_DEBUG, "interrupted\n");
            break;
        }

        /* check if one codec still needs to be handled */
        for (i = 0; i < ic->nb_streams; i++) {
            AVStream *const st  = ic->streams[i];
            FFStream *const sti = ffstream(st);
            int fps_analyze_framecount = 20;
            int count;

            if (!has_codec_parameters(st, NULL))
                break;
            /* If the timebase is coarse (like the usual millisecond precision
             * of mkv), we need to analyze more frames to reliably arrive at
             * the correct fps. */
            if (av_q2d(st->time_base) > 0.0005)
                fps_analyze_framecount *= 2;
            if (!tb_unreliable(sti->avctx))
                fps_analyze_framecount = 0;
            if (ic->fps_probe_size >= 0)
                fps_analyze_framecount = ic->fps_probe_size;
            if (st->disposition & AV_DISPOSITION_ATTACHED_PIC)
                fps_analyze_framecount = 0;
            /* variable fps and no guess at the real fps */
            count = (ic->iformat->flags & AVFMT_NOTIMESTAMPS) ?
                       sti->info->codec_info_duration_fields/2 :
                       sti->info->duration_count;
            if (!(st->r_frame_rate.num && st->avg_frame_rate.num) &&
                st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                if (count < fps_analyze_framecount)
                    break;
            }
            // Look at the first 3 frames if there is evidence of frame delay
            // but the decoder delay is not set.
            if (sti->info->frame_delay_evidence && count < 2 && sti->avctx->has_b_frames == 0)
                break;
            if (!sti->avctx->extradata &&
                (!sti->extract_extradata.inited || sti->extract_extradata.bsf) &&
                extract_extradata_check(st))
                break;
            if (sti->first_dts == AV_NOPTS_VALUE &&
                !(ic->iformat->flags & AVFMT_NOTIMESTAMPS) &&
                sti->codec_info_nb_frames < ((st->disposition & AV_DISPOSITION_ATTACHED_PIC) ? 1 : ic->max_ts_probe) &&
                (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO ||
                 st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO))
                break;
        }
        analyzed_all_streams = 0;
        if (!missing_streams || !*missing_streams)
            if (i == ic->nb_streams) {
                analyzed_all_streams = 1;
                /* NOTE: If the format has no header, then we need to read some
                 * packets to get most of the streams, so we cannot stop here. */
                if (!(ic->ctx_flags & AVFMTCTX_NOHEADER)) {
                    /* If we found the info for all the codecs, we can stop. */
                    ret = count;
                    av_log(ic, AV_LOG_DEBUG, "All info found\n");
                    flush_codecs = 0;
                    break;
                }
            }
        /* We did not get all the codec info, but we read too much data. */
        if (read_size >= probesize) {
            ret = count;
            av_log(ic, AV_LOG_DEBUG,
                   "Probe buffer size limit of %"PRId64" bytes reached\n", probesize);
            for (unsigned i = 0; i < ic->nb_streams; i++) {
                AVStream *const st  = ic->streams[i];
                FFStream *const sti = ffstream(st);
                if (!st->r_frame_rate.num &&
                    sti->info->duration_count <= 1 &&
                    st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
                    strcmp(ic->iformat->name, "image2"))
                    av_log(ic, AV_LOG_WARNING,
                           "Stream #%d: not enough frames to estimate rate; "
                           "consider increasing probesize\n", i);
            }
            break;
        }

        /* NOTE: A new stream can be added there if no header in file
         * (AVFMTCTX_NOHEADER). */
        ret = read_frame_internal(ic, pkt1);
        if (ret == AVERROR(EAGAIN))
            continue;

        if (ret < 0) {
            /* EOF or error*/
            eof_reached = 1;
            break;
        }

        if (!(ic->flags & AVFMT_FLAG_NOBUFFER)) {
            ret = avpriv_packet_list_put(&si->packet_buffer,
                                         pkt1, NULL, 0);
            if (ret < 0)
                goto unref_then_goto_end;

            pkt = &si->packet_buffer.tail->pkt;
        } else {
            pkt = pkt1;
        }

        st  = ic->streams[pkt->stream_index];
        sti = ffstream(st);
        if (!(st->disposition & AV_DISPOSITION_ATTACHED_PIC))
            read_size += pkt->size;

        avctx = sti->avctx;
        if (!sti->avctx_inited) {
            ret = avcodec_parameters_to_context(avctx, st->codecpar);
            if (ret < 0)
                goto unref_then_goto_end;
            sti->avctx_inited = 1;
        }

        if (pkt->dts != AV_NOPTS_VALUE && sti->codec_info_nb_frames > 1) {
            /* check for non-increasing dts */
            if (sti->info->fps_last_dts != AV_NOPTS_VALUE &&
                sti->info->fps_last_dts >= pkt->dts) {
                av_log(ic, AV_LOG_DEBUG,
                       "Non-increasing DTS in stream %d: packet %d with DTS "
                       "%"PRId64", packet %d with DTS %"PRId64"\n",
                       st->index, sti->info->fps_last_dts_idx,
                       sti->info->fps_last_dts, sti->codec_info_nb_frames,
                       pkt->dts);
                sti->info->fps_first_dts =
                sti->info->fps_last_dts  = AV_NOPTS_VALUE;
            }
            /* Check for a discontinuity in dts. If the difference in dts
             * is more than 1000 times the average packet duration in the
             * sequence, we treat it as a discontinuity. */
            if (sti->info->fps_last_dts != AV_NOPTS_VALUE &&
                sti->info->fps_last_dts_idx > sti->info->fps_first_dts_idx &&
                (pkt->dts - (uint64_t)sti->info->fps_last_dts) / 1000 >
                (sti->info->fps_last_dts     - (uint64_t)sti->info->fps_first_dts) /
                (sti->info->fps_last_dts_idx - sti->info->fps_first_dts_idx)) {
                av_log(ic, AV_LOG_WARNING,
                       "DTS discontinuity in stream %d: packet %d with DTS "
                       "%"PRId64", packet %d with DTS %"PRId64"\n",
                       st->index, sti->info->fps_last_dts_idx,
                       sti->info->fps_last_dts, sti->codec_info_nb_frames,
                       pkt->dts);
                sti->info->fps_first_dts =
                sti->info->fps_last_dts  = AV_NOPTS_VALUE;
            }

            /* update stored dts values */
            if (sti->info->fps_first_dts == AV_NOPTS_VALUE) {
                sti->info->fps_first_dts     = pkt->dts;
                sti->info->fps_first_dts_idx = sti->codec_info_nb_frames;
            }
            sti->info->fps_last_dts     = pkt->dts;
            sti->info->fps_last_dts_idx = sti->codec_info_nb_frames;
        }
        if (sti->codec_info_nb_frames > 1) {
            int64_t t = 0;
            int64_t limit;

            if (st->time_base.den > 0)
                t = av_rescale_q(sti->info->codec_info_duration, st->time_base, AV_TIME_BASE_Q);
            if (st->avg_frame_rate.num > 0)
                t = FFMAX(t, av_rescale_q(sti->codec_info_nb_frames, av_inv_q(st->avg_frame_rate), AV_TIME_BASE_Q));

            if (   t == 0
                && sti->codec_info_nb_frames > 30
                && sti->info->fps_first_dts != AV_NOPTS_VALUE
                && sti->info->fps_last_dts  != AV_NOPTS_VALUE) {
                int64_t dur = av_sat_sub64(sti->info->fps_last_dts, sti->info->fps_first_dts);
                t = FFMAX(t, av_rescale_q(dur, st->time_base, AV_TIME_BASE_Q));
            }

            if (analyzed_all_streams)                                limit = max_analyze_duration;
            else if (avctx->codec_type == AVMEDIA_TYPE_SUBTITLE) limit = max_subtitle_analyze_duration;
            else                                                     limit = max_stream_analyze_duration;

            if (t >= limit) {
                av_log(ic, AV_LOG_VERBOSE, "max_analyze_duration %"PRId64" reached at %"PRId64" microseconds st:%d\n",
                       limit,
                       t, pkt->stream_index);
                if (ic->flags & AVFMT_FLAG_NOBUFFER)
                    av_packet_unref(pkt1);
                break;
            }
            if (pkt->duration > 0) {
                if (avctx->codec_type == AVMEDIA_TYPE_SUBTITLE && pkt->pts != AV_NOPTS_VALUE && st->start_time != AV_NOPTS_VALUE && pkt->pts >= st->start_time
                    && (uint64_t)pkt->pts - st->start_time < INT64_MAX
                ) {
                    sti->info->codec_info_duration = FFMIN(pkt->pts - st->start_time, sti->info->codec_info_duration + pkt->duration);
                } else
                    sti->info->codec_info_duration += pkt->duration;
                sti->info->codec_info_duration_fields += sti->parser && sti->need_parsing && avctx->ticks_per_frame == 2
                                                         ? sti->parser->repeat_pict + 1 : 2;
            }
        }
        if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
#if FF_API_R_FRAME_RATE
            ff_rfps_add_frame(ic, st, pkt->dts);
#endif
            if (pkt->dts != pkt->pts && pkt->dts != AV_NOPTS_VALUE && pkt->pts != AV_NOPTS_VALUE)
                sti->info->frame_delay_evidence = 1;
        }
        if (!sti->avctx->extradata) {
            ret = extract_extradata(si, st, pkt);
            if (ret < 0)
                goto unref_then_goto_end;
        }

        /* If still no information, we try to open the codec and to
         * decompress the frame. We try to avoid that in most cases as
         * it takes longer and uses more memory. For MPEG-4, we need to
         * decompress for QuickTime.
         *
         * If AV_CODEC_CAP_CHANNEL_CONF is set this will force decoding of at
         * least one frame of codec data, this makes sure the codec initializes
         * the channel configuration and does not only trust the values from
         * the container. */
        try_decode_frame(ic, st, pkt,
                         (options && i < orig_nb_streams) ? &options[i] : NULL);

        if (ic->flags & AVFMT_FLAG_NOBUFFER)
            av_packet_unref(pkt1);

        sti->codec_info_nb_frames++;
        count++;
    }

    if (eof_reached) {
        for (unsigned stream_index = 0; stream_index < ic->nb_streams; stream_index++) {
            AVStream *const st = ic->streams[stream_index];
            AVCodecContext *const avctx = ffstream(st)->avctx;
            if (!has_codec_parameters(st, NULL)) {
                const AVCodec *codec = find_probe_decoder(ic, st, st->codecpar->codec_id);
                if (codec && !avctx->codec) {
                    AVDictionary *opts = NULL;
                    if (ic->codec_whitelist)
                        av_dict_set(&opts, "codec_whitelist", ic->codec_whitelist, 0);
                    if (avcodec_open2(avctx, codec, (options && stream_index < orig_nb_streams) ? &options[stream_index] : &opts) < 0)
                        av_log(ic, AV_LOG_WARNING,
                               "Failed to open codec in %s\n",__FUNCTION__);
                    av_dict_free(&opts);
                }
            }

            // EOF already reached while reading the stream above.
            // So continue with reoordering DTS with whatever delay we have.
            if (si->packet_buffer.head && !has_decode_delay_been_guessed(st)) {
                update_dts_from_pts(ic, stream_index, si->packet_buffer.head);
            }
        }
    }

    if (flush_codecs) {
        AVPacket *empty_pkt = si->pkt;
        int err = 0;
        av_packet_unref(empty_pkt);

        for (unsigned i = 0; i < ic->nb_streams; i++) {
            AVStream *const st  = ic->streams[i];
            FFStream *const sti = ffstream(st);

            /* flush the decoders */
            if (sti->info->found_decoder == 1) {
                err = try_decode_frame(ic, st, empty_pkt,
                                        (options && i < orig_nb_streams)
                                        ? &options[i] : NULL);

                if (err < 0) {
                    av_log(ic, AV_LOG_INFO,
                        "decoding for stream %d failed\n", st->index);
                }
            }
        }
    }

    ff_rfps_calculate(ic);

    for (unsigned i = 0; i < ic->nb_streams; i++) {
        AVStream *const st  = ic->streams[i];
        FFStream *const sti = ffstream(st);
        AVCodecContext *const avctx = sti->avctx;

        if (avctx->codec_type == AVMEDIA_TYPE_VIDEO) {
            if (avctx->codec_id == AV_CODEC_ID_RAWVIDEO && !avctx->codec_tag && !avctx->bits_per_coded_sample) {
                uint32_t tag= avcodec_pix_fmt_to_codec_tag(avctx->pix_fmt);
                if (avpriv_pix_fmt_find(PIX_FMT_LIST_RAW, tag) == avctx->pix_fmt)
                    avctx->codec_tag= tag;
            }

            /* estimate average framerate if not set by demuxer */
            if (sti->info->codec_info_duration_fields &&
                !st->avg_frame_rate.num &&
                sti->info->codec_info_duration) {
                int best_fps      = 0;
                double best_error = 0.01;
                AVRational codec_frame_rate = avctx->framerate;

                if (sti->info->codec_info_duration        >= INT64_MAX / st->time_base.num / 2||
                    sti->info->codec_info_duration_fields >= INT64_MAX / st->time_base.den ||
                    sti->info->codec_info_duration        < 0)
                    continue;
                av_reduce(&st->avg_frame_rate.num, &st->avg_frame_rate.den,
                          sti->info->codec_info_duration_fields * (int64_t) st->time_base.den,
                          sti->info->codec_info_duration * 2 * (int64_t) st->time_base.num, 60000);

                /* Round guessed framerate to a "standard" framerate if it's
                 * within 1% of the original estimate. */
                for (int j = 0; j < MAX_STD_TIMEBASES; j++) {
                    AVRational std_fps = { get_std_framerate(j), 12 * 1001 };
                    double error       = fabs(av_q2d(st->avg_frame_rate) /
                                              av_q2d(std_fps) - 1);

                    if (error < best_error) {
                        best_error = error;
                        best_fps   = std_fps.num;
                    }

                    if (si->prefer_codec_framerate && codec_frame_rate.num > 0 && codec_frame_rate.den > 0) {
                        error       = fabs(av_q2d(codec_frame_rate) /
                                           av_q2d(std_fps) - 1);
                        if (error < best_error) {
                            best_error = error;
                            best_fps   = std_fps.num;
                        }
                    }
                }
                if (best_fps)
                    av_reduce(&st->avg_frame_rate.num, &st->avg_frame_rate.den,
                              best_fps, 12 * 1001, INT_MAX);
            }

            if (!st->r_frame_rate.num) {
                if (    avctx->time_base.den * (int64_t) st->time_base.num
                    <= avctx->time_base.num * (uint64_t)avctx->ticks_per_frame * st->time_base.den) {
                    av_reduce(&st->r_frame_rate.num, &st->r_frame_rate.den,
                              avctx->time_base.den, (int64_t)avctx->time_base.num * avctx->ticks_per_frame, INT_MAX);
                } else {
                    st->r_frame_rate.num = st->time_base.den;
                    st->r_frame_rate.den = st->time_base.num;
                }
            }
            if (sti->display_aspect_ratio.num && sti->display_aspect_ratio.den) {
                AVRational hw_ratio = { avctx->height, avctx->width };
                st->sample_aspect_ratio = av_mul_q(sti->display_aspect_ratio,
                                                   hw_ratio);
            }
        } else if (avctx->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (!avctx->bits_per_coded_sample)
                avctx->bits_per_coded_sample =
                    av_get_bits_per_sample(avctx->codec_id);
            // set stream disposition based on audio service type
            switch (avctx->audio_service_type) {
            case AV_AUDIO_SERVICE_TYPE_EFFECTS:
                st->disposition = AV_DISPOSITION_CLEAN_EFFECTS;
                break;
            case AV_AUDIO_SERVICE_TYPE_VISUALLY_IMPAIRED:
                st->disposition = AV_DISPOSITION_VISUAL_IMPAIRED;
                break;
            case AV_AUDIO_SERVICE_TYPE_HEARING_IMPAIRED:
                st->disposition = AV_DISPOSITION_HEARING_IMPAIRED;
                break;
            case AV_AUDIO_SERVICE_TYPE_COMMENTARY:
                st->disposition = AV_DISPOSITION_COMMENT;
                break;
            case AV_AUDIO_SERVICE_TYPE_KARAOKE:
                st->disposition = AV_DISPOSITION_KARAOKE;
                break;
            }
        }
    }

    if (probesize)
        estimate_timings(ic, old_offset);

    av_opt_set_int(ic, "skip_clear", 0, AV_OPT_SEARCH_CHILDREN);

    if (ret >= 0 && ic->nb_streams)
        /* We could not have all the codec parameters before EOF. */
        ret = -1;
    for (unsigned i = 0; i < ic->nb_streams; i++) {
        AVStream *const st  = ic->streams[i];
        FFStream *const sti = ffstream(st);
        const char *errmsg;

        /* if no packet was ever seen, update context now for has_codec_parameters */
        if (!sti->avctx_inited) {
            if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO &&
                st->codecpar->format == AV_SAMPLE_FMT_NONE)
                st->codecpar->format = sti->avctx->sample_fmt;
            ret = avcodec_parameters_to_context(sti->avctx, st->codecpar);
            if (ret < 0)
                goto find_stream_info_err;
        }
        if (!has_codec_parameters(st, &errmsg)) {
            char buf[256];
            avcodec_string(buf, sizeof(buf), sti->avctx, 0);
            av_log(ic, AV_LOG_WARNING,
                   "Could not find codec parameters for stream %d (%s): %s\n"
                   "Consider increasing the value for the 'analyzeduration' (%"PRId64") and 'probesize' (%"PRId64") options\n",
                   i, buf, errmsg, ic->max_analyze_duration, ic->probesize);
        } else {
            ret = 0;
        }
    }

    ret = compute_chapters_end(ic);
    if (ret < 0)
        goto find_stream_info_err;

    /* update the stream parameters from the internal codec contexts */
    for (unsigned i = 0; i < ic->nb_streams; i++) {
        AVStream *const st  = ic->streams[i];
        FFStream *const sti = ffstream(st);

        if (sti->avctx_inited) {
            ret = avcodec_parameters_from_context(st->codecpar, sti->avctx);
            if (ret < 0)
                goto find_stream_info_err;
            ret = add_coded_side_data(st, sti->avctx);
            if (ret < 0)
                goto find_stream_info_err;
        }

        sti->avctx_inited = 0;
    }

find_stream_info_err:
    for (unsigned i = 0; i < ic->nb_streams; i++) {
        AVStream *const st  = ic->streams[i];
        FFStream *const sti = ffstream(st);
        if (sti->info) {
            av_freep(&sti->info->duration_error);
            av_freep(&sti->info);
        }
        avcodec_close(sti->avctx);
        av_bsf_free(&sti->extract_extradata.bsf);
    }
    if (ic->pb) {
        FFIOContext *const ctx = ffiocontext(ic->pb);
        av_log(ic, AV_LOG_DEBUG, "After avformat_find_stream_info() pos: %"PRId64" bytes read:%"PRId64" seeks:%d frames:%d\n",
               avio_tell(ic->pb), ctx->bytes_read, ctx->seek_count, count);
    }
    return ret;

unref_then_goto_end:
    av_packet_unref(pkt1);
    goto find_stream_info_err;
}
