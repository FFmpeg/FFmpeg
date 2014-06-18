/*
 * Deluxe Paint Animation demuxer
 * Copyright (c) 2009 Peter Ross
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

/**
 * @file
 * Deluxe Paint Animation demuxer
 */

#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "internal.h"

typedef struct Page {
    int base_record;
    unsigned int nb_records;
    int size;
} Page;

typedef struct AnmDemuxContext {
    unsigned int nb_pages;    /**< total pages in file */
    unsigned int nb_records;  /**< total records in file */
    int page_table_offset;
#define MAX_PAGES  256        /**< Deluxe Paint hardcoded value */
    Page pt[MAX_PAGES];       /**< page table */
    int page;                 /**< current page (or AVERROR_xxx code) */
    int record;               /**< current record (with in page) */
} AnmDemuxContext;

#define LPF_TAG  MKTAG('L','P','F',' ')
#define ANIM_TAG MKTAG('A','N','I','M')

static int probe(AVProbeData *p)
{
    /* verify tags and video dimensions */
    if (AV_RL32(&p->buf[0])  == LPF_TAG &&
        AV_RL32(&p->buf[16]) == ANIM_TAG &&
        AV_RL16(&p->buf[20]) && AV_RL16(&p->buf[22]))
        return AVPROBE_SCORE_MAX;
    return 0;
}

/**
 * @return page containing the requested record or AVERROR_XXX
 */
static int find_record(const AnmDemuxContext *anm, int record)
{
    int i;

    if (record >= anm->nb_records)
        return AVERROR_EOF;

    for (i = 0; i < MAX_PAGES; i++) {
        const Page *p = &anm->pt[i];
        if (p->nb_records > 0 && record >= p->base_record && record < p->base_record + p->nb_records)
            return i;
    }

    return AVERROR_INVALIDDATA;
}

static int read_header(AVFormatContext *s)
{
    AnmDemuxContext *anm = s->priv_data;
    AVIOContext *pb = s->pb;
    AVStream *st;
    int i, ret;

    avio_skip(pb, 4); /* magic number */
    if (avio_rl16(pb) != MAX_PAGES) {
        avpriv_request_sample(s, "max_pages != " AV_STRINGIFY(MAX_PAGES));
        return AVERROR_PATCHWELCOME;
    }

    anm->nb_pages   = avio_rl16(pb);
    anm->nb_records = avio_rl32(pb);
    avio_skip(pb, 2); /* max records per page */
    anm->page_table_offset = avio_rl16(pb);
    if (avio_rl32(pb) != ANIM_TAG)
        return AVERROR_INVALIDDATA;

    /* video stream */
    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id   = AV_CODEC_ID_ANM;
    st->codecpar->codec_tag  = 0; /* no fourcc */
    st->codecpar->width      = avio_rl16(pb);
    st->codecpar->height     = avio_rl16(pb);
    if (avio_r8(pb) != 0)
        goto invalid;
    avio_skip(pb, 1); /* frame rate multiplier info */

    /* ignore last delta record (used for looping) */
    if (avio_r8(pb))  /* has_last_delta */
        anm->nb_records = FFMAX(anm->nb_records - 1, 0);

    avio_skip(pb, 1); /* last_delta_valid */

    if (avio_r8(pb) != 0)
        goto invalid;

    if (avio_r8(pb) != 1)
        goto invalid;

    avio_skip(pb, 1); /* other recs per frame */

    if (avio_r8(pb) != 1)
        goto invalid;

    avio_skip(pb, 32); /* record_types */
    st->nb_frames = avio_rl32(pb);
    avpriv_set_pts_info(st, 64, 1, avio_rl16(pb));
    avio_skip(pb, 58);

    /* color cycling and palette data */
    st->codecpar->extradata_size = 16*8 + 4*256;
    st->codecpar->extradata      = av_mallocz(st->codecpar->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!st->codecpar->extradata) {
        return AVERROR(ENOMEM);
    }
    ret = avio_read(pb, st->codecpar->extradata, st->codecpar->extradata_size);
    if (ret < 0)
        return ret;

    /* read page table */
    ret = avio_seek(pb, anm->page_table_offset, SEEK_SET);
    if (ret < 0)
        return ret;

    for (i = 0; i < MAX_PAGES; i++) {
        Page *p = &anm->pt[i];
        p->base_record = avio_rl16(pb);
        p->nb_records  = avio_rl16(pb);
        p->size        = avio_rl16(pb);
    }

    /* find page of first frame */
    anm->page = find_record(anm, 0);
    if (anm->page < 0) {
        return anm->page;
    }

    anm->record = -1;
    return 0;

invalid:
    avpriv_request_sample(s, "Invalid header element");
    return AVERROR_PATCHWELCOME;
}

static int read_packet(AVFormatContext *s,
                       AVPacket *pkt)
{
    AnmDemuxContext *anm = s->priv_data;
    AVIOContext *pb = s->pb;
    Page *p;
    int tmp, record_size;

    if (s->pb->eof_reached)
        return AVERROR(EIO);

    if (anm->page < 0)
        return anm->page;

repeat:
    p = &anm->pt[anm->page];

    /* parse page header */
    if (anm->record < 0) {
        avio_seek(pb, anm->page_table_offset + MAX_PAGES*6 + (anm->page<<16), SEEK_SET);
        avio_skip(pb, 8 + 2*p->nb_records);
        anm->record = 0;
    }

    /* if we have fetched all records in this page, then find the
       next page and repeat */
    if (anm->record >= p->nb_records) {
        anm->page = find_record(anm, p->base_record + p->nb_records);
        if (anm->page < 0)
            return anm->page;
        anm->record = -1;
        goto repeat;
    }

    /* fetch record size */
    tmp = avio_tell(pb);
    avio_seek(pb, anm->page_table_offset + MAX_PAGES*6 + (anm->page<<16) +
              8 + anm->record * 2, SEEK_SET);
    record_size = avio_rl16(pb);
    avio_seek(pb, tmp, SEEK_SET);

    /* fetch record */
    pkt->size = av_get_packet(s->pb, pkt, record_size);
    if (pkt->size < 0)
        return pkt->size;
    if (p->base_record + anm->record == 0)
        pkt->flags |= AV_PKT_FLAG_KEY;

    anm->record++;
    return 0;
}

AVInputFormat ff_anm_demuxer = {
    .name           = "anm",
    .long_name      = NULL_IF_CONFIG_SMALL("Deluxe Paint Animation"),
    .priv_data_size = sizeof(AnmDemuxContext),
    .read_probe     = probe,
    .read_header    = read_header,
    .read_packet    = read_packet,
};
