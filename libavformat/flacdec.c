/*
 * Raw FLAC demuxer
 * Copyright (c) 2001 Fabrice Bellard
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

#include "libavutil/channel_layout.h"
#include "libavcodec/flac.h"
#include "avformat.h"
#include "flac_picture.h"
#include "internal.h"
#include "rawdec.h"
#include "oggdec.h"
#include "vorbiscomment.h"
#include "replaygain.h"

#define SEEKPOINT_SIZE 18

typedef struct FLACDecContext {
    FFRawDemuxerContext rawctx;
    int found_seektable;
} FLACDecContext;

static void reset_index_position(int64_t metadata_head_size, AVStream *st)
{
    FFStream *const sti = ffstream(st);
    /* the real seek index offset should be the size of metadata blocks with the offset in the frame blocks */
    for (int i = 0; i < sti->nb_index_entries; i++)
        sti->index_entries[i].pos += metadata_head_size;
}

static int flac_read_header(AVFormatContext *s)
{
    int ret, metadata_last=0, metadata_type, metadata_size, found_streaminfo=0;
    uint8_t header[4];
    uint8_t *buffer=NULL;
    FLACDecContext *flac = s->priv_data;
    AVStream *st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id = AV_CODEC_ID_FLAC;
    ffstream(st)->need_parsing = AVSTREAM_PARSE_FULL_RAW;
    /* the parameters will be extracted from the compressed bitstream */

    /* if fLaC marker is not found, assume there is no header */
    if (avio_rl32(s->pb) != MKTAG('f','L','a','C')) {
        avio_seek(s->pb, -4, SEEK_CUR);
        return 0;
    }

    /* process metadata blocks */
    while (!avio_feof(s->pb) && !metadata_last) {
        if (avio_read(s->pb, header, 4) != 4)
            return AVERROR(AVERROR_INVALIDDATA);
        flac_parse_block_header(header, &metadata_last, &metadata_type,
                                   &metadata_size);
        switch (metadata_type) {
        /* allocate and read metadata block for supported types */
        case FLAC_METADATA_TYPE_STREAMINFO:
        case FLAC_METADATA_TYPE_CUESHEET:
        case FLAC_METADATA_TYPE_PICTURE:
        case FLAC_METADATA_TYPE_VORBIS_COMMENT:
        case FLAC_METADATA_TYPE_SEEKTABLE:
            buffer = av_mallocz(metadata_size + AV_INPUT_BUFFER_PADDING_SIZE);
            if (!buffer) {
                return AVERROR(ENOMEM);
            }
            if (avio_read(s->pb, buffer, metadata_size) != metadata_size) {
                RETURN_ERROR(AVERROR(EIO));
            }
            break;
        /* skip metadata block for unsupported types */
        default:
            ret = avio_skip(s->pb, metadata_size);
            if (ret < 0)
                return ret;
        }

        if (metadata_type == FLAC_METADATA_TYPE_STREAMINFO) {
            uint32_t samplerate;
            uint64_t samples;

            /* STREAMINFO can only occur once */
            if (found_streaminfo) {
                RETURN_ERROR(AVERROR_INVALIDDATA);
            }
            if (metadata_size != FLAC_STREAMINFO_SIZE) {
                RETURN_ERROR(AVERROR_INVALIDDATA);
            }
            found_streaminfo = 1;
            st->codecpar->extradata      = buffer;
            st->codecpar->extradata_size = metadata_size;
            buffer = NULL;

            /* get sample rate and sample count from STREAMINFO header;
             * other parameters will be extracted by the parser */
            samplerate = AV_RB24(st->codecpar->extradata + 10) >> 4;
            samples    = (AV_RB64(st->codecpar->extradata + 13) >> 24) & ((1ULL << 36) - 1);

            /* set time base and duration */
            if (samplerate > 0) {
                avpriv_set_pts_info(st, 64, 1, samplerate);
                if (samples > 0)
                    st->duration = samples;
            }
        } else if (metadata_type == FLAC_METADATA_TYPE_CUESHEET) {
            uint8_t isrc[13];
            uint64_t start;
            const uint8_t *offset;
            int i, chapters, track, ti;
            if (metadata_size < 431)
                RETURN_ERROR(AVERROR_INVALIDDATA);
            offset = buffer + 395;
            chapters = bytestream_get_byte(&offset) - 1;
            if (chapters <= 0)
                RETURN_ERROR(AVERROR_INVALIDDATA);
            for (i = 0; i < chapters; i++) {
                if (offset + 36 - buffer > metadata_size)
                    RETURN_ERROR(AVERROR_INVALIDDATA);
                start = bytestream_get_be64(&offset);
                track = bytestream_get_byte(&offset);
                bytestream_get_buffer(&offset, isrc, 12);
                isrc[12] = 0;
                offset += 14;
                ti = bytestream_get_byte(&offset);
                if (ti <= 0) RETURN_ERROR(AVERROR_INVALIDDATA);
                offset += ti * 12;
                avpriv_new_chapter(s, track, st->time_base, start, AV_NOPTS_VALUE, isrc);
            }
            av_freep(&buffer);
        } else if (metadata_type == FLAC_METADATA_TYPE_PICTURE) {
            ret = ff_flac_parse_picture(s, &buffer, metadata_size, 1);
            av_freep(&buffer);
            if (ret < 0) {
                av_log(s, AV_LOG_ERROR, "Error parsing attached picture.\n");
                return ret;
            }
        } else if (metadata_type == FLAC_METADATA_TYPE_SEEKTABLE) {
            const uint8_t *seekpoint = buffer;
            int i, seek_point_count = metadata_size/SEEKPOINT_SIZE;
            flac->found_seektable = 1;
            if ((s->flags&AVFMT_FLAG_FAST_SEEK)) {
                for(i=0; i<seek_point_count; i++) {
                    int64_t timestamp = bytestream_get_be64(&seekpoint);
                    int64_t pos = bytestream_get_be64(&seekpoint);
                    /* skip number of samples */
                    bytestream_get_be16(&seekpoint);
                    av_add_index_entry(st, pos, timestamp, 0, 0, AVINDEX_KEYFRAME);
                }
            }
            av_freep(&buffer);
        }
        else {

            /* STREAMINFO must be the first block */
            if (!found_streaminfo) {
                RETURN_ERROR(AVERROR_INVALIDDATA);
            }
            /* process supported blocks other than STREAMINFO */
            if (metadata_type == FLAC_METADATA_TYPE_VORBIS_COMMENT) {
                AVDictionaryEntry *chmask;

                ret = ff_vorbis_comment(s, &s->metadata, buffer, metadata_size, 1);
                if (ret < 0) {
                    av_log(s, AV_LOG_WARNING, "error parsing VorbisComment metadata\n");
                } else if (ret > 0) {
                    s->event_flags |= AVFMT_EVENT_FLAG_METADATA_UPDATED;
                }

                /* parse the channels mask if present */
                chmask = av_dict_get(s->metadata, "WAVEFORMATEXTENSIBLE_CHANNEL_MASK", NULL, 0);
                if (chmask) {
                    uint64_t mask = strtol(chmask->value, NULL, 0);
                    if (!mask || mask & ~0x3ffffULL) {
                        av_log(s, AV_LOG_WARNING,
                               "Invalid value of WAVEFORMATEXTENSIBLE_CHANNEL_MASK\n");
                    } else {
                        av_channel_layout_from_mask(&st->codecpar->ch_layout, mask);
                        av_dict_set(&s->metadata, "WAVEFORMATEXTENSIBLE_CHANNEL_MASK", NULL, 0);
                    }
                }
            }
            av_freep(&buffer);
        }
    }

    ret = ff_replaygain_export(st, s->metadata);
    if (ret < 0)
        return ret;

    reset_index_position(avio_tell(s->pb), st);
    return 0;

fail:
    av_free(buffer);
    return ret;
}

static int raw_flac_probe(const AVProbeData *p)
{
    if ((p->buf[2] & 0xF0) == 0)    // blocksize code invalid
        return 0;
    if ((p->buf[2] & 0x0F) == 0x0F) // sample rate code invalid
        return 0;
    if ((p->buf[3] & 0xF0) >= FLAC_MAX_CHANNELS + FLAC_CHMODE_MID_SIDE << 4)
        // channel mode invalid
        return 0;
    if ((p->buf[3] & 0x06) == 0x06) // bits per sample code invalid
        return 0;
    if ((p->buf[3] & 0x01) == 0x01) // reserved bit set
        return 0;
    return AVPROBE_SCORE_EXTENSION / 4 + 1;
}

static int flac_probe(const AVProbeData *p)
{
    if ((AV_RB16(p->buf) & 0xFFFE) == 0xFFF8)
        return raw_flac_probe(p);

    /* file header + metadata header + checked bytes of streaminfo */
    if (p->buf_size >= 4 + 4 + 13) {
        int type           = p->buf[4] & 0x7f;
        int size           = AV_RB24(p->buf + 5);
        int min_block_size = AV_RB16(p->buf + 8);
        int max_block_size = AV_RB16(p->buf + 10);
        int sample_rate    = AV_RB24(p->buf + 18) >> 4;

        if (memcmp(p->buf, "fLaC", 4))
            return 0;
        if (type == FLAC_METADATA_TYPE_STREAMINFO &&
            size == FLAC_STREAMINFO_SIZE          &&
            min_block_size >= 16                  &&
            max_block_size >= min_block_size      &&
            sample_rate && sample_rate <= 655350)
            return AVPROBE_SCORE_MAX;
        return AVPROBE_SCORE_EXTENSION;
    }

    return 0;
}

static av_unused int64_t flac_read_timestamp(AVFormatContext *s, int stream_index,
                                             int64_t *ppos, int64_t pos_limit)
{
    FFFormatContext *const si = ffformatcontext(s);
    AVPacket *const pkt = si->parse_pkt;
    AVStream *st = s->streams[stream_index];
    AVCodecParserContext *parser;
    int ret;
    int64_t pts = AV_NOPTS_VALUE;

    if (avio_seek(s->pb, *ppos, SEEK_SET) < 0)
        return AV_NOPTS_VALUE;

    parser = av_parser_init(st->codecpar->codec_id);
    if (!parser){
        return AV_NOPTS_VALUE;
    }
    parser->flags |= PARSER_FLAG_USE_CODEC_TS;

    for (;;){
        uint8_t *data;
        int size;

        ret = ff_raw_read_partial_packet(s, pkt);
        if (ret < 0){
            if (ret == AVERROR(EAGAIN))
                continue;
            else {
                av_packet_unref(pkt);
                av_assert1(!pkt->size);
            }
        }
        av_parser_parse2(parser, ffstream(st)->avctx,
                         &data, &size, pkt->data, pkt->size,
                         pkt->pts, pkt->dts, *ppos);

        av_packet_unref(pkt);
        if (size) {
            if (parser->pts != AV_NOPTS_VALUE){
                // seeking may not have started from beginning of a frame
                // calculate frame start position from next frame backwards
                *ppos = parser->next_frame_offset - size;
                pts = parser->pts;
                break;
            }
        } else if (ret < 0)
            break;
    }
    av_parser_close(parser);
    return pts;
}

static int flac_seek(AVFormatContext *s, int stream_index, int64_t timestamp, int flags) {
    AVStream *const st  = s->streams[0];
    FFStream *const sti = ffstream(st);
    int index;
    int64_t pos;
    AVIndexEntry e;
    FLACDecContext *flac = s->priv_data;

    if (!flac->found_seektable || !(s->flags&AVFMT_FLAG_FAST_SEEK)) {
        return -1;
    }

    index = av_index_search_timestamp(st, timestamp, flags);
    if (index < 0 || index >= sti->nb_index_entries)
        return -1;

    e   = sti->index_entries[index];
    pos = avio_seek(s->pb, e.pos, SEEK_SET);
    if (pos >= 0) {
        return 0;
    }
    return -1;
}

const AVInputFormat ff_flac_demuxer = {
    .name           = "flac",
    .long_name      = NULL_IF_CONFIG_SMALL("raw FLAC"),
    .read_probe     = flac_probe,
    .read_header    = flac_read_header,
    .read_packet    = ff_raw_read_partial_packet,
    .read_seek      = flac_seek,
    .read_timestamp = flac_read_timestamp,
    .flags          = AVFMT_GENERIC_INDEX,
    .extensions     = "flac",
    .raw_codec_id   = AV_CODEC_ID_FLAC,
    .priv_data_size = sizeof(FLACDecContext),
    .priv_class     = &ff_raw_demuxer_class,
};
