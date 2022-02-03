/*
 * various utility functions for use within FFmpeg
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
#include "libavutil/bprint.h"
#include "libavutil/dict.h"
#include "libavutil/internal.h"
#include "libavutil/intmath.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/pixfmt.h"
#include "libavutil/thread.h"
#include "libavutil/time.h"

#include "libavcodec/bsf.h"
#include "libavcodec/bytestream.h"
#include "libavcodec/internal.h"
#include "libavcodec/packet_internal.h"

#include "avformat.h"
#include "avio_internal.h"
#include "internal.h"
#if CONFIG_NETWORK
#include "network.h"
#endif

#include "libavutil/ffversion.h"
const char av_format_ffversion[] = "FFmpeg version " FFMPEG_VERSION;

static AVMutex avformat_mutex = AV_MUTEX_INITIALIZER;

/**
 * @file
 * various utility functions for use within FFmpeg
 */

unsigned avformat_version(void)
{
    av_assert0(LIBAVFORMAT_VERSION_MICRO >= 100);
    return LIBAVFORMAT_VERSION_INT;
}

const char *avformat_configuration(void)
{
    return FFMPEG_CONFIGURATION;
}

const char *avformat_license(void)
{
#define LICENSE_PREFIX "libavformat license: "
    return &LICENSE_PREFIX FFMPEG_LICENSE[sizeof(LICENSE_PREFIX) - 1];
}

int ff_lock_avformat(void)
{
    return ff_mutex_lock(&avformat_mutex) ? -1 : 0;
}

int ff_unlock_avformat(void)
{
    return ff_mutex_unlock(&avformat_mutex) ? -1 : 0;
}

int64_t av_stream_get_end_pts(const AVStream *st)
{
    if (cffstream(st)->priv_pts) {
        return cffstream(st)->priv_pts->val;
    } else
        return AV_NOPTS_VALUE;
}

struct AVCodecParserContext *av_stream_get_parser(const AVStream *st)
{
    return cffstream(st)->parser;
}

void avpriv_stream_set_need_parsing(AVStream *st, enum AVStreamParseType type)
{
    ffstream(st)->need_parsing = type;
}

void av_format_inject_global_side_data(AVFormatContext *s)
{
    FFFormatContext *const si = ffformatcontext(s);
    si->inject_global_side_data = 1;
    for (unsigned i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        ffstream(st)->inject_global_side_data = 1;
    }
}

int ff_copy_whiteblacklists(AVFormatContext *dst, const AVFormatContext *src)
{
    av_assert0(!dst->codec_whitelist &&
               !dst->format_whitelist &&
               !dst->protocol_whitelist &&
               !dst->protocol_blacklist);
    dst-> codec_whitelist = av_strdup(src->codec_whitelist);
    dst->format_whitelist = av_strdup(src->format_whitelist);
    dst->protocol_whitelist = av_strdup(src->protocol_whitelist);
    dst->protocol_blacklist = av_strdup(src->protocol_blacklist);
    if (   (src-> codec_whitelist && !dst-> codec_whitelist)
        || (src->  format_whitelist && !dst->  format_whitelist)
        || (src->protocol_whitelist && !dst->protocol_whitelist)
        || (src->protocol_blacklist && !dst->protocol_blacklist)) {
        av_log(dst, AV_LOG_ERROR, "Failed to duplicate black/whitelist\n");
        return AVERROR(ENOMEM);
    }
    return 0;
}

const AVCodec *ff_find_decoder(AVFormatContext *s, const AVStream *st,
                               enum AVCodecID codec_id)
{
    switch (st->codecpar->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
        if (s->video_codec)    return s->video_codec;
        break;
    case AVMEDIA_TYPE_AUDIO:
        if (s->audio_codec)    return s->audio_codec;
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        if (s->subtitle_codec) return s->subtitle_codec;
        break;
    }

    return avcodec_find_decoder(codec_id);
}

/* an arbitrarily chosen "sane" max packet size -- 50M */
#define SANE_CHUNK_SIZE (50000000)

/* Read the data in sane-sized chunks and append to pkt.
 * Return the number of bytes read or an error. */
static int append_packet_chunked(AVIOContext *s, AVPacket *pkt, int size)
{
    int orig_size      = pkt->size;
    int ret;

    do {
        int prev_size = pkt->size;
        int read_size;

        /* When the caller requests a lot of data, limit it to the amount
         * left in file or SANE_CHUNK_SIZE when it is not known. */
        read_size = size;
        if (read_size > SANE_CHUNK_SIZE/10) {
            read_size = ffio_limit(s, read_size);
            // If filesize/maxsize is unknown, limit to SANE_CHUNK_SIZE
            if (ffiocontext(s)->maxsize < 0)
                read_size = FFMIN(read_size, SANE_CHUNK_SIZE);
        }

        ret = av_grow_packet(pkt, read_size);
        if (ret < 0)
            break;

        ret = avio_read(s, pkt->data + prev_size, read_size);
        if (ret != read_size) {
            av_shrink_packet(pkt, prev_size + FFMAX(ret, 0));
            break;
        }

        size -= read_size;
    } while (size > 0);
    if (size > 0)
        pkt->flags |= AV_PKT_FLAG_CORRUPT;

    if (!pkt->size)
        av_packet_unref(pkt);
    return pkt->size > orig_size ? pkt->size - orig_size : ret;
}

int av_get_packet(AVIOContext *s, AVPacket *pkt, int size)
{
#if FF_API_INIT_PACKET
FF_DISABLE_DEPRECATION_WARNINGS
    av_init_packet(pkt);
    pkt->data = NULL;
    pkt->size = 0;
FF_ENABLE_DEPRECATION_WARNINGS
#else
    av_packet_unref(pkt);
#endif
    pkt->pos  = avio_tell(s);

    return append_packet_chunked(s, pkt, size);
}

int av_append_packet(AVIOContext *s, AVPacket *pkt, int size)
{
    if (!pkt->size)
        return av_get_packet(s, pkt, size);
    return append_packet_chunked(s, pkt, size);
}

int av_filename_number_test(const char *filename)
{
    char buf[1024];
    return filename &&
           (av_get_frame_filename(buf, sizeof(buf), filename, 1) >= 0);
}

int avformat_queue_attached_pictures(AVFormatContext *s)
{
    FFFormatContext *const si = ffformatcontext(s);
    int ret;
    for (unsigned i = 0; i < s->nb_streams; i++)
        if (s->streams[i]->disposition & AV_DISPOSITION_ATTACHED_PIC &&
            s->streams[i]->discard < AVDISCARD_ALL) {
            if (s->streams[i]->attached_pic.size <= 0) {
                av_log(s, AV_LOG_WARNING,
                    "Attached picture on stream %d has invalid size, "
                    "ignoring\n", i);
                continue;
            }

            ret = avpriv_packet_list_put(&si->raw_packet_buffer,
                                     &s->streams[i]->attached_pic,
                                     av_packet_ref, 0);
            if (ret < 0)
                return ret;
        }
    return 0;
}

int ff_add_attached_pic(AVFormatContext *s, AVStream *st0, AVIOContext *pb,
                        AVBufferRef **buf, int size)
{
    AVStream *st = st0;
    AVPacket *pkt;
    int ret;

    if (!st && !(st = avformat_new_stream(s, NULL)))
        return AVERROR(ENOMEM);
    pkt = &st->attached_pic;
    if (buf) {
        av_assert1(*buf);
        av_packet_unref(pkt);
        pkt->buf  = *buf;
        pkt->data = (*buf)->data;
        pkt->size = (*buf)->size - AV_INPUT_BUFFER_PADDING_SIZE;
        *buf = NULL;
    } else {
        ret = av_get_packet(pb, pkt, size);
        if (ret < 0)
            goto fail;
    }
    st->disposition         |= AV_DISPOSITION_ATTACHED_PIC;
    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;

    pkt->stream_index = st->index;
    pkt->flags       |= AV_PKT_FLAG_KEY;

    return 0;
fail:
    if (!st0)
        ff_free_stream(s, st);
    return ret;
}

/**********************************************************/

int ff_is_intra_only(enum AVCodecID id)
{
    const AVCodecDescriptor *d = avcodec_descriptor_get(id);
    if (!d)
        return 0;
    if ((d->type == AVMEDIA_TYPE_VIDEO || d->type == AVMEDIA_TYPE_AUDIO) &&
        !(d->props & AV_CODEC_PROP_INTRA_ONLY))
        return 0;
    return 1;
}

/* XXX: suppress the packet queue */
void ff_flush_packet_queue(AVFormatContext *s)
{
    FFFormatContext *const si = ffformatcontext(s);
    avpriv_packet_list_free(&si->parse_queue);
    avpriv_packet_list_free(&si->packet_buffer);
    avpriv_packet_list_free(&si->raw_packet_buffer);

    si->raw_packet_buffer_size = 0;
}

int av_find_default_stream_index(AVFormatContext *s)
{
    int best_stream = 0;
    int best_score = INT_MIN;

    if (s->nb_streams <= 0)
        return -1;
    for (unsigned i = 0; i < s->nb_streams; i++) {
        const AVStream *const st = s->streams[i];
        const FFStream *const sti = cffstream(st);
        int score = 0;
        if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            if (st->disposition & AV_DISPOSITION_ATTACHED_PIC)
                score -= 400;
            if (st->codecpar->width && st->codecpar->height)
                score += 50;
            score+= 25;
        }
        if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (st->codecpar->sample_rate)
                score += 50;
        }
        if (sti->codec_info_nb_frames)
            score += 12;

        if (st->discard != AVDISCARD_ALL)
            score += 200;

        if (score > best_score) {
            best_score = score;
            best_stream = i;
        }
    }
    return best_stream;
}

/*******************************************************/

unsigned int ff_codec_get_tag(const AVCodecTag *tags, enum AVCodecID id)
{
    while (tags->id != AV_CODEC_ID_NONE) {
        if (tags->id == id)
            return tags->tag;
        tags++;
    }
    return 0;
}

enum AVCodecID ff_codec_get_id(const AVCodecTag *tags, unsigned int tag)
{
    for (int i = 0; tags[i].id != AV_CODEC_ID_NONE; i++)
        if (tag == tags[i].tag)
            return tags[i].id;
    for (int i = 0; tags[i].id != AV_CODEC_ID_NONE; i++)
        if (ff_toupper4(tag) == ff_toupper4(tags[i].tag))
            return tags[i].id;
    return AV_CODEC_ID_NONE;
}

enum AVCodecID ff_get_pcm_codec_id(int bps, int flt, int be, int sflags)
{
    if (bps <= 0 || bps > 64)
        return AV_CODEC_ID_NONE;

    if (flt) {
        switch (bps) {
        case 32:
            return be ? AV_CODEC_ID_PCM_F32BE : AV_CODEC_ID_PCM_F32LE;
        case 64:
            return be ? AV_CODEC_ID_PCM_F64BE : AV_CODEC_ID_PCM_F64LE;
        default:
            return AV_CODEC_ID_NONE;
        }
    } else {
        bps  += 7;
        bps >>= 3;
        if (sflags & (1 << (bps - 1))) {
            switch (bps) {
            case 1:
                return AV_CODEC_ID_PCM_S8;
            case 2:
                return be ? AV_CODEC_ID_PCM_S16BE : AV_CODEC_ID_PCM_S16LE;
            case 3:
                return be ? AV_CODEC_ID_PCM_S24BE : AV_CODEC_ID_PCM_S24LE;
            case 4:
                return be ? AV_CODEC_ID_PCM_S32BE : AV_CODEC_ID_PCM_S32LE;
            case 8:
                return be ? AV_CODEC_ID_PCM_S64BE : AV_CODEC_ID_PCM_S64LE;
            default:
                return AV_CODEC_ID_NONE;
            }
        } else {
            switch (bps) {
            case 1:
                return AV_CODEC_ID_PCM_U8;
            case 2:
                return be ? AV_CODEC_ID_PCM_U16BE : AV_CODEC_ID_PCM_U16LE;
            case 3:
                return be ? AV_CODEC_ID_PCM_U24BE : AV_CODEC_ID_PCM_U24LE;
            case 4:
                return be ? AV_CODEC_ID_PCM_U32BE : AV_CODEC_ID_PCM_U32LE;
            default:
                return AV_CODEC_ID_NONE;
            }
        }
    }
}

unsigned int av_codec_get_tag(const AVCodecTag *const *tags, enum AVCodecID id)
{
    unsigned int tag;
    if (!av_codec_get_tag2(tags, id, &tag))
        return 0;
    return tag;
}

int av_codec_get_tag2(const AVCodecTag * const *tags, enum AVCodecID id,
                      unsigned int *tag)
{
    for (int i = 0; tags && tags[i]; i++) {
        const AVCodecTag *codec_tags = tags[i];
        while (codec_tags->id != AV_CODEC_ID_NONE) {
            if (codec_tags->id == id) {
                *tag = codec_tags->tag;
                return 1;
            }
            codec_tags++;
        }
    }
    return 0;
}

enum AVCodecID av_codec_get_id(const AVCodecTag *const *tags, unsigned int tag)
{
    for (int i = 0; tags && tags[i]; i++) {
        enum AVCodecID id = ff_codec_get_id(tags[i], tag);
        if (id != AV_CODEC_ID_NONE)
            return id;
    }
    return AV_CODEC_ID_NONE;
}

int ff_alloc_extradata(AVCodecParameters *par, int size)
{
    av_freep(&par->extradata);
    par->extradata_size = 0;

    if (size < 0 || size >= INT32_MAX - AV_INPUT_BUFFER_PADDING_SIZE)
        return AVERROR(EINVAL);

    par->extradata = av_malloc(size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!par->extradata)
        return AVERROR(ENOMEM);

    memset(par->extradata + size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    par->extradata_size = size;

    return 0;
}

int ff_get_extradata(AVFormatContext *s, AVCodecParameters *par, AVIOContext *pb, int size)
{
    int ret = ff_alloc_extradata(par, size);
    if (ret < 0)
        return ret;
    ret = ffio_read_size(pb, par->extradata, size);
    if (ret < 0) {
        av_freep(&par->extradata);
        par->extradata_size = 0;
        av_log(s, AV_LOG_ERROR, "Failed to read extradata of size %d\n", size);
        return ret;
    }

    return ret;
}

AVProgram *av_find_program_from_stream(AVFormatContext *ic, AVProgram *last, int s)
{
    for (unsigned i = 0; i < ic->nb_programs; i++) {
        if (ic->programs[i] == last) {
            last = NULL;
        } else {
            if (!last)
                for (unsigned j = 0; j < ic->programs[i]->nb_stream_indexes; j++)
                    if (ic->programs[i]->stream_index[j] == s)
                        return ic->programs[i];
        }
    }
    return NULL;
}

int av_find_best_stream(AVFormatContext *ic, enum AVMediaType type,
                        int wanted_stream_nb, int related_stream,
                        const AVCodec **decoder_ret, int flags)
{
    int nb_streams = ic->nb_streams;
    int ret = AVERROR_STREAM_NOT_FOUND;
    int best_count = -1, best_multiframe = -1, best_disposition = -1;
    int count, multiframe, disposition;
    int64_t best_bitrate = -1;
    int64_t bitrate;
    unsigned *program = NULL;
    const AVCodec *decoder = NULL, *best_decoder = NULL;

    if (related_stream >= 0 && wanted_stream_nb < 0) {
        AVProgram *p = av_find_program_from_stream(ic, NULL, related_stream);
        if (p) {
            program    = p->stream_index;
            nb_streams = p->nb_stream_indexes;
        }
    }
    for (unsigned i = 0; i < nb_streams; i++) {
        int real_stream_index = program ? program[i] : i;
        AVStream *st          = ic->streams[real_stream_index];
        AVCodecParameters *par = st->codecpar;
        if (par->codec_type != type)
            continue;
        if (wanted_stream_nb >= 0 && real_stream_index != wanted_stream_nb)
            continue;
        if (type == AVMEDIA_TYPE_AUDIO && !(par->channels && par->sample_rate))
            continue;
        if (decoder_ret) {
            decoder = ff_find_decoder(ic, st, par->codec_id);
            if (!decoder) {
                if (ret < 0)
                    ret = AVERROR_DECODER_NOT_FOUND;
                continue;
            }
        }
        disposition = !(st->disposition & (AV_DISPOSITION_HEARING_IMPAIRED | AV_DISPOSITION_VISUAL_IMPAIRED))
                      + !! (st->disposition & AV_DISPOSITION_DEFAULT);
        count = ffstream(st)->codec_info_nb_frames;
        bitrate = par->bit_rate;
        multiframe = FFMIN(5, count);
        if ((best_disposition >  disposition) ||
            (best_disposition == disposition && best_multiframe >  multiframe) ||
            (best_disposition == disposition && best_multiframe == multiframe && best_bitrate >  bitrate) ||
            (best_disposition == disposition && best_multiframe == multiframe && best_bitrate == bitrate && best_count >= count))
            continue;
        best_disposition = disposition;
        best_count   = count;
        best_bitrate = bitrate;
        best_multiframe = multiframe;
        ret          = real_stream_index;
        best_decoder = decoder;
        if (program && i == nb_streams - 1 && ret < 0) {
            program    = NULL;
            nb_streams = ic->nb_streams;
            /* no related stream found, try again with everything */
            i = 0;
        }
    }
    if (decoder_ret)
        *decoder_ret = best_decoder;
    return ret;
}

/*******************************************************/

int av_read_play(AVFormatContext *s)
{
    if (s->iformat->read_play)
        return s->iformat->read_play(s);
    if (s->pb)
        return avio_pause(s->pb, 0);
    return AVERROR(ENOSYS);
}

int av_read_pause(AVFormatContext *s)
{
    if (s->iformat->read_pause)
        return s->iformat->read_pause(s);
    if (s->pb)
        return avio_pause(s->pb, 1);
    return AVERROR(ENOSYS);
}

int ff_stream_encode_params_copy(AVStream *dst, const AVStream *src)
{
    int ret;

    dst->id                  = src->id;
    dst->time_base           = src->time_base;
    dst->nb_frames           = src->nb_frames;
    dst->disposition         = src->disposition;
    dst->sample_aspect_ratio = src->sample_aspect_ratio;
    dst->avg_frame_rate      = src->avg_frame_rate;
    dst->r_frame_rate        = src->r_frame_rate;

    av_dict_free(&dst->metadata);
    ret = av_dict_copy(&dst->metadata, src->metadata, 0);
    if (ret < 0)
        return ret;

    ret = avcodec_parameters_copy(dst->codecpar, src->codecpar);
    if (ret < 0)
        return ret;

    ret = ff_stream_side_data_copy(dst, src);
    if (ret < 0)
        return ret;

    return 0;
}

int ff_stream_side_data_copy(AVStream *dst, const AVStream *src)
{
    /* Free existing side data*/
    for (int i = 0; i < dst->nb_side_data; i++)
        av_free(dst->side_data[i].data);
    av_freep(&dst->side_data);
    dst->nb_side_data = 0;

    /* Copy side data if present */
    if (src->nb_side_data) {
        dst->side_data = av_calloc(src->nb_side_data,
                                   sizeof(*dst->side_data));
        if (!dst->side_data)
            return AVERROR(ENOMEM);
        dst->nb_side_data = src->nb_side_data;

        for (int i = 0; i < src->nb_side_data; i++) {
            uint8_t *data = av_memdup(src->side_data[i].data,
                                      src->side_data[i].size);
            if (!data)
                return AVERROR(ENOMEM);
            dst->side_data[i].type = src->side_data[i].type;
            dst->side_data[i].size = src->side_data[i].size;
            dst->side_data[i].data = data;
        }
    }

    return 0;
}

static void free_stream(AVStream **pst)
{
    AVStream *st = *pst;
    FFStream *const sti = ffstream(st);

    if (!st)
        return;

    for (int i = 0; i < st->nb_side_data; i++)
        av_freep(&st->side_data[i].data);
    av_freep(&st->side_data);

    if (st->attached_pic.data)
        av_packet_unref(&st->attached_pic);

    av_parser_close(sti->parser);
    avcodec_free_context(&sti->avctx);
    av_bsf_free(&sti->bsfc);
    av_freep(&sti->priv_pts);
    av_freep(&sti->index_entries);
    av_freep(&sti->probe_data.buf);

    av_bsf_free(&sti->extract_extradata.bsf);

    if (sti->info) {
        av_freep(&sti->info->duration_error);
        av_freep(&sti->info);
    }

    av_dict_free(&st->metadata);
    avcodec_parameters_free(&st->codecpar);
    av_freep(&st->priv_data);

    av_freep(pst);
}

void ff_free_stream(AVFormatContext *s, AVStream *st)
{
    av_assert0(s->nb_streams>0);
    av_assert0(s->streams[ s->nb_streams - 1 ] == st);

    free_stream(&s->streams[ --s->nb_streams ]);
}

void avformat_free_context(AVFormatContext *s)
{
    FFFormatContext *si;

    if (!s)
        return;
    si = ffformatcontext(s);

    if (s->oformat && s->oformat->deinit && si->initialized)
        s->oformat->deinit(s);

    av_opt_free(s);
    if (s->iformat && s->iformat->priv_class && s->priv_data)
        av_opt_free(s->priv_data);
    if (s->oformat && s->oformat->priv_class && s->priv_data)
        av_opt_free(s->priv_data);

    for (unsigned i = 0; i < s->nb_streams; i++)
        free_stream(&s->streams[i]);
    s->nb_streams = 0;

    for (unsigned i = 0; i < s->nb_programs; i++) {
        av_dict_free(&s->programs[i]->metadata);
        av_freep(&s->programs[i]->stream_index);
        av_freep(&s->programs[i]);
    }
    s->nb_programs = 0;

    av_freep(&s->programs);
    av_freep(&s->priv_data);
    while (s->nb_chapters--) {
        av_dict_free(&s->chapters[s->nb_chapters]->metadata);
        av_freep(&s->chapters[s->nb_chapters]);
    }
    av_freep(&s->chapters);
    av_dict_free(&s->metadata);
    av_dict_free(&si->id3v2_meta);
    av_packet_free(&si->pkt);
    av_packet_free(&si->parse_pkt);
    av_freep(&s->streams);
    ff_flush_packet_queue(s);
    av_freep(&s->url);
    av_free(s);
}

static const AVOption stream_options[] = {
    { "disposition", NULL, offsetof(AVStream, disposition), AV_OPT_TYPE_FLAGS, { .i64 = 0 },
        .flags = AV_OPT_FLAG_ENCODING_PARAM, .unit = "disposition" },
        { "default",            .type = AV_OPT_TYPE_CONST, { .i64 = AV_DISPOSITION_DEFAULT           },    .unit = "disposition" },
        { "dub",                .type = AV_OPT_TYPE_CONST, { .i64 = AV_DISPOSITION_DUB               },    .unit = "disposition" },
        { "original",           .type = AV_OPT_TYPE_CONST, { .i64 = AV_DISPOSITION_ORIGINAL          },    .unit = "disposition" },
        { "comment",            .type = AV_OPT_TYPE_CONST, { .i64 = AV_DISPOSITION_COMMENT           },    .unit = "disposition" },
        { "lyrics",             .type = AV_OPT_TYPE_CONST, { .i64 = AV_DISPOSITION_LYRICS            },    .unit = "disposition" },
        { "karaoke",            .type = AV_OPT_TYPE_CONST, { .i64 = AV_DISPOSITION_KARAOKE           },    .unit = "disposition" },
        { "forced",             .type = AV_OPT_TYPE_CONST, { .i64 = AV_DISPOSITION_FORCED            },    .unit = "disposition" },
        { "hearing_impaired",   .type = AV_OPT_TYPE_CONST, { .i64 = AV_DISPOSITION_HEARING_IMPAIRED  },    .unit = "disposition" },
        { "visual_impaired",    .type = AV_OPT_TYPE_CONST, { .i64 = AV_DISPOSITION_VISUAL_IMPAIRED   },    .unit = "disposition" },
        { "clean_effects",      .type = AV_OPT_TYPE_CONST, { .i64 = AV_DISPOSITION_CLEAN_EFFECTS     },    .unit = "disposition" },
        { "attached_pic",       .type = AV_OPT_TYPE_CONST, { .i64 = AV_DISPOSITION_ATTACHED_PIC      },    .unit = "disposition" },
        { "timed_thumbnails",   .type = AV_OPT_TYPE_CONST, { .i64 = AV_DISPOSITION_TIMED_THUMBNAILS  },    .unit = "disposition" },
        { "captions",           .type = AV_OPT_TYPE_CONST, { .i64 = AV_DISPOSITION_CAPTIONS          },    .unit = "disposition" },
        { "descriptions",       .type = AV_OPT_TYPE_CONST, { .i64 = AV_DISPOSITION_DESCRIPTIONS      },    .unit = "disposition" },
        { "metadata",           .type = AV_OPT_TYPE_CONST, { .i64 = AV_DISPOSITION_METADATA          },    .unit = "disposition" },
        { "dependent",          .type = AV_OPT_TYPE_CONST, { .i64 = AV_DISPOSITION_DEPENDENT         },    .unit = "disposition" },
        { "still_image",        .type = AV_OPT_TYPE_CONST, { .i64 = AV_DISPOSITION_STILL_IMAGE       },    .unit = "disposition" },
    { NULL }
};

static const AVClass stream_class = {
    .class_name     = "AVStream",
    .item_name      = av_default_item_name,
    .version        = LIBAVUTIL_VERSION_INT,
    .option         = stream_options,
};

const AVClass *av_stream_get_class(void)
{
    return &stream_class;
}

AVStream *avformat_new_stream(AVFormatContext *s, const AVCodec *c)
{
    FFFormatContext *const si = ffformatcontext(s);
    FFStream *sti;
    AVStream *st;
    AVStream **streams;

    if (s->nb_streams >= s->max_streams) {
        av_log(s, AV_LOG_ERROR, "Number of streams exceeds max_streams parameter"
               " (%d), see the documentation if you wish to increase it\n",
               s->max_streams);
        return NULL;
    }
    streams = av_realloc_array(s->streams, s->nb_streams + 1, sizeof(*streams));
    if (!streams)
        return NULL;
    s->streams = streams;


    sti = av_mallocz(sizeof(*sti));
    if (!sti)
        return NULL;
    st = &sti->pub;

#if FF_API_AVSTREAM_CLASS
    st->av_class = &stream_class;
#endif

    st->codecpar = avcodec_parameters_alloc();
    if (!st->codecpar)
        goto fail;

    sti->avctx = avcodec_alloc_context3(NULL);
    if (!sti->avctx)
        goto fail;

    if (s->iformat) {
        sti->info = av_mallocz(sizeof(*sti->info));
        if (!sti->info)
            goto fail;

#if FF_API_R_FRAME_RATE
        sti->info->last_dts      = AV_NOPTS_VALUE;
#endif
        sti->info->fps_first_dts = AV_NOPTS_VALUE;
        sti->info->fps_last_dts  = AV_NOPTS_VALUE;

        /* default pts setting is MPEG-like */
        avpriv_set_pts_info(st, 33, 1, 90000);
        /* we set the current DTS to 0 so that formats without any timestamps
         * but durations get some timestamps, formats with some unknown
         * timestamps have their first few packets buffered and the
         * timestamps corrected before they are returned to the user */
        sti->cur_dts = RELATIVE_TS_BASE;
    } else {
        sti->cur_dts = AV_NOPTS_VALUE;
    }

    st->index      = s->nb_streams;
    st->start_time = AV_NOPTS_VALUE;
    st->duration   = AV_NOPTS_VALUE;
    sti->first_dts     = AV_NOPTS_VALUE;
    sti->probe_packets = s->max_probe_packets;
    sti->pts_wrap_reference = AV_NOPTS_VALUE;
    sti->pts_wrap_behavior  = AV_PTS_WRAP_IGNORE;

    sti->last_IP_pts = AV_NOPTS_VALUE;
    sti->last_dts_for_order_check = AV_NOPTS_VALUE;
    for (int i = 0; i < MAX_REORDER_DELAY + 1; i++)
        sti->pts_buffer[i] = AV_NOPTS_VALUE;

    st->sample_aspect_ratio = (AVRational) { 0, 1 };

    sti->inject_global_side_data = si->inject_global_side_data;

    sti->need_context_update = 1;

    s->streams[s->nb_streams++] = st;
    return st;
fail:
    free_stream(&st);
    return NULL;
}

AVProgram *av_new_program(AVFormatContext *ac, int id)
{
    AVProgram *program = NULL;
    int ret;

    av_log(ac, AV_LOG_TRACE, "new_program: id=0x%04x\n", id);

    for (unsigned i = 0; i < ac->nb_programs; i++)
        if (ac->programs[i]->id == id)
            program = ac->programs[i];

    if (!program) {
        program = av_mallocz(sizeof(AVProgram));
        if (!program)
            return NULL;
        ret = av_dynarray_add_nofree(&ac->programs, &ac->nb_programs, program);
        if (ret < 0) {
            av_free(program);
            return NULL;
        }
        program->discard = AVDISCARD_NONE;
        program->pmt_version = -1;
        program->id = id;
        program->pts_wrap_reference = AV_NOPTS_VALUE;
        program->pts_wrap_behavior = AV_PTS_WRAP_IGNORE;
        program->start_time =
        program->end_time   = AV_NOPTS_VALUE;
    }
    return program;
}

AVChapter *avpriv_new_chapter(AVFormatContext *s, int64_t id, AVRational time_base,
                              int64_t start, int64_t end, const char *title)
{
    FFFormatContext *const si = ffformatcontext(s);
    AVChapter *chapter = NULL;
    int ret;

    if (end != AV_NOPTS_VALUE && start > end) {
        av_log(s, AV_LOG_ERROR, "Chapter end time %"PRId64" before start %"PRId64"\n", end, start);
        return NULL;
    }

    if (!s->nb_chapters) {
        si->chapter_ids_monotonic = 1;
    } else if (!si->chapter_ids_monotonic || s->chapters[s->nb_chapters-1]->id >= id) {
        for (unsigned i = 0; i < s->nb_chapters; i++)
            if (s->chapters[i]->id == id)
                chapter = s->chapters[i];
        if (!chapter)
            si->chapter_ids_monotonic = 0;
    }

    if (!chapter) {
        chapter = av_mallocz(sizeof(AVChapter));
        if (!chapter)
            return NULL;
        ret = av_dynarray_add_nofree(&s->chapters, &s->nb_chapters, chapter);
        if (ret < 0) {
            av_free(chapter);
            return NULL;
        }
    }
    av_dict_set(&chapter->metadata, "title", title, 0);
    chapter->id        = id;
    chapter->time_base = time_base;
    chapter->start     = start;
    chapter->end       = end;

    return chapter;
}

void av_program_add_stream_index(AVFormatContext *ac, int progid, unsigned idx)
{
    AVProgram *program = NULL;
    void *tmp;

    if (idx >= ac->nb_streams) {
        av_log(ac, AV_LOG_ERROR, "stream index %d is not valid\n", idx);
        return;
    }

    for (unsigned i = 0; i < ac->nb_programs; i++) {
        if (ac->programs[i]->id != progid)
            continue;
        program = ac->programs[i];
        for (unsigned j = 0; j < program->nb_stream_indexes; j++)
            if (program->stream_index[j] == idx)
                return;

        tmp = av_realloc_array(program->stream_index, program->nb_stream_indexes+1, sizeof(unsigned int));
        if (!tmp)
            return;
        program->stream_index = tmp;
        program->stream_index[program->nb_stream_indexes++] = idx;
        return;
    }
}

uint64_t ff_ntp_time(void)
{
    return (av_gettime() / 1000) * 1000 + NTP_OFFSET_US;
}

uint64_t ff_get_formatted_ntp_time(uint64_t ntp_time_us)
{
    uint64_t ntp_ts, frac_part, sec;
    uint32_t usec;

    //current ntp time in seconds and micro seconds
    sec = ntp_time_us / 1000000;
    usec = ntp_time_us % 1000000;

    //encoding in ntp timestamp format
    frac_part = usec * 0xFFFFFFFFULL;
    frac_part /= 1000000;

    if (sec > 0xFFFFFFFFULL)
        av_log(NULL, AV_LOG_WARNING, "NTP time format roll over detected\n");

    ntp_ts = sec << 32;
    ntp_ts |= frac_part;

    return ntp_ts;
}

uint64_t ff_parse_ntp_time(uint64_t ntp_ts)
{
    uint64_t sec = ntp_ts >> 32;
    uint64_t frac_part = ntp_ts & 0xFFFFFFFFULL;
    uint64_t usec = (frac_part * 1000000) / 0xFFFFFFFFULL;

    return (sec * 1000000) + usec;
}

int av_get_frame_filename2(char *buf, int buf_size, const char *path, int number, int flags)
{
    const char *p;
    char *q, buf1[20], c;
    int nd, len, percentd_found;

    q = buf;
    p = path;
    percentd_found = 0;
    for (;;) {
        c = *p++;
        if (c == '\0')
            break;
        if (c == '%') {
            do {
                nd = 0;
                while (av_isdigit(*p)) {
                    if (nd >= INT_MAX / 10 - 255)
                        goto fail;
                    nd = nd * 10 + *p++ - '0';
                }
                c = *p++;
            } while (av_isdigit(c));

            switch (c) {
            case '%':
                goto addchar;
            case 'd':
                if (!(flags & AV_FRAME_FILENAME_FLAGS_MULTIPLE) && percentd_found)
                    goto fail;
                percentd_found = 1;
                if (number < 0)
                    nd += 1;
                snprintf(buf1, sizeof(buf1), "%0*d", nd, number);
                len = strlen(buf1);
                if ((q - buf + len) > buf_size - 1)
                    goto fail;
                memcpy(q, buf1, len);
                q += len;
                break;
            default:
                goto fail;
            }
        } else {
addchar:
            if ((q - buf) < buf_size - 1)
                *q++ = c;
        }
    }
    if (!percentd_found)
        goto fail;
    *q = '\0';
    return 0;
fail:
    *q = '\0';
    return -1;
}

int av_get_frame_filename(char *buf, int buf_size, const char *path, int number)
{
    return av_get_frame_filename2(buf, buf_size, path, number, 0);
}

void av_url_split(char *proto, int proto_size,
                  char *authorization, int authorization_size,
                  char *hostname, int hostname_size,
                  int *port_ptr, char *path, int path_size, const char *url)
{
    const char *p, *ls, *at, *at2, *col, *brk;

    if (port_ptr)
        *port_ptr = -1;
    if (proto_size > 0)
        proto[0] = 0;
    if (authorization_size > 0)
        authorization[0] = 0;
    if (hostname_size > 0)
        hostname[0] = 0;
    if (path_size > 0)
        path[0] = 0;

    /* parse protocol */
    if ((p = strchr(url, ':'))) {
        av_strlcpy(proto, url, FFMIN(proto_size, p + 1 - url));
        p++; /* skip ':' */
        if (*p == '/')
            p++;
        if (*p == '/')
            p++;
    } else {
        /* no protocol means plain filename */
        av_strlcpy(path, url, path_size);
        return;
    }

    /* separate path from hostname */
    ls = p + strcspn(p, "/?#");
    av_strlcpy(path, ls, path_size);

    /* the rest is hostname, use that to parse auth/port */
    if (ls != p) {
        /* authorization (user[:pass]@hostname) */
        at2 = p;
        while ((at = strchr(p, '@')) && at < ls) {
            av_strlcpy(authorization, at2,
                       FFMIN(authorization_size, at + 1 - at2));
            p = at + 1; /* skip '@' */
        }

        if (*p == '[' && (brk = strchr(p, ']')) && brk < ls) {
            /* [host]:port */
            av_strlcpy(hostname, p + 1,
                       FFMIN(hostname_size, brk - p));
            if (brk[1] == ':' && port_ptr)
                *port_ptr = atoi(brk + 2);
        } else if ((col = strchr(p, ':')) && col < ls) {
            av_strlcpy(hostname, p,
                       FFMIN(col + 1 - p, hostname_size));
            if (port_ptr)
                *port_ptr = atoi(col + 1);
        } else
            av_strlcpy(hostname, p,
                       FFMIN(ls + 1 - p, hostname_size));
    }
}

int ff_mkdir_p(const char *path)
{
    int ret = 0;
    char *temp = av_strdup(path);
    char *pos = temp;
    char tmp_ch = '\0';

    if (!path || !temp) {
        return -1;
    }

    if (!av_strncasecmp(temp, "/", 1) || !av_strncasecmp(temp, "\\", 1)) {
        pos++;
    } else if (!av_strncasecmp(temp, "./", 2) || !av_strncasecmp(temp, ".\\", 2)) {
        pos += 2;
    }

    for ( ; *pos != '\0'; ++pos) {
        if (*pos == '/' || *pos == '\\') {
            tmp_ch = *pos;
            *pos = '\0';
            ret = mkdir(temp, 0755);
            *pos = tmp_ch;
        }
    }

    if ((*(pos - 1) != '/') || (*(pos - 1) != '\\')) {
        ret = mkdir(temp, 0755);
    }

    av_free(temp);
    return ret;
}

char *ff_data_to_hex(char *buff, const uint8_t *src, int s, int lowercase)
{
    static const char hex_table_uc[16] = { '0', '1', '2', '3',
                                           '4', '5', '6', '7',
                                           '8', '9', 'A', 'B',
                                           'C', 'D', 'E', 'F' };
    static const char hex_table_lc[16] = { '0', '1', '2', '3',
                                           '4', '5', '6', '7',
                                           '8', '9', 'a', 'b',
                                           'c', 'd', 'e', 'f' };
    const char *hex_table = lowercase ? hex_table_lc : hex_table_uc;

    for (int i = 0; i < s; i++) {
        buff[i * 2]     = hex_table[src[i] >> 4];
        buff[i * 2 + 1] = hex_table[src[i] & 0xF];
    }
    buff[2 * s] = '\0';

    return buff;
}

int ff_hex_to_data(uint8_t *data, const char *p)
{
    int c, len, v;

    len = 0;
    v   = 1;
    for (;;) {
        p += strspn(p, SPACE_CHARS);
        if (*p == '\0')
            break;
        c = av_toupper((unsigned char) *p++);
        if (c >= '0' && c <= '9')
            c = c - '0';
        else if (c >= 'A' && c <= 'F')
            c = c - 'A' + 10;
        else
            break;
        v = (v << 4) | c;
        if (v & 0x100) {
            if (data)
                data[len] = v;
            len++;
            v = 1;
        }
    }
    return len;
}

void avpriv_set_pts_info(AVStream *st, int pts_wrap_bits,
                         unsigned int pts_num, unsigned int pts_den)
{
    FFStream *const sti = ffstream(st);
    AVRational new_tb;
    if (av_reduce(&new_tb.num, &new_tb.den, pts_num, pts_den, INT_MAX)) {
        if (new_tb.num != pts_num)
            av_log(NULL, AV_LOG_DEBUG,
                   "st:%d removing common factor %d from timebase\n",
                   st->index, pts_num / new_tb.num);
    } else
        av_log(NULL, AV_LOG_WARNING,
               "st:%d has too large timebase, reducing\n", st->index);

    if (new_tb.num <= 0 || new_tb.den <= 0) {
        av_log(NULL, AV_LOG_ERROR,
               "Ignoring attempt to set invalid timebase %d/%d for st:%d\n",
               new_tb.num, new_tb.den,
               st->index);
        return;
    }
    st->time_base     = new_tb;
    sti->avctx->pkt_timebase = new_tb;
    st->pts_wrap_bits = pts_wrap_bits;
}

void ff_parse_key_value(const char *str, ff_parse_key_val_cb callback_get_buf,
                        void *context)
{
    const char *ptr = str;

    /* Parse key=value pairs. */
    for (;;) {
        const char *key;
        char *dest = NULL, *dest_end;
        int key_len, dest_len = 0;

        /* Skip whitespace and potential commas. */
        while (*ptr && (av_isspace(*ptr) || *ptr == ','))
            ptr++;
        if (!*ptr)
            break;

        key = ptr;

        if (!(ptr = strchr(key, '=')))
            break;
        ptr++;
        key_len = ptr - key;

        callback_get_buf(context, key, key_len, &dest, &dest_len);
        dest_end = dest ? dest + dest_len - 1 : NULL;

        if (*ptr == '\"') {
            ptr++;
            while (*ptr && *ptr != '\"') {
                if (*ptr == '\\') {
                    if (!ptr[1])
                        break;
                    if (dest && dest < dest_end)
                        *dest++ = ptr[1];
                    ptr += 2;
                } else {
                    if (dest && dest < dest_end)
                        *dest++ = *ptr;
                    ptr++;
                }
            }
            if (*ptr == '\"')
                ptr++;
        } else {
            for (; *ptr && !(av_isspace(*ptr) || *ptr == ','); ptr++)
                if (dest && dest < dest_end)
                    *dest++ = *ptr;
        }
        if (dest)
            *dest = 0;
    }
}

int ff_find_stream_index(const AVFormatContext *s, int id)
{
    for (unsigned i = 0; i < s->nb_streams; i++)
        if (s->streams[i]->id == id)
            return i;
    return -1;
}

int avformat_query_codec(const AVOutputFormat *ofmt, enum AVCodecID codec_id,
                         int std_compliance)
{
    if (ofmt) {
        unsigned int codec_tag;
        if (ofmt->query_codec)
            return ofmt->query_codec(codec_id, std_compliance);
        else if (ofmt->codec_tag)
            return !!av_codec_get_tag2(ofmt->codec_tag, codec_id, &codec_tag);
        else if (codec_id == ofmt->video_codec ||
                 codec_id == ofmt->audio_codec ||
                 codec_id == ofmt->subtitle_codec ||
                 codec_id == ofmt->data_codec)
            return 1;
    }
    return AVERROR_PATCHWELCOME;
}

int avformat_network_init(void)
{
#if CONFIG_NETWORK
    int ret;
    if ((ret = ff_network_init()) < 0)
        return ret;
    if ((ret = ff_tls_init()) < 0)
        return ret;
#endif
    return 0;
}

int avformat_network_deinit(void)
{
#if CONFIG_NETWORK
    ff_network_close();
    ff_tls_deinit();
#endif
    return 0;
}

int ff_add_param_change(AVPacket *pkt, int32_t channels,
                        uint64_t channel_layout, int32_t sample_rate,
                        int32_t width, int32_t height)
{
    uint32_t flags = 0;
    int size = 4;
    uint8_t *data;
    if (!pkt)
        return AVERROR(EINVAL);
    if (channels) {
        size  += 4;
        flags |= AV_SIDE_DATA_PARAM_CHANGE_CHANNEL_COUNT;
    }
    if (channel_layout) {
        size  += 8;
        flags |= AV_SIDE_DATA_PARAM_CHANGE_CHANNEL_LAYOUT;
    }
    if (sample_rate) {
        size  += 4;
        flags |= AV_SIDE_DATA_PARAM_CHANGE_SAMPLE_RATE;
    }
    if (width || height) {
        size  += 8;
        flags |= AV_SIDE_DATA_PARAM_CHANGE_DIMENSIONS;
    }
    data = av_packet_new_side_data(pkt, AV_PKT_DATA_PARAM_CHANGE, size);
    if (!data)
        return AVERROR(ENOMEM);
    bytestream_put_le32(&data, flags);
    if (channels)
        bytestream_put_le32(&data, channels);
    if (channel_layout)
        bytestream_put_le64(&data, channel_layout);
    if (sample_rate)
        bytestream_put_le32(&data, sample_rate);
    if (width || height) {
        bytestream_put_le32(&data, width);
        bytestream_put_le32(&data, height);
    }
    return 0;
}

AVRational av_guess_sample_aspect_ratio(AVFormatContext *format, AVStream *stream, AVFrame *frame)
{
    AVRational undef = {0, 1};
    AVRational stream_sample_aspect_ratio = stream ? stream->sample_aspect_ratio : undef;
    AVRational codec_sample_aspect_ratio  = stream && stream->codecpar ? stream->codecpar->sample_aspect_ratio : undef;
    AVRational frame_sample_aspect_ratio  = frame  ? frame->sample_aspect_ratio  : codec_sample_aspect_ratio;

    av_reduce(&stream_sample_aspect_ratio.num, &stream_sample_aspect_ratio.den,
               stream_sample_aspect_ratio.num,  stream_sample_aspect_ratio.den, INT_MAX);
    if (stream_sample_aspect_ratio.num <= 0 || stream_sample_aspect_ratio.den <= 0)
        stream_sample_aspect_ratio = undef;

    av_reduce(&frame_sample_aspect_ratio.num, &frame_sample_aspect_ratio.den,
               frame_sample_aspect_ratio.num,  frame_sample_aspect_ratio.den, INT_MAX);
    if (frame_sample_aspect_ratio.num <= 0 || frame_sample_aspect_ratio.den <= 0)
        frame_sample_aspect_ratio = undef;

    if (stream_sample_aspect_ratio.num)
        return stream_sample_aspect_ratio;
    else
        return frame_sample_aspect_ratio;
}

AVRational av_guess_frame_rate(AVFormatContext *format, AVStream *st, AVFrame *frame)
{
    AVRational fr = st->r_frame_rate;
    AVCodecContext *const avctx = ffstream(st)->avctx;
    AVRational codec_fr = avctx->framerate;
    AVRational   avg_fr = st->avg_frame_rate;

    if (avg_fr.num > 0 && avg_fr.den > 0 && fr.num > 0 && fr.den > 0 &&
        av_q2d(avg_fr) < 70 && av_q2d(fr) > 210) {
        fr = avg_fr;
    }


    if (avctx->ticks_per_frame > 1) {
        if (   codec_fr.num > 0 && codec_fr.den > 0 &&
            (fr.num == 0 || av_q2d(codec_fr) < av_q2d(fr)*0.7 && fabs(1.0 - av_q2d(av_div_q(avg_fr, fr))) > 0.1))
            fr = codec_fr;
    }

    return fr;
}

/**
 * Matches a stream specifier (but ignores requested index).
 *
 * @param indexptr set to point to the requested stream index if there is one
 *
 * @return <0 on error
 *         0  if st is NOT a matching stream
 *         >0 if st is a matching stream
 */
static int match_stream_specifier(const AVFormatContext *s, const AVStream *st,
                                  const char *spec, const char **indexptr,
                                  const AVProgram **p)
{
    int match = 1;                      /* Stores if the specifier matches so far. */
    while (*spec) {
        if (*spec <= '9' && *spec >= '0') { /* opt:index */
            if (indexptr)
                *indexptr = spec;
            return match;
        } else if (*spec == 'v' || *spec == 'a' || *spec == 's' || *spec == 'd' ||
                   *spec == 't' || *spec == 'V') { /* opt:[vasdtV] */
            enum AVMediaType type;
            int nopic = 0;

            switch (*spec++) {
            case 'v': type = AVMEDIA_TYPE_VIDEO;      break;
            case 'a': type = AVMEDIA_TYPE_AUDIO;      break;
            case 's': type = AVMEDIA_TYPE_SUBTITLE;   break;
            case 'd': type = AVMEDIA_TYPE_DATA;       break;
            case 't': type = AVMEDIA_TYPE_ATTACHMENT; break;
            case 'V': type = AVMEDIA_TYPE_VIDEO; nopic = 1; break;
            default:  av_assert0(0);
            }
            if (*spec && *spec++ != ':')         /* If we are not at the end, then another specifier must follow. */
                return AVERROR(EINVAL);

            if (type != st->codecpar->codec_type)
                match = 0;
            if (nopic && (st->disposition & AV_DISPOSITION_ATTACHED_PIC))
                match = 0;
        } else if (*spec == 'p' && *(spec + 1) == ':') {
            int prog_id;
            int found = 0;
            char *endptr;
            spec += 2;
            prog_id = strtol(spec, &endptr, 0);
            /* Disallow empty id and make sure that if we are not at the end, then another specifier must follow. */
            if (spec == endptr || (*endptr && *endptr++ != ':'))
                return AVERROR(EINVAL);
            spec = endptr;
            if (match) {
                for (unsigned i = 0; i < s->nb_programs; i++) {
                    if (s->programs[i]->id != prog_id)
                        continue;

                    for (unsigned j = 0; j < s->programs[i]->nb_stream_indexes; j++) {
                        if (st->index == s->programs[i]->stream_index[j]) {
                            found = 1;
                            if (p)
                                *p = s->programs[i];
                            i = s->nb_programs;
                            break;
                        }
                    }
                }
            }
            if (!found)
                match = 0;
        } else if (*spec == '#' ||
                   (*spec == 'i' && *(spec + 1) == ':')) {
            int stream_id;
            char *endptr;
            spec += 1 + (*spec == 'i');
            stream_id = strtol(spec, &endptr, 0);
            if (spec == endptr || *endptr)                /* Disallow empty id and make sure we are at the end. */
                return AVERROR(EINVAL);
            return match && (stream_id == st->id);
        } else if (*spec == 'm' && *(spec + 1) == ':') {
            const AVDictionaryEntry *tag;
            char *key, *val;
            int ret;

            if (match) {
                spec += 2;
                val = strchr(spec, ':');

                key = val ? av_strndup(spec, val - spec) : av_strdup(spec);
                if (!key)
                    return AVERROR(ENOMEM);

                tag = av_dict_get(st->metadata, key, NULL, 0);
                if (tag) {
                    if (!val || !strcmp(tag->value, val + 1))
                        ret = 1;
                    else
                        ret = 0;
                } else
                    ret = 0;

                av_freep(&key);
            }
            return match && ret;
        } else if (*spec == 'u' && *(spec + 1) == '\0') {
            const AVCodecParameters *par = st->codecpar;
            int val;
            switch (par->codec_type) {
            case AVMEDIA_TYPE_AUDIO:
                val = par->sample_rate && par->channels;
                if (par->format == AV_SAMPLE_FMT_NONE)
                    return 0;
                break;
            case AVMEDIA_TYPE_VIDEO:
                val = par->width && par->height;
                if (par->format == AV_PIX_FMT_NONE)
                    return 0;
                break;
            case AVMEDIA_TYPE_UNKNOWN:
                val = 0;
                break;
            default:
                val = 1;
                break;
            }
            return match && (par->codec_id != AV_CODEC_ID_NONE && val != 0);
        } else {
            return AVERROR(EINVAL);
        }
    }

    return match;
}


int avformat_match_stream_specifier(AVFormatContext *s, AVStream *st,
                                    const char *spec)
{
    int ret, index;
    char *endptr;
    const char *indexptr = NULL;
    const AVProgram *p = NULL;
    int nb_streams;

    ret = match_stream_specifier(s, st, spec, &indexptr, &p);
    if (ret < 0)
        goto error;

    if (!indexptr)
        return ret;

    index = strtol(indexptr, &endptr, 0);
    if (*endptr) {                  /* We can't have anything after the requested index. */
        ret = AVERROR(EINVAL);
        goto error;
    }

    /* This is not really needed but saves us a loop for simple stream index specifiers. */
    if (spec == indexptr)
        return (index == st->index);

    /* If we requested a matching stream index, we have to ensure st is that. */
    nb_streams = p ? p->nb_stream_indexes : s->nb_streams;
    for (int i = 0; i < nb_streams && index >= 0; i++) {
        const AVStream *candidate = s->streams[p ? p->stream_index[i] : i];
        ret = match_stream_specifier(s, candidate, spec, NULL, NULL);
        if (ret < 0)
            goto error;
        if (ret > 0 && index-- == 0 && st == candidate)
            return 1;
    }
    return 0;

error:
    if (ret == AVERROR(EINVAL))
        av_log(s, AV_LOG_ERROR, "Invalid stream specifier: %s.\n", spec);
    return ret;
}

int ff_generate_avci_extradata(AVStream *st)
{
    static const uint8_t avci100_1080p_extradata[] = {
        // SPS
        0x00, 0x00, 0x00, 0x01, 0x67, 0x7a, 0x10, 0x29,
        0xb6, 0xd4, 0x20, 0x22, 0x33, 0x19, 0xc6, 0x63,
        0x23, 0x21, 0x01, 0x11, 0x98, 0xce, 0x33, 0x19,
        0x18, 0x21, 0x02, 0x56, 0xb9, 0x3d, 0x7d, 0x7e,
        0x4f, 0xe3, 0x3f, 0x11, 0xf1, 0x9e, 0x08, 0xb8,
        0x8c, 0x54, 0x43, 0xc0, 0x78, 0x02, 0x27, 0xe2,
        0x70, 0x1e, 0x30, 0x10, 0x10, 0x14, 0x00, 0x00,
        0x03, 0x00, 0x04, 0x00, 0x00, 0x03, 0x00, 0xca,
        0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        // PPS
        0x00, 0x00, 0x00, 0x01, 0x68, 0xce, 0x33, 0x48,
        0xd0
    };
    static const uint8_t avci100_1080i_extradata[] = {
        // SPS
        0x00, 0x00, 0x00, 0x01, 0x67, 0x7a, 0x10, 0x29,
        0xb6, 0xd4, 0x20, 0x22, 0x33, 0x19, 0xc6, 0x63,
        0x23, 0x21, 0x01, 0x11, 0x98, 0xce, 0x33, 0x19,
        0x18, 0x21, 0x03, 0x3a, 0x46, 0x65, 0x6a, 0x65,
        0x24, 0xad, 0xe9, 0x12, 0x32, 0x14, 0x1a, 0x26,
        0x34, 0xad, 0xa4, 0x41, 0x82, 0x23, 0x01, 0x50,
        0x2b, 0x1a, 0x24, 0x69, 0x48, 0x30, 0x40, 0x2e,
        0x11, 0x12, 0x08, 0xc6, 0x8c, 0x04, 0x41, 0x28,
        0x4c, 0x34, 0xf0, 0x1e, 0x01, 0x13, 0xf2, 0xe0,
        0x3c, 0x60, 0x20, 0x20, 0x28, 0x00, 0x00, 0x03,
        0x00, 0x08, 0x00, 0x00, 0x03, 0x01, 0x94, 0x20,
        // PPS
        0x00, 0x00, 0x00, 0x01, 0x68, 0xce, 0x33, 0x48,
        0xd0
    };
    static const uint8_t avci50_1080p_extradata[] = {
        // SPS
        0x00, 0x00, 0x00, 0x01, 0x67, 0x6e, 0x10, 0x28,
        0xa6, 0xd4, 0x20, 0x32, 0x33, 0x0c, 0x71, 0x18,
        0x88, 0x62, 0x10, 0x19, 0x19, 0x86, 0x38, 0x8c,
        0x44, 0x30, 0x21, 0x02, 0x56, 0x4e, 0x6f, 0x37,
        0xcd, 0xf9, 0xbf, 0x81, 0x6b, 0xf3, 0x7c, 0xde,
        0x6e, 0x6c, 0xd3, 0x3c, 0x05, 0xa0, 0x22, 0x7e,
        0x5f, 0xfc, 0x00, 0x0c, 0x00, 0x13, 0x8c, 0x04,
        0x04, 0x05, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00,
        0x00, 0x03, 0x00, 0x32, 0x84, 0x00, 0x00, 0x00,
        // PPS
        0x00, 0x00, 0x00, 0x01, 0x68, 0xee, 0x31, 0x12,
        0x11
    };
    static const uint8_t avci50_1080i_extradata[] = {
        // SPS
        0x00, 0x00, 0x00, 0x01, 0x67, 0x6e, 0x10, 0x28,
        0xa6, 0xd4, 0x20, 0x32, 0x33, 0x0c, 0x71, 0x18,
        0x88, 0x62, 0x10, 0x19, 0x19, 0x86, 0x38, 0x8c,
        0x44, 0x30, 0x21, 0x02, 0x56, 0x4e, 0x6e, 0x61,
        0x87, 0x3e, 0x73, 0x4d, 0x98, 0x0c, 0x03, 0x06,
        0x9c, 0x0b, 0x73, 0xe6, 0xc0, 0xb5, 0x18, 0x63,
        0x0d, 0x39, 0xe0, 0x5b, 0x02, 0xd4, 0xc6, 0x19,
        0x1a, 0x79, 0x8c, 0x32, 0x34, 0x24, 0xf0, 0x16,
        0x81, 0x13, 0xf7, 0xff, 0x80, 0x02, 0x00, 0x01,
        0xf1, 0x80, 0x80, 0x80, 0xa0, 0x00, 0x00, 0x03,
        0x00, 0x20, 0x00, 0x00, 0x06, 0x50, 0x80, 0x00,
        // PPS
        0x00, 0x00, 0x00, 0x01, 0x68, 0xee, 0x31, 0x12,
        0x11
    };
    static const uint8_t avci100_720p_extradata[] = {
        // SPS
        0x00, 0x00, 0x00, 0x01, 0x67, 0x7a, 0x10, 0x29,
        0xb6, 0xd4, 0x20, 0x2a, 0x33, 0x1d, 0xc7, 0x62,
        0xa1, 0x08, 0x40, 0x54, 0x66, 0x3b, 0x8e, 0xc5,
        0x42, 0x02, 0x10, 0x25, 0x64, 0x2c, 0x89, 0xe8,
        0x85, 0xe4, 0x21, 0x4b, 0x90, 0x83, 0x06, 0x95,
        0xd1, 0x06, 0x46, 0x97, 0x20, 0xc8, 0xd7, 0x43,
        0x08, 0x11, 0xc2, 0x1e, 0x4c, 0x91, 0x0f, 0x01,
        0x40, 0x16, 0xec, 0x07, 0x8c, 0x04, 0x04, 0x05,
        0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x03,
        0x00, 0x64, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00,
        // PPS
        0x00, 0x00, 0x00, 0x01, 0x68, 0xce, 0x31, 0x12,
        0x11
    };
    static const uint8_t avci50_720p_extradata[] = {
        // SPS
        0x00, 0x00, 0x00, 0x01, 0x67, 0x6e, 0x10, 0x20,
        0xa6, 0xd4, 0x20, 0x32, 0x33, 0x0c, 0x71, 0x18,
        0x88, 0x62, 0x10, 0x19, 0x19, 0x86, 0x38, 0x8c,
        0x44, 0x30, 0x21, 0x02, 0x56, 0x4e, 0x6f, 0x37,
        0xcd, 0xf9, 0xbf, 0x81, 0x6b, 0xf3, 0x7c, 0xde,
        0x6e, 0x6c, 0xd3, 0x3c, 0x0f, 0x01, 0x6e, 0xff,
        0xc0, 0x00, 0xc0, 0x01, 0x38, 0xc0, 0x40, 0x40,
        0x50, 0x00, 0x00, 0x03, 0x00, 0x10, 0x00, 0x00,
        0x06, 0x48, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00,
        // PPS
        0x00, 0x00, 0x00, 0x01, 0x68, 0xee, 0x31, 0x12,
        0x11
    };

    const uint8_t *data = NULL;
    int ret, size       = 0;

    if (st->codecpar->width == 1920) {
        if (st->codecpar->field_order == AV_FIELD_PROGRESSIVE) {
            data = avci100_1080p_extradata;
            size = sizeof(avci100_1080p_extradata);
        } else {
            data = avci100_1080i_extradata;
            size = sizeof(avci100_1080i_extradata);
        }
    } else if (st->codecpar->width == 1440) {
        if (st->codecpar->field_order == AV_FIELD_PROGRESSIVE) {
            data = avci50_1080p_extradata;
            size = sizeof(avci50_1080p_extradata);
        } else {
            data = avci50_1080i_extradata;
            size = sizeof(avci50_1080i_extradata);
        }
    } else if (st->codecpar->width == 1280) {
        data = avci100_720p_extradata;
        size = sizeof(avci100_720p_extradata);
    } else if (st->codecpar->width == 960) {
        data = avci50_720p_extradata;
        size = sizeof(avci50_720p_extradata);
    }

    if (!size)
        return 0;

    if ((ret = ff_alloc_extradata(st->codecpar, size)) < 0)
        return ret;
    memcpy(st->codecpar->extradata, data, size);

    return 0;
}

uint8_t *av_stream_get_side_data(const AVStream *st,
                                 enum AVPacketSideDataType type, size_t *size)
{
    for (int i = 0; i < st->nb_side_data; i++) {
        if (st->side_data[i].type == type) {
            if (size)
                *size = st->side_data[i].size;
            return st->side_data[i].data;
        }
    }
    if (size)
        *size = 0;
    return NULL;
}

int av_stream_add_side_data(AVStream *st, enum AVPacketSideDataType type,
                            uint8_t *data, size_t size)
{
    AVPacketSideData *sd, *tmp;

    for (int i = 0; i < st->nb_side_data; i++) {
        sd = &st->side_data[i];

        if (sd->type == type) {
            av_freep(&sd->data);
            sd->data = data;
            sd->size = size;
            return 0;
        }
    }

    if ((unsigned)st->nb_side_data + 1 >= INT_MAX / sizeof(*st->side_data))
        return AVERROR(ERANGE);

    tmp = av_realloc(st->side_data, (st->nb_side_data + 1) * sizeof(*tmp));
    if (!tmp) {
        return AVERROR(ENOMEM);
    }

    st->side_data = tmp;
    st->nb_side_data++;

    sd = &st->side_data[st->nb_side_data - 1];
    sd->type = type;
    sd->data = data;
    sd->size = size;

    return 0;
}

uint8_t *av_stream_new_side_data(AVStream *st, enum AVPacketSideDataType type,
                                 size_t size)
{
    int ret;
    uint8_t *data = av_malloc(size);

    if (!data)
        return NULL;

    ret = av_stream_add_side_data(st, type, data, size);
    if (ret < 0) {
        av_freep(&data);
        return NULL;
    }

    return data;
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

int ff_format_output_open(AVFormatContext *s, const char *url, AVDictionary **options)
{
    if (!s->oformat)
        return AVERROR(EINVAL);

    if (!(s->oformat->flags & AVFMT_NOFILE))
        return s->io_open(s, &s->pb, url, AVIO_FLAG_WRITE, options);
    return 0;
}

void ff_format_io_close_default(AVFormatContext *s, AVIOContext *pb)
{
    avio_close(pb);
}

int ff_format_io_close(AVFormatContext *s, AVIOContext **pb)
{
    int ret = 0;
    if (*pb) {
        if (s->io_close == ff_format_io_close_default || s->io_close == NULL)
            ret = s->io_close2(s, *pb);
        else
            s->io_close(s, *pb);
    }
    *pb = NULL;
    return ret;
}

int ff_is_http_proto(const char *filename) {
    const char *proto = avio_find_protocol_name(filename);
    return proto ? (!av_strcasecmp(proto, "http") || !av_strcasecmp(proto, "https")) : 0;
}

int ff_parse_creation_time_metadata(AVFormatContext *s, int64_t *timestamp, int return_seconds)
{
    AVDictionaryEntry *entry;
    int64_t parsed_timestamp;
    int ret;
    if ((entry = av_dict_get(s->metadata, "creation_time", NULL, 0))) {
        if ((ret = av_parse_time(&parsed_timestamp, entry->value, 0)) >= 0) {
            *timestamp = return_seconds ? parsed_timestamp / 1000000 : parsed_timestamp;
            return 1;
        } else {
            av_log(s, AV_LOG_WARNING, "Failed to parse creation_time %s\n", entry->value);
            return ret;
        }
    }
    return 0;
}

int ff_standardize_creation_time(AVFormatContext *s)
{
    int64_t timestamp;
    int ret = ff_parse_creation_time_metadata(s, &timestamp, 0);
    if (ret == 1)
        return avpriv_dict_set_timestamp(&s->metadata, "creation_time", timestamp);
    return ret;
}

int ff_get_packet_palette(AVFormatContext *s, AVPacket *pkt, int ret, uint32_t *palette)
{
    uint8_t *side_data;
    size_t size;

    side_data = av_packet_get_side_data(pkt, AV_PKT_DATA_PALETTE, &size);
    if (side_data) {
        if (size != AVPALETTE_SIZE) {
            av_log(s, AV_LOG_ERROR, "Invalid palette side data\n");
            return AVERROR_INVALIDDATA;
        }
        memcpy(palette, side_data, AVPALETTE_SIZE);
        return 1;
    }

    if (ret == CONTAINS_PAL) {
        for (int i = 0; i < AVPALETTE_COUNT; i++)
            palette[i] = AV_RL32(pkt->data + pkt->size - AVPALETTE_SIZE + i*4);
        return 1;
    }

    return 0;
}

int ff_bprint_to_codecpar_extradata(AVCodecParameters *par, struct AVBPrint *buf)
{
    int ret;
    char *str;

    ret = av_bprint_finalize(buf, &str);
    if (ret < 0)
        return ret;
    if (!av_bprint_is_complete(buf)) {
        av_free(str);
        return AVERROR(ENOMEM);
    }

    par->extradata = str;
    /* Note: the string is NUL terminated (so extradata can be read as a
     * string), but the ending character is not accounted in the size (in
     * binary formats you are likely not supposed to mux that character). When
     * extradata is copied, it is also padded with AV_INPUT_BUFFER_PADDING_SIZE
     * zeros. */
    par->extradata_size = buf->len;
    return 0;
}

int avformat_transfer_internal_stream_timing_info(const AVOutputFormat *ofmt,
                                                  AVStream *ost, const AVStream *ist,
                                                  enum AVTimebaseSource copy_tb)
{
    const AVCodecContext *const dec_ctx = cffstream(ist)->avctx;
    AVCodecContext       *const enc_ctx =  ffstream(ost)->avctx;

    enc_ctx->time_base = ist->time_base;
    /*
     * Avi is a special case here because it supports variable fps but
     * having the fps and timebase differe significantly adds quite some
     * overhead
     */
    if (!strcmp(ofmt->name, "avi")) {
#if FF_API_R_FRAME_RATE
        if (copy_tb == AVFMT_TBCF_AUTO && ist->r_frame_rate.num
            && av_q2d(ist->r_frame_rate) >= av_q2d(ist->avg_frame_rate)
            && 0.5/av_q2d(ist->r_frame_rate) > av_q2d(ist->time_base)
            && 0.5/av_q2d(ist->r_frame_rate) > av_q2d(dec_ctx->time_base)
            && av_q2d(ist->time_base) < 1.0/500 && av_q2d(dec_ctx->time_base) < 1.0/500
            || copy_tb == AVFMT_TBCF_R_FRAMERATE) {
            enc_ctx->time_base.num = ist->r_frame_rate.den;
            enc_ctx->time_base.den = 2*ist->r_frame_rate.num;
            enc_ctx->ticks_per_frame = 2;
        } else
#endif
            if (copy_tb == AVFMT_TBCF_AUTO && av_q2d(dec_ctx->time_base)*dec_ctx->ticks_per_frame > 2*av_q2d(ist->time_base)
                   && av_q2d(ist->time_base) < 1.0/500
                   || copy_tb == AVFMT_TBCF_DECODER) {
            enc_ctx->time_base = dec_ctx->time_base;
            enc_ctx->time_base.num *= dec_ctx->ticks_per_frame;
            enc_ctx->time_base.den *= 2;
            enc_ctx->ticks_per_frame = 2;
        }
    } else if (!(ofmt->flags & AVFMT_VARIABLE_FPS)
               && !av_match_name(ofmt->name, "mov,mp4,3gp,3g2,psp,ipod,ismv,f4v")) {
        if (copy_tb == AVFMT_TBCF_AUTO && dec_ctx->time_base.den
            && av_q2d(dec_ctx->time_base)*dec_ctx->ticks_per_frame > av_q2d(ist->time_base)
            && av_q2d(ist->time_base) < 1.0/500
            || copy_tb == AVFMT_TBCF_DECODER) {
            enc_ctx->time_base = dec_ctx->time_base;
            enc_ctx->time_base.num *= dec_ctx->ticks_per_frame;
        }
    }

    if ((enc_ctx->codec_tag == AV_RL32("tmcd") || ost->codecpar->codec_tag == AV_RL32("tmcd"))
        && dec_ctx->time_base.num < dec_ctx->time_base.den
        && dec_ctx->time_base.num > 0
        && 121LL*dec_ctx->time_base.num > dec_ctx->time_base.den) {
        enc_ctx->time_base = dec_ctx->time_base;
    }

    av_reduce(&enc_ctx->time_base.num, &enc_ctx->time_base.den,
              enc_ctx->time_base.num, enc_ctx->time_base.den, INT_MAX);

    return 0;
}

AVRational av_stream_get_codec_timebase(const AVStream *st)
{
    // See avformat_transfer_internal_stream_timing_info() TODO.
    return cffstream(st)->avctx->time_base;
}

void ff_format_set_url(AVFormatContext *s, char *url)
{
    av_assert0(url);
    av_freep(&s->url);
    s->url = url;
}

static int option_is_disposition(const AVOption *opt)
{
    return opt->type == AV_OPT_TYPE_CONST &&
           opt->unit && !strcmp(opt->unit, "disposition");
}

int av_disposition_from_string(const char *disp)
{
    for (const AVOption *opt = stream_options; opt->name; opt++)
        if (option_is_disposition(opt) && !strcmp(disp, opt->name))
            return opt->default_val.i64;
    return AVERROR(EINVAL);
}

const char *av_disposition_to_string(int disposition)
{
    int val;

    if (disposition <= 0)
        return NULL;

    val = 1 << ff_ctz(disposition);
    for (const AVOption *opt = stream_options; opt->name; opt++)
        if (option_is_disposition(opt) && opt->default_val.i64 == val)
            return opt->name;

    return NULL;
}

int ff_format_shift_data(AVFormatContext *s, int64_t read_start, int shift_size)
{
    int ret;
    int64_t pos, pos_end;
    uint8_t *buf, *read_buf[2];
    int read_buf_id = 0;
    int read_size[2];
    AVIOContext *read_pb;

    buf = av_malloc_array(shift_size, 2);
    if (!buf)
        return AVERROR(ENOMEM);
    read_buf[0] = buf;
    read_buf[1] = buf + shift_size;

    /* Shift the data: the AVIO context of the output can only be used for
     * writing, so we re-open the same output, but for reading. It also avoids
     * a read/seek/write/seek back and forth. */
    avio_flush(s->pb);
    ret = s->io_open(s, &read_pb, s->url, AVIO_FLAG_READ, NULL);
    if (ret < 0) {
        av_log(s, AV_LOG_ERROR, "Unable to re-open %s output file for shifting data\n", s->url);
        goto end;
    }

    /* mark the end of the shift to up to the last data we wrote, and get ready
     * for writing */
    pos_end = avio_tell(s->pb);
    avio_seek(s->pb, read_start + shift_size, SEEK_SET);

    avio_seek(read_pb, read_start, SEEK_SET);
    pos = avio_tell(read_pb);

#define READ_BLOCK do {                                                             \
    read_size[read_buf_id] = avio_read(read_pb, read_buf[read_buf_id], shift_size);  \
    read_buf_id ^= 1;                                                               \
} while (0)

    /* shift data by chunk of at most shift_size */
    READ_BLOCK;
    do {
        int n;
        READ_BLOCK;
        n = read_size[read_buf_id];
        if (n <= 0)
            break;
        avio_write(s->pb, read_buf[read_buf_id], n);
        pos += n;
    } while (pos < pos_end);
    ret = ff_format_io_close(s, &read_pb);

end:
    av_free(buf);
    return ret;
}
