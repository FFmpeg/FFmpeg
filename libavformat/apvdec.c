/*
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

#include "libavcodec/apv.h"
#include "libavcodec/bytestream.h"

#include "avformat.h"
#include "avio_internal.h"
#include "demux.h"
#include "internal.h"


typedef struct APVHeaderInfo {
    uint8_t  pbu_type;
    uint16_t group_id;

    uint8_t  profile_idc;
    uint8_t  level_idc;
    uint8_t  band_idc;

    int      frame_width;
    int      frame_height;

    uint8_t  bit_depth_minus8;
} APVHeaderInfo;

static int apv_extract_header_info(GetByteContext *gbc)
{
    APVHeaderInfo header, *info = &header;
    int zero, byte;

    info->pbu_type = bytestream2_get_byte(gbc);
    info->group_id = bytestream2_get_be16(gbc);

    zero = bytestream2_get_byte(gbc);
    if (zero != 0)
        return AVERROR_INVALIDDATA;

    if (info->pbu_type == APV_PBU_ACCESS_UNIT_INFORMATION) {
        unsigned int num_frames = bytestream2_get_be16(gbc);

        if (num_frames < 1)
            return AVERROR_INVALIDDATA;

        info->pbu_type = bytestream2_get_byte(gbc);
        if (info->pbu_type != APV_PBU_PRIMARY_FRAME &&
            info->pbu_type != APV_PBU_NON_PRIMARY_FRAME &&
            (info->pbu_type < APV_PBU_PREVIEW_FRAME || info->pbu_type > APV_PBU_ALPHA_FRAME))
            return AVERROR_INVALIDDATA;

        bytestream2_skip(gbc, 2); // group_id
        zero = bytestream2_get_byte(gbc);
        if (zero != 0)
            return AVERROR_INVALIDDATA;
    } else if (info->pbu_type != APV_PBU_PRIMARY_FRAME)
        return AVERROR_INVALIDDATA;

    info->profile_idc = bytestream2_get_byte(gbc);
    info->level_idc   = bytestream2_get_byte(gbc);

    byte = bytestream2_get_byte(gbc);
    info->band_idc = byte >> 3;
    zero = byte & 7;
    if (zero != 0)
        return AVERROR_INVALIDDATA;

    info->frame_width  = bytestream2_get_be24(gbc);
    info->frame_height = bytestream2_get_be24(gbc);
    if (info->frame_width  < 1 || info->frame_width  > 65536 ||
        info->frame_height < 1 || info->frame_height > 65536)
        return AVERROR_INVALIDDATA;

    byte = bytestream2_get_byte(gbc);
    info->bit_depth_minus8  = byte & 0xf;

    if (info->bit_depth_minus8 > 8) {
        return AVERROR_INVALIDDATA;
    }
    if (info->bit_depth_minus8 % 2) {
        // Odd bit depths are technically valid but not useful here.
        return AVERROR_INVALIDDATA;
    }

    // Ignore capture_time_distance.
    bytestream2_skip(gbc, 1);

    zero = bytestream2_get_byte(gbc);
    if (zero != 0)
        return AVERROR_INVALIDDATA;

    return 1;
}

static int apv_probe(const AVProbeData *p)
{
    GetByteContext gbc;
    uint32_t au_size, signature, pbu_size;
    int err;

    if (p->buf_size < 28) {
        // Too small to fit an APV header.
        return 0;
    }

    bytestream2_init(&gbc, p->buf, p->buf_size);

    au_size = bytestream2_get_be32(&gbc);
    if (au_size < 24) {
        // Too small.
        return 0;
    }
    signature = bytestream2_get_be32(&gbc);
    if (signature != APV_SIGNATURE) {
        // Signature is mandatory.
        return 0;
    }
    pbu_size = bytestream2_get_be32(&gbc);
    if (pbu_size < 16) {
        // Too small.
        return 0;
    }

    err = apv_extract_header_info(&gbc);
    if (err < 0) {
        // Header does not look like APV.
        return 0;
    }
    return AVPROBE_SCORE_MAX;
}

static int apv_read_header(AVFormatContext *s)
{
    AVStream *st;
    GetByteContext gbc;
    uint8_t buffer[12];
    uint32_t au_size, signature, pbu_size;
    int err, size;

    err = ffio_ensure_seekback(s->pb, sizeof(buffer));
    if (err < 0)
        return err;
    size = ffio_read_size(s->pb, buffer, sizeof(buffer));
    if (size < 0)
        return size;

    bytestream2_init(&gbc, buffer, sizeof(buffer));

    au_size = bytestream2_get_be32(&gbc);
    if (au_size < 24) {
        // Too small.
        return AVERROR_INVALIDDATA;
    }
    signature = bytestream2_get_be32(&gbc);
    if (signature != APV_SIGNATURE) {
        // Signature is mandatory.
        return AVERROR_INVALIDDATA;
    }
    pbu_size = bytestream2_get_be32(&gbc);
    if (pbu_size < 16) {
        // Too small.
        return AVERROR_INVALIDDATA;
    }

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id   = AV_CODEC_ID_APV;

    ffstream(st)->need_parsing = AVSTREAM_PARSE_HEADERS;
    st->avg_frame_rate = (AVRational){ 30, 1 };
    avpriv_set_pts_info(st, 64, 1, 30);

    avio_seek(s->pb, -size, SEEK_CUR);

    return 0;
}

static int apv_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    uint32_t au_size, signature;
    int ret;

    au_size = avio_rb32(s->pb);
    if (au_size == 0 && avio_feof(s->pb))
        return AVERROR_EOF;
    if (au_size < 24 || au_size > 1 << 24) {
        av_log(s, AV_LOG_ERROR,
               "APV AU has invalid size: %"PRIu32"\n", au_size);
        return AVERROR_INVALIDDATA;
    }

    ret = av_get_packet(s->pb, pkt, au_size);
    if (ret < 0)
        return ret;
    pkt->pos  -= 4;
    pkt->flags = AV_PKT_FLAG_KEY;

    signature = AV_RB32(pkt->data);
    if (signature != APV_SIGNATURE) {
        av_log(s, AV_LOG_ERROR, "APV AU has invalid signature.\n");
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

const FFInputFormat ff_apv_demuxer = {
    .p.name         = "apv",
    .p.long_name    = NULL_IF_CONFIG_SMALL("APV raw bitstream"),
    .p.extensions   = "apv",
    .p.flags        = AVFMT_GENERIC_INDEX | AVFMT_NOTIMESTAMPS,
    .read_probe     = apv_probe,
    .read_header    = apv_read_header,
    .read_packet    = apv_read_packet,
};
