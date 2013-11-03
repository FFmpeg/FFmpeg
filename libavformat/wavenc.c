/*
 * WAV muxer
 * Copyright (c) 2001, 2002 Fabrice Bellard
 *
 * Sony Wave64 muxer
 * Copyright (c) 2012 Paul B Mahol
 *
 * WAV muxer RF64 support
 * Copyright (c) 2013 Daniel Verkamp <daniel@drv.nu>
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

#define RF64_AUTO   (-1)
#define RF64_NEVER  0
#define RF64_ALWAYS 1

typedef struct WAVMuxContext {
    const AVClass *class;
    int64_t data;
    int64_t fact_pos;
    int64_t ds64;
    int64_t minpts;
    int64_t maxpts;
    int last_duration;
    int write_bext;
    int rf64;
} WAVMuxContext;

#if CONFIG_WAV_MUXER
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

    if (s->nb_streams != 1) {
        av_log(s, AV_LOG_ERROR, "WAVE files have exactly one stream\n");
        return AVERROR(EINVAL);
    }

    if (wav->rf64 == RF64_ALWAYS) {
        ffio_wfourcc(pb, "RF64");
        avio_wl32(pb, -1); /* RF64 chunk size: use size in ds64 */
    } else {
        ffio_wfourcc(pb, "RIFF");
        avio_wl32(pb, 0); /* file length */
    }

    ffio_wfourcc(pb, "WAVE");

    if (wav->rf64 != RF64_NEVER) {
        /* write empty ds64 chunk or JUNK chunk to reserve space for ds64 */
        ffio_wfourcc(pb, wav->rf64 == RF64_ALWAYS ? "ds64" : "JUNK");
        avio_wl32(pb, 28); /* chunk size */
        wav->ds64 = avio_tell(pb);
        ffio_fill(pb, 0, 28);
    }

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
    int64_t file_size, data_size;
    int64_t number_of_samples = 0;
    int rf64 = 0;

    avio_flush(pb);

    if (s->pb->seekable) {
        /* update file size */
        file_size = avio_tell(pb);
        data_size = file_size - wav->data;
        if (wav->rf64 == RF64_ALWAYS || (wav->rf64 == RF64_AUTO && file_size - 8 > UINT32_MAX)) {
            rf64 = 1;
        } else {
            avio_seek(pb, 4, SEEK_SET);
            avio_wl32(pb, (uint32_t)(file_size - 8));
            avio_seek(pb, file_size, SEEK_SET);

            ff_end_tag(pb, wav->data);
            avio_flush(pb);
        }

        number_of_samples = av_rescale(wav->maxpts - wav->minpts + wav->last_duration,
                                       s->streams[0]->codec->sample_rate * (int64_t)s->streams[0]->time_base.num,
                                       s->streams[0]->time_base.den);

        if(s->streams[0]->codec->codec_tag != 0x01) {
            /* Update num_samps in fact chunk */
            avio_seek(pb, wav->fact_pos, SEEK_SET);
            if (rf64 || (wav->rf64 == RF64_AUTO && number_of_samples > UINT32_MAX)) {
                rf64 = 1;
                avio_wl32(pb, -1);
            } else {
                avio_wl32(pb, number_of_samples);
                avio_seek(pb, file_size, SEEK_SET);
                avio_flush(pb);
            }
        }

        if (rf64) {
            /* overwrite RIFF with RF64 */
            avio_seek(pb, 0, SEEK_SET);
            ffio_wfourcc(pb, "RF64");
            avio_wl32(pb, -1);

            /* write ds64 chunk (overwrite JUNK if rf64 == RF64_AUTO) */
            avio_seek(pb, wav->ds64 - 8, SEEK_SET);
            ffio_wfourcc(pb, "ds64");
            avio_wl32(pb, 28);                  /* ds64 chunk size */
            avio_wl64(pb, file_size - 8);       /* RF64 chunk size */
            avio_wl64(pb, data_size);           /* data chunk size */
            avio_wl64(pb, number_of_samples);   /* fact chunk number of samples */
            avio_wl32(pb, 0);                   /* number of table entries for non-'data' chunks */

            /* write -1 in data chunk size */
            avio_seek(pb, wav->data - 4, SEEK_SET);
            avio_wl32(pb, -1);

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
    { "rf64",       "Use RF64 header rather than RIFF for large files.",    OFFSET(rf64), AV_OPT_TYPE_INT,   { .i64 = RF64_NEVER  },-1, 1, ENC, "rf64" },
    { "auto",       "Write RF64 header if file grows large enough.",        0,            AV_OPT_TYPE_CONST, { .i64 = RF64_AUTO   }, 0, 0, ENC, "rf64" },
    { "always",     "Always write RF64 header regardless of file size.",    0,            AV_OPT_TYPE_CONST, { .i64 = RF64_ALWAYS }, 0, 0, ENC, "rf64" },
    { "never",      "Never write RF64 header regardless of file size.",     0,            AV_OPT_TYPE_CONST, { .i64 = RF64_NEVER  }, 0, 0, ENC, "rf64" },
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
#endif /* CONFIG_WAV_MUXER */

#if CONFIG_W64_MUXER
#include "w64.h"

static void start_guid(AVIOContext *pb, const uint8_t *guid, int64_t *pos)
{
    *pos = avio_tell(pb);

    avio_write(pb, guid, 16);
    avio_wl64(pb, INT64_MAX);
}

static void end_guid(AVIOContext *pb, int64_t start)
{
    int64_t end, pos = avio_tell(pb);

    end = FFALIGN(pos, 8);
    ffio_fill(pb, 0, end - pos);
    avio_seek(pb, start + 16, SEEK_SET);
    avio_wl64(pb, end - start);
    avio_seek(pb, end, SEEK_SET);
}

static int w64_write_header(AVFormatContext *s)
{
    WAVMuxContext *wav = s->priv_data;
    AVIOContext *pb = s->pb;
    int64_t start;
    int ret;

    avio_write(pb, ff_w64_guid_riff, sizeof(ff_w64_guid_riff));
    avio_wl64(pb, -1);
    avio_write(pb, ff_w64_guid_wave, sizeof(ff_w64_guid_wave));
    start_guid(pb, ff_w64_guid_fmt, &start);
    if ((ret = ff_put_wav_header(pb, s->streams[0]->codec)) < 0) {
        av_log(s, AV_LOG_ERROR, "%s codec not supported\n",
               s->streams[0]->codec->codec ? s->streams[0]->codec->codec->name : "NONE");
        return ret;
    }
    end_guid(pb, start);

    if (s->streams[0]->codec->codec_tag != 0x01 /* hence for all other than PCM */
        && s->pb->seekable) {
        start_guid(pb, ff_w64_guid_fact, &wav->fact_pos);
        avio_wl64(pb, 0);
        end_guid(pb, wav->fact_pos);
    }

    start_guid(pb, ff_w64_guid_data, &wav->data);

    return 0;
}

static int w64_write_trailer(AVFormatContext *s)
{
    AVIOContext    *pb = s->pb;
    WAVMuxContext *wav = s->priv_data;
    int64_t file_size;

    if (pb->seekable) {
        end_guid(pb, wav->data);

        file_size = avio_tell(pb);
        avio_seek(pb, 16, SEEK_SET);
        avio_wl64(pb, file_size);

        if (s->streams[0]->codec->codec_tag != 0x01) {
            int64_t number_of_samples;

            number_of_samples = av_rescale(wav->maxpts - wav->minpts + wav->last_duration,
                                           s->streams[0]->codec->sample_rate * (int64_t)s->streams[0]->time_base.num,
                                           s->streams[0]->time_base.den);
            avio_seek(pb, wav->fact_pos + 24, SEEK_SET);
            avio_wl64(pb, number_of_samples);
        }

        avio_seek(pb, file_size, SEEK_SET);
        avio_flush(pb);
    }

    return 0;
}

AVOutputFormat ff_w64_muxer = {
    .name              = "w64",
    .long_name         = NULL_IF_CONFIG_SMALL("Sony Wave64"),
    .extensions        = "w64",
    .priv_data_size    = sizeof(WAVMuxContext),
    .audio_codec       = AV_CODEC_ID_PCM_S16LE,
    .video_codec       = AV_CODEC_ID_NONE,
    .write_header      = w64_write_header,
    .write_packet      = wav_write_packet,
    .write_trailer     = w64_write_trailer,
    .flags             = AVFMT_TS_NONSTRICT,
    .codec_tag         = (const AVCodecTag* const []){ ff_codec_wav_tags, 0 },
};
#endif /* CONFIG_W64_MUXER */
