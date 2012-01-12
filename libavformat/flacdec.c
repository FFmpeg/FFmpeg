/*
 * Raw FLAC demuxer
 * Copyright (c) 2001 Fabrice Bellard
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

#include "libavcodec/flac.h"
#include "avformat.h"
#include "internal.h"
#include "rawdec.h"
#include "oggdec.h"
#include "vorbiscomment.h"
#include "libavcodec/bytestream.h"

static int flac_read_header(AVFormatContext *s)
{
    int ret, metadata_last=0, metadata_type, metadata_size, found_streaminfo=0;
    uint8_t header[4];
    uint8_t *buffer=NULL;
    AVStream *st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codec->codec_id = CODEC_ID_FLAC;
    st->need_parsing = AVSTREAM_PARSE_FULL;
    /* the parameters will be extracted from the compressed bitstream */

    /* if fLaC marker is not found, assume there is no header */
    if (avio_rl32(s->pb) != MKTAG('f','L','a','C')) {
        avio_seek(s->pb, -4, SEEK_CUR);
        return 0;
    }

    /* process metadata blocks */
    while (!s->pb->eof_reached && !metadata_last) {
        avio_read(s->pb, header, 4);
        avpriv_flac_parse_block_header(header, &metadata_last, &metadata_type,
                                   &metadata_size);
        switch (metadata_type) {
        /* allocate and read metadata block for supported types */
        case FLAC_METADATA_TYPE_STREAMINFO:
        case FLAC_METADATA_TYPE_CUESHEET:
        case FLAC_METADATA_TYPE_VORBIS_COMMENT:
            buffer = av_mallocz(metadata_size + FF_INPUT_BUFFER_PADDING_SIZE);
            if (!buffer) {
                return AVERROR(ENOMEM);
            }
            if (avio_read(s->pb, buffer, metadata_size) != metadata_size) {
                av_freep(&buffer);
                return AVERROR(EIO);
            }
            break;
        /* skip metadata block for unsupported types */
        default:
            ret = avio_skip(s->pb, metadata_size);
            if (ret < 0)
                return ret;
        }

        if (metadata_type == FLAC_METADATA_TYPE_STREAMINFO) {
            FLACStreaminfo si;
            /* STREAMINFO can only occur once */
            if (found_streaminfo) {
                av_freep(&buffer);
                return AVERROR_INVALIDDATA;
            }
            if (metadata_size != FLAC_STREAMINFO_SIZE) {
                av_freep(&buffer);
                return AVERROR_INVALIDDATA;
            }
            found_streaminfo = 1;
            st->codec->extradata      = buffer;
            st->codec->extradata_size = metadata_size;
            buffer = NULL;

            /* get codec params from STREAMINFO header */
            avpriv_flac_parse_streaminfo(st->codec, &si, st->codec->extradata);

            /* set time base and duration */
            if (si.samplerate > 0) {
                avpriv_set_pts_info(st, 64, 1, si.samplerate);
                if (si.samples > 0)
                    st->duration = si.samples;
            }
        } else if (metadata_type == FLAC_METADATA_TYPE_CUESHEET) {
            uint8_t isrc[13];
            uint64_t start;
            const uint8_t *offset;
            int i, chapters, track, ti;
            if (metadata_size < 431)
                return AVERROR_INVALIDDATA;
            offset = buffer + 395;
            chapters = bytestream_get_byte(&offset) - 1;
            if (chapters <= 0)
                return AVERROR_INVALIDDATA;
            for (i = 0; i < chapters; i++) {
                if (offset + 36 - buffer > metadata_size)
                    return AVERROR_INVALIDDATA;
                start = bytestream_get_be64(&offset);
                track = bytestream_get_byte(&offset);
                bytestream_get_buffer(&offset, isrc, 12);
                isrc[12] = 0;
                offset += 14;
                ti = bytestream_get_byte(&offset);
                if (ti <= 0) return AVERROR_INVALIDDATA;
                offset += ti * 12;
                avpriv_new_chapter(s, track, st->time_base, start, AV_NOPTS_VALUE, isrc);
            }
        } else {
            /* STREAMINFO must be the first block */
            if (!found_streaminfo) {
                av_freep(&buffer);
                return AVERROR_INVALIDDATA;
            }
            /* process supported blocks other than STREAMINFO */
            if (metadata_type == FLAC_METADATA_TYPE_VORBIS_COMMENT) {
                if (ff_vorbis_comment(s, &s->metadata, buffer, metadata_size)) {
                    av_log(s, AV_LOG_WARNING, "error parsing VorbisComment metadata\n");
                }
            }
            av_freep(&buffer);
        }
    }

    return 0;
}

static int flac_probe(AVProbeData *p)
{
    uint8_t *bufptr = p->buf;
    uint8_t *end    = p->buf + p->buf_size;

    if(bufptr > end-4 || memcmp(bufptr, "fLaC", 4)) return 0;
    else                                            return AVPROBE_SCORE_MAX/2;
}

AVInputFormat ff_flac_demuxer = {
    .name           = "flac",
    .long_name      = NULL_IF_CONFIG_SMALL("raw FLAC"),
    .read_probe     = flac_probe,
    .read_header    = flac_read_header,
    .read_packet    = ff_raw_read_partial_packet,
    .flags= AVFMT_GENERIC_INDEX,
    .extensions = "flac",
    .value = CODEC_ID_FLAC,
};
