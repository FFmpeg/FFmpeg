/*
 * AAX demuxer
 * Copyright (c) 2020 Paul B Mahol
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

#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "avio_internal.h"
#include "internal.h"

typedef struct AAXColumn {
    uint8_t flag;
    uint8_t type;
    const char *name;
    uint32_t offset;
    int size;
} AAXColumn;

typedef struct AAXSegment {
    int64_t start;
    int64_t end;
} AAXSegment;

typedef struct AAXContext {
    int64_t table_size;
    uint16_t version;
    int64_t rows_offset;
    int64_t strings_offset;
    int64_t data_offset;
    int64_t name_offset;
    uint16_t columns;
    uint16_t row_width;
    uint32_t nb_segments;
    int64_t schema_offset;
    int64_t strings_size;
    char *string_table;

    uint32_t current_segment;

    AAXColumn *xcolumns;
    AAXSegment *segments;
} AAXContext;

static int aax_probe(const AVProbeData *p)
{
    if (AV_RB32(p->buf) != MKBETAG('@','U','T','F'))
        return 0;
    if (AV_RB32(p->buf + 4) == 0)
        return 0;
    if (AV_RB16(p->buf + 8) > 1)
        return 0;
    if (AV_RB32(p->buf + 28) < 1)
        return 0;

    return AVPROBE_SCORE_MAX;
}

enum ColumnFlag {
    COLUMN_FLAG_NAME            = 0x1,
    COLUMN_FLAG_DEFAULT         = 0x2,
    COLUMN_FLAG_ROW             = 0x4,
    COLUMN_FLAG_UNDEFINED       = 0x8 /* shouldn't exist */
};

enum ColumnType {
    COLUMN_TYPE_UINT8           = 0x00,
    COLUMN_TYPE_SINT8           = 0x01,
    COLUMN_TYPE_UINT16          = 0x02,
    COLUMN_TYPE_SINT16          = 0x03,
    COLUMN_TYPE_UINT32          = 0x04,
    COLUMN_TYPE_SINT32          = 0x05,
    COLUMN_TYPE_UINT64          = 0x06,
    COLUMN_TYPE_SINT64          = 0x07,
    COLUMN_TYPE_FLOAT           = 0x08,
    COLUMN_TYPE_DOUBLE          = 0x09,
    COLUMN_TYPE_STRING          = 0x0a,
    COLUMN_TYPE_VLDATA          = 0x0b,
    COLUMN_TYPE_UINT128         = 0x0c, /* for GUIDs */
    COLUMN_TYPE_UNDEFINED       = -1
};

static int64_t get_pts(AVFormatContext *s, int64_t pos, int size)
{
    AAXContext *a = s->priv_data;
    int64_t pts = 0;

    for (int seg = 0; seg < a->current_segment; seg++)
        pts += (a->segments[seg].end - a->segments[seg].start) / size;

    pts += ((pos - a->segments[a->current_segment].start) / size);

    return pts;
}

static int aax_read_header(AVFormatContext *s)
{
    AAXContext *a = s->priv_data;
    AVIOContext *pb = s->pb;
    AVCodecParameters *par;
    AVStream *st;
    int64_t column_offset = 0;
    int ret, extradata_size;
    char *codec;
    int64_t ret64;

    avio_skip(pb, 4);
    a->table_size      = avio_rb32(pb) + 8LL;
    a->version         = avio_rb16(pb);
    a->rows_offset     = avio_rb16(pb) + 8LL;
    a->strings_offset  = avio_rb32(pb) + 8LL;
    a->data_offset     = avio_rb32(pb) + 8LL;
    a->name_offset     = avio_rb32(pb);
    a->columns         = avio_rb16(pb);
    a->row_width       = avio_rb16(pb);
    a->nb_segments     = avio_rb32(pb);

    if (a->nb_segments < 1)
        return AVERROR_INVALIDDATA;

    a->schema_offset   = 0x20;
    a->strings_size    = a->data_offset - a->strings_offset;

    if (a->rows_offset > a->table_size ||
        a->strings_offset > a->table_size ||
        a->data_offset > a->table_size)
        return AVERROR_INVALIDDATA;
    if (a->strings_size <= 0 || a->name_offset >= a->strings_size ||
        a->strings_size > UINT16_MAX)
        return AVERROR_INVALIDDATA;
    if (a->columns <= 0)
        return AVERROR_INVALIDDATA;

    a->segments = av_calloc(a->nb_segments, sizeof(*a->segments));
    if (!a->segments)
        return AVERROR(ENOMEM);

    a->xcolumns = av_calloc(a->columns, sizeof(*a->xcolumns));
    if (!a->xcolumns)
        return AVERROR(ENOMEM);

    a->string_table = av_calloc(a->strings_size + 1, sizeof(*a->string_table));
    if (!a->string_table)
        return AVERROR(ENOMEM);

    for (int c = 0; c < a->columns; c++) {
        uint8_t info = avio_r8(pb);
        uint32_t offset = avio_rb32(pb);
        int value_size;

        if (offset >= a->strings_size)
            return AVERROR_INVALIDDATA;

        a->xcolumns[c].flag = info >>   4;
        a->xcolumns[c].type = info & 0x0F;

        switch (a->xcolumns[c].type) {
        case COLUMN_TYPE_UINT8:
        case COLUMN_TYPE_SINT8:
            value_size = 0x01;
            break;
        case COLUMN_TYPE_UINT16:
        case COLUMN_TYPE_SINT16:
            value_size = 0x02;
            break;
        case COLUMN_TYPE_UINT32:
        case COLUMN_TYPE_SINT32:
        case COLUMN_TYPE_FLOAT:
        case COLUMN_TYPE_STRING:
            value_size = 0x04;
            break;
        case COLUMN_TYPE_VLDATA:
            value_size = 0x08;
            break;
        case COLUMN_TYPE_UINT128:
            value_size = 0x10;
            break;
        default:
            return AVERROR_INVALIDDATA;
        }

        a->xcolumns[c].size = value_size;

        if (a->xcolumns[c].flag & COLUMN_FLAG_NAME)
            a->xcolumns[c].name = a->string_table + offset;

        if (a->xcolumns[c].flag & COLUMN_FLAG_DEFAULT) {
            /* data is found relative to columns start */
            a->xcolumns[c].offset = avio_tell(pb) - a->schema_offset;
            avio_skip(pb, value_size);
        }

        if (a->xcolumns[c].flag & COLUMN_FLAG_ROW) {
            /* data is found relative to row start */
            a->xcolumns[c].offset = column_offset;
            column_offset += value_size;
        }
    }

    ret = ret64 = avio_seek(pb, a->strings_offset, SEEK_SET);
    if (ret64 < 0)
        return ret;

    ret = ffio_read_size(pb, a->string_table, a->strings_size);
    if (ret < 0)
        return ret;

    for (int c = 0; c < a->columns; c++) {
        int64_t data_offset = 0;
        int64_t col_offset;
        int flag, type;

        if (!a->xcolumns[c].name || strcmp(a->xcolumns[c].name, "data"))
            continue;

        type = a->xcolumns[c].type;
        flag = a->xcolumns[c].flag;
        col_offset = a->xcolumns[c].offset;

        for (uint64_t r = 0; r < a->nb_segments; r++) {
            if (flag & COLUMN_FLAG_DEFAULT) {
                data_offset = a->schema_offset + col_offset;
            } else if (flag & COLUMN_FLAG_ROW) {
                data_offset = a->rows_offset + r * a->row_width + col_offset;
            } else
                return AVERROR_INVALIDDATA;

            ret = ret64 = avio_seek(pb, data_offset, SEEK_SET);
            if (ret64 < 0)
                return ret;

            if (type == COLUMN_TYPE_VLDATA) {
                int64_t start, size;

                start = avio_rb32(pb);
                size  = avio_rb32(pb);
                a->segments[r].start = start + a->data_offset;
                a->segments[r].end   = a->segments[r].start + size;
            } else
                return AVERROR_INVALIDDATA;
        }
    }

    if (!a->segments[0].end)
        return AVERROR_INVALIDDATA;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    st->start_time = 0;
    par = s->streams[0]->codecpar;
    par->codec_type = AVMEDIA_TYPE_AUDIO;

    codec = a->string_table + a->name_offset;
    if (!strcmp(codec, "AAX")) {
        par->codec_id = AV_CODEC_ID_ADPCM_ADX;
        ret64 = avio_seek(pb, a->segments[0].start, SEEK_SET);
        if (ret64 < 0 || avio_rb16(pb) != 0x8000)
            return AVERROR_INVALIDDATA;
        extradata_size = avio_rb16(pb) + 4;
        if (extradata_size < 12)
            return AVERROR_INVALIDDATA;
        avio_seek(pb, -4, SEEK_CUR);
        ret = ff_get_extradata(s, par, pb, extradata_size);
        if (ret < 0)
            return ret;
        par->ch_layout.nb_channels = AV_RB8 (par->extradata + 7);
        par->sample_rate = AV_RB32(par->extradata + 8);
        if (!par->ch_layout.nb_channels || !par->sample_rate)
            return AVERROR_INVALIDDATA;

        avpriv_set_pts_info(st, 64, 32, par->sample_rate);
  /*} else if (!strcmp(codec, "HCA") ){
        par->codec_id = AV_CODEC_ID_HCA;*/
    } else {
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

static int aax_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    AAXContext *a = s->priv_data;
    AVCodecParameters *par = s->streams[0]->codecpar;
    AVIOContext *pb = s->pb;
    const int size = 18 * par->ch_layout.nb_channels;
    int ret, extradata_size = 0;
    uint8_t *extradata = NULL;
    int skip = 0;

    if (avio_feof(pb))
        return AVERROR_EOF;

    pkt->pos = avio_tell(pb);

    for (uint32_t seg = 0; seg < a->nb_segments; seg++) {
        int64_t start = a->segments[seg].start;
        int64_t end   = a->segments[seg].end;

        if (pkt->pos >= start && pkt->pos <= end) {
            a->current_segment = seg;
            if (par->codec_id == AV_CODEC_ID_ADPCM_ADX)
                skip = (end - start) - ((end - start) / size) * size;
            break;
        }
    }

    if (pkt->pos >= a->segments[a->current_segment].end - skip) {
        if (a->current_segment + 1 == a->nb_segments)
            return AVERROR_EOF;
        a->current_segment++;
        avio_seek(pb, a->segments[a->current_segment].start, SEEK_SET);

        if (par->codec_id == AV_CODEC_ID_ADPCM_ADX) {
            if (avio_rb16(pb) != 0x8000)
                return AVERROR_INVALIDDATA;
            extradata_size = avio_rb16(pb) + 4;
            avio_seek(pb, -4, SEEK_CUR);
            if (extradata_size < 12)
                return AVERROR_INVALIDDATA;
            extradata = av_malloc(extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
            if (!extradata)
                return AVERROR(ENOMEM);
            if (avio_read(pb, extradata, extradata_size) != extradata_size) {
                av_free(extradata);
                return AVERROR(EIO);
            }
            memset(extradata + extradata_size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
        }
    }

    ret = av_get_packet(pb, pkt, size);
    if (ret != size) {
        av_free(extradata);
        return ret < 0 ? ret : AVERROR(EIO);
    }
    pkt->duration = 1;
    pkt->stream_index = 0;
    pkt->pts = get_pts(s, pkt->pos, size);

    if (extradata) {
        ret = av_packet_add_side_data(pkt, AV_PKT_DATA_NEW_EXTRADATA, extradata, extradata_size);
        if (ret < 0) {
            av_free(extradata);
            return ret;
        }
    }

    return ret;
}

static int aax_read_close(AVFormatContext *s)
{
    AAXContext *a = s->priv_data;

    av_freep(&a->segments);
    av_freep(&a->xcolumns);
    av_freep(&a->string_table);

    return 0;
}

const AVInputFormat ff_aax_demuxer = {
    .name           = "aax",
    .long_name      = NULL_IF_CONFIG_SMALL("CRI AAX"),
    .priv_data_size = sizeof(AAXContext),
    .flags_internal = FF_FMT_INIT_CLEANUP,
    .read_probe     = aax_probe,
    .read_header    = aax_read_header,
    .read_packet    = aax_read_packet,
    .read_close     = aax_read_close,
    .extensions     = "aax",
    .flags          = AVFMT_GENERIC_INDEX,
};
