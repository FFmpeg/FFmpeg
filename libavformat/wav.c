/*
 * WAV muxer and demuxer
 * Copyright (c) 2001, 2002 Fabrice Bellard
 *
 * Sony Wave64 demuxer
 * RF64 demuxer
 * Copyright (c) 2009 Daniel Verkamp
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
#include "libavutil/log.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "avformat.h"
#include "avio_internal.h"
#include "pcm.h"
#include "riff.h"
#include "avio.h"
#include "avio_internal.h"
#include "metadata.h"

typedef struct {
    const AVClass *class;
    int64_t data;
    int64_t data_end;
    int64_t minpts;
    int64_t maxpts;
    int last_duration;
    int w64;
    int write_bext;
    int64_t smv_data_ofs;
    int smv_block_size;
    int smv_frames_per_jpeg;
    int smv_block;
    int smv_last_stream;
    int smv_eof;
    int audio_eof;
} WAVContext;

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
    WAVContext *wav = s->priv_data;
    AVIOContext *pb = s->pb;
    int64_t fmt, fact;

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
        fact = ff_start_tag(pb, "fact");
        avio_wl32(pb, 0);
        ff_end_tag(pb, fact);
    }

    if (wav->write_bext)
        bwf_write_bext_chunk(s);

    av_set_pts_info(s->streams[0], 64, 1, s->streams[0]->codec->sample_rate);
    wav->maxpts = wav->last_duration = 0;
    wav->minpts = INT64_MAX;

    /* data header */
    wav->data = ff_start_tag(pb, "data");

    avio_flush(pb);

    return 0;
}

static int wav_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVIOContext *pb  = s->pb;
    WAVContext    *wav = s->priv_data;
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
    WAVContext    *wav = s->priv_data;
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
            avio_seek(pb, wav->data-12, SEEK_SET);
            avio_wl32(pb, number_of_samples);
            avio_seek(pb, file_size, SEEK_SET);
            avio_flush(pb);
        }
    }
    return 0;
}

#define OFFSET(x) offsetof(WAVContext, x)
#define ENC AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "write_bext", "Write BEXT chunk.", OFFSET(write_bext), AV_OPT_TYPE_INT, { 0 }, 0, 1, ENC },
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
    .long_name         = NULL_IF_CONFIG_SMALL("WAV format"),
    .mime_type         = "audio/x-wav",
    .extensions        = "wav",
    .priv_data_size    = sizeof(WAVContext),
    .audio_codec       = CODEC_ID_PCM_S16LE,
    .video_codec       = CODEC_ID_NONE,
    .write_header      = wav_write_header,
    .write_packet      = wav_write_packet,
    .write_trailer     = wav_write_trailer,
    .codec_tag= (const AVCodecTag* const []){ff_codec_wav_tags, 0},
    .priv_class = &wav_muxer_class,
};
#endif /* CONFIG_WAV_MUXER */


#if CONFIG_WAV_DEMUXER

static int64_t next_tag(AVIOContext *pb, unsigned int *tag)
{
    *tag = avio_rl32(pb);
    return avio_rl32(pb);
}

/* return the size of the found tag */
static int64_t find_tag(AVIOContext *pb, uint32_t tag1)
{
    unsigned int tag;
    int64_t size;

    for (;;) {
        if (url_feof(pb))
            return -1;
        size = next_tag(pb, &tag);
        if (tag == tag1)
            break;
        avio_skip(pb, size);
    }
    return size;
}

static int wav_probe(AVProbeData *p)
{
    /* check file header */
    if (p->buf_size <= 32)
        return 0;
    if (!memcmp(p->buf + 8, "WAVE", 4)) {
        if (!memcmp(p->buf, "RIFF", 4))
            /*
              Since ACT demuxer has standard WAV header at top of it's own,
              returning score is decreased to avoid probe conflict
              between ACT and WAV.
            */
            return AVPROBE_SCORE_MAX - 1;
        else if (!memcmp(p->buf,      "RF64", 4) &&
                 !memcmp(p->buf + 12, "ds64", 4))
            return AVPROBE_SCORE_MAX;
    }
    return 0;
}

static int wav_parse_fmt_tag(AVFormatContext *s, int64_t size, AVStream **st)
{
    AVIOContext *pb = s->pb;
    int ret;

    /* parse fmt header */
    *st = avformat_new_stream(s, NULL);
    if (!*st)
        return AVERROR(ENOMEM);

    ret = ff_get_wav_header(pb, (*st)->codec, size);
    if (ret < 0)
        return ret;
    (*st)->need_parsing = AVSTREAM_PARSE_FULL;

    av_set_pts_info(*st, 64, 1, (*st)->codec->sample_rate);

    return 0;
}

static inline int wav_parse_bext_string(AVFormatContext *s, const char *key,
                                        int length)
{
    char temp[257];
    int ret;

    av_assert0(length <= sizeof(temp));
    if ((ret = avio_read(s->pb, temp, length)) < 0)
        return ret;

    temp[length] = 0;

    if (strlen(temp))
        return av_dict_set(&s->metadata, key, temp, 0);

    return 0;
}

static int wav_parse_bext_tag(AVFormatContext *s, int64_t size)
{
    char temp[131], *coding_history;
    int ret, x;
    uint64_t time_reference;
    int64_t umid_parts[8], umid_mask = 0;

    if ((ret = wav_parse_bext_string(s, "description", 256)) < 0 ||
        (ret = wav_parse_bext_string(s, "originator", 32)) < 0 ||
        (ret = wav_parse_bext_string(s, "originator_reference", 32)) < 0 ||
        (ret = wav_parse_bext_string(s, "origination_date", 10)) < 0 ||
        (ret = wav_parse_bext_string(s, "origination_time", 8)) < 0)
        return ret;

    time_reference = avio_rl64(s->pb);
    snprintf(temp, sizeof(temp), "%"PRIu64, time_reference);
    if ((ret = av_dict_set(&s->metadata, "time_reference", temp, 0)) < 0)
        return ret;

    /* check if version is >= 1, in which case an UMID may be present */
    if (avio_rl16(s->pb) >= 1) {
        for (x = 0; x < 8; x++)
            umid_mask |= umid_parts[x] = avio_rb64(s->pb);

        if (umid_mask) {
            /* the string formatting below is per SMPTE 330M-2004 Annex C */
            if (umid_parts[4] == 0 && umid_parts[5] == 0 && umid_parts[6] == 0 && umid_parts[7] == 0) {
                /* basic UMID */
                snprintf(temp, sizeof(temp), "0x%016"PRIX64"%016"PRIX64"%016"PRIX64"%016"PRIX64,
                         umid_parts[0], umid_parts[1], umid_parts[2], umid_parts[3]);
            } else {
                /* extended UMID */
                snprintf(temp, sizeof(temp), "0x%016"PRIX64"%016"PRIX64"%016"PRIX64"%016"PRIX64
                                             "0x%016"PRIX64"%016"PRIX64"%016"PRIX64"%016"PRIX64,
                         umid_parts[0], umid_parts[1], umid_parts[2], umid_parts[3],
                         umid_parts[4], umid_parts[5], umid_parts[6], umid_parts[7]);
            }

            if ((ret = av_dict_set(&s->metadata, "umid", temp, 0)) < 0)
                return ret;
        }

        avio_skip(s->pb, 190);
    } else
        avio_skip(s->pb, 254);

    if (size > 602) {
        /* CodingHistory present */
        size -= 602;

        if (!(coding_history = av_malloc(size+1)))
            return AVERROR(ENOMEM);

        if ((ret = avio_read(s->pb, coding_history, size)) < 0)
            return ret;

        coding_history[size] = 0;
        if ((ret = av_dict_set(&s->metadata, "coding_history", coding_history,
                               AV_DICT_DONT_STRDUP_VAL)) < 0)
            return ret;
    }

    return 0;
}

static const AVMetadataConv wav_metadata_conv[] = {
    {"description",      "comment"      },
    {"originator",       "encoded_by"   },
    {"origination_date", "date"         },
    {"origination_time", "creation_time"},
    {0},
};

/* wav input */
static int wav_read_header(AVFormatContext *s,
                           AVFormatParameters *ap)
{
    int64_t size, av_uninit(data_size);
    int64_t sample_count=0;
    int rf64;
    unsigned int tag;
    AVIOContext *pb = s->pb;
    AVStream *st = NULL;
    WAVContext *wav = s->priv_data;
    int ret, got_fmt = 0;
    int64_t next_tag_ofs, data_ofs = -1;

    wav->smv_data_ofs = -1;

    /* check RIFF header */
    tag = avio_rl32(pb);

    rf64 = tag == MKTAG('R', 'F', '6', '4');
    if (!rf64 && tag != MKTAG('R', 'I', 'F', 'F'))
        return -1;
    avio_rl32(pb); /* file size */
    tag = avio_rl32(pb);
    if (tag != MKTAG('W', 'A', 'V', 'E'))
        return -1;

    if (rf64) {
        if (avio_rl32(pb) != MKTAG('d', 's', '6', '4'))
            return -1;
        size = avio_rl32(pb);
        if (size < 24)
            return -1;
        avio_rl64(pb); /* RIFF size */
        data_size = avio_rl64(pb);
        sample_count = avio_rl64(pb);
        if (data_size < 0 || sample_count < 0) {
            av_log(s, AV_LOG_ERROR, "negative data_size and/or sample_count in "
                   "ds64: data_size = %"PRId64", sample_count = %"PRId64"\n",
                   data_size, sample_count);
            return AVERROR_INVALIDDATA;
        }
        avio_skip(pb, size - 24); /* skip rest of ds64 chunk */

    }

    for (;;) {
        AVStream *vst;
        size = next_tag(pb, &tag);
        next_tag_ofs = avio_tell(pb) + size;

        if (url_feof(pb))
            break;

        switch (tag) {
        case MKTAG('f', 'm', 't', ' '):
            /* only parse the first 'fmt ' tag found */
            if (!got_fmt && (ret = wav_parse_fmt_tag(s, size, &st)) < 0) {
                return ret;
            } else if (got_fmt)
                av_log(s, AV_LOG_WARNING, "found more than one 'fmt ' tag\n");

            got_fmt = 1;
            break;
        case MKTAG('d', 'a', 't', 'a'):
            if (!got_fmt) {
                av_log(s, AV_LOG_ERROR, "found no 'fmt ' tag before the 'data' tag\n");
                return AVERROR_INVALIDDATA;
            }

            if (rf64) {
                next_tag_ofs = wav->data_end = avio_tell(pb) + data_size;
            } else {
                data_size = size;
                next_tag_ofs = wav->data_end = size ? next_tag_ofs : INT64_MAX;
            }

            data_ofs = avio_tell(pb);

            /* don't look for footer metadata if we can't seek or if we don't
             * know where the data tag ends
             */
            if (!pb->seekable || (!rf64 && !size))
                goto break_loop;
            break;
        case MKTAG('f','a','c','t'):
            if (!sample_count)
                sample_count = avio_rl32(pb);
            break;
        case MKTAG('b','e','x','t'):
            if ((ret = wav_parse_bext_tag(s, size)) < 0)
                return ret;
            break;
        case MKTAG('S','M','V','0'):
            // SMV file, a wav file with video appended.
            if (size != MKTAG('0','2','0','0')) {
                av_log(s, AV_LOG_ERROR, "Unknown SMV version found\n");
                goto break_loop;
            }
            av_log(s, AV_LOG_DEBUG, "Found SMV data\n");
            vst = avformat_new_stream(s, NULL);
            if (!vst)
                return AVERROR(ENOMEM);
            avio_r8(pb);
            vst->id = 1;
            vst->codec->codec_type = AVMEDIA_TYPE_VIDEO;
            vst->codec->codec_id = CODEC_ID_MJPEG;
            vst->codec->width  = avio_rl24(pb);
            vst->codec->height = avio_rl24(pb);
            size = avio_rl24(pb);
            wav->smv_data_ofs = avio_tell(pb) + (size - 5) * 3;
            avio_rl24(pb);
            wav->smv_block_size = avio_rl24(pb);
            av_set_pts_info(vst, 32, 1, avio_rl24(pb));
            vst->duration = avio_rl24(pb);
            avio_rl24(pb);
            avio_rl24(pb);
            wav->smv_frames_per_jpeg = avio_rl24(pb);
            goto break_loop;
        }

        /* seek to next tag unless we know that we'll run into EOF */
        if ((avio_size(pb) > 0 && next_tag_ofs >= avio_size(pb)) ||
            avio_seek(pb, next_tag_ofs, SEEK_SET) < 0) {
            break;
        }
    }
break_loop:
    if (data_ofs < 0) {
        av_log(s, AV_LOG_ERROR, "no 'data' tag found\n");
        return AVERROR_INVALIDDATA;
    }

    avio_seek(pb, data_ofs, SEEK_SET);

    if (!sample_count && st->codec->channels && av_get_bits_per_sample(st->codec->codec_id))
        sample_count = (data_size<<3) / (st->codec->channels * (uint64_t)av_get_bits_per_sample(st->codec->codec_id));
    if (sample_count)
        st->duration = sample_count;

    ff_metadata_conv_ctx(s, NULL, wav_metadata_conv);

    return 0;
}

/** Find chunk with w64 GUID by skipping over other chunks
 * @return the size of the found chunk
 */
static int64_t find_guid(AVIOContext *pb, const uint8_t guid1[16])
{
    uint8_t guid[16];
    int64_t size;

    while (!url_feof(pb)) {
        avio_read(pb, guid, 16);
        size = avio_rl64(pb);
        if (size <= 24)
            return -1;
        if (!memcmp(guid, guid1, 16))
            return size;
        avio_skip(pb, FFALIGN(size, INT64_C(8)) - 24);
    }
    return -1;
}

static const uint8_t guid_data[16] = { 'd', 'a', 't', 'a',
    0xF3, 0xAC, 0xD3, 0x11, 0x8C, 0xD1, 0x00, 0xC0, 0x4F, 0x8E, 0xDB, 0x8A };

#define MAX_SIZE 4096

static int wav_read_packet(AVFormatContext *s,
                           AVPacket *pkt)
{
    int ret, size;
    int64_t left;
    AVStream *st;
    WAVContext *wav = s->priv_data;

    if (wav->smv_data_ofs > 0) {
        int64_t audio_dts, video_dts;
smv_retry:
        audio_dts = s->streams[0]->cur_dts;
        video_dts = s->streams[1]->cur_dts;
        if (audio_dts != AV_NOPTS_VALUE && video_dts != AV_NOPTS_VALUE) {
            audio_dts = av_rescale_q(audio_dts, s->streams[0]->time_base, AV_TIME_BASE_Q);
            video_dts = av_rescale_q(video_dts, s->streams[1]->time_base, AV_TIME_BASE_Q);
            wav->smv_last_stream = video_dts >= audio_dts;
        }
        wav->smv_last_stream = !wav->smv_last_stream;
        wav->smv_last_stream |= wav->audio_eof;
        wav->smv_last_stream &= !wav->smv_eof;
        if (wav->smv_last_stream) {
            uint64_t old_pos = avio_tell(s->pb);
            uint64_t new_pos = wav->smv_data_ofs +
                wav->smv_block * wav->smv_block_size;
            if (avio_seek(s->pb, new_pos, SEEK_SET) < 0) {
                ret = AVERROR_EOF;
                goto smv_out;
            }
            size = avio_rl24(s->pb);
            ret  = av_get_packet(s->pb, pkt, size);
            if (ret < 0)
                goto smv_out;
            pkt->pos -= 3;
            pkt->pts = wav->smv_block * wav->smv_frames_per_jpeg;
            wav->smv_block++;
            pkt->stream_index = 1;
smv_out:
            avio_seek(s->pb, old_pos, SEEK_SET);
            if (ret == AVERROR_EOF) {
                wav->smv_eof = 1;
                goto smv_retry;
            }
            return ret;
        }
    }

    st = s->streams[0];

    left = wav->data_end - avio_tell(s->pb);
    if (left <= 0){
        if (CONFIG_W64_DEMUXER && wav->w64)
            left = find_guid(s->pb, guid_data) - 24;
        else
            left = find_tag(s->pb, MKTAG('d', 'a', 't', 'a'));
        if (left < 0) {
            wav->audio_eof = 1;
            if (wav->smv_data_ofs > 0 && !wav->smv_eof)
                goto smv_retry;
            return AVERROR_EOF;
        }
        wav->data_end= avio_tell(s->pb) + left;
    }

    size = MAX_SIZE;
    if (st->codec->block_align > 1) {
        if (size < st->codec->block_align)
            size = st->codec->block_align;
        size = (size / st->codec->block_align) * st->codec->block_align;
    }
    size = FFMIN(size, left);
    ret  = av_get_packet(s->pb, pkt, size);
    if (ret < 0)
        return ret;
    pkt->stream_index = 0;

    return ret;
}

static int wav_read_seek(AVFormatContext *s,
                         int stream_index, int64_t timestamp, int flags)
{
    WAVContext *wav = s->priv_data;
    AVStream *st;
    wav->smv_eof = 0;
    wav->audio_eof = 0;
    if (wav->smv_data_ofs > 0) {
        int64_t smv_timestamp = timestamp;
        if (stream_index == 0)
            smv_timestamp = av_rescale_q(timestamp, s->streams[0]->time_base, s->streams[1]->time_base);
        else
            timestamp = av_rescale_q(smv_timestamp, s->streams[1]->time_base, s->streams[0]->time_base);
        wav->smv_block = smv_timestamp / wav->smv_frames_per_jpeg;
    }

    st = s->streams[0];
    switch (st->codec->codec_id) {
    case CODEC_ID_MP2:
    case CODEC_ID_MP3:
    case CODEC_ID_AC3:
    case CODEC_ID_DTS:
        /* use generic seeking with dynamically generated indexes */
        return -1;
    default:
        break;
    }
    return pcm_read_seek(s, stream_index, timestamp, flags);
}

AVInputFormat ff_wav_demuxer = {
    .name           = "wav",
    .long_name      = NULL_IF_CONFIG_SMALL("WAV format"),
    .priv_data_size = sizeof(WAVContext),
    .read_probe     = wav_probe,
    .read_header    = wav_read_header,
    .read_packet    = wav_read_packet,
    .read_seek      = wav_read_seek,
    .flags= AVFMT_GENERIC_INDEX,
    .codec_tag= (const AVCodecTag* const []){ff_codec_wav_tags, 0},
};
#endif /* CONFIG_WAV_DEMUXER */


#if CONFIG_W64_DEMUXER
static const uint8_t guid_riff[16] = { 'r', 'i', 'f', 'f',
    0x2E, 0x91, 0xCF, 0x11, 0xA5, 0xD6, 0x28, 0xDB, 0x04, 0xC1, 0x00, 0x00 };

static const uint8_t guid_wave[16] = { 'w', 'a', 'v', 'e',
    0xF3, 0xAC, 0xD3, 0x11, 0x8C, 0xD1, 0x00, 0xC0, 0x4F, 0x8E, 0xDB, 0x8A };

static const uint8_t guid_fmt [16] = { 'f', 'm', 't', ' ',
    0xF3, 0xAC, 0xD3, 0x11, 0x8C, 0xD1, 0x00, 0xC0, 0x4F, 0x8E, 0xDB, 0x8A };

static int w64_probe(AVProbeData *p)
{
    if (p->buf_size <= 40)
        return 0;
    if (!memcmp(p->buf,      guid_riff, 16) &&
        !memcmp(p->buf + 24, guid_wave, 16))
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}

static int w64_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    int64_t size;
    AVIOContext *pb  = s->pb;
    WAVContext    *wav = s->priv_data;
    AVStream *st;
    uint8_t guid[16];
    int ret;

    avio_read(pb, guid, 16);
    if (memcmp(guid, guid_riff, 16))
        return -1;

    if (avio_rl64(pb) < 16 + 8 + 16 + 8 + 16 + 8) /* riff + wave + fmt + sizes */
        return -1;

    avio_read(pb, guid, 16);
    if (memcmp(guid, guid_wave, 16)) {
        av_log(s, AV_LOG_ERROR, "could not find wave guid\n");
        return -1;
    }

    size = find_guid(pb, guid_fmt);
    if (size < 0) {
        av_log(s, AV_LOG_ERROR, "could not find fmt guid\n");
        return -1;
    }

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    /* subtract chunk header size - normal wav file doesn't count it */
    ret = ff_get_wav_header(pb, st->codec, size - 24);
    if (ret < 0)
        return ret;
    avio_skip(pb, FFALIGN(size, INT64_C(8)) - size);

    st->need_parsing = AVSTREAM_PARSE_FULL;

    av_set_pts_info(st, 64, 1, st->codec->sample_rate);

    size = find_guid(pb, guid_data);
    if (size < 0) {
        av_log(s, AV_LOG_ERROR, "could not find data guid\n");
        return -1;
    }
    wav->data_end = avio_tell(pb) + size - 24;
    wav->w64      = 1;

    return 0;
}

AVInputFormat ff_w64_demuxer = {
    .name           = "w64",
    .long_name      = NULL_IF_CONFIG_SMALL("Sony Wave64 format"),
    .priv_data_size = sizeof(WAVContext),
    .read_probe     = w64_probe,
    .read_header    = w64_read_header,
    .read_packet    = wav_read_packet,
    .read_seek      = wav_read_seek,
    .flags = AVFMT_GENERIC_INDEX,
    .codec_tag = (const AVCodecTag* const []){ff_codec_wav_tags, 0},
};
#endif /* CONFIG_W64_DEMUXER */
