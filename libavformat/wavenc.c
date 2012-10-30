/*
 * WAV muxer
 * Copyright (c) 2001, 2002 Fabrice Bellard
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

#include <stdint.h>
#include <string.h>

#include "libavutil/dict.h"
#include "libavutil/common.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"

#include "avformat.h"
#include "avio.h"
#include "avio_internal.h"
#include "internal.h"
#include "riff.h"

typedef struct WAVMuxContext {
    const AVClass *class;
    int64_t data;
    int64_t fact_pos;
    int64_t minpts;
    int64_t maxpts;
    int last_duration;
    int write_bext;
} WAVMuxContext;

static inline void bwf_write_bext_string(AVFormatContext *s, const char *key, int maxlen)
{
    AVDictionaryEntry *tag;
    int len = 0;

    if (tag = av_dict_get(s->metadata, key, NULL, 0)) {
        len = strlen(tag->value);
        len = FFMIN(len, maxlen);
        avio_write(s->pb, tag->value, len);
    }

    ffio_fill(s->pb, 0, maxlen - len);
}

static void bwf_write_bext_chunk(AVFormatContext *s)
{
    AVDictionaryEntry *tmp_tag;
    uint64_t time_reference = 0;
    int64_t bext = ff_start_tag(s->pb, "bext");

    bwf_write_bext_string(s, "description", 256);
    bwf_write_bext_string(s, "originator", 32);
    bwf_write_bext_string(s, "originator_reference", 32);
    bwf_write_bext_string(s, "origination_date", 10);
    bwf_write_bext_string(s, "origination_time", 8);

    if (tmp_tag = av_dict_get(s->metadata, "time_reference", NULL, 0))
        time_reference = strtoll(tmp_tag->value, NULL, 10);
    avio_wl64(s->pb, time_reference);
    avio_wl16(s->pb, 1);  // set version to 1

    if (tmp_tag = av_dict_get(s->metadata, "umid", NULL, 0)) {
        unsigned char umidpart_str[17] = {0};
        int i;
        uint64_t umidpart;
        int len = strlen(tmp_tag->value+2);

        for (i = 0; i < len/16; i++) {
            memcpy(umidpart_str, tmp_tag->value + 2 + (i*16), 16);
            umidpart = strtoll(umidpart_str, NULL, 16);
            avio_wb64(s->pb, umidpart);
        }
        ffio_fill(s->pb, 0, 64 - i*8);
    } else
        ffio_fill(s->pb, 0, 64); // zero UMID

    ffio_fill(s->pb, 0, 190); // Reserved

    if (tmp_tag = av_dict_get(s->metadata, "coding_history", NULL, 0))
        avio_put_str(s->pb, tmp_tag->value);

    ff_end_tag(s->pb, bext);
}

static int wav_write_header(AVFormatContext *s)
{
    WAVMuxContext *wav = s->priv_data;
    AVIOContext *pb = s->pb;
    int64_t fmt;

    ffio_wfourcc(pb, "RIFF");
    avio_wl32(pb, 0); /* file length */
    ffio_wfourcc(pb, "WAVE");

    /* format header */
    fmt = ff_start_tag(pb, "fmt ");
    if (ff_put_wav_header(pb, s->streams[0]->codec) < 0) {
        av_log(s, AV_LOG_ERROR, "%s codec not supported in WAVE format\n",
               s->streams[0]->codec->codec ? s->streams[0]->codec->codec->name : "NONE");
        return -1;
    }
    ff_end_tag(pb, fmt);

    if (s->streams[0]->codec->codec_tag != 0x01 /* hence for all other than PCM */
        && s->pb->seekable) {
        wav->fact_pos = ff_start_tag(pb, "fact");
        avio_wl32(pb, 0);
        ff_end_tag(pb, wav->fact_pos);
    }

    if (wav->write_bext)
        bwf_write_bext_chunk(s);

    avpriv_set_pts_info(s->streams[0], 64, 1, s->streams[0]->codec->sample_rate);
    wav->maxpts = wav->last_duration = 0;
    wav->minpts = INT64_MAX;

    /* info header */
    ff_riff_write_info(s);

    /* data header */
    wav->data = ff_start_tag(pb, "data");

    avio_flush(pb);

    return 0;
}

static int wav_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVIOContext *pb  = s->pb;
    WAVMuxContext    *wav = s->priv_data;
    avio_write(pb, pkt->data, pkt->size);
    if(pkt->pts != AV_NOPTS_VALUE) {
        wav->minpts        = FFMIN(wav->minpts, pkt->pts);
        wav->maxpts        = FFMAX(wav->maxpts, pkt->pts);
        wav->last_duration = pkt->duration;
    } else
        av_log(s, AV_LOG_ERROR, "wav_write_packet: NOPTS\n");
    return 0;
}

static int wav_write_trailer(AVFormatContext *s)
{
    AVIOContext *pb  = s->pb;
    WAVMuxContext    *wav = s->priv_data;
    int64_t file_size;

    avio_flush(pb);

    if (s->pb->seekable) {
        ff_end_tag(pb, wav->data);

        /* update file size */
        file_size = avio_tell(pb);
        avio_seek(pb, 4, SEEK_SET);
        avio_wl32(pb, (uint32_t)(file_size - 8));
        avio_seek(pb, file_size, SEEK_SET);

        avio_flush(pb);

        if(s->streams[0]->codec->codec_tag != 0x01) {
            /* Update num_samps in fact chunk */
            int number_of_samples;
            number_of_samples = av_rescale(wav->maxpts - wav->minpts + wav->last_duration,
                                           s->streams[0]->codec->sample_rate * (int64_t)s->streams[0]->time_base.num,
                                           s->streams[0]->time_base.den);
            avio_seek(pb, wav->fact_pos, SEEK_SET);
            avio_wl32(pb, number_of_samples);
            avio_seek(pb, file_size, SEEK_SET);
            avio_flush(pb);
        }
    }
    return 0;
}

#define OFFSET(x) offsetof(WAVMuxContext, x)
#define ENC AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "write_bext", "Write BEXT chunk.", OFFSET(write_bext), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, ENC },
    { NULL },
};

static const AVClass wav_muxer_class = {
    .class_name = "WAV muxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVOutputFormat ff_wav_muxer = {
    .name              = "wav",
    .long_name         = NULL_IF_CONFIG_SMALL("WAV / WAVE (Waveform Audio)"),
    .mime_type         = "audio/x-wav",
    .extensions        = "wav",
    .priv_data_size    = sizeof(WAVMuxContext),
    .audio_codec       = AV_CODEC_ID_PCM_S16LE,
    .video_codec       = AV_CODEC_ID_NONE,
    .write_header      = wav_write_header,
    .write_packet      = wav_write_packet,
    .write_trailer     = wav_write_trailer,
    .flags             = AVFMT_TS_NONSTRICT,
    .codec_tag         = (const AVCodecTag* const []){ ff_codec_wav_tags, 0 },
    .priv_class        = &wav_muxer_class,
};
