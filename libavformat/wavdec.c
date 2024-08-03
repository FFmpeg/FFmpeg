/*
 * WAV demuxer
 * Copyright (c) 2001, 2002 Fabrice Bellard
 *
 * Sony Wave64 demuxer
 * RF64 demuxer
 * Copyright (c) 2009 Daniel Verkamp
 *
 * BW64 demuxer
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

#include "config_components.h"
#include "libavutil/avassert.h"
#include "libavutil/dict.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/log.h"
#include "libavutil/mathematics.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavcodec/internal.h"
#include "avformat.h"
#include "avio.h"
#include "avio_internal.h"
#include "demux.h"
#include "id3v2.h"
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
    AVStream *vst;
    int64_t smv_data_ofs;
    int smv_block_size;
    int smv_frames_per_jpeg;
    int smv_block;
    int smv_last_stream;
    int smv_eof;
    int audio_eof;
    int ignore_length;
    int max_size;
    int spdif;
    int smv_given_first;
    int unaligned; // e.g. if an odd number of bytes ID3 tag was prepended
    int rifx; // RIFX: integer byte order for parameters is big endian
} WAVDemuxContext;

#define OFFSET(x) offsetof(WAVDemuxContext, x)
#define DEC AV_OPT_FLAG_DECODING_PARAM
static const AVOption demux_options[] = {
#define W64_DEMUXER_OPTIONS_OFFSET (1 * CONFIG_WAV_DEMUXER)
#if CONFIG_WAV_DEMUXER
    { "ignore_length", "Ignore length", OFFSET(ignore_length), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, DEC },
#endif
    { "max_size",      "max size of single packet", OFFSET(max_size), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1 << 22, DEC },
    { NULL },
};

static void set_max_size(AVStream *st, WAVDemuxContext *wav)
{
    if (wav->max_size <= 0) {
        int max_size = ff_pcm_default_packet_size(st->codecpar);
        wav->max_size = max_size < 0 ? 4096 : max_size;
    }
}

static void set_spdif(AVFormatContext *s, WAVDemuxContext *wav)
{
    if (CONFIG_SPDIF_DEMUXER && s->streams[0]->codecpar->codec_tag == 1) {
        enum AVCodecID codec;
        int len = 1<<16;
        int ret = ffio_ensure_seekback(s->pb, len);

        if (ret >= 0) {
            uint8_t *buf = av_malloc(len + AV_INPUT_BUFFER_PADDING_SIZE);
            if (!buf) {
                ret = AVERROR(ENOMEM);
            } else {
                int64_t pos = avio_tell(s->pb);
                len = ret = avio_read(s->pb, buf, len);
                if (len >= 0) {
                    ret = ff_spdif_probe(buf, len, &codec);
                    if (ret > AVPROBE_SCORE_EXTENSION) {
                        s->streams[0]->codecpar->codec_id = codec;
                        wav->spdif = 1;
                    }
                }
                avio_seek(s->pb, pos, SEEK_SET);
                av_free(buf);
            }
        }

        if (ret < 0)
            av_log(s, AV_LOG_WARNING, "Cannot check for SPDIF\n");
    }
}

#if CONFIG_WAV_DEMUXER

static int64_t next_tag(AVIOContext *pb, uint32_t *tag, int big_endian)
{
    *tag = avio_rl32(pb);
    if (!big_endian) {
        return avio_rl32(pb);
    } else {
        return avio_rb32(pb);
    }
}

/* RIFF chunks are always at even offsets relative to where they start. */
static int64_t wav_seek_tag(WAVDemuxContext * wav, AVIOContext *s, int64_t offset, int whence)
{
    offset += offset < INT64_MAX && offset + wav->unaligned & 1;

    return avio_seek(s, offset, whence);
}

/* return the size of the found tag */
static int64_t find_tag(WAVDemuxContext * wav, AVIOContext *pb, uint32_t tag1)
{
    unsigned int tag;
    int64_t size;

    for (;;) {
        if (avio_feof(pb))
            return AVERROR_EOF;
        size = next_tag(pb, &tag, wav->rifx);
        if (tag == tag1)
            break;
        wav_seek_tag(wav, pb, size, SEEK_CUR);
    }
    return size;
}

static int wav_probe(const AVProbeData *p)
{
    /* check file header */
    if (p->buf_size <= 32)
        return 0;
    if (!memcmp(p->buf + 8, "WAVE", 4)) {
        if (!memcmp(p->buf, "RIFF", 4) || !memcmp(p->buf, "RIFX", 4))
            /* Since the ACT demuxer has a standard WAV header at the top of
             * its own, the returned score is decreased to avoid a probe
             * conflict between ACT and WAV. */
            return AVPROBE_SCORE_MAX - 1;
        else if ((!memcmp(p->buf,      "RF64", 4) ||
                  !memcmp(p->buf,      "BW64", 4)) &&
                 !memcmp(p->buf + 12, "ds64", 4))
            return AVPROBE_SCORE_MAX;
    }
    return 0;
}

static void handle_stream_probing(AVStream *st)
{
    if (st->codecpar->codec_id == AV_CODEC_ID_PCM_S16LE) {
        FFStream *const sti = ffstream(st);
        sti->request_probe = AVPROBE_SCORE_EXTENSION;
        sti->probe_packets = FFMIN(sti->probe_packets, 32);
    }
}

static int wav_parse_fmt_tag(AVFormatContext *s, int64_t size, AVStream *st)
{
    AVIOContext *pb = s->pb;
    WAVDemuxContext *wav = s->priv_data;
    int ret;

    /* parse fmt header */
    ret = ff_get_wav_header(s, pb, st->codecpar, size, wav->rifx);
    if (ret < 0)
        return ret;
    handle_stream_probing(st);

    ffstream(st)->need_parsing = AVSTREAM_PARSE_FULL_RAW;

    avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);

    return 0;
}

static int wav_parse_xma2_tag(AVFormatContext *s, int64_t size, AVStream *st)
{
    AVIOContext *pb = s->pb;
    int version, num_streams, i, channels = 0, ret;

    if (size < 36)
        return AVERROR_INVALIDDATA;

    st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id   = AV_CODEC_ID_XMA2;
    ffstream(st)->need_parsing = AVSTREAM_PARSE_FULL_RAW;

    version = avio_r8(pb);
    if (version != 3 && version != 4)
        return AVERROR_INVALIDDATA;
    num_streams = avio_r8(pb);
    if (size != (32 + ((version==3)?0:8) + 4*num_streams))
        return AVERROR_INVALIDDATA;
    avio_skip(pb, 10);
    st->codecpar->sample_rate = avio_rb32(pb);
    if (version == 4)
        avio_skip(pb, 8);
    avio_skip(pb, 4);
    st->duration = avio_rb32(pb);
    avio_skip(pb, 8);

    for (i = 0; i < num_streams; i++) {
        channels += avio_r8(pb);
        avio_skip(pb, 3);
    }
    av_channel_layout_uninit(&st->codecpar->ch_layout);
    st->codecpar->ch_layout.order       = AV_CHANNEL_ORDER_UNSPEC;
    st->codecpar->ch_layout.nb_channels = channels;

    if (st->codecpar->ch_layout.nb_channels <= 0 || st->codecpar->sample_rate <= 0)
        return AVERROR_INVALIDDATA;

    avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);

    avio_seek(pb, -size, SEEK_CUR);
    if ((ret = ff_get_extradata(s, st->codecpar, pb, size)) < 0)
        return ret;

    return 0;
}

static inline int wav_parse_bext_string(AVFormatContext *s, const char *key,
                                        int length)
{
    char temp[257];
    int ret;

    av_assert0(length < sizeof(temp));
    if ((ret = ffio_read_size(s->pb, temp, length)) < 0)
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

        if ((ret = ffio_read_size(s->pb, coding_history, size)) < 0) {
            av_free(coding_history);
            return ret;
        }

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
    int rf64 = 0, bw64 = 0;
    uint32_t tag;
    AVIOContext *pb      = s->pb;
    AVStream *st         = NULL;
    WAVDemuxContext *wav = s->priv_data;
    int ret, got_fmt = 0, got_xma2 = 0;
    int64_t next_tag_ofs, data_ofs = -1;

    wav->unaligned = avio_tell(s->pb) & 1;

    wav->smv_data_ofs = -1;

    /* read chunk ID */
    tag = avio_rl32(pb);
    switch (tag) {
    case MKTAG('R', 'I', 'F', 'F'):
        break;
    case MKTAG('R', 'I', 'F', 'X'):
        wav->rifx = 1;
        break;
    case MKTAG('R', 'F', '6', '4'):
        rf64 = 1;
        break;
    case MKTAG('B', 'W', '6', '4'):
        bw64 = 1;
        break;
    default:
        av_log(s, AV_LOG_ERROR, "invalid start code %s in RIFF header\n",
               av_fourcc2str(tag));
        return AVERROR_INVALIDDATA;
    }

    /* read chunk size */
    avio_rl32(pb);

    /* read format */
    if (avio_rl32(pb) != MKTAG('W', 'A', 'V', 'E')) {
        av_log(s, AV_LOG_ERROR, "invalid format in RIFF header\n");
        return AVERROR_INVALIDDATA;
    }

    if (rf64 || bw64) {
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

    /* Create the audio stream now so that its index is always zero */
    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    for (;;) {
        AVStream *vst;
        size         = next_tag(pb, &tag, wav->rifx);
        next_tag_ofs = avio_tell(pb) + size;

        if (avio_feof(pb))
            break;

        switch (tag) {
        case MKTAG('f', 'm', 't', ' '):
            /* only parse the first 'fmt ' tag found */
            if (!got_xma2 && !got_fmt && (ret = wav_parse_fmt_tag(s, size, st)) < 0) {
                return ret;
            } else if (got_fmt)
                av_log(s, AV_LOG_WARNING, "found more than one 'fmt ' tag\n");

            got_fmt = 1;
            break;
        case MKTAG('X', 'M', 'A', '2'):
            /* only parse the first 'XMA2' tag found */
            if (!got_fmt && !got_xma2 && (ret = wav_parse_xma2_tag(s, size, st)) < 0) {
                return ret;
            } else if (got_xma2)
                av_log(s, AV_LOG_WARNING, "found more than one 'XMA2' tag\n");

            got_xma2 = 1;
            break;
        case MKTAG('d', 'a', 't', 'a'):
            if (!(pb->seekable & AVIO_SEEKABLE_NORMAL) && !got_fmt && !got_xma2) {
                av_log(s, AV_LOG_ERROR,
                       "found no 'fmt ' tag before the 'data' tag\n");
                return AVERROR_INVALIDDATA;
            }

            if (rf64 || bw64) {
                next_tag_ofs = wav->data_end = av_sat_add64(avio_tell(pb), data_size);
            } else if (size != 0xFFFFFFFF) {
                data_size    = size;
                next_tag_ofs = wav->data_end = size ? next_tag_ofs : INT64_MAX;
            } else {
                av_log(s, AV_LOG_WARNING, "Ignoring maximum wav data size, "
                       "file may be invalid\n");
                data_size    = 0;
                next_tag_ofs = wav->data_end = INT64_MAX;
            }

            data_ofs = avio_tell(pb);

            /* don't look for footer metadata if we can't seek or if we don't
             * know where the data tag ends
             */
            if (!(pb->seekable & AVIO_SEEKABLE_NORMAL) || (!(rf64 && !bw64) && !size))
                goto break_loop;
            break;
        case MKTAG('f', 'a', 'c', 't'):
            if (!sample_count)
                sample_count = (!wav->rifx ? avio_rl32(pb) : avio_rb32(pb));
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
            wav->vst = vst;
            avio_r8(pb);
            vst->id = 1;
            vst->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
            vst->codecpar->codec_id = AV_CODEC_ID_SMVJPEG;
            vst->codecpar->width  = avio_rl24(pb);
            vst->codecpar->height = avio_rl24(pb);
            if ((ret = ff_alloc_extradata(vst->codecpar, 4)) < 0) {
                av_log(s, AV_LOG_ERROR, "Could not allocate extradata.\n");
                return ret;
            }
            size = avio_rl24(pb);
            wav->smv_data_ofs = avio_tell(pb) + (size - 5) * 3;
            avio_rl24(pb);
            wav->smv_block_size = avio_rl24(pb);
            if (!wav->smv_block_size)
                return AVERROR_INVALIDDATA;
            avpriv_set_pts_info(vst, 32, 1, avio_rl24(pb));
            vst->duration = avio_rl24(pb);
            avio_rl24(pb);
            avio_rl24(pb);
            wav->smv_frames_per_jpeg = avio_rl24(pb);
            if (wav->smv_frames_per_jpeg > 65536) {
                av_log(s, AV_LOG_ERROR, "too many frames per jpeg\n");
                return AVERROR_INVALIDDATA;
            }
            AV_WL32(vst->codecpar->extradata, wav->smv_frames_per_jpeg);
            goto break_loop;
        case MKTAG('L', 'I', 'S', 'T'):
        case MKTAG('l', 'i', 's', 't'):
            if (size < 4) {
                av_log(s, AV_LOG_ERROR, "too short LIST tag\n");
                return AVERROR_INVALIDDATA;
            }
            switch (avio_rl32(pb)) {
            case MKTAG('I', 'N', 'F', 'O'):
                ff_read_riff_info(s, size - 4);
                break;
            case MKTAG('a', 'd', 't', 'l'):
                if (s->nb_chapters > 0) {
                    while (avio_tell(pb) < next_tag_ofs &&
                           !avio_feof(pb)) {
                        char cue_label[512];
                        unsigned id, sub_size;

                        if (avio_rl32(pb) != MKTAG('l', 'a', 'b', 'l'))
                            break;

                        sub_size = avio_rl32(pb);
                        if (sub_size < 5)
                            break;
                        id       = avio_rl32(pb);
                        avio_get_str(pb, sub_size - 4, cue_label, sizeof(cue_label));
                        avio_skip(pb, avio_tell(pb) & 1);

                        for (int i = 0; i < s->nb_chapters; i++) {
                            if (s->chapters[i]->id == id) {
                                av_dict_set(&s->chapters[i]->metadata, "title", cue_label, 0);
                                break;
                            }
                        }
                    }
                }
                break;
            }
            break;
        case MKTAG('I', 'D', '3', ' '):
        case MKTAG('i', 'd', '3', ' '): {
            ID3v2ExtraMeta *id3v2_extra_meta;
            ff_id3v2_read(s, ID3v2_DEFAULT_MAGIC, &id3v2_extra_meta, 0);
            if (id3v2_extra_meta) {
                ff_id3v2_parse_apic(s, id3v2_extra_meta);
                ff_id3v2_parse_chapters(s, id3v2_extra_meta);
                ff_id3v2_parse_priv(s, id3v2_extra_meta);
            }
            ff_id3v2_free_extra_meta(&id3v2_extra_meta);
            }
            break;
        case MKTAG('c', 'u', 'e', ' '):
            if (size >= 4 && got_fmt && st->codecpar->sample_rate > 0) {
                AVRational tb = {1, st->codecpar->sample_rate};
                unsigned nb_cues = avio_rl32(pb);

                if (size >= nb_cues * 24LL + 4LL) {
                    for (int i = 0; i < nb_cues; i++) {
                        unsigned offset, id = avio_rl32(pb);

                        if (avio_feof(pb))
                            return AVERROR_INVALIDDATA;

                        avio_skip(pb, 16);
                        offset = avio_rl32(pb);

                        if (!avpriv_new_chapter(s, id, tb, offset, AV_NOPTS_VALUE, NULL))
                            return AVERROR(ENOMEM);
                    }
                }
            }
            break;
        }

        /* seek to next tag unless we know that we'll run into EOF */
        if ((avio_size(pb) > 0 && next_tag_ofs >= avio_size(pb)) ||
            wav_seek_tag(wav, pb, next_tag_ofs, SEEK_SET) < 0) {
            break;
        }
    }

break_loop:
    if (!got_fmt && !got_xma2) {
        av_log(s, AV_LOG_ERROR, "no 'fmt ' or 'XMA2' tag found\n");
        return AVERROR_INVALIDDATA;
    }

    if (data_ofs < 0) {
        av_log(s, AV_LOG_ERROR, "no 'data' tag found\n");
        return AVERROR_INVALIDDATA;
    }

    avio_seek(pb, data_ofs, SEEK_SET);

    if (data_size > (INT64_MAX>>3)) {
        av_log(s, AV_LOG_WARNING, "Data size %"PRId64" is too large\n", data_size);
        data_size = 0;
    }

    if (   st->codecpar->bit_rate > 0 && data_size > 0
        && st->codecpar->sample_rate > 0
        && sample_count > 0 && st->codecpar->ch_layout.nb_channels > 1
        && sample_count % st->codecpar->ch_layout.nb_channels == 0) {
        if (fabs(8.0 * data_size * st->codecpar->ch_layout.nb_channels * st->codecpar->sample_rate /
            sample_count /st->codecpar->bit_rate - 1.0) < 0.3)
            sample_count /= st->codecpar->ch_layout.nb_channels;
    }

    if (data_size > 0 && sample_count && st->codecpar->ch_layout.nb_channels &&
        (data_size << 3) / sample_count / st->codecpar->ch_layout.nb_channels > st->codecpar->bits_per_coded_sample  + 1) {
        av_log(s, AV_LOG_WARNING, "ignoring wrong sample_count %"PRId64"\n", sample_count);
        sample_count = 0;
    }

    /* G.729 hack (for Ticket4577)
     * FIXME: Come up with cleaner, more general solution */
    if (st->codecpar->codec_id == AV_CODEC_ID_G729 && sample_count && (data_size << 3) > sample_count) {
        av_log(s, AV_LOG_WARNING, "ignoring wrong sample_count %"PRId64"\n", sample_count);
        sample_count = 0;
    }

    if (!sample_count || av_get_exact_bits_per_sample(st->codecpar->codec_id) > 0)
        if (   st->codecpar->ch_layout.nb_channels
            && data_size
            && av_get_bits_per_sample(st->codecpar->codec_id)
            && wav->data_end <= avio_size(pb))
            sample_count = (data_size << 3)
                                  /
                (st->codecpar->ch_layout.nb_channels * (uint64_t)av_get_bits_per_sample(st->codecpar->codec_id));

    if (sample_count)
        st->duration = sample_count;

    if (st->codecpar->codec_id == AV_CODEC_ID_PCM_S32LE &&
        st->codecpar->block_align == st->codecpar->ch_layout.nb_channels * 4 &&
        st->codecpar->bits_per_coded_sample == 32 &&
        st->codecpar->extradata_size == 2 &&
        AV_RL16(st->codecpar->extradata) == 1) {
        st->codecpar->codec_id = AV_CODEC_ID_PCM_F16LE;
        st->codecpar->bits_per_coded_sample = 16;
    } else if (st->codecpar->codec_id == AV_CODEC_ID_PCM_S24LE &&
               st->codecpar->block_align == st->codecpar->ch_layout.nb_channels * 4 &&
               st->codecpar->bits_per_coded_sample == 24) {
        st->codecpar->codec_id = AV_CODEC_ID_PCM_F24LE;
    } else if (st->codecpar->codec_id == AV_CODEC_ID_XMA1 ||
               st->codecpar->codec_id == AV_CODEC_ID_XMA2) {
        st->codecpar->block_align = 2048;
    } else if (st->codecpar->codec_id == AV_CODEC_ID_ADPCM_MS && st->codecpar->ch_layout.nb_channels > 2 &&
               st->codecpar->block_align < INT_MAX / st->codecpar->ch_layout.nb_channels) {
        st->codecpar->block_align *= st->codecpar->ch_layout.nb_channels;
    }

    ff_metadata_conv_ctx(s, NULL, wav_metadata_conv);
    ff_metadata_conv_ctx(s, NULL, ff_riff_info_conv);

    set_spdif(s, wav);
    set_max_size(st, wav);

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

    while (!avio_feof(pb)) {
        avio_read(pb, guid, 16);
        size = avio_rl64(pb);
        if (size <= 24 || size > INT64_MAX - 8)
            return AVERROR_INVALIDDATA;
        if (!memcmp(guid, guid1, 16))
            return size;
        avio_skip(pb, FFALIGN(size, INT64_C(8)) - 24);
    }
    return AVERROR_EOF;
}

static int wav_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret, size;
    int64_t left;
    WAVDemuxContext *wav = s->priv_data;
    AVStream *st = s->streams[0];

    if (CONFIG_SPDIF_DEMUXER && wav->spdif == 1)
        return ff_spdif_read_packet(s, pkt);

    if (wav->smv_data_ofs > 0) {
        int64_t audio_dts, video_dts;
        AVStream *vst = wav->vst;
smv_retry:
        audio_dts = (int32_t)ffstream( st)->cur_dts;
        video_dts = (int32_t)ffstream(vst)->cur_dts;

        if (audio_dts != AV_NOPTS_VALUE && video_dts != AV_NOPTS_VALUE) {
            /*We always return a video frame first to get the pixel format first*/
            wav->smv_last_stream = wav->smv_given_first ?
                av_compare_ts(video_dts, vst->time_base,
                              audio_dts,  st->time_base) > 0 : 0;
            wav->smv_given_first = 1;
        }
        wav->smv_last_stream = !wav->smv_last_stream;
        wav->smv_last_stream |= wav->audio_eof;
        wav->smv_last_stream &= !wav->smv_eof;
        if (wav->smv_last_stream) {
            uint64_t old_pos = avio_tell(s->pb);
            uint64_t new_pos = wav->smv_data_ofs +
                wav->smv_block * (int64_t)wav->smv_block_size;
            if (avio_seek(s->pb, new_pos, SEEK_SET) < 0) {
                ret = AVERROR_EOF;
                goto smv_out;
            }
            size = avio_rl24(s->pb);
            if (size > wav->smv_block_size) {
                ret = AVERROR_EOF;
                goto smv_out;
            }
            ret  = av_get_packet(s->pb, pkt, size);
            if (ret < 0)
                goto smv_out;
            pkt->pos -= 3;
            pkt->pts = wav->smv_block * wav->smv_frames_per_jpeg;
            pkt->duration = wav->smv_frames_per_jpeg;
            wav->smv_block++;

            pkt->stream_index = vst->index;
smv_out:
            avio_seek(s->pb, old_pos, SEEK_SET);
            if (ret == AVERROR_EOF) {
                wav->smv_eof = 1;
                goto smv_retry;
            }
            return ret;
        }
    }

    left = wav->data_end - avio_tell(s->pb);
    if (wav->ignore_length)
        left = INT_MAX;
    if (left <= 0) {
        if (CONFIG_W64_DEMUXER && wav->w64)
            left = find_guid(s->pb, ff_w64_guid_data) - 24;
        else
            left = find_tag(wav, s->pb, MKTAG('d', 'a', 't', 'a'));
        if (left < 0) {
            wav->audio_eof = 1;
            if (wav->smv_data_ofs > 0 && !wav->smv_eof)
                goto smv_retry;
            return AVERROR_EOF;
        }
        if (INT64_MAX - left < avio_tell(s->pb))
            return AVERROR_INVALIDDATA;
        wav->data_end = avio_tell(s->pb) + left;
    }

    size = wav->max_size;
    if (st->codecpar->block_align > 1) {
        if (size < st->codecpar->block_align)
            size = st->codecpar->block_align;
        size = (size / st->codecpar->block_align) * st->codecpar->block_align;
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
    AVStream *ast = s->streams[0], *vst = wav->vst;
    wav->smv_eof = 0;
    wav->audio_eof = 0;

    if (stream_index != 0 && (!vst || stream_index != vst->index))
        return AVERROR(EINVAL);
    if (wav->smv_data_ofs > 0) {
        int64_t smv_timestamp = timestamp;
        if (stream_index == 0)
            smv_timestamp = av_rescale_q(timestamp, ast->time_base, vst->time_base);
        else
            timestamp = av_rescale_q(smv_timestamp, vst->time_base, ast->time_base);
        if (wav->smv_frames_per_jpeg > 0) {
            wav->smv_block = smv_timestamp / wav->smv_frames_per_jpeg;
        }
    }

    switch (ast->codecpar->codec_id) {
    case AV_CODEC_ID_MP2:
    case AV_CODEC_ID_MP3:
    case AV_CODEC_ID_AC3:
    case AV_CODEC_ID_DTS:
    case AV_CODEC_ID_XMA2:
        /* use generic seeking with dynamically generated indexes */
        return -1;
    default:
        break;
    }
    return ff_pcm_read_seek(s, 0, timestamp, flags);
}

static const AVClass wav_demuxer_class = {
    .class_name = "WAV demuxer",
    .item_name  = av_default_item_name,
    .option     = demux_options,
    .version    = LIBAVUTIL_VERSION_INT,
};
const FFInputFormat ff_wav_demuxer = {
    .p.name         = "wav",
    .p.long_name    = NULL_IF_CONFIG_SMALL("WAV / WAVE (Waveform Audio)"),
    .p.flags        = AVFMT_GENERIC_INDEX,
    .p.codec_tag    = ff_wav_codec_tags_list,
    .p.priv_class   = &wav_demuxer_class,
    .priv_data_size = sizeof(WAVDemuxContext),
    .read_probe     = wav_probe,
    .read_header    = wav_read_header,
    .read_packet    = wav_read_packet,
    .read_seek      = wav_read_seek,
};
#endif /* CONFIG_WAV_DEMUXER */

#if CONFIG_W64_DEMUXER
static int w64_probe(const AVProbeData *p)
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

    if (avio_read(pb, guid, 16) != 16 || memcmp(guid, ff_w64_guid_riff, 16))
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

    while (!avio_feof(pb)) {
        if (avio_read(pb, guid, 16) != 16)
            break;
        size = avio_rl64(pb);
        if (size <= 24 || INT64_MAX - size < avio_tell(pb)) {
            if (data_ofs)
                break;
            return AVERROR_INVALIDDATA;
        }

        if (!memcmp(guid, ff_w64_guid_fmt, 16)) {
            /* subtract chunk header size - normal wav file doesn't count it */
            ret = ff_get_wav_header(s, pb, st->codecpar, size - 24, 0);
            if (ret < 0)
                return ret;
            avio_skip(pb, FFALIGN(size, INT64_C(8)) - size);
            if (st->codecpar->block_align &&
                st->codecpar->ch_layout.nb_channels < FF_SANE_NB_CHANNELS &&
                st->codecpar->bits_per_coded_sample < 128) {
                int block_align = st->codecpar->block_align;

                block_align = FFMAX(block_align,
                                    ((st->codecpar->bits_per_coded_sample + 7) / 8) *
                                    st->codecpar->ch_layout.nb_channels);
                if (block_align > st->codecpar->block_align) {
                    av_log(s, AV_LOG_WARNING, "invalid block_align: %d, broken file.\n",
                           st->codecpar->block_align);
                    st->codecpar->block_align = block_align;
                }
            }
            avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);
        } else if (!memcmp(guid, ff_w64_guid_fact, 16)) {
            int64_t samples;

            samples = avio_rl64(pb);
            if (samples > 0)
                st->duration = samples;
            avio_skip(pb, FFALIGN(size, INT64_C(8)) - 32);
        } else if (!memcmp(guid, ff_w64_guid_data, 16)) {
            wav->data_end = avio_tell(pb) + size - 24;

            data_ofs = avio_tell(pb);
            if (!(pb->seekable & AVIO_SEEKABLE_NORMAL))
                break;

            avio_skip(pb, size - 24);
        } else if (!memcmp(guid, ff_w64_guid_summarylist, 16)) {
            int64_t start, end, cur;
            uint32_t count, chunk_size, i;
            int64_t filesize  = avio_size(s->pb);

            start = avio_tell(pb);
            end = start + FFALIGN(size, INT64_C(8)) - 24;
            count = avio_rl32(pb);

            for (i = 0; i < count; i++) {
                char chunk_key[5], *value;

                if (avio_feof(pb) || (cur = avio_tell(pb)) < 0 || cur > end - 8 /* = tag + size */)
                    break;

                chunk_key[4] = 0;
                avio_read(pb, chunk_key, 4);
                chunk_size = avio_rl32(pb);
                if (chunk_size == UINT32_MAX || (filesize >= 0 && chunk_size > filesize))
                    return AVERROR_INVALIDDATA;

                value = av_malloc(chunk_size + 1);
                if (!value)
                    return AVERROR(ENOMEM);

                ret = avio_get_str16le(pb, chunk_size, value, chunk_size);
                if (ret < 0) {
                    av_free(value);
                    return ret;
                }
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
    ffstream(st)->need_parsing = AVSTREAM_PARSE_FULL_RAW;

    avio_seek(pb, data_ofs, SEEK_SET);

    set_spdif(s, wav);
    set_max_size(st, wav);

    return 0;
}

static const AVClass w64_demuxer_class = {
    .class_name = "W64 demuxer",
    .item_name  = av_default_item_name,
    .option     = &demux_options[W64_DEMUXER_OPTIONS_OFFSET],
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFInputFormat ff_w64_demuxer = {
    .p.name         = "w64",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Sony Wave64"),
    .p.flags        = AVFMT_GENERIC_INDEX,
    .p.codec_tag    = ff_wav_codec_tags_list,
    .p.priv_class   = &w64_demuxer_class,
    .priv_data_size = sizeof(WAVDemuxContext),
    .read_probe     = w64_probe,
    .read_header    = w64_read_header,
    .read_packet    = wav_read_packet,
    .read_seek      = wav_read_seek,
};
#endif /* CONFIG_W64_DEMUXER */
