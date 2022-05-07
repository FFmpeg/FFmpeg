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

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/bsf.h"
#include "libavcodec/packet_internal.h"
#include "avformat.h"
#include "demux.h"
#include "internal.h"

void ff_free_stream(AVStream **pst)
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

void ff_remove_stream(AVFormatContext *s, AVStream *st)
{
    av_assert0(s->nb_streams>0);
    av_assert0(s->streams[ s->nb_streams - 1 ] == st);

    ff_free_stream(&s->streams[ --s->nb_streams ]);
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

    if (s->oformat && s->oformat->deinit && si->initialized)
        s->oformat->deinit(s);

    av_opt_free(s);
    if (s->iformat && s->iformat->priv_class && s->priv_data)
        av_opt_free(s->priv_data);
    if (s->oformat && s->oformat->priv_class && s->priv_data)
        av_opt_free(s->priv_data);

    for (unsigned i = 0; i < s->nb_streams; i++)
        ff_free_stream(&s->streams[i]);
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
