/*
 * RIFF demuxing functions and data
 * Copyright (c) 2000 Fabrice Bellard
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

#include "libavutil/dict.h"
#include "libavutil/error.h"
#include "libavutil/log.h"
#include "libavutil/mathematics.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/bytestream.h"
#include "avformat.h"
#include "avio_internal.h"
#include "riff.h"

const AVCodecGuid ff_codec_wav_guids[] = {
    { AV_CODEC_ID_AC3,      { 0x2C, 0x80, 0x6D, 0xE0, 0x46, 0xDB, 0xCF, 0x11, 0xB4, 0xD1, 0x00, 0x80, 0x5F, 0x6C, 0xBB, 0xEA } },
    { AV_CODEC_ID_ATRAC3P,  { 0xBF, 0xAA, 0x23, 0xE9, 0x58, 0xCB, 0x71, 0x44, 0xA1, 0x19, 0xFF, 0xFA, 0x01, 0xE4, 0xCE, 0x62 } },
    { AV_CODEC_ID_EAC3,     { 0xAF, 0x87, 0xFB, 0xA7, 0x02, 0x2D, 0xFB, 0x42, 0xA4, 0xD4, 0x05, 0xCD, 0x93, 0x84, 0x3B, 0xDD } },
    { AV_CODEC_ID_MP2,      { 0x2B, 0x80, 0x6D, 0xE0, 0x46, 0xDB, 0xCF, 0x11, 0xB4, 0xD1, 0x00, 0x80, 0x5F, 0x6C, 0xBB, 0xEA } },
    { AV_CODEC_ID_NONE }
};

void ff_get_guid(AVIOContext *s, ff_asf_guid *g)
{
    av_assert0(sizeof(*g) == 16); //compiler will optimize this out
    if (avio_read(s, *g, sizeof(*g)) < (int)sizeof(*g))
        memset(*g, 0, sizeof(*g));
}

enum AVCodecID ff_codec_guid_get_id(const AVCodecGuid *guids, ff_asf_guid guid)
{
    int i;
    for (i = 0; guids[i].id != AV_CODEC_ID_NONE; i++)
        if (!ff_guidcmp(guids[i].guid, guid))
            return guids[i].id;
    return AV_CODEC_ID_NONE;
}

/* We could be given one of the three possible structures here:
 * WAVEFORMAT, PCMWAVEFORMAT or WAVEFORMATEX. Each structure
 * is an expansion of the previous one with the fields added
 * at the bottom. PCMWAVEFORMAT adds 'WORD wBitsPerSample' and
 * WAVEFORMATEX adds 'WORD  cbSize' and basically makes itself
 * an openended structure.
 */

static void parse_waveformatex(AVIOContext *pb, AVCodecContext *c)
{
    ff_asf_guid subformat;
    int bps = avio_rl16(pb);
    if (bps)
        c->bits_per_coded_sample = bps;

    c->channel_layout        = avio_rl32(pb); /* dwChannelMask */

    ff_get_guid(pb, &subformat);
    if (!memcmp(subformat + 4,
                (const uint8_t[]){ FF_MEDIASUBTYPE_BASE_GUID }, 12)) {
        c->codec_tag = AV_RL32(subformat);
        c->codec_id  = ff_wav_codec_get_id(c->codec_tag,
                                           c->bits_per_coded_sample);
    } else {
        c->codec_id = ff_codec_guid_get_id(ff_codec_wav_guids, subformat);
        if (!c->codec_id)
            av_log(c, AV_LOG_WARNING,
                   "unknown subformat:"FF_PRI_GUID"\n",
                   FF_ARG_GUID(subformat));
    }
}

int ff_get_wav_header(AVIOContext *pb, AVCodecContext *codec, int size)
{
    int id;

    id                 = avio_rl16(pb);
    codec->codec_type  = AVMEDIA_TYPE_AUDIO;
    codec->channels    = avio_rl16(pb);
    codec->sample_rate = avio_rl32(pb);
    codec->bit_rate    = avio_rl32(pb) * 8;
    codec->block_align = avio_rl16(pb);
    if (size == 14) {  /* We're dealing with plain vanilla WAVEFORMAT */
        codec->bits_per_coded_sample = 8;
    } else
        codec->bits_per_coded_sample = avio_rl16(pb);
    if (id == 0xFFFE) {
        codec->codec_tag = 0;
    } else {
        codec->codec_tag = id;
        codec->codec_id  = ff_wav_codec_get_id(id,
                                               codec->bits_per_coded_sample);
    }
    if (size >= 18) {  /* We're obviously dealing with WAVEFORMATEX */
        int cbSize = avio_rl16(pb); /* cbSize */
        size  -= 18;
        cbSize = FFMIN(size, cbSize);
        if (cbSize >= 22 && id == 0xfffe) { /* WAVEFORMATEXTENSIBLE */
            parse_waveformatex(pb, codec);
            cbSize -= 22;
            size   -= 22;
        }
        codec->extradata_size = cbSize;
        if (cbSize > 0) {
            av_free(codec->extradata);
            codec->extradata = av_mallocz(codec->extradata_size +
                                          FF_INPUT_BUFFER_PADDING_SIZE);
            if (!codec->extradata)
                return AVERROR(ENOMEM);
            avio_read(pb, codec->extradata, codec->extradata_size);
            size -= cbSize;
        }

        /* It is possible for the chunk to contain garbage at the end */
        if (size > 0)
            avio_skip(pb, size);
    }
    if (codec->codec_id == AV_CODEC_ID_AAC_LATM) {
        /* Channels and sample_rate values are those prior to applying SBR
         * and/or PS. */
        codec->channels    = 0;
        codec->sample_rate = 0;
    }
    /* override bits_per_coded_sample for G.726 */
    if (codec->codec_id == AV_CODEC_ID_ADPCM_G726 && codec->sample_rate)
        codec->bits_per_coded_sample = codec->bit_rate / codec->sample_rate;

    return 0;
}

enum AVCodecID ff_wav_codec_get_id(unsigned int tag, int bps)
{
    enum AVCodecID id;
    id = ff_codec_get_id(ff_codec_wav_tags, tag);
    if (id <= 0)
        return id;

    if (id == AV_CODEC_ID_PCM_S16LE)
        id = ff_get_pcm_codec_id(bps, 0, 0, ~1);
    else if (id == AV_CODEC_ID_PCM_F32LE)
        id = ff_get_pcm_codec_id(bps, 1, 0,  0);

    if (id == AV_CODEC_ID_ADPCM_IMA_WAV && bps == 8)
        id = AV_CODEC_ID_PCM_ZORK;
    return id;
}

int ff_get_bmp_header(AVIOContext *pb, AVStream *st, unsigned *esize)
{
    int tag1;
    if(esize) *esize  = avio_rl32(pb);
    else                avio_rl32(pb);
    st->codec->width  = avio_rl32(pb);
    st->codec->height = (int32_t)avio_rl32(pb);
    avio_rl16(pb); /* planes */
    st->codec->bits_per_coded_sample = avio_rl16(pb); /* depth */
    tag1                             = avio_rl32(pb);
    avio_rl32(pb); /* ImageSize */
    avio_rl32(pb); /* XPelsPerMeter */
    avio_rl32(pb); /* YPelsPerMeter */
    avio_rl32(pb); /* ClrUsed */
    avio_rl32(pb); /* ClrImportant */
    return tag1;
}

int ff_read_riff_info(AVFormatContext *s, int64_t size)
{
    int64_t start, end, cur;
    AVIOContext *pb = s->pb;

    start = avio_tell(pb);
    end   = start + size;

    while ((cur = avio_tell(pb)) >= 0 &&
           cur <= end - 8 /* = tag + size */) {
        uint32_t chunk_code;
        int64_t chunk_size;
        char key[5] = { 0 };
        char *value;

        chunk_code = avio_rl32(pb);
        chunk_size = avio_rl32(pb);
        if (url_feof(pb)) {
            if (chunk_code || chunk_size) {
                av_log(s, AV_LOG_WARNING, "INFO subchunk truncated\n");
                return AVERROR_INVALIDDATA;
            }
            return AVERROR_EOF;
        }
        if (chunk_size > end ||
            end - chunk_size < cur ||
            chunk_size == UINT_MAX) {
            avio_seek(pb, -9, SEEK_CUR);
            chunk_code = avio_rl32(pb);
            chunk_size = avio_rl32(pb);
            if (chunk_size > end || end - chunk_size < cur || chunk_size == UINT_MAX) {
                av_log(s, AV_LOG_WARNING, "too big INFO subchunk\n");
                return AVERROR_INVALIDDATA;
            }
        }

        chunk_size += (chunk_size & 1);

        if (!chunk_code) {
            if (chunk_size)
                avio_skip(pb, chunk_size);
            else if (pb->eof_reached) {
                av_log(s, AV_LOG_WARNING, "truncated file\n");
                return AVERROR_EOF;
            }
            continue;
        }

        value = av_mallocz(chunk_size + 1);
        if (!value) {
            av_log(s, AV_LOG_ERROR,
                   "out of memory, unable to read INFO tag\n");
            return AVERROR(ENOMEM);
        }

        AV_WL32(key, chunk_code);

        if (avio_read(pb, value, chunk_size) != chunk_size) {
            av_log(s, AV_LOG_WARNING,
                   "premature end of file while reading INFO tag\n");
        }

        av_dict_set(&s->metadata, key, value, AV_DICT_DONT_STRDUP_VAL);
    }

    return 0;
}
