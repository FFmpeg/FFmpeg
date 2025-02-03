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

#include "libavutil/avassert.h"
#include "libavutil/dict.h"
#include "libavutil/error.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "avformat.h"
#include "avio_internal.h"
#include "demux.h"
#include "riff.h"

int ff_get_guid(AVIOContext *s, ff_asf_guid *g)
{
    int ret;
    av_assert0(sizeof(*g) == 16); //compiler will optimize this out
    ret = ffio_read_size(s, *g, sizeof(*g));
    if (ret < 0) {
        memset(*g, 0, sizeof(*g));
        return ret;
    }
    return 0;
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

static void parse_waveformatex(void *logctx, AVIOContext *pb, AVCodecParameters *par)
{
    ff_asf_guid subformat;
    int bps;
    uint64_t mask;

    bps = avio_rl16(pb);
    if (bps)
        par->bits_per_coded_sample = bps;

    mask = avio_rl32(pb); /* dwChannelMask */
    av_channel_layout_from_mask(&par->ch_layout, mask);

    ff_get_guid(pb, &subformat);
    if (!memcmp(subformat + 4,
                (const uint8_t[]){ FF_AMBISONIC_BASE_GUID }, 12) ||
        !memcmp(subformat + 4,
                (const uint8_t[]){ FF_BROKEN_BASE_GUID }, 12) ||
        !memcmp(subformat + 4,
                (const uint8_t[]){ FF_MEDIASUBTYPE_BASE_GUID }, 12)) {
        par->codec_tag = AV_RL32(subformat);
        par->codec_id  = ff_wav_codec_get_id(par->codec_tag,
                                             par->bits_per_coded_sample);
    } else {
        par->codec_id = ff_codec_guid_get_id(ff_codec_wav_guids, subformat);
        if (!par->codec_id)
            av_log(logctx, AV_LOG_WARNING,
                   "unknown subformat:"FF_PRI_GUID"\n",
                   FF_ARG_GUID(subformat));
    }
}

/* "big_endian" values are needed for RIFX file format */
int ff_get_wav_header(AVFormatContext *s, AVIOContext *pb,
                      AVCodecParameters *par, int size, int big_endian)
{
    int id, channels = 0, ret;
    uint64_t bitrate = 0;

    if (size < 14) {
        avpriv_request_sample(s, "wav header size < 14");
        return AVERROR_INVALIDDATA;
    }

    av_channel_layout_uninit(&par->ch_layout);

    par->codec_type  = AVMEDIA_TYPE_AUDIO;
    if (!big_endian) {
        id                 = avio_rl16(pb);
        if (id != 0x0165) {
            channels         = avio_rl16(pb);
            par->sample_rate = avio_rl32(pb);
            bitrate            = avio_rl32(pb) * 8LL;
            par->block_align = avio_rl16(pb);
        }
    } else {
        id                 = avio_rb16(pb);
        channels           = avio_rb16(pb);
        par->sample_rate = avio_rb32(pb);
        bitrate            = avio_rb32(pb) * 8LL;
        par->block_align = avio_rb16(pb);
    }
    if (size == 14) {  /* We're dealing with plain vanilla WAVEFORMAT */
        par->bits_per_coded_sample = 8;
    } else {
        if (!big_endian) {
            par->bits_per_coded_sample = avio_rl16(pb);
        } else {
            par->bits_per_coded_sample = avio_rb16(pb);
        }
    }
    if (id == 0xFFFE) {
        par->codec_tag = 0;
    } else {
        par->codec_tag = id;
        par->codec_id  = ff_wav_codec_get_id(id,
                                             par->bits_per_coded_sample);
    }
    if (size >= 18 && id != 0x0165) {  /* We're obviously dealing with WAVEFORMATEX */
        int cbSize = avio_rl16(pb); /* cbSize */
        if (big_endian) {
            avpriv_report_missing_feature(s, "WAVEFORMATEX support for RIFX files");
            return AVERROR_PATCHWELCOME;
        }
        size  -= 18;
        cbSize = FFMIN(size, cbSize);
        if (cbSize >= 22 && id == 0xfffe) { /* WAVEFORMATEXTENSIBLE */
            parse_waveformatex(s, pb, par);
            cbSize -= 22;
            size   -= 22;
        }
        if (cbSize > 0) {
            ret = ff_get_extradata(s, par, pb, cbSize);
            if (ret < 0)
                return ret;
            size -= cbSize;
        }

        /* It is possible for the chunk to contain garbage at the end */
        if (size > 0)
            avio_skip(pb, size);
    } else if (id == 0x0165 && size >= 32) {
        int nb_streams, i;

        size -= 4;
        ret = ff_get_extradata(s, par, pb, size);
        if (ret < 0)
            return ret;
        nb_streams         = AV_RL16(par->extradata + 4);
        par->sample_rate   = AV_RL32(par->extradata + 12);
        channels           = 0;
        bitrate            = 0;
        if (size < 8 + nb_streams * 20)
            return AVERROR_INVALIDDATA;
        for (i = 0; i < nb_streams; i++)
            channels += par->extradata[8 + i * 20 + 17];
    }

    par->bit_rate = bitrate;

    if (par->sample_rate <= 0) {
        av_log(s, AV_LOG_ERROR,
               "Invalid sample rate: %d\n", par->sample_rate);
        return AVERROR_INVALIDDATA;
    }
    if (par->codec_id == AV_CODEC_ID_AAC_LATM) {
        /* Channels and sample_rate values are those prior to applying SBR
         * and/or PS. */
        channels         = 0;
        par->sample_rate = 0;
    }
    /* override bits_per_coded_sample for G.726 */
    if (par->codec_id == AV_CODEC_ID_ADPCM_G726 && par->sample_rate)
        par->bits_per_coded_sample = par->bit_rate / par->sample_rate;

    /* ignore WAVEFORMATEXTENSIBLE layout if different from channel count */
    if (channels != par->ch_layout.nb_channels) {
        av_channel_layout_uninit(&par->ch_layout);
        par->ch_layout.order       = AV_CHANNEL_ORDER_UNSPEC;
        par->ch_layout.nb_channels = channels;
    }

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
        id = AV_CODEC_ID_ADPCM_ZORK;
    return id;
}

int ff_get_bmp_header(AVIOContext *pb, AVStream *st, uint32_t *size)
{
    int tag1;
    uint32_t size_ = avio_rl32(pb);
    if (size)
        *size = size_;
    st->codecpar->width  = avio_rl32(pb);
    st->codecpar->height = (int32_t)avio_rl32(pb);
    avio_rl16(pb); /* planes */
    st->codecpar->bits_per_coded_sample = avio_rl16(pb); /* depth */
    tag1                                = avio_rl32(pb);
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
        if (avio_feof(pb)) {
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
        // Work around VC++ 2015 Update 1 code-gen bug:
        // https://connect.microsoft.com/VisualStudio/feedback/details/2291638
        key[4] = 0;

        if (avio_read(pb, value, chunk_size) != chunk_size) {
            av_log(s, AV_LOG_WARNING,
                   "premature end of file while reading INFO tag\n");
        }

        av_dict_set(&s->metadata, key, value, AV_DICT_DONT_STRDUP_VAL);
    }

    return 0;
}
