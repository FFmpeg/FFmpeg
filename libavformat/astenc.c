/*
 * AST muxer
 * Copyright (c) 2012 James Almer
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
#include "avio_internal.h"
#include "internal.h"
#include "ast.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"

typedef struct ASTMuxContext {
    AVClass *class;
    int64_t  size;
    int64_t  samples;
    int64_t  loopstart;
    int64_t  loopend;
    int      fbs;
} ASTMuxContext;

#define CHECK_LOOP(type) \
    if (ast->loop ## type > 0) { \
        ast->loop ## type = av_rescale_rnd(ast->loop ## type, par->sample_rate, 1000, AV_ROUND_DOWN); \
        if (ast->loop ## type < 0 || ast->loop ## type > UINT_MAX) { \
            av_log(s, AV_LOG_ERROR, "Invalid loop" #type " value\n"); \
            return AVERROR(EINVAL);  \
        } \
    }

static int ast_write_header(AVFormatContext *s)
{
    ASTMuxContext *ast = s->priv_data;
    AVIOContext *pb = s->pb;
    AVCodecParameters *par;
    unsigned int codec_tag;

    if (s->nb_streams == 1) {
        par = s->streams[0]->codecpar;
    } else {
        av_log(s, AV_LOG_ERROR, "only one stream is supported\n");
        return AVERROR(EINVAL);
    }

    if (par->codec_id == AV_CODEC_ID_ADPCM_AFC) {
        av_log(s, AV_LOG_ERROR, "muxing ADPCM AFC is not implemented\n");
        return AVERROR_PATCHWELCOME;
    }

    codec_tag = ff_codec_get_tag(ff_codec_ast_tags, par->codec_id);
    if (!codec_tag) {
        av_log(s, AV_LOG_ERROR, "unsupported codec\n");
        return AVERROR(EINVAL);
    }

    if (ast->loopend > 0 && ast->loopstart >= ast->loopend) {
        av_log(s, AV_LOG_ERROR, "loopend can't be less or equal to loopstart\n");
        return AVERROR(EINVAL);
    }

    /* Convert milliseconds to samples */
    CHECK_LOOP(start)
    CHECK_LOOP(end)

    ffio_wfourcc(pb, "STRM");

    ast->size = avio_tell(pb);
    avio_wb32(pb, 0); /* File size minus header */
    avio_wb16(pb, codec_tag);
    avio_wb16(pb, 16); /* Bit depth */
    avio_wb16(pb, par->channels);
    avio_wb16(pb, 0); /* Loop flag */
    avio_wb32(pb, par->sample_rate);

    ast->samples = avio_tell(pb);
    avio_wb32(pb, 0); /* Number of samples */
    avio_wb32(pb, 0); /* Loopstart */
    avio_wb32(pb, 0); /* Loopend */
    avio_wb32(pb, 0); /* Size of first block */

    /* Unknown */
    avio_wb32(pb, 0);
    avio_wl32(pb, 0x7F);
    avio_wb64(pb, 0);
    avio_wb64(pb, 0);
    avio_wb32(pb, 0);

    return 0;
}

static int ast_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVIOContext *pb = s->pb;
    ASTMuxContext *ast = s->priv_data;
    AVCodecParameters *par = s->streams[0]->codecpar;
    int size = pkt->size / par->channels;

    if (s->streams[0]->nb_frames == 0)
        ast->fbs = size;

    ffio_wfourcc(pb, "BLCK");
    avio_wb32(pb, size); /* Block size */

    /* padding */
    ffio_fill(pb, 0, 24);

    avio_write(pb, pkt->data, pkt->size);

    return 0;
}

static int ast_write_trailer(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    ASTMuxContext *ast = s->priv_data;
    AVCodecParameters *par = s->streams[0]->codecpar;
    int64_t file_size = avio_tell(pb);
    int64_t samples = (file_size - 64 - (32 * s->streams[0]->nb_frames)) / par->block_align; /* PCM_S16BE_PLANAR */

    av_log(s, AV_LOG_DEBUG, "total samples: %"PRId64"\n", samples);

    if (s->pb->seekable & AVIO_SEEKABLE_NORMAL) {
        /* Number of samples */
        avio_seek(pb, ast->samples, SEEK_SET);
        avio_wb32(pb, samples);

        /* Loopstart if provided */
        if (ast->loopstart > 0) {
        if (ast->loopstart >= samples) {
            av_log(s, AV_LOG_WARNING, "Loopstart value is out of range and will be ignored\n");
            ast->loopstart = -1;
            avio_skip(pb, 4);
        } else
        avio_wb32(pb, ast->loopstart);
        } else
            avio_skip(pb, 4);

        /* Loopend if provided. Otherwise number of samples again */
        if (ast->loopend && ast->loopstart >= 0) {
            if (ast->loopend > samples) {
                av_log(s, AV_LOG_WARNING, "Loopend value is out of range and will be ignored\n");
                ast->loopend = samples;
            }
            avio_wb32(pb, ast->loopend);
        } else {
            avio_wb32(pb, samples);
        }

        /* Size of first block */
        avio_wb32(pb, ast->fbs);

        /* File size minus header */
        avio_seek(pb, ast->size, SEEK_SET);
        avio_wb32(pb, file_size - 64);

        /* Loop flag */
        if (ast->loopstart >= 0) {
            avio_skip(pb, 6);
            avio_wb16(pb, 0xFFFF);
        }

        avio_seek(pb, file_size, SEEK_SET);
    }
    return 0;
}

#define OFFSET(obj) offsetof(ASTMuxContext, obj)
static const AVOption options[] = {
  { "loopstart", "Loopstart position in milliseconds.", OFFSET(loopstart), AV_OPT_TYPE_INT64, { .i64 = -1 }, -1, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM },
  { "loopend",   "Loopend position in milliseconds.",   OFFSET(loopend),   AV_OPT_TYPE_INT64, { .i64 = 0 }, 0, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM },
  { NULL },
};

static const AVClass ast_muxer_class = {
    .class_name = "AST muxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const AVOutputFormat ff_ast_muxer = {
    .name              = "ast",
    .long_name         = NULL_IF_CONFIG_SMALL("AST (Audio Stream)"),
    .extensions        = "ast",
    .priv_data_size    = sizeof(ASTMuxContext),
    .audio_codec       = AV_CODEC_ID_PCM_S16BE_PLANAR,
    .video_codec       = AV_CODEC_ID_NONE,
    .write_header      = ast_write_header,
    .write_packet      = ast_write_packet,
    .write_trailer     = ast_write_trailer,
    .priv_class        = &ast_muxer_class,
    .codec_tag         = ff_ast_codec_tags_list,
};
