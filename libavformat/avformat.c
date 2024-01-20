/*
 * Various functions used by both muxers and demuxers
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

#include <math.h>
#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/channel_layout.h"
#include "libavutil/frame.h"
#include "libavutil/iamf.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixfmt.h"
#include "libavutil/samplefmt.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/codec.h"
#include "libavcodec/bsf.h"
#include "libavcodec/codec_desc.h"
#include "libavcodec/packet_internal.h"
#include "avformat.h"
#include "avio.h"
#include "demux.h"
#include "mux.h"
#include "internal.h"

void ff_free_stream(AVStream **pst)
{
    AVStream *st = *pst;
    FFStream *const sti = ffstream(st);

    if (!st)
        return;

#if FF_API_AVSTREAM_SIDE_DATA
FF_DISABLE_DEPRECATION_WARNINGS
    for (int i = 0; i < st->nb_side_data; i++)
        av_freep(&st->side_data[i].data);
    av_freep(&st->side_data);
FF_ENABLE_DEPRECATION_WARNINGS
#endif

    if (st->attached_pic.data)
        av_packet_unref(&st->attached_pic);

    av_parser_close(sti->parser);
    avcodec_free_context(&sti->avctx);
    av_bsf_free(&sti->bsfc);
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

void ff_free_stream_group(AVStreamGroup **pstg)
{
    AVStreamGroup *stg = *pstg;

    if (!stg)
        return;

    av_freep(&stg->streams);
    av_dict_free(&stg->metadata);
    av_freep(&stg->priv_data);
    switch (stg->type) {
    case AV_STREAM_GROUP_PARAMS_IAMF_AUDIO_ELEMENT: {
        av_iamf_audio_element_free(&stg->params.iamf_audio_element);
        break;
    }
    case AV_STREAM_GROUP_PARAMS_IAMF_MIX_PRESENTATION: {
        av_iamf_mix_presentation_free(&stg->params.iamf_mix_presentation);
        break;
    }
    case AV_STREAM_GROUP_PARAMS_TILE_GRID:
        av_opt_free(stg->params.tile_grid);
        av_freep(&stg->params.tile_grid->offsets);
        av_freep(&stg->params.tile_grid);
        break;
    default:
        break;
    }

    av_freep(pstg);
}

void ff_remove_stream(AVFormatContext *s, AVStream *st)
{
    av_assert0(s->nb_streams>0);
    av_assert0(s->streams[ s->nb_streams - 1 ] == st);

    ff_free_stream(&s->streams[ --s->nb_streams ]);
}

void ff_remove_stream_group(AVFormatContext *s, AVStreamGroup *stg)
{
    av_assert0(s->nb_stream_groups > 0);
    av_assert0(s->stream_groups[ s->nb_stream_groups - 1 ] == stg);

    ff_free_stream_group(&s->stream_groups[ --s->nb_stream_groups ]);
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

void avformat_free_context(AVFormatContext *s)
{
    FFFormatContext *si;

    if (!s)
        return;
    si = ffformatcontext(s);

    if (s->oformat && ffofmt(s->oformat)->deinit && si->initialized)
        ffofmt(s->oformat)->deinit(s);

    av_opt_free(s);
    if (s->iformat && s->iformat->priv_class && s->priv_data)
        av_opt_free(s->priv_data);
    if (s->oformat && s->oformat->priv_class && s->priv_data)
        av_opt_free(s->priv_data);

    for (unsigned i = 0; i < s->nb_streams; i++)
        ff_free_stream(&s->streams[i]);
    for (unsigned i = 0; i < s->nb_stream_groups; i++)
        ff_free_stream_group(&s->stream_groups[i]);
    s->nb_stream_groups = 0;
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
    av_freep(&s->stream_groups);
    ff_flush_packet_queue(s);
    av_freep(&s->url);
    av_free(s);
}

#if FF_API_AVSTREAM_SIDE_DATA
FF_DISABLE_DEPRECATION_WARNINGS
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

    if (st->nb_side_data + 1U > FFMIN(INT_MAX, SIZE_MAX / sizeof(*tmp)))
        return AVERROR(ERANGE);

    tmp = av_realloc_array(st->side_data, st->nb_side_data + 1, sizeof(*tmp));
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
FF_ENABLE_DEPRECATION_WARNINGS
#endif

/**
 * Copy all stream parameters from source to destination stream, with the
 * exception of the index field, which is usually set by avformat_new_stream().
 *
 * @param dst pointer to destination AVStream
 * @param src pointer to source AVStream
 * @return >=0 on success, AVERROR code on error
 */
static int stream_params_copy(AVStream *dst, const AVStream *src)
{
    int ret;

    dst->id                  = src->id;
    dst->time_base           = src->time_base;
    dst->start_time          = src->start_time;
    dst->duration            = src->duration;
    dst->nb_frames           = src->nb_frames;
    dst->disposition         = src->disposition;
    dst->discard             = src->discard;
    dst->sample_aspect_ratio = src->sample_aspect_ratio;
    dst->avg_frame_rate      = src->avg_frame_rate;
    dst->event_flags         = src->event_flags;
    dst->r_frame_rate        = src->r_frame_rate;
    dst->pts_wrap_bits       = src->pts_wrap_bits;

    av_dict_free(&dst->metadata);
    ret = av_dict_copy(&dst->metadata, src->metadata, 0);
    if (ret < 0)
        return ret;

    ret = avcodec_parameters_copy(dst->codecpar, src->codecpar);
    if (ret < 0)
        return ret;

    av_packet_unref(&dst->attached_pic);
    if (src->attached_pic.data) {
        ret = av_packet_ref(&dst->attached_pic, &src->attached_pic);
        if (ret < 0)
            return ret;
    }

    return 0;
}

AVStream *ff_stream_clone(AVFormatContext *dst_ctx, const AVStream *src)
{
    AVStream *st;
    int ret;

    st = avformat_new_stream(dst_ctx, NULL);
    if (!st)
        return NULL;

    ret = stream_params_copy(st, src);
    if (ret < 0) {
        ff_remove_stream(dst_ctx, st);
        return NULL;
    }

    return st;
}

const char *avformat_stream_group_name(enum AVStreamGroupParamsType type)
{
    switch(type) {
    case AV_STREAM_GROUP_PARAMS_IAMF_AUDIO_ELEMENT:        return "IAMF Audio Element";
    case AV_STREAM_GROUP_PARAMS_IAMF_MIX_PRESENTATION:     return "IAMF Mix Presentation";
    case AV_STREAM_GROUP_PARAMS_TILE_GRID:                 return "Tile Grid";
    }
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
        program = av_mallocz(sizeof(*program));
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
        if (type == AVMEDIA_TYPE_AUDIO && !(par->ch_layout.nb_channels && par->sample_rate))
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
                                  const AVStreamGroup **g, const AVProgram **p)
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
        } else if (*spec == 'g' && *(spec + 1) == ':') {
            int64_t group_idx = -1, group_id = -1;
            int found = 0;
            char *endptr;
            spec += 2;
            if (*spec == '#' || (*spec == 'i' && *(spec + 1) == ':')) {
                spec += 1 + (*spec == 'i');
                group_id = strtol(spec, &endptr, 0);
                if (spec == endptr || (*endptr && *endptr++ != ':'))
                    return AVERROR(EINVAL);
                spec = endptr;
            } else {
                group_idx = strtol(spec, &endptr, 0);
                /* Disallow empty id and make sure that if we are not at the end, then another specifier must follow. */
                if (spec == endptr || (*endptr && *endptr++ != ':'))
                    return AVERROR(EINVAL);
                spec = endptr;
            }
            if (match) {
                if (group_id > 0) {
                    for (unsigned i = 0; i < s->nb_stream_groups; i++) {
                        if (group_id == s->stream_groups[i]->id) {
                            group_idx = i;
                            break;
                        }
                    }
                }
                if (group_idx < 0 || group_idx >= s->nb_stream_groups)
                    return AVERROR(EINVAL);
                for (unsigned j = 0; j < s->stream_groups[group_idx]->nb_streams; j++) {
                    if (st->index == s->stream_groups[group_idx]->streams[j]->index) {
                        found = 1;
                        if (g)
                            *g = s->stream_groups[group_idx];
                        break;
                    }
                }
            }
            if (!found)
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
                val = par->sample_rate && par->ch_layout.nb_channels;
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
    const AVStreamGroup *g = NULL;
    const AVProgram *p = NULL;
    int nb_streams;

    ret = match_stream_specifier(s, st, spec, &indexptr, &g, &p);
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
    nb_streams = g ? g->nb_streams : (p ? p->nb_stream_indexes : s->nb_streams);
    for (int i = 0; i < nb_streams && index >= 0; i++) {
        unsigned idx = g ? g->streams[i]->index : (p ? p->stream_index[i] : i);
        const AVStream *candidate = s->streams[idx];
        ret = match_stream_specifier(s, candidate, spec, NULL, NULL, NULL);
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
    const AVCodecDescriptor *desc = cffstream(st)->codec_desc;
    AVRational   avg_fr = st->avg_frame_rate;

    if (avg_fr.num > 0 && avg_fr.den > 0 && fr.num > 0 && fr.den > 0 &&
        av_q2d(avg_fr) < 70 && av_q2d(fr) > 210) {
        fr = avg_fr;
    }

    if (desc && (desc->props & AV_CODEC_PROP_FIELDS)) {
        const AVCodecContext *const avctx = ffstream(st)->avctx;
        AVRational codec_fr = avctx->framerate;

        if (   codec_fr.num > 0 && codec_fr.den > 0 &&
            (fr.num == 0 || av_q2d(codec_fr) < av_q2d(fr)*0.7 && fabs(1.0 - av_q2d(av_div_q(avg_fr, fr))) > 0.1))
            fr = codec_fr;
    }

    return fr;
}

int avformat_transfer_internal_stream_timing_info(const AVOutputFormat *ofmt,
                                                  AVStream *ost, const AVStream *ist,
                                                  enum AVTimebaseSource copy_tb)
{
    const AVCodecDescriptor       *desc = cffstream(ist)->codec_desc;
    const AVCodecContext *const dec_ctx = cffstream(ist)->avctx;

    AVRational mul = (AVRational){ desc && (desc->props & AV_CODEC_PROP_FIELDS) ? 2 : 1, 1 };
    AVRational dec_ctx_framerate = dec_ctx ? dec_ctx->framerate : (AVRational){ 0, 0 };
    AVRational dec_ctx_tb = dec_ctx_framerate.num ? av_inv_q(av_mul_q(dec_ctx_framerate, mul))
                                                   : (ist->codecpar->codec_type == AVMEDIA_TYPE_AUDIO ? (AVRational){0, 1}
                                                                                                      : ist->time_base);
    AVRational enc_tb = ist->time_base;
#if FF_API_TICKS_PER_FRAME
FF_DISABLE_DEPRECATION_WARNINGS
    int ticks_per_frame = dec_ctx ? dec_ctx->ticks_per_frame : 1;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

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
            && 0.5/av_q2d(ist->r_frame_rate) > av_q2d(dec_ctx_tb)
            && av_q2d(ist->time_base) < 1.0/500 && av_q2d(dec_ctx_tb) < 1.0/500
            || copy_tb == AVFMT_TBCF_R_FRAMERATE) {
            enc_tb.num = ist->r_frame_rate.den;
            enc_tb.den = 2*ist->r_frame_rate.num;
        } else
#endif
            if (copy_tb == AVFMT_TBCF_AUTO && dec_ctx_framerate.num &&
                av_q2d(av_inv_q(dec_ctx_framerate)) > 2*av_q2d(ist->time_base)
                   && av_q2d(ist->time_base) < 1.0/500
                   || (copy_tb == AVFMT_TBCF_DECODER &&
                       (dec_ctx_framerate.num || ist->codecpar->codec_type == AVMEDIA_TYPE_AUDIO))) {
            enc_tb = dec_ctx_tb;
            enc_tb.den *= 2;
#if FF_API_TICKS_PER_FRAME
            enc_tb.num *= ticks_per_frame;
#endif
        }
    } else if (!(ofmt->flags & AVFMT_VARIABLE_FPS)
               && !av_match_name(ofmt->name, "mov,mp4,3gp,3g2,psp,ipod,ismv,f4v")) {
        if (copy_tb == AVFMT_TBCF_AUTO && dec_ctx_framerate.num
            && av_q2d(av_inv_q(dec_ctx_framerate)) > av_q2d(ist->time_base)
            && av_q2d(ist->time_base) < 1.0/500
            || (copy_tb == AVFMT_TBCF_DECODER &&
                (dec_ctx_framerate.num || ist->codecpar->codec_type == AVMEDIA_TYPE_AUDIO))) {
            enc_tb = dec_ctx_tb;
#if FF_API_TICKS_PER_FRAME
            enc_tb.num *= ticks_per_frame;
#endif
        }
    }

    if (ost->codecpar->codec_tag == AV_RL32("tmcd")
        && dec_ctx_tb.num < dec_ctx_tb.den
        && dec_ctx_tb.num > 0
        && 121LL*dec_ctx_tb.num > dec_ctx_tb.den) {
        enc_tb = dec_ctx_tb;
    }

    av_reduce(&ffstream(ost)->transferred_mux_tb.num,
              &ffstream(ost)->transferred_mux_tb.den,
              enc_tb.num, enc_tb.den, INT_MAX);

    return 0;
}

AVRational av_stream_get_codec_timebase(const AVStream *st)
{
    return cffstream(st)->avctx ? cffstream(st)->avctx->time_base : cffstream(st)->transferred_mux_tb;
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
    if (sti->avctx)
        sti->avctx->pkt_timebase = new_tb;
    st->pts_wrap_bits = pts_wrap_bits;
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

int ff_copy_whiteblacklists(AVFormatContext *dst, const AVFormatContext *src)
{
#define OFF(field) offsetof(AVFormatContext, field)
    static const unsigned offsets[] = {
        OFF(codec_whitelist),    OFF(format_whitelist),
        OFF(protocol_whitelist), OFF(protocol_blacklist),
    };
#undef OFF
    av_assert0(!dst->codec_whitelist &&
               !dst->format_whitelist &&
               !dst->protocol_whitelist &&
               !dst->protocol_blacklist);
    for (unsigned i = 0; i < FF_ARRAY_ELEMS(offsets); i++) {
        const char *src_str = *(char *const*)((const char*)src + offsets[i]);

        if (src_str) {
            char *dst_str = av_strdup(src_str);
            if (!dst_str) {
                av_log(dst, AV_LOG_ERROR, "Failed to duplicate black/whitelist\n");
                return AVERROR(ENOMEM);
            }

            *(char **)((char*)dst + offsets[i]) = dst_str;
        }
    }
    return 0;
}

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

void ff_format_set_url(AVFormatContext *s, char *url)
{
    av_assert0(url);
    av_freep(&s->url);
    s->url = url;
}

int ff_format_io_close(AVFormatContext *s, AVIOContext **pb)
{
    int ret = 0;
    if (*pb)
        ret = s->io_close2(s, *pb);
    *pb = NULL;
    return ret;
}
