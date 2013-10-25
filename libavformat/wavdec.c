/*
 * WAV demuxer
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
#include "libavutil/intreadwrite.h"
#include "libavutil/log.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "avformat.h"
#include "avio.h"
#include "avio_internal.h"
#include "internal.h"
#include "metadata.h"
#include "pcm.h"
#include "riff.h"
#include "w64.h"
#include "spdif.h"

typedef struct WAVDemuxContext {
    const AVClass *class;
    int64_t data_end;
    int w64;
    int64_t smv_data_ofs;
    int smv_block_size;
    int smv_frames_per_jpeg;
    int smv_block;
    int smv_last_stream;
    int smv_eof;
    int audio_eof;
    int ignore_length;
    int spdif;
    int smv_cur_pt;
    int smv_given_first;
} WAVDemuxContext;

#if CONFIG_WAV_DEMUXER

static int64_t next_tag(AVIOContext *pb, uint32_t *tag)
{
    *tag = avio_rl32(pb);
    return avio_rl32(pb);
}

/* RIFF chunks are always on a even offset. */
static int64_t wav_seek_tag(AVIOContext *s, int64_t offset, int whence)
{
    offset += offset < INT64_MAX && offset & 1;

    return avio_seek(s, offset, whence);
}

/* return the size of the found tag */
static int64_t find_tag(AVIOContext *pb, uint32_t tag1)
{
    unsigned int tag;
    int64_t size;

    for (;;) {
        if (url_feof(pb))
            return AVERROR_EOF;
        size = next_tag(pb, &tag);
        if (tag == tag1)
            break;
        wav_seek_tag(pb, size, SEEK_CUR);
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
            /* Since the ACT demuxer has a standard WAV header at the top of
             * its own, the returned score is decreased to avoid a probe
             * conflict between ACT and WAV. */
            return AVPROBE_SCORE_MAX - 1;
        else if (!memcmp(p->buf,      "RF64", 4) &&
                 !memcmp(p->buf + 12, "ds64", 4))
            return AVPROBE_SCORE_MAX;
    }
    return 0;
}

static void handle_stream_probing(AVStream *st)
{
    if (st->codec->codec_id == AV_CODEC_ID_PCM_S16LE) {
        st->request_probe = AVPROBE_SCORE_EXTENSION;
        st->probe_packets = FFMIN(st->probe_packets, 4);
    }
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
    handle_stream_probing(*st);

    (*st)->need_parsing = AVSTREAM_PARSE_FULL_RAW;

    avpriv_set_pts_info(*st, 64, 1, (*st)->codec->sample_rate);

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
            if (umid_parts[4] == 0 && umid_parts[5] == 0 &&
                umid_parts[6] == 0 && umid_parts[7] == 0) {
                /* basic UMID */
                snprintf(temp, sizeof(temp),
                         "0x%016"PRIX64"%016"PRIX64"%016"PRIX64"%016"PRIX64,
                         umid_parts[0], umid_parts[1],
                         umid_parts[2], umid_parts[3]);
            } else {
                /* extended UMID */
                snprintf(temp, sizeof(temp),
                         "0x%016"PRIX64"%016"PRIX64"%016"PRIX64"%016"PRIX64
                         "%016"PRIX64"%016"PRIX64"%016"PRIX64"%016"PRIX64,
                         umid_parts[0], umid_parts[1],
                         umid_parts[2], umid_parts[3],
                         umid_parts[4], umid_parts[5],
                         umid_parts[6], umid_parts[7]);
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

        if (!(coding_history = av_malloc(size + 1)))
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
    { "description",      "comment"       },
    { "originator",       "encoded_by"    },
    { "origination_date", "date"          },
    { "origination_time", "creation_time" },
    { 0 },
};

/* wav input */
static int wav_read_header(AVFormatContext *s)
{
    int64_t size, av_uninit(data_size);
    int64_t sample_count = 0;
    int rf64;
    uint32_t tag;
    AVIOContext *pb      = s->pb;
    AVStream *st         = NULL;
    WAVDemuxContext *wav = s->priv_data;
    int ret, got_fmt = 0;
    int64_t next_tag_ofs, data_ofs = -1;

    wav->smv_data_ofs = -1;

    /* check RIFF header */
    tag = avio_rl32(pb);

    rf64 = tag == MKTAG('R', 'F', '6', '4');
    if (!rf64 && tag != MKTAG('R', 'I', 'F', 'F'))
        return AVERROR_INVALIDDATA;
    avio_rl32(pb); /* file size */
    tag = avio_rl32(pb);
    if (tag != MKTAG('W', 'A', 'V', 'E'))
        return AVERROR_INVALIDDATA;

    if (rf64) {
        if (avio_rl32(pb) != MKTAG('d', 's', '6', '4'))
            return AVERROR_INVALIDDATA;
        size = avio_rl32(pb);
        if (size < 24)
            return AVERROR_INVALIDDATA;
        avio_rl64(pb); /* RIFF size */

        data_size    = avio_rl64(pb);
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
        size         = next_tag(pb, &tag);
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
                av_log(s, AV_LOG_ERROR,
                       "found no 'fmt ' tag before the 'data' tag\n");
                return AVERROR_INVALIDDATA;
            }

            if (rf64) {
                next_tag_ofs = wav->data_end = avio_tell(pb) + data_size;
            } else {
                data_size    = size;
                next_tag_ofs = wav->data_end = size ? next_tag_ofs : INT64_MAX;
            }

            data_ofs = avio_tell(pb);

            /* don't look for footer metadata if we can't seek or if we don't
             * know where the data tag ends
             */
            if (!pb->seekable || (!rf64 && !size))
                goto break_loop;
            break;
        case MKTAG('f', 'a', 'c', 't'):
            if (!sample_count)
                sample_count = avio_rl32(pb);
            break;
        case MKTAG('b', 'e', 'x', 't'):
            if ((ret = wav_parse_bext_tag(s, size)) < 0)
                return ret;
            break;
        case MKTAG('S','M','V','0'):
            if (!got_fmt) {
                av_log(s, AV_LOG_ERROR, "found no 'fmt ' tag before the 'SMV0' tag\n");
                return AVERROR_INVALIDDATA;
            }
            // SMV file, a wav file with video appended.
            if (size != MKTAG('0','2','0','0')) {
                av_log(s, AV_LOG_ERROR, "Unknown SMV version found\n");
                goto break_loop;
            }
            av_log(s, AV_LOG_DEBUG, "Found SMV data\n");
            wav->smv_given_first = 0;
            vst = avformat_new_stream(s, NULL);
            if (!vst)
                return AVERROR(ENOMEM);
            avio_r8(pb);
            vst->id = 1;
            vst->codec->codec_type = AVMEDIA_TYPE_VIDEO;
            vst->codec->codec_id = AV_CODEC_ID_SMVJPEG;
            vst->codec->width  = avio_rl24(pb);
            vst->codec->height = avio_rl24(pb);
            vst->codec->extradata_size = 4;
            vst->codec->extradata = av_malloc(vst->codec->extradata_size +
                                              FF_INPUT_BUFFER_PADDING_SIZE);
            if (!vst->codec->extradata) {
                av_log(s, AV_LOG_ERROR, "Could not allocate extradata.\n");
                return AVERROR(ENOMEM);
            }
            size = avio_rl24(pb);
            wav->smv_data_ofs = avio_tell(pb) + (size - 5) * 3;
            avio_rl24(pb);
            wav->smv_block_size = avio_rl24(pb);
            avpriv_set_pts_info(vst, 32, 1, avio_rl24(pb));
            vst->duration = avio_rl24(pb);
            avio_rl24(pb);
            avio_rl24(pb);
            wav->smv_frames_per_jpeg = avio_rl24(pb);
            AV_WL32(vst->codec->extradata, wav->smv_frames_per_jpeg);
            wav->smv_cur_pt = 0;
            goto break_loop;
        case MKTAG('L', 'I', 'S', 'T'):
            if (size < 4) {
                av_log(s, AV_LOG_ERROR, "too short LIST tag\n");
                return AVERROR_INVALIDDATA;
            }
            switch (avio_rl32(pb)) {
            case MKTAG('I', 'N', 'F', 'O'):
                ff_read_riff_info(s, size - 4);
            }
            break;
        }

        /* seek to next tag unless we know that we'll run into EOF */
        if ((avio_size(pb) > 0 && next_tag_ofs >= avio_size(pb)) ||
            wav_seek_tag(pb, next_tag_ofs, SEEK_SET) < 0) {
            break;
        }
    }

break_loop:
    if (data_ofs < 0) {
        av_log(s, AV_LOG_ERROR, "no 'data' tag found\n");
        return AVERROR_INVALIDDATA;
    }

    avio_seek(pb, data_ofs, SEEK_SET);

    if (!sample_count || av_get_exact_bits_per_sample(st->codec->codec_id) > 0)
        if (   st->codec->channels
            && data_size
            && av_get_bits_per_sample(st->codec->codec_id)
            && wav->data_end <= avio_size(pb))
            sample_count = (data_size << 3)
                                  /
                (st->codec->channels * (uint64_t)av_get_bits_per_sample(st->codec->codec_id));

    if (sample_count)
        st->duration = sample_count;

    ff_metadata_conv_ctx(s, NULL, wav_metadata_conv);
    ff_metadata_conv_ctx(s, NULL, ff_riff_info_conv);

    return 0;
}

/**
 * Find chunk with w64 GUID by skipping over other chunks.
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
            return AVERROR_INVALIDDATA;
        if (!memcmp(guid, guid1, 16))
            return size;
        avio_skip(pb, FFALIGN(size, INT64_C(8)) - 24);
    }
    return AVERROR_EOF;
}

#define MAX_SIZE 4096

static int wav_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret, size;
    int64_t left;
    AVStream *st;
    WAVDemuxContext *wav = s->priv_data;

    if (CONFIG_SPDIF_DEMUXER && wav->spdif == 0 &&
        s->streams[0]->codec->codec_tag == 1) {
        enum AVCodecID codec;
        ret = ff_spdif_probe(s->pb->buffer, s->pb->buf_end - s->pb->buffer,
                             &codec);
        if (ret > AVPROBE_SCORE_EXTENSION) {
            s->streams[0]->codec->codec_id = codec;
            wav->spdif = 1;
        } else {
            wav->spdif = -1;
        }
    }
    if (CONFIG_SPDIF_DEMUXER && wav->spdif == 1)
        return ff_spdif_read_packet(s, pkt);

    if (wav->smv_data_ofs > 0) {
        int64_t audio_dts, video_dts;
smv_retry:
        audio_dts = (int32_t)s->streams[0]->cur_dts;
        video_dts = (int32_t)s->streams[1]->cur_dts;

        if (audio_dts != AV_NOPTS_VALUE && video_dts != AV_NOPTS_VALUE) {
            /*We always return a video frame first to get the pixel format first*/
            wav->smv_last_stream = wav->smv_given_first ?
                av_compare_ts(video_dts, s->streams[1]->time_base,
                              audio_dts, s->streams[0]->time_base) > 0 : 0;
            wav->smv_given_first = 1;
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
            pkt->pts = wav->smv_block * wav->smv_frames_per_jpeg + wav->smv_cur_pt;
            wav->smv_cur_pt++;
            if (wav->smv_frames_per_jpeg > 0)
                wav->smv_cur_pt %= wav->smv_frames_per_jpeg;
            if (!wav->smv_cur_pt)
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
    if (wav->ignore_length)
        left = INT_MAX;
    if (left <= 0) {
        if (CONFIG_W64_DEMUXER && wav->w64)
            left = find_guid(s->pb, ff_w64_guid_data) - 24;
        else
            left = find_tag(s->pb, MKTAG('d', 'a', 't', 'a'));
        if (left < 0) {
            wav->audio_eof = 1;
            if (wav->smv_data_ofs > 0 && !wav->smv_eof)
                goto smv_retry;
            return AVERROR_EOF;
        }
        wav->data_end = avio_tell(s->pb) + left;
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
    WAVDemuxContext *wav = s->priv_data;
    AVStream *st;
    wav->smv_eof = 0;
    wav->audio_eof = 0;
    if (wav->smv_data_ofs > 0) {
        int64_t smv_timestamp = timestamp;
        if (stream_index == 0)
            smv_timestamp = av_rescale_q(timestamp, s->streams[0]->time_base, s->streams[1]->time_base);
        else
            timestamp = av_rescale_q(smv_timestamp, s->streams[1]->time_base, s->streams[0]->time_base);
        if (wav->smv_frames_per_jpeg > 0) {
            wav->smv_block = smv_timestamp / wav->smv_frames_per_jpeg;
            wav->smv_cur_pt = smv_timestamp % wav->smv_frames_per_jpeg;
        }
    }

    st = s->streams[0];
    switch (st->codec->codec_id) {
    case AV_CODEC_ID_MP2:
    case AV_CODEC_ID_MP3:
    case AV_CODEC_ID_AC3:
    case AV_CODEC_ID_DTS:
        /* use generic seeking with dynamically generated indexes */
        return -1;
    default:
        break;
    }
    return ff_pcm_read_seek(s, stream_index, timestamp, flags);
}

#define OFFSET(x) offsetof(WAVDemuxContext, x)
#define DEC AV_OPT_FLAG_DECODING_PARAM
static const AVOption demux_options[] = {
    { "ignore_length", "Ignore length", OFFSET(ignore_length), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, DEC },
    { NULL },
};

static const AVClass wav_demuxer_class = {
    .class_name = "WAV demuxer",
    .item_name  = av_default_item_name,
    .option     = demux_options,
    .version    = LIBAVUTIL_VERSION_INT,
};
AVInputFormat ff_wav_demuxer = {
    .name           = "wav",
    .long_name      = NULL_IF_CONFIG_SMALL("WAV / WAVE (Waveform Audio)"),
    .priv_data_size = sizeof(WAVDemuxContext),
    .read_probe     = wav_probe,
    .read_header    = wav_read_header,
    .read_packet    = wav_read_packet,
    .read_seek      = wav_read_seek,
    .flags          = AVFMT_GENERIC_INDEX,
    .codec_tag      = (const AVCodecTag * const []) { ff_codec_wav_tags,  0 },
    .priv_class     = &wav_demuxer_class,
};
#endif /* CONFIG_WAV_DEMUXER */

#if CONFIG_W64_DEMUXER
static int w64_probe(AVProbeData *p)
{
    if (p->buf_size <= 40)
        return 0;
    if (!memcmp(p->buf,      ff_w64_guid_riff, 16) &&
        !memcmp(p->buf + 24, ff_w64_guid_wave, 16))
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}

static int w64_read_header(AVFormatContext *s)
{
    int64_t size, data_ofs = 0;
    AVIOContext *pb      = s->pb;
    WAVDemuxContext *wav = s->priv_data;
    AVStream *st;
    uint8_t guid[16];
    int ret;

    avio_read(pb, guid, 16);
    if (memcmp(guid, ff_w64_guid_riff, 16))
        return AVERROR_INVALIDDATA;

    /* riff + wave + fmt + sizes */
    if (avio_rl64(pb) < 16 + 8 + 16 + 8 + 16 + 8)
        return AVERROR_INVALIDDATA;

    avio_read(pb, guid, 16);
    if (memcmp(guid, ff_w64_guid_wave, 16)) {
        av_log(s, AV_LOG_ERROR, "could not find wave guid\n");
        return AVERROR_INVALIDDATA;
    }

    wav->w64 = 1;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    while (!url_feof(pb)) {
        if (avio_read(pb, guid, 16) != 16)
            break;
        size = avio_rl64(pb);
        if (size <= 24 || INT64_MAX - size < avio_tell(pb))
            return AVERROR_INVALIDDATA;

        if (!memcmp(guid, ff_w64_guid_fmt, 16)) {
            /* subtract chunk header size - normal wav file doesn't count it */
            ret = ff_get_wav_header(pb, st->codec, size - 24);
            if (ret < 0)
                return ret;
            avio_skip(pb, FFALIGN(size, INT64_C(8)) - size);

            avpriv_set_pts_info(st, 64, 1, st->codec->sample_rate);
        } else if (!memcmp(guid, ff_w64_guid_fact, 16)) {
            int64_t samples;

            samples = avio_rl64(pb);
            if (samples > 0)
                st->duration = samples;
        } else if (!memcmp(guid, ff_w64_guid_data, 16)) {
            wav->data_end = avio_tell(pb) + size - 24;

            data_ofs = avio_tell(pb);
            if (!pb->seekable)
                break;

            avio_skip(pb, size - 24);
        } else if (!memcmp(guid, ff_w64_guid_summarylist, 16)) {
            int64_t start, end, cur;
            uint32_t count, chunk_size, i;

            start = avio_tell(pb);
            end = start + FFALIGN(size, INT64_C(8)) - 24;
            count = avio_rl32(pb);

            for (i = 0; i < count; i++) {
                char chunk_key[5], *value;

                if (url_feof(pb) || (cur = avio_tell(pb)) < 0 || cur > end - 8 /* = tag + size */)
                    break;

                chunk_key[4] = 0;
                avio_read(pb, chunk_key, 4);
                chunk_size = avio_rl32(pb);

                value = av_mallocz(chunk_size + 1);
                if (!value)
                    return AVERROR(ENOMEM);

                ret = avio_get_str16le(pb, chunk_size, value, chunk_size);
                avio_skip(pb, chunk_size - ret);

                av_dict_set(&s->metadata, chunk_key, value, AV_DICT_DONT_STRDUP_VAL);
            }

            avio_skip(pb, end - avio_tell(pb));
        } else {
            av_log(s, AV_LOG_DEBUG, "unknown guid: "FF_PRI_GUID"\n", FF_ARG_GUID(guid));
            avio_skip(pb, FFALIGN(size, INT64_C(8)) - 24);
        }
    }

    if (!data_ofs)
        return AVERROR_EOF;

    ff_metadata_conv_ctx(s, NULL, wav_metadata_conv);
    ff_metadata_conv_ctx(s, NULL, ff_riff_info_conv);

    handle_stream_probing(st);
    st->need_parsing = AVSTREAM_PARSE_FULL_RAW;

    avio_seek(pb, data_ofs, SEEK_SET);

    return 0;
}

AVInputFormat ff_w64_demuxer = {
    .name           = "w64",
    .long_name      = NULL_IF_CONFIG_SMALL("Sony Wave64"),
    .priv_data_size = sizeof(WAVDemuxContext),
    .read_probe     = w64_probe,
    .read_header    = w64_read_header,
    .read_packet    = wav_read_packet,
    .read_seek      = wav_read_seek,
    .flags          = AVFMT_GENERIC_INDEX,
    .codec_tag      = (const AVCodecTag * const []) { ff_codec_wav_tags, 0 },
};
#endif /* CONFIG_W64_DEMUXER */
