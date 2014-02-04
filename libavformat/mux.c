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
#include "libavutil/avassert.h"
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

/* fraction handling */

/**
 * f = val + (num / den) + 0.5.
 *
 * 'num' is normalized so that it is such as 0 <= num < den.
 *
 * @param f fractional number
 * @param val integer value
 * @param num must be >= 0
 * @param den must be >= 1
 */
static void frac_init(AVFrac *f, int64_t val, int64_t num, int64_t den)
{
    num += (den >> 1);
    if (num >= den) {
        val += num / den;
        num  = num % den;
    }
    f->val = val;
    f->num = num;
    f->den = den;
}

/**
 * Fractional addition to f: f = f + (incr / f->den).
 *
 * @param f fractional number
 * @param incr increment, can be positive or negative
 */
static void frac_add(AVFrac *f, int64_t incr)
{
    int64_t num, den;

    num = f->num + incr;
    den = f->den;
    if (num < 0) {
        f->val += num / den;
        num     = num % den;
        if (num < 0) {
            num += den;
            f->val--;
        }
    } else if (num >= den) {
        f->val += num / den;
        num     = num % den;
    }
    f->num = num;
}

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
            if (avpriv_toupper4(avctag->tag) == avpriv_toupper4(st->codec->codec_tag)) {
                id = avctag->id;
                if (id == st->codec->codec_id)
                    return 1;
            }
            if (avctag->id == st->codec->codec_id)
                tag = avctag->tag;
            avctag++;
        }
    }
    if (id != AV_CODEC_ID_NONE)
        return 0;
    if (tag && (st->codec->strict_std_compliance >= FF_COMPLIANCE_NORMAL))
        return 0;
    return 1;
}


static int init_muxer(AVFormatContext *s, AVDictionary **options)
{
    int ret = 0, i;
    AVStream *st;
    AVDictionary *tmp = NULL;
    AVCodecContext *codec = NULL;
    AVOutputFormat *of = s->oformat;

    if (options)
        av_dict_copy(&tmp, *options, 0);

    if ((ret = av_opt_set_dict(s, &tmp)) < 0)
        goto fail;

    // some sanity checks
    if (s->nb_streams == 0 && !(of->flags & AVFMT_NOSTREAMS)) {
        av_log(s, AV_LOG_ERROR, "no streams\n");
        ret = AVERROR(EINVAL);
        goto fail;
    }

    for (i = 0; i < s->nb_streams; i++) {
        st    = s->streams[i];
        codec = st->codec;

        switch (codec->codec_type) {
        case AVMEDIA_TYPE_AUDIO:
            if (codec->sample_rate <= 0) {
                av_log(s, AV_LOG_ERROR, "sample rate not set\n");
                ret = AVERROR(EINVAL);
                goto fail;
            }
            if (!codec->block_align)
                codec->block_align = codec->channels *
                                     av_get_bits_per_sample(codec->codec_id) >> 3;
            break;
        case AVMEDIA_TYPE_VIDEO:
            if (codec->time_base.num <= 0 ||
                codec->time_base.den <= 0) { //FIXME audio too?
                av_log(s, AV_LOG_ERROR, "time base not set\n");
                ret = AVERROR(EINVAL);
                goto fail;
            }

            if ((codec->width <= 0 || codec->height <= 0) &&
                !(of->flags & AVFMT_NODIMENSIONS)) {
                av_log(s, AV_LOG_ERROR, "dimensions not set\n");
                ret = AVERROR(EINVAL);
                goto fail;
            }

            if (av_cmp_q(st->sample_aspect_ratio,
                         codec->sample_aspect_ratio)) {
                if (st->sample_aspect_ratio.num != 0 &&
                    st->sample_aspect_ratio.den != 0 &&
                    codec->sample_aspect_ratio.den != 0 &&
                    codec->sample_aspect_ratio.den != 0) {
                    av_log(s, AV_LOG_ERROR, "Aspect ratio mismatch between muxer "
                            "(%d/%d) and encoder layer (%d/%d)\n",
                            st->sample_aspect_ratio.num, st->sample_aspect_ratio.den,
                            codec->sample_aspect_ratio.num,
                            codec->sample_aspect_ratio.den);
                    ret = AVERROR(EINVAL);
                    goto fail;
                }
            }
            break;
        }

        if (of->codec_tag) {
            if (codec->codec_tag &&
                codec->codec_id == AV_CODEC_ID_RAWVIDEO &&
                !av_codec_get_tag(of->codec_tag, codec->codec_id) &&
                !validate_codec_tag(s, st)) {
                // the current rawvideo encoding system ends up setting
                // the wrong codec_tag for avi, we override it here
                codec->codec_tag = 0;
            }
            if (codec->codec_tag) {
                if (!validate_codec_tag(s, st)) {
                    char tagbuf[32];
                    av_get_codec_tag_string(tagbuf, sizeof(tagbuf), codec->codec_tag);
                    av_log(s, AV_LOG_ERROR,
                           "Tag %s/0x%08x incompatible with output codec id '%d'\n",
                           tagbuf, codec->codec_tag, codec->codec_id);
                    ret = AVERROR_INVALIDDATA;
                    goto fail;
                }
            } else
                codec->codec_tag = av_codec_get_tag(of->codec_tag, codec->codec_id);
        }

        if (of->flags & AVFMT_GLOBALHEADER &&
            !(codec->flags & CODEC_FLAG_GLOBAL_HEADER))
            av_log(s, AV_LOG_WARNING,
                   "Codec for stream %d does not use global headers "
                   "but container format requires global headers\n", i);

        if (codec->codec_type != AVMEDIA_TYPE_ATTACHMENT)
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
    if (s->nb_streams && !(s->streams[0]->codec->flags & CODEC_FLAG_BITEXACT)) {
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

static int init_pts(AVFormatContext *s)
{
    int i;
    AVStream *st;

    /* init PTS generation */
    for (i = 0; i < s->nb_streams; i++) {
        int64_t den = AV_NOPTS_VALUE;
        st = s->streams[i];

        switch (st->codec->codec_type) {
        case AVMEDIA_TYPE_AUDIO:
            den = (int64_t)st->time_base.num * st->codec->sample_rate;
            break;
        case AVMEDIA_TYPE_VIDEO:
            den = (int64_t)st->time_base.num * st->codec->time_base.den;
            break;
        default:
            break;
        }
        if (den != AV_NOPTS_VALUE) {
            if (den <= 0)
                return AVERROR_INVALIDDATA;

            frac_init(&st->pts, 0, 0, den);
        }
    }

    return 0;
}

int avformat_write_header(AVFormatContext *s, AVDictionary **options)
{
    int ret = 0;

    if (ret = init_muxer(s, options))
        return ret;

    if (s->oformat->write_header) {
        ret = s->oformat->write_header(s);
        if (ret < 0)
            return ret;
    }

    if ((ret = init_pts(s)) < 0)
        return ret;

    return 0;
}

//FIXME merge with compute_pkt_fields
static int compute_pkt_fields2(AVFormatContext *s, AVStream *st, AVPacket *pkt)
{
    int delay = FFMAX(st->codec->has_b_frames, !!st->codec->max_b_frames);
    int num, den, frame_size, i;

    av_dlog(s, "compute_pkt_fields2: pts:%" PRId64 " dts:%" PRId64 " cur_dts:%" PRId64 " b:%d size:%d st:%d\n",
            pkt->pts, pkt->dts, st->cur_dts, delay, pkt->size, pkt->stream_index);

/*    if(pkt->pts == AV_NOPTS_VALUE && pkt->dts == AV_NOPTS_VALUE)
 *      return AVERROR(EINVAL);*/

    /* duration field */
    if (pkt->duration == 0) {
        ff_compute_frame_duration(&num, &den, st, NULL, pkt);
        if (den && num) {
            pkt->duration = av_rescale(1, num * (int64_t)st->time_base.den * st->codec->ticks_per_frame, den * (int64_t)st->time_base.num);
        }
    }

    if (pkt->pts == AV_NOPTS_VALUE && pkt->dts != AV_NOPTS_VALUE && delay == 0)
        pkt->pts = pkt->dts;

    //XXX/FIXME this is a temporary hack until all encoders output pts
    if ((pkt->pts == 0 || pkt->pts == AV_NOPTS_VALUE) && pkt->dts == AV_NOPTS_VALUE && !delay) {
        pkt->dts =
//        pkt->pts= st->cur_dts;
            pkt->pts = st->pts.val;
    }

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
        av_log(s, AV_LOG_ERROR, "pts < dts in stream %d\n", st->index);
        return AVERROR(EINVAL);
    }

    av_dlog(s, "av_write_frame: pts2:%"PRId64" dts2:%"PRId64"\n",
            pkt->pts, pkt->dts);
    st->cur_dts = pkt->dts;
    st->pts.val = pkt->dts;

    /* update pts */
    switch (st->codec->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        frame_size = ff_get_audio_frame_size(st->codec, pkt->size, 1);

        /* HACK/FIXME, we skip the initial 0 size packets as they are most
         * likely equal to the encoder delay, but it would be better if we
         * had the real timestamps from the encoder */
        if (frame_size >= 0 && (pkt->size || st->pts.num != st->pts.den >> 1 || st->pts.val)) {
            frac_add(&st->pts, (int64_t)st->time_base.den * frame_size);
        }
        break;
    case AVMEDIA_TYPE_VIDEO:
        frac_add(&st->pts, (int64_t)st->time_base.den * st->codec->time_base.num);
        break;
    default:
        break;
    }
    return 0;
}

/*
 * FIXME: this function should NEVER get undefined pts/dts beside when the
 * AVFMT_NOTIMESTAMPS is set.
 * Those additional safety checks should be dropped once the correct checks
 * are set in the callers.
 */

static int write_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret;
    if (!(s->oformat->flags & (AVFMT_TS_NEGATIVE | AVFMT_NOTIMESTAMPS))) {
        AVRational time_base = s->streams[pkt->stream_index]->time_base;
        int64_t offset = 0;

        if (!s->offset && pkt->dts != AV_NOPTS_VALUE && pkt->dts < 0) {
            s->offset = -pkt->dts;
            s->offset_timebase = time_base;
        }
        if (s->offset)
            offset = av_rescale_q(s->offset, s->offset_timebase, time_base);

        if (pkt->dts != AV_NOPTS_VALUE)
            pkt->dts += offset;
        if (pkt->pts != AV_NOPTS_VALUE)
            pkt->pts += offset;
    }
    ret = s->oformat->write_packet(s, pkt);

    if (s->pb && ret >= 0 && s->flags & AVFMT_FLAG_FLUSH_PACKETS)
        avio_flush(s->pb);

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

    if (s->streams[pkt->stream_index]->codec->codec_type == AVMEDIA_TYPE_ATTACHMENT) {
        av_log(s, AV_LOG_ERROR, "Received a packet for an attachment stream.\n");
        return AVERROR(EINVAL);
    }

    return 0;
}

int av_write_frame(AVFormatContext *s, AVPacket *pkt)
{
    int ret;

    ret = check_packet(s, pkt);
    if (ret < 0)
        return ret;

    if (!pkt) {
        if (s->oformat->flags & AVFMT_ALLOW_FLUSH)
            return s->oformat->write_packet(s, pkt);
        return 1;
    }

    ret = compute_pkt_fields2(s, s->streams[pkt->stream_index], pkt);

    if (ret < 0 && !(s->oformat->flags & AVFMT_NOTIMESTAMPS))
        return ret;

    ret = write_packet(s, pkt);

    if (ret >= 0)
        s->streams[pkt->stream_index]->nb_frames++;
    return ret;
}

void ff_interleave_add_packet(AVFormatContext *s, AVPacket *pkt,
                              int (*compare)(AVFormatContext *, AVPacket *, AVPacket *))
{
    AVPacketList **next_point, *this_pktl;

    this_pktl      = av_mallocz(sizeof(AVPacketList));
    this_pktl->pkt = *pkt;
#if FF_API_DESTRUCT_PACKET
FF_DISABLE_DEPRECATION_WARNINGS
    pkt->destruct  = NULL;           // do not free original but only the copy
FF_ENABLE_DEPRECATION_WARNINGS
#endif
    pkt->buf       = NULL;
    av_dup_packet(&this_pktl->pkt);  // duplicate the packet if it uses non-alloced memory

    if (s->streams[pkt->stream_index]->last_in_packet_buffer) {
        next_point = &(s->streams[pkt->stream_index]->last_in_packet_buffer->next);
    } else
        next_point = &s->packet_buffer;

    if (*next_point) {
        if (compare(s, &s->packet_buffer_end->pkt, pkt)) {
            while (!compare(s, &(*next_point)->pkt, pkt))
                next_point = &(*next_point)->next;
            goto next_non_null;
        } else {
            next_point = &(s->packet_buffer_end->next);
        }
    }
    assert(!*next_point);

    s->packet_buffer_end = this_pktl;
next_non_null:

    this_pktl->next = *next_point;

    s->streams[pkt->stream_index]->last_in_packet_buffer =
        *next_point                                      = this_pktl;
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
    int i;

    if (pkt) {
        ff_interleave_add_packet(s, pkt, interleave_compare_dts);
    }

    if (s->max_interleave_delta > 0 && s->packet_buffer && !flush) {
        AVPacket *top_pkt = &s->packet_buffer->pkt;
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
        pktl = s->packet_buffer;
        *out = pktl->pkt;

        s->packet_buffer = pktl->next;
        if (!s->packet_buffer)
            s->packet_buffer_end = NULL;

        if (s->streams[out->stream_index]->last_in_packet_buffer == pktl)
            s->streams[out->stream_index]->last_in_packet_buffer = NULL;
        av_freep(&pktl);
        return 1;
    } else {
        av_init_packet(out);
        return 0;
    }
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
            av_free_packet(in);
        return ret;
    } else
        return ff_interleave_packet_per_dts(s, out, in, flush);
}

int av_interleaved_write_frame(AVFormatContext *s, AVPacket *pkt)
{
    int ret, flush = 0;

    ret = check_packet(s, pkt);
    if (ret < 0)
        goto fail;

    if (pkt) {
        AVStream *st = s->streams[pkt->stream_index];

        //FIXME/XXX/HACK drop zero sized packets
        if (st->codec->codec_type == AVMEDIA_TYPE_AUDIO && pkt->size == 0) {
            ret = 0;
            goto fail;
        }

        av_dlog(s, "av_interleaved_write_frame size:%d dts:%" PRId64 " pts:%" PRId64 "\n",
                pkt->size, pkt->dts, pkt->pts);
        if ((ret = compute_pkt_fields2(s, st, pkt)) < 0 && !(s->oformat->flags & AVFMT_NOTIMESTAMPS))
            goto fail;

        if (pkt->dts == AV_NOPTS_VALUE && !(s->oformat->flags & AVFMT_NOTIMESTAMPS)) {
            ret = AVERROR(EINVAL);
            goto fail;
        }
    } else {
        av_dlog(s, "av_interleaved_write_frame FLUSH\n");
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

        av_free_packet(&opkt);

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

        av_free_packet(&pkt);

        if (ret < 0)
            goto fail;
    }

    if (s->oformat->write_trailer)
        ret = s->oformat->write_trailer(s);

    if (!(s->oformat->flags & AVFMT_NOFILE))
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
