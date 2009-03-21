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

#include "libavcodec/flac.h"
#include "avformat.h"
#include "raw.h"
#include "id3v2.h"
#include "oggdec.h"

static int flac_read_header(AVFormatContext *s,
                             AVFormatParameters *ap)
{
    uint8_t buf[ID3v2_HEADER_SIZE];
    int ret, metadata_last=0, metadata_type, metadata_size, found_streaminfo=0;
    uint8_t header[4];
    uint8_t *buffer=NULL;
    AVStream *st = av_new_stream(s, 0);
    if (!st)
        return AVERROR(ENOMEM);
    st->codec->codec_type = CODEC_TYPE_AUDIO;
    st->codec->codec_id = CODEC_ID_FLAC;
    st->need_parsing = AVSTREAM_PARSE_FULL;
    /* the parameters will be extracted from the compressed bitstream */

    /* skip ID3v2 header if found */
    ret = get_buffer(s->pb, buf, ID3v2_HEADER_SIZE);
    if (ret == ID3v2_HEADER_SIZE && ff_id3v2_match(buf)) {
        int len = ff_id3v2_tag_len(buf);
        url_fseek(s->pb, len - ID3v2_HEADER_SIZE, SEEK_CUR);
    } else {
        url_fseek(s->pb, 0, SEEK_SET);
    }

    /* if fLaC marker is not found, assume there is no header */
    if (get_le32(s->pb) != MKTAG('f','L','a','C')) {
        url_fseek(s->pb, -4, SEEK_CUR);
        return 0;
    }

    /* process metadata blocks */
    while (!url_feof(s->pb) && !metadata_last) {
        get_buffer(s->pb, header, 4);
        ff_flac_parse_block_header(header, &metadata_last, &metadata_type,
                                   &metadata_size);
        switch (metadata_type) {
        /* allocate and read metadata block for supported types */
        case FLAC_METADATA_TYPE_STREAMINFO:
        case FLAC_METADATA_TYPE_VORBIS_COMMENT:
            buffer = av_mallocz(metadata_size + FF_INPUT_BUFFER_PADDING_SIZE);
            if (!buffer) {
                return AVERROR_NOMEM;
            }
            if (get_buffer(s->pb, buffer, metadata_size) != metadata_size) {
                av_freep(&buffer);
                return AVERROR_IO;
            }
            break;
        /* skip metadata block for unsupported types */
        default:
            ret = url_fseek(s->pb, metadata_size, SEEK_CUR);
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
            ff_flac_parse_streaminfo(st->codec, &si, st->codec->extradata);

            /* set time base and duration */
            if (si.samplerate > 0) {
                av_set_pts_info(st, 64, 1, si.samplerate);
                if (si.samples > 0)
                    st->duration = si.samples;
            }
        } else {
            /* STREAMINFO must be the first block */
            if (!found_streaminfo) {
                av_freep(&buffer);
                return AVERROR_INVALIDDATA;
            }
            /* process supported blocks other than STREAMINFO */
            if (metadata_type == FLAC_METADATA_TYPE_VORBIS_COMMENT) {
                if (vorbis_comment(s, buffer, metadata_size)) {
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

    if(ff_id3v2_match(bufptr))
        bufptr += ff_id3v2_tag_len(bufptr);

    if(bufptr > end-4 || memcmp(bufptr, "fLaC", 4)) return 0;
    else                                            return AVPROBE_SCORE_MAX/2;
}

AVInputFormat flac_demuxer = {
    "flac",
    NULL_IF_CONFIG_SMALL("raw FLAC"),
    0,
    flac_probe,
    flac_read_header,
    ff_raw_read_partial_packet,
    .flags= AVFMT_GENERIC_INDEX,
    .extensions = "flac",
    .value = CODEC_ID_FLAC,
};
