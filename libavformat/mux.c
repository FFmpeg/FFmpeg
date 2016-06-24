/*
 * muxing functions for use within Libav
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "avformat.h"
#include "avio_internal.h"
#include "internal.h"
#include "libavcodec/internal.h"
#include "libavcodec/bytestream.h"
#include "libavutil/opt.h"
#include "libavutil/dict.h"
#include "libavutil/pixdesc.h"
#include "metadata.h"
#include "id3v2.h"
#include "libavutil/avstring.h"
#include "libavutil/internal.h"
#include "libavutil/mathematics.h"
#include "libavutil/parseutils.h"
#include "libavutil/time.h"
#include "riff.h"
#include "audiointerleave.h"
#include "url.h"
#include <stdarg.h>
#if CONFIG_NETWORK
#include "network.h"
#endif

#undef NDEBUG
#include <assert.h>

/**
 * @file
 * muxing functions for use within Libav
 */

static int validate_codec_tag(AVFormatContext *s, AVStream *st)
{
    const AVCodecTag *avctag;
    int n;
    enum AVCodecID id = AV_CODEC_ID_NONE;
    unsigned int tag  = 0;

    /**
     * Check that tag + id is in the table
     * If neither is in the table -> OK
     * If tag is in the table with another id -> FAIL
     * If id is in the table with another tag -> FAIL unless strict < normal
     */
    for (n = 0; s->oformat->codec_tag[n]; n++) {
        avctag = s->oformat->codec_tag[n];
        while (avctag->id != AV_CODEC_ID_NONE) {
            if (avpriv_toupper4(avctag->tag) == avpriv_toupper4(st->codecpar->codec_tag)) {
                id = avctag->id;
                if (id == st->codecpar->codec_id)
                    return 1;
            }
            if (avctag->id == st->codecpar->codec_id)
                tag = avctag->tag;
            avctag++;
        }
    }
    if (id != AV_CODEC_ID_NONE)
        return 0;
    if (tag && (s->strict_std_compliance >= FF_COMPLIANCE_NORMAL))
        return 0;
    return 1;
}


static int init_muxer(AVFormatContext *s, AVDictionary **options)
{
    int ret = 0, i;
    AVStream *st;
    AVDictionary *tmp = NULL;
    AVCodecParameters *par = NULL;
    AVOutputFormat *of = s->oformat;
    const AVCodecDescriptor *desc;

    if (options)
        av_dict_copy(&tmp, *options, 0);

    if ((ret = av_opt_set_dict(s, &tmp)) < 0)
        goto fail;

#if FF_API_LAVF_BITEXACT && FF_API_LAVF_AVCTX
FF_DISABLE_DEPRECATION_WARNINGS
    if (s->nb_streams && s->streams[0]->codec->flags & AV_CODEC_FLAG_BITEXACT)
        s->flags |= AVFMT_FLAG_BITEXACT;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

    // some sanity checks
    if (s->nb_streams == 0 && !(of->flags & AVFMT_NOSTREAMS)) {
        av_log(s, AV_LOG_ERROR, "no streams\n");
        ret = AVERROR(EINVAL);
        goto fail;
    }

    for (i = 0; i < s->nb_streams; i++) {
        st  = s->streams[i];
        par = st->codecpar;

#if FF_API_LAVF_CODEC_TB && FF_API_LAVF_AVCTX
FF_DISABLE_DEPRECATION_WARNINGS
        if (!st->time_base.num && st->codec->time_base.num) {
            av_log(s, AV_LOG_WARNING, "Using AVStream.codec.time_base as a "
                   "timebase hint to the muxer is deprecated. Set "
                   "AVStream.time_base instead.\n");
            avpriv_set_pts_info(st, 64, st->codec->time_base.num, st->codec->time_base.den);
        }
FF_ENABLE_DEPRECATION_WARNINGS
#endif

#if FF_API_LAVF_AVCTX
FF_DISABLE_DEPRECATION_WARNINGS
        if (st->codecpar->codec_type == AVMEDIA_TYPE_UNKNOWN &&
            st->codec->codec_type    != AVMEDIA_TYPE_UNKNOWN) {
            av_log(s, AV_LOG_WARNING, "Using AVStream.codec to pass codec "
                   "parameters to muxers is deprecated, use AVStream.codecpar "
                   "instead.\n");
            ret = avcodec_parameters_from_context(st->codecpar, st->codec);
            if (ret < 0)
                goto fail;
        }
FF_ENABLE_DEPRECATION_WARNINGS
#endif

        if (!st->time_base.num) {
            /* fall back on the default timebase values */
            if (par->codec_type == AVMEDIA_TYPE_AUDIO && par->sample_rate)
                avpriv_set_pts_info(st, 64, 1, par->sample_rate);
            else
                avpriv_set_pts_info(st, 33, 1, 90000);
        }

        switch (par->codec_type) {
        case AVMEDIA_TYPE_AUDIO:
            if (par->sample_rate <= 0) {
                av_log(s, AV_LOG_ERROR, "sample rate not set\n");
                ret = AVERROR(EINVAL);
                goto fail;
            }
            if (!par->block_align)
                par->block_align = par->channels *
                                   av_get_bits_per_sample(par->codec_id) >> 3;
            break;
        case AVMEDIA_TYPE_VIDEO:
            if ((par->width <= 0 || par->height <= 0) &&
                !(of->flags & AVFMT_NODIMENSIONS)) {
                av_log(s, AV_LOG_ERROR, "dimensions not set\n");
                ret = AVERROR(EINVAL);
                goto fail;
            }

            if (av_cmp_q(st->sample_aspect_ratio,
                         par->sample_aspect_ratio)) {
                if (st->sample_aspect_ratio.num != 0 &&
                    st->sample_aspect_ratio.den != 0 &&
                    par->sample_aspect_ratio.den != 0 &&
                    par->sample_aspect_ratio.den != 0) {
                    av_log(s, AV_LOG_ERROR, "Aspect ratio mismatch between muxer "
                            "(%d/%d) and encoder layer (%d/%d)\n",
                            st->sample_aspect_ratio.num, st->sample_aspect_ratio.den,
                            par->sample_aspect_ratio.num,
                            par->sample_aspect_ratio.den);
                    ret = AVERROR(EINVAL);
                    goto fail;
                }
            }
            break;
        }

        desc = avcodec_descriptor_get(par->codec_id);
        if (desc && desc->props & AV_CODEC_PROP_REORDER)
            st->internal->reorder = 1;

        if (of->codec_tag) {
            if (par->codec_tag &&
                par->codec_id == AV_CODEC_ID_RAWVIDEO &&
                !av_codec_get_tag(of->codec_tag, par->codec_id) &&
                !validate_codec_tag(s, st)) {
                // the current rawvideo encoding system ends up setting
                // the wrong codec_tag for avi, we override it here
                par->codec_tag = 0;
            }
            if (par->codec_tag) {
                if (!validate_codec_tag(s, st)) {
                    char tagbuf[32];
                    av_get_codec_tag_string(tagbuf, sizeof(tagbuf), par->codec_tag);
                    av_log(s, AV_LOG_ERROR,
                           "Tag %s/0x%08x incompatible with output codec id '%d'\n",
                           tagbuf, par->codec_tag, par->codec_id);
                    ret = AVERROR_INVALIDDATA;
                    goto fail;
                }
            } else
                par->codec_tag = av_codec_get_tag(of->codec_tag, par->codec_id);
        }

        if (par->codec_type != AVMEDIA_TYPE_ATTACHMENT)
            s->internal->nb_interleaved_streams++;
    }

    if (!s->priv_data && of->priv_data_size > 0) {
        s->priv_data = av_mallocz(of->priv_data_size);
        if (!s->priv_data) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        if (of->priv_class) {
            *(const AVClass **)s->priv_data = of->priv_class;
            av_opt_set_defaults(s->priv_data);
            if ((ret = av_opt_set_dict(s->priv_data, &tmp)) < 0)
                goto fail;
        }
    }

    /* set muxer identification string */
    if (!(s->flags & AVFMT_FLAG_BITEXACT)) {
        av_dict_set(&s->metadata, "encoder", LIBAVFORMAT_IDENT, 0);
    }

    if (options) {
         av_dict_free(options);
         *options = tmp;
    }

    return 0;

fail:
    av_dict_free(&tmp);
    return ret;
}

int avformat_write_header(AVFormatContext *s, AVDictionary **options)
{
    int ret = 0;

    if (ret = init_muxer(s, options))
        return ret;

    if (!(s->oformat->flags & AVFMT_NOFILE) && s->pb)
        avio_write_marker(s->pb, AV_NOPTS_VALUE, AVIO_DATA_MARKER_HEADER);
    if (s->oformat->write_header) {
        ret = s->oformat->write_header(s);
        if (ret < 0)
            return ret;
    }
    if (!(s->oformat->flags & AVFMT_NOFILE) && s->pb)
        avio_write_marker(s->pb, AV_NOPTS_VALUE, AVIO_DATA_MARKER_UNKNOWN);

    if (s->avoid_negative_ts == AVFMT_AVOID_NEG_TS_AUTO) {
        if (s->oformat->flags & (AVFMT_TS_NEGATIVE | AVFMT_NOTIMESTAMPS)) {
            s->avoid_negative_ts = 0;
        } else
            s->avoid_negative_ts = AVFMT_AVOID_NEG_TS_MAKE_NON_NEGATIVE;
    }

    return 0;
}

#if FF_API_COMPUTE_PKT_FIELDS2 && FF_API_LAVF_AVCTX
FF_DISABLE_DEPRECATION_WARNINGS
//FIXME merge with compute_pkt_fields
static int compute_pkt_fields2(AVFormatContext *s, AVStream *st, AVPacket *pkt)
{
    int delay = FFMAX(st->codec->has_b_frames, !!st->codec->max_b_frames);
    int num, den, i;

    if (!s->internal->missing_ts_warning &&
        !(s->oformat->flags & AVFMT_NOTIMESTAMPS) &&
        (pkt->pts == AV_NOPTS_VALUE || pkt->dts == AV_NOPTS_VALUE)) {
        av_log(s, AV_LOG_WARNING,
               "Timestamps are unset in a packet for stream %d. "
               "This is deprecated and will stop working in the future. "
               "Fix your code to set the timestamps properly\n", st->index);
        s->internal->missing_ts_warning = 1;
    }

    av_log(s, AV_LOG_TRACE, "compute_pkt_fields2: pts:%" PRId64 " dts:%" PRId64 " cur_dts:%" PRId64 " b:%d size:%d st:%d\n",
            pkt->pts, pkt->dts, st->cur_dts, delay, pkt->size, pkt->stream_index);

/*    if(pkt->pts == AV_NOPTS_VALUE && pkt->dts == AV_NOPTS_VALUE)
 *      return AVERROR(EINVAL);*/

    /* duration field */
    if (pkt->duration == 0) {
        ff_compute_frame_duration(s, &num, &den, st, NULL, pkt);
        if (den && num) {
            pkt->duration = av_rescale(1, num * (int64_t)st->time_base.den * st->codec->ticks_per_frame, den * (int64_t)st->time_base.num);
        }
    }

    if (pkt->pts == AV_NOPTS_VALUE && pkt->dts != AV_NOPTS_VALUE && delay == 0)
        pkt->pts = pkt->dts;

    //calculate dts from pts
    if (pkt->pts != AV_NOPTS_VALUE && pkt->dts == AV_NOPTS_VALUE && delay <= MAX_REORDER_DELAY) {
        st->pts_buffer[0] = pkt->pts;
        for (i = 1; i < delay + 1 && st->pts_buffer[i] == AV_NOPTS_VALUE; i++)
            st->pts_buffer[i] = pkt->pts + (i - delay - 1) * pkt->duration;
        for (i = 0; i<delay && st->pts_buffer[i] > st->pts_buffer[i + 1]; i++)
            FFSWAP(int64_t, st->pts_buffer[i], st->pts_buffer[i + 1]);

        pkt->dts = st->pts_buffer[0];
    }

    if (st->cur_dts && st->cur_dts != AV_NOPTS_VALUE &&
        ((!(s->oformat->flags & AVFMT_TS_NONSTRICT) &&
          st->cur_dts >= pkt->dts) || st->cur_dts > pkt->dts)) {
        av_log(s, AV_LOG_ERROR,
               "Application provided invalid, non monotonically increasing dts to muxer in stream %d: %" PRId64 " >= %" PRId64 "\n",
               st->index, st->cur_dts, pkt->dts);
        return AVERROR(EINVAL);
    }
    if (pkt->dts != AV_NOPTS_VALUE && pkt->pts != AV_NOPTS_VALUE && pkt->pts < pkt->dts) {
        av_log(s, AV_LOG_ERROR,
               "pts %" PRId64 " < dts %" PRId64 " in stream %d\n",
               pkt->pts, pkt->dts,
               st->index);
        return AVERROR(EINVAL);
    }

    av_log(s, AV_LOG_TRACE, "av_write_frame: pts2:%"PRId64" dts2:%"PRId64"\n",
            pkt->pts, pkt->dts);
    st->cur_dts = pkt->dts;

    return 0;
}
FF_ENABLE_DEPRECATION_WARNINGS
#endif

/*
 * FIXME: this function should NEVER get undefined pts/dts beside when the
 * AVFMT_NOTIMESTAMPS is set.
 * Those additional safety checks should be dropped once the correct checks
 * are set in the callers.
 */

static int write_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret;
    // If the timestamp offsetting below is adjusted, adjust
    // ff_interleaved_peek similarly.
    if (s->avoid_negative_ts > 0) {
        AVRational time_base = s->streams[pkt->stream_index]->time_base;
        int64_t offset = 0;

        if (s->internal->offset == AV_NOPTS_VALUE && pkt->dts != AV_NOPTS_VALUE &&
            (pkt->dts < 0 || s->avoid_negative_ts == AVFMT_AVOID_NEG_TS_MAKE_ZERO)) {
            s->internal->offset = -pkt->dts;
            s->internal->offset_timebase = time_base;
        }
        if (s->internal->offset != AV_NOPTS_VALUE)
            offset = av_rescale_q(s->internal->offset, s->internal->offset_timebase, time_base);

        if (pkt->dts != AV_NOPTS_VALUE)
            pkt->dts += offset;
        if (pkt->pts != AV_NOPTS_VALUE)
            pkt->pts += offset;

        if (pkt->dts != AV_NOPTS_VALUE && pkt->dts < 0) {
            av_log(s, AV_LOG_WARNING,
                   "Packets poorly interleaved, failed to avoid negative "
                   "timestamp %"PRId64" in stream %d.\n"
                   "Try -max_interleave_delta 0 as a possible workaround.\n",
                   pkt->dts, pkt->stream_index);
        }
    }
    ret = s->oformat->write_packet(s, pkt);

    if (s->pb && ret >= 0) {
        if (s->flags & AVFMT_FLAG_FLUSH_PACKETS)
            avio_flush(s->pb);
        if (s->pb->error < 0)
            ret = s->pb->error;
    }

    return ret;
}

static int check_packet(AVFormatContext *s, AVPacket *pkt)
{
    if (!pkt)
        return 0;

    if (pkt->stream_index < 0 || pkt->stream_index >= s->nb_streams) {
        av_log(s, AV_LOG_ERROR, "Invalid packet stream index: %d\n",
               pkt->stream_index);
        return AVERROR(EINVAL);
    }

    if (s->streams[pkt->stream_index]->codecpar->codec_type == AVMEDIA_TYPE_ATTACHMENT) {
        av_log(s, AV_LOG_ERROR, "Received a packet for an attachment stream.\n");
        return AVERROR(EINVAL);
    }

    return 0;
}

static int prepare_input_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret;

    ret = check_packet(s, pkt);
    if (ret < 0)
        return ret;

#if !FF_API_COMPUTE_PKT_FIELDS2 && FF_API_LAVF_AVCTX
    /* sanitize the timestamps */
    if (!(s->oformat->flags & AVFMT_NOTIMESTAMPS)) {
        AVStream *st = s->streams[pkt->stream_index];

        /* when there is no reordering (so dts is equal to pts), but
         * only one of them is set, set the other as well */
        if (!st->internal->reorder) {
            if (pkt->pts == AV_NOPTS_VALUE && pkt->dts != AV_NOPTS_VALUE)
                pkt->pts = pkt->dts;
            if (pkt->dts == AV_NOPTS_VALUE && pkt->pts != AV_NOPTS_VALUE)
                pkt->dts = pkt->pts;
        }

        /* check that the timestamps are set */
        if (pkt->pts == AV_NOPTS_VALUE || pkt->dts == AV_NOPTS_VALUE) {
            av_log(s, AV_LOG_ERROR,
                   "Timestamps are unset in a packet for stream %d\n", st->index);
            return AVERROR(EINVAL);
        }

        /* check that the dts are increasing (or at least non-decreasing,
         * if the format allows it */
        if (st->cur_dts != AV_NOPTS_VALUE &&
            ((!(s->oformat->flags & AVFMT_TS_NONSTRICT) && st->cur_dts >= pkt->dts) ||
             st->cur_dts > pkt->dts)) {
            av_log(s, AV_LOG_ERROR,
                   "Application provided invalid, non monotonically increasing "
                   "dts to muxer in stream %d: %" PRId64 " >= %" PRId64 "\n",
                   st->index, st->cur_dts, pkt->dts);
            return AVERROR(EINVAL);
        }

        if (pkt->pts < pkt->dts) {
            av_log(s, AV_LOG_ERROR, "pts %" PRId64 " < dts %" PRId64 " in stream %d\n",
                   pkt->pts, pkt->dts, st->index);
            return AVERROR(EINVAL);
        }
    }
#endif

    return 0;
}

int av_write_frame(AVFormatContext *s, AVPacket *pkt)
{
    int ret;

    ret = prepare_input_packet(s, pkt);
    if (ret < 0)
        return ret;

    if (!pkt) {
        if (s->oformat->flags & AVFMT_ALLOW_FLUSH)
            return s->oformat->write_packet(s, pkt);
        return 1;
    }

#if FF_API_COMPUTE_PKT_FIELDS2 && FF_API_LAVF_AVCTX
    ret = compute_pkt_fields2(s, s->streams[pkt->stream_index], pkt);

    if (ret < 0 && !(s->oformat->flags & AVFMT_NOTIMESTAMPS))
        return ret;
#endif

    ret = write_packet(s, pkt);

    if (ret >= 0)
        s->streams[pkt->stream_index]->nb_frames++;
    return ret;
}

int ff_interleave_add_packet(AVFormatContext *s, AVPacket *pkt,
                             int (*compare)(AVFormatContext *, AVPacket *, AVPacket *))
{
    int ret;
    AVPacketList **next_point, *this_pktl;

    this_pktl      = av_mallocz(sizeof(AVPacketList));
    if (!this_pktl)
        return AVERROR(ENOMEM);

    if ((ret = av_packet_ref(&this_pktl->pkt, pkt)) < 0) {
        av_free(this_pktl);
        return ret;
    }

    if (s->streams[pkt->stream_index]->last_in_packet_buffer) {
        next_point = &(s->streams[pkt->stream_index]->last_in_packet_buffer->next);
    } else
        next_point = &s->internal->packet_buffer;

    if (*next_point) {
        if (compare(s, &s->internal->packet_buffer_end->pkt, pkt)) {
            while (!compare(s, &(*next_point)->pkt, pkt))
                next_point = &(*next_point)->next;
            goto next_non_null;
        } else {
            next_point = &(s->internal->packet_buffer_end->next);
        }
    }
    assert(!*next_point);

    s->internal->packet_buffer_end = this_pktl;
next_non_null:

    this_pktl->next = *next_point;

    s->streams[pkt->stream_index]->last_in_packet_buffer =
        *next_point                                      = this_pktl;

    av_packet_unref(pkt);

    return 0;
}

static int interleave_compare_dts(AVFormatContext *s, AVPacket *next,
                                  AVPacket *pkt)
{
    AVStream *st  = s->streams[pkt->stream_index];
    AVStream *st2 = s->streams[next->stream_index];
    int comp      = av_compare_ts(next->dts, st2->time_base, pkt->dts,
                                  st->time_base);

    if (comp == 0)
        return pkt->stream_index < next->stream_index;
    return comp > 0;
}

int ff_interleave_packet_per_dts(AVFormatContext *s, AVPacket *out,
                                 AVPacket *pkt, int flush)
{
    AVPacketList *pktl;
    int stream_count = 0;
    int i, ret;

    if (pkt) {
        if ((ret = ff_interleave_add_packet(s, pkt, interleave_compare_dts)) < 0)
            return ret;
    }

    if (s->max_interleave_delta > 0 && s->internal->packet_buffer && !flush) {
        AVPacket *top_pkt = &s->internal->packet_buffer->pkt;
        int64_t delta_dts = INT64_MIN;
        int64_t top_dts = av_rescale_q(top_pkt->dts,
                                       s->streams[top_pkt->stream_index]->time_base,
                                       AV_TIME_BASE_Q);

        for (i = 0; i < s->nb_streams; i++) {
            int64_t last_dts;
            const AVPacketList *last = s->streams[i]->last_in_packet_buffer;

            if (!last)
                continue;

            last_dts = av_rescale_q(last->pkt.dts,
                                    s->streams[i]->time_base,
                                    AV_TIME_BASE_Q);
            delta_dts = FFMAX(delta_dts, last_dts - top_dts);
            stream_count++;
        }

        if (delta_dts > s->max_interleave_delta) {
            av_log(s, AV_LOG_DEBUG,
                   "Delay between the first packet and last packet in the "
                   "muxing queue is %"PRId64" > %"PRId64": forcing output\n",
                   delta_dts, s->max_interleave_delta);
            flush = 1;
        }
    } else {
        for (i = 0; i < s->nb_streams; i++)
            stream_count += !!s->streams[i]->last_in_packet_buffer;
    }


    if (stream_count && (s->internal->nb_interleaved_streams == stream_count || flush)) {
        pktl = s->internal->packet_buffer;
        *out = pktl->pkt;

        s->internal->packet_buffer = pktl->next;
        if (!s->internal->packet_buffer)
            s->internal->packet_buffer_end = NULL;

        if (s->streams[out->stream_index]->last_in_packet_buffer == pktl)
            s->streams[out->stream_index]->last_in_packet_buffer = NULL;
        av_freep(&pktl);
        return 1;
    } else {
        av_init_packet(out);
        return 0;
    }
}

int ff_interleaved_peek(AVFormatContext *s, int stream,
                        AVPacket *pkt, int add_offset)
{
    AVPacketList *pktl = s->internal->packet_buffer;
    while (pktl) {
        if (pktl->pkt.stream_index == stream) {
            *pkt = pktl->pkt;
            if (add_offset && s->internal->offset != AV_NOPTS_VALUE) {
                int64_t offset = av_rescale_q(s->internal->offset,
                                              s->internal->offset_timebase,
                                              s->streams[stream]->time_base);
                if (pkt->dts != AV_NOPTS_VALUE)
                    pkt->dts += offset;
                if (pkt->pts != AV_NOPTS_VALUE)
                    pkt->pts += offset;
            }
            return 0;
        }
        pktl = pktl->next;
    }
    return AVERROR(ENOENT);
}

/**
 * Interleave an AVPacket correctly so it can be muxed.
 * @param out the interleaved packet will be output here
 * @param in the input packet
 * @param flush 1 if no further packets are available as input and all
 *              remaining packets should be output
 * @return 1 if a packet was output, 0 if no packet could be output,
 *         < 0 if an error occurred
 */
static int interleave_packet(AVFormatContext *s, AVPacket *out, AVPacket *in, int flush)
{
    if (s->oformat->interleave_packet) {
        int ret = s->oformat->interleave_packet(s, out, in, flush);
        if (in)
            av_packet_unref(in);
        return ret;
    } else
        return ff_interleave_packet_per_dts(s, out, in, flush);
}

int av_interleaved_write_frame(AVFormatContext *s, AVPacket *pkt)
{
    int ret, flush = 0;

    ret = prepare_input_packet(s, pkt);
    if (ret < 0)
        goto fail;

    if (pkt) {
#if FF_API_COMPUTE_PKT_FIELDS2 && FF_API_LAVF_AVCTX
        AVStream *st = s->streams[pkt->stream_index];

        av_log(s, AV_LOG_TRACE, "av_interleaved_write_frame size:%d dts:%" PRId64 " pts:%" PRId64 "\n",
                pkt->size, pkt->dts, pkt->pts);
        if ((ret = compute_pkt_fields2(s, st, pkt)) < 0 && !(s->oformat->flags & AVFMT_NOTIMESTAMPS))
            goto fail;
#endif

        if (pkt->dts == AV_NOPTS_VALUE && !(s->oformat->flags & AVFMT_NOTIMESTAMPS)) {
            ret = AVERROR(EINVAL);
            goto fail;
        }
    } else {
        av_log(s, AV_LOG_TRACE, "av_interleaved_write_frame FLUSH\n");
        flush = 1;
    }

    for (;; ) {
        AVPacket opkt;
        int ret = interleave_packet(s, &opkt, pkt, flush);
        if (pkt) {
            memset(pkt, 0, sizeof(*pkt));
            av_init_packet(pkt);
            pkt = NULL;
        }
        if (ret <= 0) //FIXME cleanup needed for ret<0 ?
            return ret;

        ret = write_packet(s, &opkt);
        if (ret >= 0)
            s->streams[opkt.stream_index]->nb_frames++;

        av_packet_unref(&opkt);

        if (ret < 0)
            return ret;
    }
fail:
    av_packet_unref(pkt);
    return ret;
}

int av_write_trailer(AVFormatContext *s)
{
    int ret, i;

    for (;; ) {
        AVPacket pkt;
        ret = interleave_packet(s, &pkt, NULL, 1);
        if (ret < 0) //FIXME cleanup needed for ret<0 ?
            goto fail;
        if (!ret)
            break;

        ret = write_packet(s, &pkt);
        if (ret >= 0)
            s->streams[pkt.stream_index]->nb_frames++;

        av_packet_unref(&pkt);

        if (ret < 0)
            goto fail;
    }

    if (!(s->oformat->flags & AVFMT_NOFILE) && s->pb)
        avio_write_marker(s->pb, AV_NOPTS_VALUE, AVIO_DATA_MARKER_TRAILER);
    if (s->oformat->write_trailer)
        ret = s->oformat->write_trailer(s);

    if (!(s->oformat->flags & AVFMT_NOFILE) && s->pb)
        avio_flush(s->pb);

fail:
    for (i = 0; i < s->nb_streams; i++) {
        av_freep(&s->streams[i]->priv_data);
        av_freep(&s->streams[i]->index_entries);
    }
    if (s->oformat->priv_class)
        av_opt_free(s->priv_data);
    av_freep(&s->priv_data);
    return ret;
}

int ff_write_chained(AVFormatContext *dst, int dst_stream, AVPacket *pkt,
                     AVFormatContext *src)
{
    AVPacket local_pkt;

    local_pkt = *pkt;
    local_pkt.stream_index = dst_stream;
    if (pkt->pts != AV_NOPTS_VALUE)
        local_pkt.pts = av_rescale_q(pkt->pts,
                                     src->streams[pkt->stream_index]->time_base,
                                     dst->streams[dst_stream]->time_base);
    if (pkt->dts != AV_NOPTS_VALUE)
        local_pkt.dts = av_rescale_q(pkt->dts,
                                     src->streams[pkt->stream_index]->time_base,
                                     dst->streams[dst_stream]->time_base);
    return av_write_frame(dst, &local_pkt);
}
