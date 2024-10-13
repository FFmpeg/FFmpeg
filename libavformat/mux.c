/*
 * muxing functions for use within FFmpeg
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

#include "avformat.h"
#include "avformat_internal.h"
#include "internal.h"
#include "mux.h"
#include "version.h"
#include "libavcodec/bsf.h"
#include "libavcodec/codec_desc.h"
#include "libavcodec/internal.h"
#include "libavcodec/packet_internal.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/dict.h"
#include "libavutil/timestamp.h"
#include "libavutil/avassert.h"
#include "libavutil/frame.h"
#include "libavutil/internal.h"
#include "libavutil/mathematics.h"

/**
 * @file
 * muxing functions for use within libavformat
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
static void frac_init(FFFrac *f, int64_t val, int64_t num, int64_t den)
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
static void frac_add(FFFrac *f, int64_t incr)
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

int avformat_alloc_output_context2(AVFormatContext **avctx, const AVOutputFormat *oformat,
                                   const char *format, const char *filename)
{
    AVFormatContext *s = avformat_alloc_context();
    int ret = 0;

    *avctx = NULL;
    if (!s)
        goto nomem;

    if (!oformat) {
        if (format) {
            oformat = av_guess_format(format, NULL, NULL);
            if (!oformat) {
                av_log(s, AV_LOG_ERROR, "Requested output format '%s' is not known.\n", format);
                ret = AVERROR(EINVAL);
                goto error;
            }
        } else {
            oformat = av_guess_format(NULL, filename, NULL);
            if (!oformat) {
                ret = AVERROR(EINVAL);
                av_log(s, AV_LOG_ERROR,
                       "Unable to choose an output format for '%s'; "
                       "use a standard extension for the filename or specify "
                       "the format manually.\n", filename);
                goto error;
            }
        }
    }

    s->oformat = oformat;
    if (ffofmt(s->oformat)->priv_data_size > 0) {
        s->priv_data = av_mallocz(ffofmt(s->oformat)->priv_data_size);
        if (!s->priv_data)
            goto nomem;
        if (s->oformat->priv_class) {
            *(const AVClass**)s->priv_data= s->oformat->priv_class;
            av_opt_set_defaults(s->priv_data);
        }
    } else
        s->priv_data = NULL;

    if (filename) {
        if (!(s->url = av_strdup(filename)))
            goto nomem;

    }
    *avctx = s;
    return 0;
nomem:
    av_log(s, AV_LOG_ERROR, "Out of memory\n");
    ret = AVERROR(ENOMEM);
error:
    avformat_free_context(s);
    return ret;
}

static int validate_codec_tag(const AVFormatContext *s, const AVStream *st)
{
    const AVCodecTag *avctag;
    enum AVCodecID id = AV_CODEC_ID_NONE;
    unsigned uppercase_tag = ff_toupper4(st->codecpar->codec_tag);
    int64_t tag  = -1;

    /**
     * Check that tag + id is in the table
     * If neither is in the table -> OK
     * If tag is in the table with another id -> FAIL
     * If id is in the table with another tag -> FAIL unless strict < normal
     */
    for (int n = 0; s->oformat->codec_tag[n]; n++) {
        avctag = s->oformat->codec_tag[n];
        while (avctag->id != AV_CODEC_ID_NONE) {
            if (ff_toupper4(avctag->tag) == uppercase_tag) {
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
    if (tag >= 0 && (s->strict_std_compliance >= FF_COMPLIANCE_NORMAL))
        return 0;
    return 1;
}


static int init_muxer(AVFormatContext *s, AVDictionary **options)
{
    FormatContextInternal *const fci = ff_fc_internal(s);
    AVDictionary *tmp = NULL;
    const FFOutputFormat *of = ffofmt(s->oformat);
    AVDictionaryEntry *e;
    static const unsigned default_codec_offsets[] = {
        [AVMEDIA_TYPE_VIDEO]    = offsetof(AVOutputFormat, video_codec),
        [AVMEDIA_TYPE_AUDIO]    = offsetof(AVOutputFormat, audio_codec),
        [AVMEDIA_TYPE_SUBTITLE] = offsetof(AVOutputFormat, subtitle_codec),
    };
    unsigned nb_type[FF_ARRAY_ELEMS(default_codec_offsets)] = { 0 };
    int ret = 0;

    if (options)
        av_dict_copy(&tmp, *options, 0);

    if ((ret = av_opt_set_dict(s, &tmp)) < 0)
        goto fail;
    if (s->priv_data && s->oformat->priv_class && *(const AVClass**)s->priv_data==s->oformat->priv_class &&
        (ret = av_opt_set_dict2(s->priv_data, &tmp, AV_OPT_SEARCH_CHILDREN)) < 0)
        goto fail;

    if (!s->url && !(s->url = av_strdup(""))) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    // some sanity checks
    if (s->nb_streams == 0 && !(of->p.flags & AVFMT_NOSTREAMS)) {
        av_log(s, AV_LOG_ERROR, "No streams to mux were specified\n");
        ret = AVERROR(EINVAL);
        goto fail;
    }

    for (unsigned i = 0; i < s->nb_streams; i++) {
        AVStream          *const  st = s->streams[i];
        FFStream          *const sti = ffstream(st);
        AVCodecParameters *const par = st->codecpar;
        const AVCodecDescriptor *desc;

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
                par->block_align = par->ch_layout.nb_channels *
                                   av_get_bits_per_sample(par->codec_id) >> 3;
            break;
        case AVMEDIA_TYPE_VIDEO:
            if ((par->width <= 0 || par->height <= 0) &&
                !(of->p.flags & AVFMT_NODIMENSIONS)) {
                av_log(s, AV_LOG_ERROR, "dimensions not set\n");
                ret = AVERROR(EINVAL);
                goto fail;
            }
            if (av_cmp_q(st->sample_aspect_ratio, par->sample_aspect_ratio)
                && fabs(av_q2d(st->sample_aspect_ratio) - av_q2d(par->sample_aspect_ratio)) > 0.004*av_q2d(st->sample_aspect_ratio)
            ) {
                if (st->sample_aspect_ratio.num != 0 &&
                    st->sample_aspect_ratio.den != 0 &&
                    par->sample_aspect_ratio.num != 0 &&
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
        if (of->flags_internal & (FF_OFMT_FLAG_MAX_ONE_OF_EACH | FF_OFMT_FLAG_ONLY_DEFAULT_CODECS)) {
            enum AVCodecID default_codec_id = AV_CODEC_ID_NONE;
            unsigned nb;
            if ((unsigned)par->codec_type < FF_ARRAY_ELEMS(default_codec_offsets)) {
                nb = ++nb_type[par->codec_type];
                if (default_codec_offsets[par->codec_type])
                    default_codec_id = *(const enum AVCodecID*)((const char*)of + default_codec_offsets[par->codec_type]);
            }
            if (of->flags_internal & FF_OFMT_FLAG_ONLY_DEFAULT_CODECS &&
                default_codec_id != AV_CODEC_ID_NONE && par->codec_id != default_codec_id) {
                av_log(s, AV_LOG_ERROR, "%s muxer supports only codec %s for type %s\n",
                       of->p.name, avcodec_get_name(default_codec_id), av_get_media_type_string(par->codec_type));
                ret = AVERROR(EINVAL);
                goto fail;
            } else if (default_codec_id == AV_CODEC_ID_NONE ||
                       (of->flags_internal & FF_OFMT_FLAG_MAX_ONE_OF_EACH && nb > 1)) {
                const char *type = av_get_media_type_string(par->codec_type);
                av_log(s, AV_LOG_ERROR, "%s muxer does not support %s stream of type %s\n",
                       of->p.name, default_codec_id == AV_CODEC_ID_NONE ? "any" : "more than one",
                       type ? type : "unknown");
                ret = AVERROR(EINVAL);
                goto fail;
            }
        }

#if FF_API_AVSTREAM_SIDE_DATA
FF_DISABLE_DEPRECATION_WARNINGS
        /* if the caller is using the deprecated AVStream side_data API,
         * copy its contents to AVStream.codecpar, giving it priority
           over existing side data in the latter */
        for (int i = 0; i < st->nb_side_data; i++) {
            const AVPacketSideData *sd_src = &st->side_data[i];
            AVPacketSideData *sd_dst;

            sd_dst = av_packet_side_data_new(&st->codecpar->coded_side_data,
                                             &st->codecpar->nb_coded_side_data,
                                             sd_src->type, sd_src->size, 0);
            if (!sd_dst) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }
            memcpy(sd_dst->data, sd_src->data, sd_src->size);
        }
FF_ENABLE_DEPRECATION_WARNINGS
#endif

        desc = avcodec_descriptor_get(par->codec_id);
        if (desc && desc->props & AV_CODEC_PROP_REORDER)
            sti->reorder = 1;

        sti->is_intra_only = ff_is_intra_only(par->codec_id);

        if (of->p.codec_tag) {
            if (   par->codec_tag
                && par->codec_id == AV_CODEC_ID_RAWVIDEO
                && (   av_codec_get_tag(of->p.codec_tag, par->codec_id) == 0
                    || av_codec_get_tag(of->p.codec_tag, par->codec_id) == MKTAG('r', 'a', 'w', ' '))
                && !validate_codec_tag(s, st)) {
                // the current rawvideo encoding system ends up setting
                // the wrong codec_tag for avi/mov, we override it here
                par->codec_tag = 0;
            }
            if (par->codec_tag) {
                if (!validate_codec_tag(s, st)) {
                    const uint32_t otag = av_codec_get_tag(s->oformat->codec_tag, par->codec_id);
                    av_log(s, AV_LOG_ERROR,
                           "Tag %s incompatible with output codec id '%d' (%s)\n",
                           av_fourcc2str(par->codec_tag), par->codec_id, av_fourcc2str(otag));
                    ret = AVERROR_INVALIDDATA;
                    goto fail;
                }
            } else
                par->codec_tag = av_codec_get_tag(of->p.codec_tag, par->codec_id);
        }

        if (par->codec_type != AVMEDIA_TYPE_ATTACHMENT &&
            par->codec_id != AV_CODEC_ID_SMPTE_2038)
            fci->nb_interleaved_streams++;
    }
    fci->interleave_packet = of->interleave_packet;
    if (!fci->interleave_packet)
        fci->interleave_packet = fci->nb_interleaved_streams > 1 ?
                                 ff_interleave_packet_per_dts :
                                 ff_interleave_packet_passthrough;

    if (!s->priv_data && of->priv_data_size > 0) {
        s->priv_data = av_mallocz(of->priv_data_size);
        if (!s->priv_data) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        if (of->p.priv_class) {
            *(const AVClass **)s->priv_data = of->p.priv_class;
            av_opt_set_defaults(s->priv_data);
            if ((ret = av_opt_set_dict2(s->priv_data, &tmp, AV_OPT_SEARCH_CHILDREN)) < 0)
                goto fail;
        }
    }

    /* set muxer identification string */
    if (!(s->flags & AVFMT_FLAG_BITEXACT)) {
        av_dict_set(&s->metadata, "encoder", LIBAVFORMAT_IDENT, 0);
    } else {
        av_dict_set(&s->metadata, "encoder", NULL, 0);
    }

    for (e = NULL; e = av_dict_get(s->metadata, "encoder-", e, AV_DICT_IGNORE_SUFFIX); ) {
        av_dict_set(&s->metadata, e->key, NULL, 0);
    }

    if (options) {
         av_dict_free(options);
         *options = tmp;
    }

    if (of->init) {
        if ((ret = of->init(s)) < 0) {
            if (of->deinit)
                of->deinit(s);
            return ret;
        }
        return ret == 0;
    }

    return 0;

fail:
    av_dict_free(&tmp);
    return ret;
}

static int init_pts(AVFormatContext *s)
{
    FFFormatContext *const si = ffformatcontext(s);

    /* init PTS generation */
    for (unsigned i = 0; i < s->nb_streams; i++) {
        AVStream *const st = s->streams[i];
        FFStream *const sti = ffstream(st);
        int64_t den = AV_NOPTS_VALUE;

        switch (st->codecpar->codec_type) {
        case AVMEDIA_TYPE_AUDIO:
            den = (int64_t)st->time_base.num * st->codecpar->sample_rate;
            break;
        case AVMEDIA_TYPE_VIDEO:
            den = (int64_t)st->time_base.num * st->time_base.den;
            break;
        default:
            break;
        }

        if (den != AV_NOPTS_VALUE) {
            if (den <= 0)
                return AVERROR_INVALIDDATA;

            frac_init(&sti->priv_pts, 0, 0, den);
        }
    }

    si->avoid_negative_ts_status = AVOID_NEGATIVE_TS_UNKNOWN;
    if (s->avoid_negative_ts < 0) {
        av_assert2(s->avoid_negative_ts == AVFMT_AVOID_NEG_TS_AUTO);
        if (s->oformat->flags & (AVFMT_TS_NEGATIVE | AVFMT_NOTIMESTAMPS)) {
            s->avoid_negative_ts = AVFMT_AVOID_NEG_TS_DISABLED;
            si->avoid_negative_ts_status = AVOID_NEGATIVE_TS_DISABLED;
        } else
            s->avoid_negative_ts = AVFMT_AVOID_NEG_TS_MAKE_NON_NEGATIVE;
    } else if (s->avoid_negative_ts == AVFMT_AVOID_NEG_TS_DISABLED)
        si->avoid_negative_ts_status = AVOID_NEGATIVE_TS_DISABLED;

    return 0;
}

static void flush_if_needed(AVFormatContext *s)
{
    if (s->pb && s->pb->error >= 0) {
        if (s->flush_packets == 1 || s->flags & AVFMT_FLAG_FLUSH_PACKETS)
            avio_flush(s->pb);
        else if (s->flush_packets && !(s->oformat->flags & AVFMT_NOFILE))
            avio_write_marker(s->pb, AV_NOPTS_VALUE, AVIO_DATA_MARKER_FLUSH_POINT);
    }
}

static void deinit_muxer(AVFormatContext *s)
{
    FormatContextInternal *const fci = ff_fc_internal(s);
    const FFOutputFormat *const of = ffofmt(s->oformat);
    if (of && of->deinit && fci->initialized)
        of->deinit(s);
    fci->initialized =
    fci->streams_initialized = 0;
}

int avformat_init_output(AVFormatContext *s, AVDictionary **options)
{
    FormatContextInternal *const fci = ff_fc_internal(s);
    int ret = 0;

    if ((ret = init_muxer(s, options)) < 0)
        return ret;

    fci->initialized = 1;
    fci->streams_initialized = ret;

    if (ffofmt(s->oformat)->init && ret) {
        if ((ret = init_pts(s)) < 0)
            return ret;

        return AVSTREAM_INIT_IN_INIT_OUTPUT;
    }

    return AVSTREAM_INIT_IN_WRITE_HEADER;
}

int avformat_write_header(AVFormatContext *s, AVDictionary **options)
{
    FormatContextInternal *const fci = ff_fc_internal(s);
    int already_initialized = fci->initialized;
    int streams_already_initialized = fci->streams_initialized;
    int ret = 0;

    if (!already_initialized)
        if ((ret = avformat_init_output(s, options)) < 0)
            return ret;

    if (ffofmt(s->oformat)->write_header) {
        if (!(s->oformat->flags & AVFMT_NOFILE) && s->pb)
            avio_write_marker(s->pb, AV_NOPTS_VALUE, AVIO_DATA_MARKER_HEADER);
        ret = ffofmt(s->oformat)->write_header(s);
        if (ret >= 0 && s->pb && s->pb->error < 0)
            ret = s->pb->error;
        if (ret < 0)
            goto fail;
        flush_if_needed(s);
    }
    if (!(s->oformat->flags & AVFMT_NOFILE) && s->pb)
        avio_write_marker(s->pb, AV_NOPTS_VALUE, AVIO_DATA_MARKER_UNKNOWN);

    if (!fci->streams_initialized) {
        if ((ret = init_pts(s)) < 0)
            goto fail;
    }

    return streams_already_initialized;

fail:
    deinit_muxer(s);
    return ret;
}

#define AV_PKT_FLAG_UNCODED_FRAME 0x2000


#if FF_API_COMPUTE_PKT_FIELDS2
FF_DISABLE_DEPRECATION_WARNINGS
//FIXME merge with compute_pkt_fields
static int compute_muxer_pkt_fields(AVFormatContext *s, AVStream *st, AVPacket *pkt)
{
    FormatContextInternal *const fci = ff_fc_internal(s);
    FFStream *const sti = ffstream(st);
    int delay = st->codecpar->video_delay;
    int frame_size;

    if (!fci->missing_ts_warning &&
        !(s->oformat->flags & AVFMT_NOTIMESTAMPS) &&
        (!(st->disposition & AV_DISPOSITION_ATTACHED_PIC) || (st->disposition & AV_DISPOSITION_TIMED_THUMBNAILS)) &&
        (pkt->pts == AV_NOPTS_VALUE || pkt->dts == AV_NOPTS_VALUE)) {
        av_log(s, AV_LOG_WARNING,
               "Timestamps are unset in a packet for stream %d. "
               "This is deprecated and will stop working in the future. "
               "Fix your code to set the timestamps properly\n", st->index);
        fci->missing_ts_warning = 1;
    }

    if (s->debug & FF_FDEBUG_TS)
        av_log(s, AV_LOG_DEBUG, "compute_muxer_pkt_fields: pts:%s dts:%s cur_dts:%s b:%d size:%d st:%d\n",
            av_ts2str(pkt->pts), av_ts2str(pkt->dts), av_ts2str(sti->cur_dts), delay, pkt->size, pkt->stream_index);

    if (pkt->pts == AV_NOPTS_VALUE && pkt->dts != AV_NOPTS_VALUE && delay == 0)
        pkt->pts = pkt->dts;

    //XXX/FIXME this is a temporary hack until all encoders output pts
    if ((pkt->pts == 0 || pkt->pts == AV_NOPTS_VALUE) && pkt->dts == AV_NOPTS_VALUE && !delay) {
        static int warned;
        if (!warned) {
            av_log(s, AV_LOG_WARNING, "Encoder did not produce proper pts, making some up.\n");
            warned = 1;
        }
        pkt->dts =
//        pkt->pts= st->cur_dts;
            pkt->pts = sti->priv_pts.val;
    }

    //calculate dts from pts
    if (pkt->pts != AV_NOPTS_VALUE && pkt->dts == AV_NOPTS_VALUE && delay <= MAX_REORDER_DELAY) {
        sti->pts_buffer[0] = pkt->pts;
        for (int i = 1; i < delay + 1 && sti->pts_buffer[i] == AV_NOPTS_VALUE; i++)
            sti->pts_buffer[i] = pkt->pts + (i - delay - 1) * pkt->duration;
        for (int i = 0; i<delay && sti->pts_buffer[i] > sti->pts_buffer[i + 1]; i++)
            FFSWAP(int64_t, sti->pts_buffer[i], sti->pts_buffer[i + 1]);

        pkt->dts = sti->pts_buffer[0];
    }

    if (sti->cur_dts && sti->cur_dts != AV_NOPTS_VALUE &&
        ((!(s->oformat->flags & AVFMT_TS_NONSTRICT) &&
          st->codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE &&
          st->codecpar->codec_type != AVMEDIA_TYPE_DATA &&
          sti->cur_dts >= pkt->dts) || sti->cur_dts > pkt->dts)) {
        av_log(s, AV_LOG_ERROR,
               "Application provided invalid, non monotonically increasing dts to muxer in stream %d: %s >= %s\n",
               st->index, av_ts2str(sti->cur_dts), av_ts2str(pkt->dts));
        return AVERROR(EINVAL);
    }
    if (pkt->dts != AV_NOPTS_VALUE && pkt->pts != AV_NOPTS_VALUE && pkt->pts < pkt->dts) {
        av_log(s, AV_LOG_ERROR,
               "pts (%s) < dts (%s) in stream %d\n",
               av_ts2str(pkt->pts), av_ts2str(pkt->dts),
               st->index);
        return AVERROR(EINVAL);
    }

    if (s->debug & FF_FDEBUG_TS)
        av_log(s, AV_LOG_DEBUG, "av_write_frame: pts2:%s dts2:%s\n",
            av_ts2str(pkt->pts), av_ts2str(pkt->dts));

    sti->cur_dts      = pkt->dts;
    sti->priv_pts.val = pkt->dts;

    /* update pts */
    switch (st->codecpar->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        frame_size = (pkt->flags & AV_PKT_FLAG_UNCODED_FRAME) ?
                     (*(AVFrame **)pkt->data)->nb_samples :
                     av_get_audio_frame_duration2(st->codecpar, pkt->size);

        /* HACK/FIXME, we skip the initial 0 size packets as they are most
         * likely equal to the encoder delay, but it would be better if we
         * had the real timestamps from the encoder */
        if (frame_size >= 0 && (pkt->size || sti->priv_pts.num != sti->priv_pts.den >> 1 || sti->priv_pts.val)) {
            frac_add(&sti->priv_pts, (int64_t)st->time_base.den * frame_size);
        }
        break;
    case AVMEDIA_TYPE_VIDEO:
        frac_add(&sti->priv_pts, (int64_t)st->time_base.den * st->time_base.num);
        break;
    }
    return 0;
}
FF_ENABLE_DEPRECATION_WARNINGS
#endif

static void guess_pkt_duration(AVFormatContext *s, AVStream *st, AVPacket *pkt)
{
    if (pkt->duration < 0 && st->codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) {
        av_log(s, AV_LOG_WARNING, "Packet with invalid duration %"PRId64" in stream %d\n",
               pkt->duration, pkt->stream_index);
        pkt->duration = 0;
    }

    if (pkt->duration)
        return;

    switch (st->codecpar->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
        if (st->avg_frame_rate.num > 0 && st->avg_frame_rate.den > 0) {
            pkt->duration = av_rescale_q(1, av_inv_q(st->avg_frame_rate),
                                         st->time_base);
        } else if (st->time_base.num * 1000LL > st->time_base.den)
            pkt->duration = 1;
        break;
    case AVMEDIA_TYPE_AUDIO: {
        int frame_size = av_get_audio_frame_duration2(st->codecpar, pkt->size);
        if (frame_size && st->codecpar->sample_rate) {
            pkt->duration = av_rescale_q(frame_size,
                                         (AVRational){1, st->codecpar->sample_rate},
                                         st->time_base);
        }
        break;
        }
    }
}

static void handle_avoid_negative_ts(FFFormatContext *si, FFStream *sti,
                                     AVPacket *pkt)
{
    AVFormatContext *const s = &si->pub;
    int64_t offset;

    if (!AVOID_NEGATIVE_TS_ENABLED(si->avoid_negative_ts_status))
        return;

    if (si->avoid_negative_ts_status == AVOID_NEGATIVE_TS_UNKNOWN) {
        int use_pts = si->avoid_negative_ts_use_pts;
        int64_t ts = use_pts ? pkt->pts : pkt->dts;
        AVRational tb = sti->pub.time_base;

        if (ts == AV_NOPTS_VALUE)
            return;

        ts -= sti->lowest_ts_allowed;

        /* Peek into the muxing queue to improve our estimate
         * of the lowest timestamp if av_interleaved_write_frame() is used. */
        for (const PacketListEntry *pktl = si->packet_buffer.head;
             pktl; pktl = pktl->next) {
            AVRational cmp_tb = s->streams[pktl->pkt.stream_index]->time_base;
            int64_t cmp_ts = use_pts ? pktl->pkt.pts : pktl->pkt.dts;
            if (cmp_ts == AV_NOPTS_VALUE)
                continue;
            cmp_ts -= ffstream(s->streams[pktl->pkt.stream_index])->lowest_ts_allowed;
            if (s->output_ts_offset)
                cmp_ts += av_rescale_q(s->output_ts_offset, AV_TIME_BASE_Q, cmp_tb);
            if (av_compare_ts(cmp_ts, cmp_tb, ts, tb) < 0) {
                ts = cmp_ts;
                tb = cmp_tb;
            }
        }

        if (ts < 0 ||
            ts > 0 && s->avoid_negative_ts == AVFMT_AVOID_NEG_TS_MAKE_ZERO) {
            for (unsigned i = 0; i < s->nb_streams; i++) {
                AVStream *const st2  = s->streams[i];
                FFStream *const sti2 = ffstream(st2);
                sti2->mux_ts_offset = av_rescale_q_rnd(-ts, tb,
                                                       st2->time_base,
                                                       AV_ROUND_UP);
            }
        }
        si->avoid_negative_ts_status = AVOID_NEGATIVE_TS_KNOWN;
    }

    offset = sti->mux_ts_offset;

    if (pkt->dts != AV_NOPTS_VALUE)
        pkt->dts += offset;
    if (pkt->pts != AV_NOPTS_VALUE)
        pkt->pts += offset;

    if (si->avoid_negative_ts_use_pts) {
        if (pkt->pts != AV_NOPTS_VALUE && pkt->pts < sti->lowest_ts_allowed) {
            av_log(s, AV_LOG_WARNING, "failed to avoid negative "
                   "pts %s in stream %d.\n"
                   "Try -avoid_negative_ts 1 as a possible workaround.\n",
                   av_ts2str(pkt->pts),
                   pkt->stream_index
            );
        }
    } else {
        if (pkt->dts != AV_NOPTS_VALUE && pkt->dts < sti->lowest_ts_allowed) {
            av_log(s, AV_LOG_WARNING,
                   "Packets poorly interleaved, failed to avoid negative "
                   "timestamp %s in stream %d.\n"
                   "Try -max_interleave_delta 0 as a possible workaround.\n",
                   av_ts2str(pkt->dts),
                   pkt->stream_index
            );
        }
    }
}

/**
 * Shift timestamps and call muxer; the original pts/dts are not kept.
 *
 * FIXME: this function should NEVER get undefined pts/dts beside when the
 * AVFMT_NOTIMESTAMPS is set.
 * Those additional safety checks should be dropped once the correct checks
 * are set in the callers.
 */
static int write_packet(AVFormatContext *s, AVPacket *pkt)
{
    FFFormatContext *const si = ffformatcontext(s);
    AVStream *const st = s->streams[pkt->stream_index];
    FFStream *const sti = ffstream(st);
    int ret;

    // If the timestamp offsetting below is adjusted, adjust
    // ff_interleaved_peek similarly.
    if (s->output_ts_offset) {
        int64_t offset = av_rescale_q(s->output_ts_offset, AV_TIME_BASE_Q, st->time_base);

        if (pkt->dts != AV_NOPTS_VALUE)
            pkt->dts += offset;
        if (pkt->pts != AV_NOPTS_VALUE)
            pkt->pts += offset;
    }
    handle_avoid_negative_ts(si, sti, pkt);

    if ((pkt->flags & AV_PKT_FLAG_UNCODED_FRAME)) {
        AVFrame **frame = (AVFrame **)pkt->data;
        av_assert0(pkt->size == sizeof(*frame));
        ret = ffofmt(s->oformat)->write_uncoded_frame(s, pkt->stream_index, frame, 0);
    } else {
        ret = ffofmt(s->oformat)->write_packet(s, pkt);
    }

    if (s->pb && ret >= 0) {
        flush_if_needed(s);
        if (s->pb->error < 0)
            ret = s->pb->error;
    }

    if (ret >= 0)
        st->nb_frames++;

    return ret;
}

static int check_packet(AVFormatContext *s, AVPacket *pkt)
{
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

static int prepare_input_packet(AVFormatContext *s, AVStream *st, AVPacket *pkt)
{
    FFStream *const sti = ffstream(st);
#if !FF_API_COMPUTE_PKT_FIELDS2
    /* sanitize the timestamps */
    if (!(s->oformat->flags & AVFMT_NOTIMESTAMPS)) {

        /* when there is no reordering (so dts is equal to pts), but
         * only one of them is set, set the other as well */
        if (!sti->reorder) {
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
        if (sti->cur_dts != AV_NOPTS_VALUE &&
            ((!(s->oformat->flags & AVFMT_TS_NONSTRICT) && sti->cur_dts >= pkt->dts) ||
             sti->cur_dts > pkt->dts)) {
            av_log(s, AV_LOG_ERROR,
                   "Application provided invalid, non monotonically increasing "
                   "dts to muxer in stream %d: %" PRId64 " >= %" PRId64 "\n",
                   st->index, sti->cur_dts, pkt->dts);
            return AVERROR(EINVAL);
        }

        if (pkt->pts < pkt->dts) {
            av_log(s, AV_LOG_ERROR, "pts %" PRId64 " < dts %" PRId64 " in stream %d\n",
                   pkt->pts, pkt->dts, st->index);
            return AVERROR(EINVAL);
        }
    }
#endif
    /* update flags */
    if (sti->is_intra_only)
        pkt->flags |= AV_PKT_FLAG_KEY;

    if (!pkt->data && !pkt->side_data_elems) {
        /* Such empty packets signal EOS for the BSF API; so sanitize
         * the packet by allocating data of size 0 (+ padding). */
        av_buffer_unref(&pkt->buf);
        return av_packet_make_refcounted(pkt);
    }

    return 0;
}

#define CHUNK_START 0x1000

int ff_interleave_add_packet(AVFormatContext *s, AVPacket *pkt,
                             int (*compare)(AVFormatContext *, const AVPacket *, const AVPacket *))
{
    int ret;
    FFFormatContext *const si = ffformatcontext(s);
    PacketListEntry **next_point, *this_pktl;
    AVStream *st = s->streams[pkt->stream_index];
    FFStream *const sti = ffstream(st);
    int chunked  = s->max_chunk_size || s->max_chunk_duration;

    this_pktl    = av_malloc(sizeof(*this_pktl));
    if (!this_pktl) {
        av_packet_unref(pkt);
        return AVERROR(ENOMEM);
    }
    if ((ret = av_packet_make_refcounted(pkt)) < 0) {
        av_free(this_pktl);
        av_packet_unref(pkt);
        return ret;
    }

    av_packet_move_ref(&this_pktl->pkt, pkt);
    pkt = &this_pktl->pkt;

    if (sti->last_in_packet_buffer) {
        next_point = &(sti->last_in_packet_buffer->next);
    } else {
        next_point = &si->packet_buffer.head;
    }

    if (chunked) {
        uint64_t max= av_rescale_q_rnd(s->max_chunk_duration, AV_TIME_BASE_Q, st->time_base, AV_ROUND_UP);
        sti->interleaver_chunk_size     += pkt->size;
        sti->interleaver_chunk_duration += pkt->duration;
        if (   (s->max_chunk_size && sti->interleaver_chunk_size > s->max_chunk_size)
            || (max && sti->interleaver_chunk_duration           > max)) {
            sti->interleaver_chunk_size = 0;
            pkt->flags |= CHUNK_START;
            if (max && sti->interleaver_chunk_duration > max) {
                int64_t syncoffset = (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)*max/2;
                int64_t syncto = av_rescale(pkt->dts + syncoffset, 1, max)*max - syncoffset;

                sti->interleaver_chunk_duration += (pkt->dts - syncto)/8 - max;
            } else
                sti->interleaver_chunk_duration  = 0;
        }
    }
    if (*next_point) {
        if (chunked && !(pkt->flags & CHUNK_START))
            goto next_non_null;

        if (compare(s, &si->packet_buffer.tail->pkt, pkt)) {
            while (   *next_point
                   && ((chunked && !((*next_point)->pkt.flags&CHUNK_START))
                       || !compare(s, &(*next_point)->pkt, pkt)))
                next_point = &(*next_point)->next;
            if (*next_point)
                goto next_non_null;
        } else {
            next_point = &(si->packet_buffer.tail->next);
        }
    }
    av_assert1(!*next_point);

    si->packet_buffer.tail = this_pktl;
next_non_null:

    this_pktl->next = *next_point;

    sti->last_in_packet_buffer = *next_point = this_pktl;

    return 0;
}

static int interleave_compare_dts(AVFormatContext *s, const AVPacket *next,
                                                      const AVPacket *pkt)
{
    AVStream *st  = s->streams[pkt->stream_index];
    AVStream *st2 = s->streams[next->stream_index];
    int comp      = av_compare_ts(next->dts, st2->time_base, pkt->dts,
                                  st->time_base);
    if (s->audio_preload) {
        int preload  = st ->codecpar->codec_type == AVMEDIA_TYPE_AUDIO;
        int preload2 = st2->codecpar->codec_type == AVMEDIA_TYPE_AUDIO;
        if (preload != preload2) {
            int64_t ts, ts2;
            preload  *= s->audio_preload;
            preload2 *= s->audio_preload;
            ts = av_rescale_q(pkt ->dts, st ->time_base, AV_TIME_BASE_Q) - preload;
            ts2= av_rescale_q(next->dts, st2->time_base, AV_TIME_BASE_Q) - preload2;
            if (ts == ts2) {
                ts  = ((uint64_t)pkt ->dts*st ->time_base.num*AV_TIME_BASE - (uint64_t)preload *st ->time_base.den)*st2->time_base.den
                    - ((uint64_t)next->dts*st2->time_base.num*AV_TIME_BASE - (uint64_t)preload2*st2->time_base.den)*st ->time_base.den;
                ts2 = 0;
            }
            comp = (ts2 > ts) - (ts2 < ts);
        }
    }

    if (comp == 0)
        return pkt->stream_index < next->stream_index;
    return comp > 0;
}

int ff_interleave_packet_per_dts(AVFormatContext *s, AVPacket *pkt,
                                 int flush, int has_packet)
{
    FormatContextInternal *const fci = ff_fc_internal(s);
    FFFormatContext *const si = &fci->fc;
    int stream_count = 0;
    int noninterleaved_count = 0;
    int ret;
    int eof = flush;

    if (has_packet) {
        if ((ret = ff_interleave_add_packet(s, pkt, interleave_compare_dts)) < 0)
            return ret;
    }

    for (unsigned i = 0; i < s->nb_streams; i++) {
        const AVStream *const st  = s->streams[i];
        const FFStream *const sti = cffstream(st);
        const AVCodecParameters *const par = st->codecpar;
        if (sti->last_in_packet_buffer) {
            ++stream_count;
        } else if (par->codec_type != AVMEDIA_TYPE_ATTACHMENT &&
                   par->codec_id != AV_CODEC_ID_VP8 &&
                   par->codec_id != AV_CODEC_ID_VP9 &&
                   par->codec_id != AV_CODEC_ID_SMPTE_2038) {
            ++noninterleaved_count;
        }
    }

    if (fci->nb_interleaved_streams == stream_count)
        flush = 1;

    if (s->max_interleave_delta > 0 &&
        si->packet_buffer.head &&
        si->packet_buffer.head->pkt.dts != AV_NOPTS_VALUE &&
        !flush &&
        fci->nb_interleaved_streams == stream_count+noninterleaved_count
    ) {
        AVPacket *const top_pkt = &si->packet_buffer.head->pkt;
        int64_t delta_dts = INT64_MIN;
        int64_t top_dts = av_rescale_q(top_pkt->dts,
                                       s->streams[top_pkt->stream_index]->time_base,
                                       AV_TIME_BASE_Q);

        for (unsigned i = 0; i < s->nb_streams; i++) {
            const AVStream *const st  = s->streams[i];
            const FFStream *const sti = cffstream(st);
            const PacketListEntry *const last = sti->last_in_packet_buffer;
            int64_t last_dts;

            if (!last || st->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE)
                continue;

            last_dts = av_rescale_q(last->pkt.dts,
                                    st->time_base,
                                    AV_TIME_BASE_Q);
            delta_dts = FFMAX(delta_dts, last_dts - top_dts);
        }

        if (delta_dts > s->max_interleave_delta) {
            av_log(s, AV_LOG_DEBUG,
                   "Delay between the first packet and last packet in the "
                   "muxing queue is %"PRId64" > %"PRId64": forcing output\n",
                   delta_dts, s->max_interleave_delta);
            flush = 1;
        }
    }

#if FF_API_LAVF_SHORTEST
    if (si->packet_buffer.head &&
        eof &&
        (s->flags & AVFMT_FLAG_SHORTEST) &&
        fci->shortest_end == AV_NOPTS_VALUE) {
        AVPacket *const top_pkt = &si->packet_buffer.head->pkt;

        fci->shortest_end = av_rescale_q(top_pkt->dts,
                                         s->streams[top_pkt->stream_index]->time_base,
                                         AV_TIME_BASE_Q);
    }

    if (fci->shortest_end != AV_NOPTS_VALUE) {
        while (si->packet_buffer.head) {
            PacketListEntry *pktl = si->packet_buffer.head;
            AVPacket *const top_pkt = &pktl->pkt;
            AVStream *const st = s->streams[top_pkt->stream_index];
            FFStream *const sti = ffstream(st);
            int64_t top_dts = av_rescale_q(top_pkt->dts, st->time_base,
                                        AV_TIME_BASE_Q);

            if (fci->shortest_end + 1 >= top_dts)
                break;

            si->packet_buffer.head = pktl->next;
            if (!si->packet_buffer.head)
                si->packet_buffer.tail = NULL;

            if (sti->last_in_packet_buffer == pktl)
                sti->last_in_packet_buffer = NULL;

            av_packet_unref(&pktl->pkt);
            av_freep(&pktl);
            flush = 0;
        }
    }
#endif

    if (stream_count && flush) {
        PacketListEntry *pktl = si->packet_buffer.head;
        AVStream *const st = s->streams[pktl->pkt.stream_index];
        FFStream *const sti = ffstream(st);

        if (sti->last_in_packet_buffer == pktl)
            sti->last_in_packet_buffer = NULL;
        avpriv_packet_list_get(&si->packet_buffer, pkt);

        return 1;
    } else {
        return 0;
    }
}

int ff_interleave_packet_passthrough(AVFormatContext *s, AVPacket *pkt,
                                     int flush, int has_packet)
{
    return has_packet;
}

int ff_get_muxer_ts_offset(AVFormatContext *s, int stream_index, int64_t *offset)
{
    AVStream *st;

    if (stream_index < 0 || stream_index >= s->nb_streams)
        return AVERROR(EINVAL);

    st = s->streams[stream_index];
    *offset = ffstream(st)->mux_ts_offset;

    if (s->output_ts_offset)
        *offset += av_rescale_q(s->output_ts_offset, AV_TIME_BASE_Q, st->time_base);

    return 0;
}

const AVPacket *ff_interleaved_peek(AVFormatContext *s, int stream)
{
    FFFormatContext *const si = ffformatcontext(s);
    PacketListEntry *pktl = si->packet_buffer.head;
    while (pktl) {
        if (pktl->pkt.stream_index == stream) {
            return &pktl->pkt;
        }
        pktl = pktl->next;
    }
    return NULL;
}

static int check_bitstream(AVFormatContext *s, FFStream *sti, AVPacket *pkt)
{
    int ret;

    if (!(s->flags & AVFMT_FLAG_AUTO_BSF))
        return 1;

    if (ffofmt(s->oformat)->check_bitstream) {
        if (!sti->bitstream_checked) {
            if ((ret = ffofmt(s->oformat)->check_bitstream(s, &sti->pub, pkt)) < 0)
                return ret;
            else if (ret == 1)
                sti->bitstream_checked = 1;
        }
    }

    return 1;
}

static int interleaved_write_packet(AVFormatContext *s, AVPacket *pkt,
                                    int flush, int has_packet)
{
    FormatContextInternal *const fci = ff_fc_internal(s);

    for (;; ) {
        int ret = fci->interleave_packet(s, pkt, flush, has_packet);
        if (ret <= 0)
            return ret;

        has_packet = 0;

        ret = write_packet(s, pkt);
        av_packet_unref(pkt);
        if (ret < 0)
            return ret;
    }
}

static int write_packet_common(AVFormatContext *s, AVStream *st, AVPacket *pkt, int interleaved)
{
    int ret;

    if (s->debug & FF_FDEBUG_TS)
        av_log(s, AV_LOG_DEBUG, "%s size:%d dts:%s pts:%s\n", __func__,
               pkt->size, av_ts2str(pkt->dts), av_ts2str(pkt->pts));

    guess_pkt_duration(s, st, pkt);

#if FF_API_COMPUTE_PKT_FIELDS2
    if ((ret = compute_muxer_pkt_fields(s, st, pkt)) < 0 && !(s->oformat->flags & AVFMT_NOTIMESTAMPS))
        return ret;
#endif

    if (interleaved) {
        if (pkt->dts == AV_NOPTS_VALUE && !(s->oformat->flags & AVFMT_NOTIMESTAMPS))
            return AVERROR(EINVAL);
        return interleaved_write_packet(s, pkt, 0, 1);
    } else {
        return write_packet(s, pkt);
    }
}

static int write_packets_from_bsfs(AVFormatContext *s, AVStream *st, AVPacket *pkt, int interleaved)
{
    FFStream *const sti = ffstream(st);
    AVBSFContext *const bsfc = sti->bsfc;
    int ret;

    if ((ret = av_bsf_send_packet(bsfc, pkt)) < 0) {
        av_log(s, AV_LOG_ERROR,
                "Failed to send packet to filter %s for stream %d\n",
                bsfc->filter->name, st->index);
        return ret;
    }

    do {
        ret = av_bsf_receive_packet(bsfc, pkt);
        if (ret < 0) {
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                return 0;
            av_log(s, AV_LOG_ERROR, "Error applying bitstream filters to an output "
                   "packet for stream #%d: %s\n", st->index, av_err2str(ret));
            if (!(s->error_recognition & AV_EF_EXPLODE) && ret != AVERROR(ENOMEM))
                continue;
            return ret;
        }
        av_packet_rescale_ts(pkt, bsfc->time_base_out, st->time_base);
        ret = write_packet_common(s, st, pkt, interleaved);
        if (ret >= 0 && !interleaved) // a successful write_packet_common already unrefed pkt for interleaved
            av_packet_unref(pkt);
    } while (ret >= 0);

    return ret;
}

static int write_packets_common(AVFormatContext *s, AVPacket *pkt, int interleaved)
{
    AVStream *st;
    FFStream *sti;
    int ret = check_packet(s, pkt);
    if (ret < 0)
        return ret;
    st = s->streams[pkt->stream_index];
    sti = ffstream(st);

    ret = prepare_input_packet(s, st, pkt);
    if (ret < 0)
        return ret;

    ret = check_bitstream(s, sti, pkt);
    if (ret < 0)
        return ret;

    if (sti->bsfc) {
        return write_packets_from_bsfs(s, st, pkt, interleaved);
    } else {
        return write_packet_common(s, st, pkt, interleaved);
    }
}

int av_write_frame(AVFormatContext *s, AVPacket *in)
{
    FFFormatContext *const si = ffformatcontext(s);
    AVPacket *pkt = si->parse_pkt;
    int ret;

    if (!in) {
        if (ffofmt(s->oformat)->flags_internal & FF_OFMT_FLAG_ALLOW_FLUSH) {
            ret = ffofmt(s->oformat)->write_packet(s, NULL);
            flush_if_needed(s);
            if (ret >= 0 && s->pb && s->pb->error < 0)
                ret = s->pb->error;
            return ret;
        }
        return 1;
    }

    if (in->flags & AV_PKT_FLAG_UNCODED_FRAME) {
        pkt = in;
    } else {
        /* We don't own in, so we have to make sure not to modify it.
         * (ff_write_chained() relies on this fact.)
         * The following avoids copying in's data unnecessarily.
         * Copying side data is unavoidable as a bitstream filter
         * may change it, e.g. free it on errors. */
        pkt->data = in->data;
        pkt->size = in->size;
        ret = av_packet_copy_props(pkt, in);
        if (ret < 0)
            return ret;
        if (in->buf) {
            pkt->buf = av_buffer_ref(in->buf);
            if (!pkt->buf) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }
        }
    }

    ret = write_packets_common(s, pkt, 0/*non-interleaved*/);

fail:
    // Uncoded frames using the noninterleaved codepath are also freed here
    av_packet_unref(pkt);
    return ret;
}

int av_interleaved_write_frame(AVFormatContext *s, AVPacket *pkt)
{
    int ret;

    if (pkt) {
        ret = write_packets_common(s, pkt, 1/*interleaved*/);
        if (ret < 0)
            av_packet_unref(pkt);
        return ret;
    } else {
        av_log(s, AV_LOG_TRACE, "av_interleaved_write_frame FLUSH\n");
        return interleaved_write_packet(s, ffformatcontext(s)->parse_pkt, 1/*flush*/, 0);
    }
}

int av_write_trailer(AVFormatContext *s)
{
    FFFormatContext *const si = ffformatcontext(s);
    AVPacket *const pkt = si->parse_pkt;
    int ret1, ret = 0;

    for (unsigned i = 0; i < s->nb_streams; i++) {
        AVStream *const st  = s->streams[i];
        FFStream *const sti = ffstream(st);
        if (sti->bsfc) {
            ret1 = write_packets_from_bsfs(s, st, pkt, 1/*interleaved*/);
            if (ret1 < 0)
                av_packet_unref(pkt);
            if (ret >= 0)
                ret = ret1;
        }
    }
    ret1 = interleaved_write_packet(s, pkt, 1, 0);
    if (ret >= 0)
        ret = ret1;

    if (ffofmt(s->oformat)->write_trailer) {
        if (!(s->oformat->flags & AVFMT_NOFILE) && s->pb)
            avio_write_marker(s->pb, AV_NOPTS_VALUE, AVIO_DATA_MARKER_TRAILER);
        ret1 = ffofmt(s->oformat)->write_trailer(s);
        if (ret >= 0)
            ret = ret1;
    }

    deinit_muxer(s);

    if (s->pb)
       avio_flush(s->pb);
    if (ret == 0)
       ret = s->pb ? s->pb->error : 0;
    for (unsigned i = 0; i < s->nb_streams; i++) {
        av_freep(&s->streams[i]->priv_data);
        av_freep(&ffstream(s->streams[i])->index_entries);
    }
    if (s->oformat->priv_class)
        av_opt_free(s->priv_data);
    av_freep(&s->priv_data);
    av_packet_unref(si->pkt);
    return ret;
}

int av_get_output_timestamp(struct AVFormatContext *s, int stream,
                            int64_t *dts, int64_t *wall)
{
    const FFOutputFormat *const of = ffofmt(s->oformat);
    if (!of || !of->get_output_timestamp)
        return AVERROR(ENOSYS);
    of->get_output_timestamp(s, stream, dts, wall);
    return 0;
}

int ff_stream_add_bitstream_filter(AVStream *st, const char *name, const char *args)
{
    int ret;
    const AVBitStreamFilter *bsf;
    FFStream *const sti = ffstream(st);
    AVBSFContext *bsfc;

    av_assert0(!sti->bsfc);

    if (!(bsf = av_bsf_get_by_name(name))) {
        av_log(NULL, AV_LOG_ERROR, "Unknown bitstream filter '%s'\n", name);
        return AVERROR_BSF_NOT_FOUND;
    }

    if ((ret = av_bsf_alloc(bsf, &bsfc)) < 0)
        return ret;

    bsfc->time_base_in = st->time_base;
    if ((ret = avcodec_parameters_copy(bsfc->par_in, st->codecpar)) < 0) {
        av_bsf_free(&bsfc);
        return ret;
    }

    if (args && bsfc->filter->priv_class) {
        if ((ret = av_set_options_string(bsfc->priv_data, args, "=", ":")) < 0) {
            av_bsf_free(&bsfc);
            return ret;
        }
    }

    if ((ret = av_bsf_init(bsfc)) < 0) {
        av_bsf_free(&bsfc);
        return ret;
    }

    sti->bsfc = bsfc;

    av_log(NULL, AV_LOG_VERBOSE,
           "Automatically inserted bitstream filter '%s'; args='%s'\n",
           name, args ? args : "");
    return 1;
}

int ff_write_chained(AVFormatContext *dst, int dst_stream, AVPacket *pkt,
                     AVFormatContext *src, int interleave)
{
    int64_t pts = pkt->pts, dts = pkt->dts, duration = pkt->duration;
    int stream_index = pkt->stream_index;
    AVRational time_base = pkt->time_base;
    int ret;

    pkt->stream_index = dst_stream;

    av_packet_rescale_ts(pkt,
                         src->streams[stream_index]->time_base,
                         dst->streams[dst_stream]->time_base);

    if (!interleave) {
        ret = av_write_frame(dst, pkt);
        /* We only have to backup and restore the fields that
         * we changed ourselves, because av_write_frame() does not
         * modify the packet given to it. */
        pkt->pts          = pts;
        pkt->dts          = dts;
        pkt->duration     = duration;
        pkt->stream_index = stream_index;
        pkt->time_base    = time_base;
    } else
        ret = av_interleaved_write_frame(dst, pkt);

    return ret;
}

static void uncoded_frame_free(void *unused, uint8_t *data)
{
    av_frame_free((AVFrame **)data);
    av_free(data);
}

static int write_uncoded_frame_internal(AVFormatContext *s, int stream_index,
                                        AVFrame *frame, int interleaved)
{
    FFFormatContext *const si = ffformatcontext(s);
    AVPacket *pkt = si->parse_pkt;

    av_assert0(s->oformat);
    if (!ffofmt(s->oformat)->write_uncoded_frame) {
        av_frame_free(&frame);
        return AVERROR(ENOSYS);
    }

    if (!frame) {
        pkt = NULL;
    } else {
        size_t   bufsize = sizeof(frame) + AV_INPUT_BUFFER_PADDING_SIZE;
        AVFrame **framep = av_mallocz(bufsize);

        if (!framep)
            goto fail;
        pkt->buf = av_buffer_create((void *)framep, bufsize,
                                   uncoded_frame_free, NULL, 0);
        if (!pkt->buf) {
            av_free(framep);
    fail:
            av_frame_free(&frame);
            return AVERROR(ENOMEM);
        }
        *framep = frame;

        pkt->data         = (void *)framep;
        pkt->size         = sizeof(frame);
        pkt->pts          =
        pkt->dts          = frame->pts;
        pkt->duration = frame->duration;
        pkt->stream_index = stream_index;
        pkt->flags |= AV_PKT_FLAG_UNCODED_FRAME;
    }

    return interleaved ? av_interleaved_write_frame(s, pkt) :
                         av_write_frame(s, pkt);
}

int av_write_uncoded_frame(AVFormatContext *s, int stream_index,
                           AVFrame *frame)
{
    return write_uncoded_frame_internal(s, stream_index, frame, 0);
}

int av_interleaved_write_uncoded_frame(AVFormatContext *s, int stream_index,
                                       AVFrame *frame)
{
    return write_uncoded_frame_internal(s, stream_index, frame, 1);
}

int av_write_uncoded_frame_query(AVFormatContext *s, int stream_index)
{
    const FFOutputFormat *const of = ffofmt(s->oformat);
    av_assert0(of);
    if (!of->write_uncoded_frame)
        return AVERROR(ENOSYS);
    return of->write_uncoded_frame(s, stream_index, NULL,
                                   AV_WRITE_UNCODED_FRAME_QUERY);
}
