/*
 * TiVo ty stream demuxer
 * Copyright (c) 2005 VLC authors and VideoLAN
 * Copyright (c) 2005 by Neal Symms (tivo@freakinzoo.com) - February 2005
 * based on code by Christopher Wingert for tivo-mplayer
 * tivo(at)wingert.org, February 2003
 * Copyright (c) 2017 Paul B Mahol
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
#include "internal.h"
#include "mpeg.h"

#define SERIES1_PES_LENGTH  11    /* length of audio PES hdr on S1 */
#define SERIES2_PES_LENGTH  16    /* length of audio PES hdr on S2 */
#define AC3_PES_LENGTH      14    /* length of audio PES hdr for AC3 */
#define VIDEO_PES_LENGTH    16    /* length of video PES header */
#define DTIVO_PTS_OFFSET    6     /* offs into PES for MPEG PTS on DTivo */
#define SA_PTS_OFFSET       9     /* offset into PES for MPEG PTS on SA */
#define AC3_PTS_OFFSET      9     /* offset into PES for AC3 PTS on DTivo */
#define VIDEO_PTS_OFFSET    9     /* offset into PES for video PTS on all */
#define AC3_PKT_LENGTH      1536  /* size of TiVo AC3 pkts (w/o PES hdr) */

static const uint8_t ty_VideoPacket[]     = { 0x00, 0x00, 0x01, 0xe0 };
static const uint8_t ty_MPEGAudioPacket[] = { 0x00, 0x00, 0x01, 0xc0 };
static const uint8_t ty_AC3AudioPacket[]  = { 0x00, 0x00, 0x01, 0xbd };

#define TIVO_PES_FILEID   0xf5467abd
#define CHUNK_SIZE        (128 * 1024)
#define CHUNK_PEEK_COUNT  3      /* number of chunks to probe */

typedef struct TyRecHdr {
    int64_t   rec_size;
    uint8_t   ex[2];
    uint8_t   rec_type;
    uint8_t   subrec_type;
    uint64_t  ty_pts;            /* TY PTS in the record header */
} TyRecHdr;

typedef enum {
    TIVO_TYPE_UNKNOWN,
    TIVO_TYPE_SA,
    TIVO_TYPE_DTIVO
} TiVo_type;

typedef enum {
    TIVO_SERIES_UNKNOWN,
    TIVO_SERIES1,
    TIVO_SERIES2
} TiVo_series;

typedef enum {
    TIVO_AUDIO_UNKNOWN,
    TIVO_AUDIO_AC3,
    TIVO_AUDIO_MPEG
} TiVo_audio;

typedef struct TYDemuxContext {
    unsigned        cur_chunk;
    unsigned        cur_chunk_pos;
    int64_t         cur_pos;
    TiVo_type       tivo_type;        /* TiVo type (SA / DTiVo) */
    TiVo_series     tivo_series;      /* Series1 or Series2 */
    TiVo_audio      audio_type;       /* AC3 or MPEG */
    int             pes_length;       /* Length of Audio PES header */
    int             pts_offset;       /* offset into audio PES of PTS */
    uint8_t         pes_buffer[20];   /* holds incomplete pes headers */
    int             pes_buf_cnt;      /* how many bytes in our buffer */
    size_t          ac3_pkt_size;     /* length of ac3 pkt we've seen so far */
    uint64_t        last_ty_pts;      /* last TY timestamp we've seen */

    int64_t         first_audio_pts;
    int64_t         last_audio_pts;
    int64_t         last_video_pts;

    TyRecHdr       *rec_hdrs;         /* record headers array */
    int             cur_rec;          /* current record in this chunk */
    int             num_recs;         /* number of recs in this chunk */
    int             first_chunk;

    uint8_t         chunk[CHUNK_SIZE];
} TYDemuxContext;

static int ty_probe(const AVProbeData *p)
{
    int i;

    for (i = 0; i + 12 < p->buf_size; i += CHUNK_SIZE) {
        if (AV_RB32(p->buf + i) == TIVO_PES_FILEID &&
            AV_RB32(p->buf + i + 4) == 0x02 &&
            AV_RB32(p->buf + i + 8) == CHUNK_SIZE) {
            return AVPROBE_SCORE_MAX;
        }
    }

    return 0;
}

static TyRecHdr *parse_chunk_headers(const uint8_t *buf,
                                     int num_recs)
{
    TyRecHdr *hdrs, *rec_hdr;
    int i;

    hdrs = av_calloc(num_recs, sizeof(TyRecHdr));
    if (!hdrs)
        return NULL;

    for (i = 0; i < num_recs; i++) {
        const uint8_t *record_header = buf + (i * 16);

        rec_hdr = &hdrs[i];     /* for brevity */
        rec_hdr->rec_type = record_header[3];
        rec_hdr->subrec_type = record_header[2] & 0x0f;
        if ((record_header[0] & 0x80) == 0x80) {
            uint8_t b1, b2;

            /* marker bit 2 set, so read extended data */
            b1 = (((record_header[0] & 0x0f) << 4) |
                  ((record_header[1] & 0xf0) >> 4));
            b2 = (((record_header[1] & 0x0f) << 4) |
                  ((record_header[2] & 0xf0) >> 4));

            rec_hdr->ex[0] = b1;
            rec_hdr->ex[1] = b2;
            rec_hdr->rec_size = 0;
            rec_hdr->ty_pts = 0;
        } else {
            rec_hdr->rec_size = (record_header[0] << 8 |
                                 record_header[1]) << 4 |
                                (record_header[2] >> 4);
            rec_hdr->ty_pts = AV_RB64(&record_header[8]);
        }
    }
    return hdrs;
}

static int find_es_header(const uint8_t *header,
                          const uint8_t *buffer, int search_len)
{
    int count;

    for (count = 0; count < search_len; count++) {
        if (!memcmp(&buffer[count], header, 4))
            return count;
    }
    return -1;
}

static int analyze_chunk(AVFormatContext *s, const uint8_t *chunk)
{
    TYDemuxContext *ty = s->priv_data;
    int num_recs, i;
    TyRecHdr *hdrs;
    int num_6e0, num_be0, num_9c0, num_3c0;

    /* skip if it's a Part header */
    if (AV_RB32(&chunk[0]) == TIVO_PES_FILEID)
        return 0;

    /* number of records in chunk (we ignore high order byte;
     * rarely are there > 256 chunks & we don't need that many anyway) */
    num_recs = chunk[0];
    if (num_recs < 5) {
        /* try again with the next chunk.  Sometimes there are dead ones */
        return 0;
    }

    chunk += 4;       /* skip past rec count & SEQ bytes */
    ff_dlog(s, "probe: chunk has %d recs\n", num_recs);
    hdrs = parse_chunk_headers(chunk, num_recs);
    if (!hdrs)
        return AVERROR(ENOMEM);

    /* scan headers.
     * 1. check video packets.  Presence of 0x6e0 means S1.
     *    No 6e0 but have be0 means S2.
     * 2. probe for audio 0x9c0 vs 0x3c0 (AC3 vs Mpeg)
     *    If AC-3, then we have DTivo.
     *    If MPEG, search for PTS offset.  This will determine SA vs. DTivo.
     */
    num_6e0 = num_be0 = num_9c0 = num_3c0 = 0;
    for (i = 0; i < num_recs; i++) {
        switch (hdrs[i].subrec_type << 8 | hdrs[i].rec_type) {
        case 0x6e0:
            num_6e0++;
            break;
        case 0xbe0:
            num_be0++;
            break;
        case 0x3c0:
            num_3c0++;
            break;
        case 0x9c0:
            num_9c0++;
            break;
        }
    }
    ff_dlog(s, "probe: chunk has %d 0x6e0 recs, %d 0xbe0 recs.\n",
            num_6e0, num_be0);

    /* set up our variables */
    if (num_6e0 > 0) {
        ff_dlog(s, "detected Series 1 Tivo\n");
        ty->tivo_series = TIVO_SERIES1;
        ty->pes_length = SERIES1_PES_LENGTH;
    } else if (num_be0 > 0) {
        ff_dlog(s, "detected Series 2 Tivo\n");
        ty->tivo_series = TIVO_SERIES2;
        ty->pes_length = SERIES2_PES_LENGTH;
    }
    if (num_9c0 > 0) {
        ff_dlog(s, "detected AC-3 Audio (DTivo)\n");
        ty->audio_type = TIVO_AUDIO_AC3;
        ty->tivo_type = TIVO_TYPE_DTIVO;
        ty->pts_offset = AC3_PTS_OFFSET;
        ty->pes_length = AC3_PES_LENGTH;
    } else if (num_3c0 > 0) {
        ty->audio_type = TIVO_AUDIO_MPEG;
        ff_dlog(s, "detected MPEG Audio\n");
    }

    /* if tivo_type still unknown, we can check PTS location
     * in MPEG packets to determine tivo_type */
    if (ty->tivo_type == TIVO_TYPE_UNKNOWN) {
        uint32_t data_offset = 16 * num_recs;

        for (i = 0; i < num_recs; i++) {
            if (data_offset + hdrs[i].rec_size > CHUNK_SIZE)
                break;

            if ((hdrs[i].subrec_type << 8 | hdrs[i].rec_type) == 0x3c0 && hdrs[i].rec_size > 15) {
                /* first make sure we're aligned */
                int pes_offset = find_es_header(ty_MPEGAudioPacket,
                        &chunk[data_offset], 5);
                if (pes_offset >= 0) {
                    /* pes found. on SA, PES has hdr data at offset 6, not PTS. */
                    if ((chunk[data_offset + 6 + pes_offset] & 0x80) == 0x80) {
                        /* S1SA or S2(any) Mpeg Audio (PES hdr, not a PTS start) */
                        if (ty->tivo_series == TIVO_SERIES1)
                            ff_dlog(s, "detected Stand-Alone Tivo\n");
                        ty->tivo_type = TIVO_TYPE_SA;
                        ty->pts_offset = SA_PTS_OFFSET;
                    } else {
                        if (ty->tivo_series == TIVO_SERIES1)
                            ff_dlog(s, "detected DirecTV Tivo\n");
                        ty->tivo_type = TIVO_TYPE_DTIVO;
                        ty->pts_offset = DTIVO_PTS_OFFSET;
                    }
                    break;
                }
            }
            data_offset += hdrs[i].rec_size;
        }
    }
    av_free(hdrs);

    return 0;
}

static int ty_read_header(AVFormatContext *s)
{
    TYDemuxContext *ty = s->priv_data;
    AVIOContext *pb = s->pb;
    AVStream *st, *ast;
    int i, ret = 0;

    ty->first_audio_pts = AV_NOPTS_VALUE;
    ty->last_audio_pts = AV_NOPTS_VALUE;
    ty->last_video_pts = AV_NOPTS_VALUE;

    for (i = 0; i < CHUNK_PEEK_COUNT; i++) {
        avio_read(pb, ty->chunk, CHUNK_SIZE);

        ret = analyze_chunk(s, ty->chunk);
        if (ret < 0)
            return ret;
        if (ty->tivo_series != TIVO_SERIES_UNKNOWN &&
            ty->audio_type  != TIVO_AUDIO_UNKNOWN &&
            ty->tivo_type   != TIVO_TYPE_UNKNOWN)
            break;
    }

    if (ty->tivo_series == TIVO_SERIES_UNKNOWN ||
        ty->audio_type == TIVO_AUDIO_UNKNOWN ||
        ty->tivo_type == TIVO_TYPE_UNKNOWN)
        return AVERROR(EIO);

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id   = AV_CODEC_ID_MPEG2VIDEO;
    st->need_parsing         = AVSTREAM_PARSE_FULL_RAW;
    avpriv_set_pts_info(st, 64, 1, 90000);

    ast = avformat_new_stream(s, NULL);
    if (!ast)
        return AVERROR(ENOMEM);
    ast->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;

    if (ty->audio_type == TIVO_AUDIO_MPEG) {
        ast->codecpar->codec_id = AV_CODEC_ID_MP2;
        ast->need_parsing       = AVSTREAM_PARSE_FULL_RAW;
    } else {
        ast->codecpar->codec_id = AV_CODEC_ID_AC3;
    }
    avpriv_set_pts_info(ast, 64, 1, 90000);

    ty->first_chunk = 1;

    avio_seek(pb, 0, SEEK_SET);

    return 0;
}

static int get_chunk(AVFormatContext *s)
{
    TYDemuxContext *ty = s->priv_data;
    AVIOContext *pb = s->pb;
    int read_size, num_recs;

    ff_dlog(s, "parsing ty chunk #%d\n", ty->cur_chunk);

    /* if we have left-over filler space from the last chunk, get that */
    if (avio_feof(pb))
        return AVERROR_EOF;

    /* read the TY packet header */
    read_size = avio_read(pb, ty->chunk, CHUNK_SIZE);
    ty->cur_chunk++;

    if ((read_size < 4) || (AV_RB32(ty->chunk) == 0)) {
        return AVERROR_EOF;
    }

    /* check if it's a PART Header */
    if (AV_RB32(ty->chunk) == TIVO_PES_FILEID) {
        /* skip master chunk and read new chunk */
        return get_chunk(s);
    }

    /* number of records in chunk (8- or 16-bit number) */
    if (ty->chunk[3] & 0x80) {
        /* 16 bit rec cnt */
        ty->num_recs = num_recs = (ty->chunk[1] << 8) + ty->chunk[0];
    } else {
        /* 8 bit reclen - TiVo 1.3 format */
        ty->num_recs = num_recs = ty->chunk[0];
    }
    ty->cur_rec = 0;
    ty->first_chunk = 0;

    ff_dlog(s, "chunk has %d records\n", num_recs);
    ty->cur_chunk_pos = 4;

    av_freep(&ty->rec_hdrs);

    if (num_recs * 16 >= CHUNK_SIZE - 4)
        return AVERROR_INVALIDDATA;

    ty->rec_hdrs = parse_chunk_headers(ty->chunk + 4, num_recs);
    if (!ty->rec_hdrs)
        return AVERROR(ENOMEM);
    ty->cur_chunk_pos += 16 * num_recs;

    return 0;
}

static int demux_video(AVFormatContext *s, TyRecHdr *rec_hdr, AVPacket *pkt)
{
    TYDemuxContext *ty = s->priv_data;
    const int subrec_type = rec_hdr->subrec_type;
    const int64_t rec_size = rec_hdr->rec_size;
    int es_offset1, ret;
    int got_packet = 0;

    if (subrec_type != 0x02 && subrec_type != 0x0c &&
        subrec_type != 0x08 && rec_size > 4) {
        /* get the PTS from this packet if it has one.
         * on S1, only 0x06 has PES.  On S2, however, most all do.
         * Do NOT Pass the PES Header to the MPEG2 codec */
        es_offset1 = find_es_header(ty_VideoPacket, ty->chunk + ty->cur_chunk_pos, 5);
        if (es_offset1 != -1) {
            ty->last_video_pts = ff_parse_pes_pts(
                    ty->chunk + ty->cur_chunk_pos + es_offset1 + VIDEO_PTS_OFFSET);
            if (subrec_type != 0x06) {
                /* if we found a PES, and it's not type 6, then we're S2 */
                /* The packet will have video data (& other headers) so we
                 * chop out the PES header and send the rest */
                if (rec_size >= VIDEO_PES_LENGTH + es_offset1) {
                    int size = rec_hdr->rec_size - VIDEO_PES_LENGTH - es_offset1;

                    ty->cur_chunk_pos += VIDEO_PES_LENGTH + es_offset1;
                    if ((ret = av_new_packet(pkt, size)) < 0)
                        return ret;
                    memcpy(pkt->data, ty->chunk + ty->cur_chunk_pos, size);
                    ty->cur_chunk_pos += size;
                    pkt->stream_index = 0;
                    got_packet = 1;
                } else {
                    ff_dlog(s, "video rec type 0x%02x has short PES"
                        " (%"PRId64" bytes)\n", subrec_type, rec_size);
                    /* nuke this block; it's too short, but has PES marker */
                    ty->cur_chunk_pos += rec_size;
                    return 0;
                }
            }
        }
    }

    if (subrec_type == 0x06) {
        /* type 6 (S1 DTivo) has no data, so we're done */
        ty->cur_chunk_pos += rec_size;
        return 0;
    }

    if (!got_packet) {
        if ((ret = av_new_packet(pkt, rec_size)) < 0)
            return ret;
        memcpy(pkt->data, ty->chunk + ty->cur_chunk_pos, rec_size);
        ty->cur_chunk_pos += rec_size;
        pkt->stream_index = 0;
        got_packet = 1;
    }

    /* if it's not a continue blk, then set PTS */
    if (subrec_type != 0x02) {
        if (subrec_type == 0x0c && pkt->size >= 6)
            pkt->data[5] |= 0x08;
        if (subrec_type == 0x07) {
            ty->last_ty_pts = rec_hdr->ty_pts;
        } else {
            /* yes I know this is a cheap hack.  It's the timestamp
               used for display and skipping fwd/back, so it
               doesn't have to be accurate to the millisecond.
               I adjust it here by roughly one 1/30 sec.  Yes it
               will be slightly off for UK streams, but it's OK.
             */
            ty->last_ty_pts += 35000000;
            //ty->last_ty_pts += 33366667;
        }
        /* set PTS for this block before we send */
        if (ty->last_video_pts > AV_NOPTS_VALUE) {
            pkt->pts = ty->last_video_pts;
            /* PTS gets used ONCE.
             * Any subsequent frames we get BEFORE next PES
             * header will have their PTS computed in the codec */
            ty->last_video_pts = AV_NOPTS_VALUE;
        }
    }

    return got_packet;
}

static int check_sync_pes(AVFormatContext *s, AVPacket *pkt,
                          int32_t offset, int32_t rec_len)
{
    TYDemuxContext *ty = s->priv_data;

    if (offset < 0 || offset + ty->pes_length > rec_len) {
        /* entire PES header not present */
        ff_dlog(s, "PES header at %"PRId32" not complete in record. storing.\n", offset);
        /* save the partial pes header */
        if (offset < 0) {
            /* no header found, fake some 00's (this works, believe me) */
            memset(ty->pes_buffer, 0, 4);
            ty->pes_buf_cnt = 4;
            if (rec_len > 4)
                ff_dlog(s, "PES header not found in record of %"PRId32" bytes!\n", rec_len);
            return -1;
        }
        /* copy the partial pes header we found */
        memcpy(ty->pes_buffer, pkt->data + offset, rec_len - offset);
        ty->pes_buf_cnt = rec_len - offset;

        if (offset > 0) {
            /* PES Header was found, but not complete, so trim the end of this record */
            pkt->size -= rec_len - offset;
            return 1;
        }
        return -1;    /* partial PES, no audio data */
    }
    /* full PES header present, extract PTS */
    ty->last_audio_pts = ff_parse_pes_pts(&pkt->data[ offset + ty->pts_offset]);
    if (ty->first_audio_pts == AV_NOPTS_VALUE)
        ty->first_audio_pts = ty->last_audio_pts;
    pkt->pts = ty->last_audio_pts;
    memmove(pkt->data + offset, pkt->data + offset + ty->pes_length, rec_len - ty->pes_length);
    pkt->size -= ty->pes_length;
    return 0;
}

static int demux_audio(AVFormatContext *s, TyRecHdr *rec_hdr, AVPacket *pkt)
{
    TYDemuxContext *ty = s->priv_data;
    const int subrec_type = rec_hdr->subrec_type;
    const int64_t rec_size = rec_hdr->rec_size;
    int es_offset1, ret;

    if (subrec_type == 2) {
        int need = 0;
        /* SA or DTiVo Audio Data, no PES (continued block)
         * ================================================
         */

        /* continue PES if previous was incomplete */
        if (ty->pes_buf_cnt > 0) {
            need = ty->pes_length - ty->pes_buf_cnt;

            ff_dlog(s, "continuing PES header\n");
            /* do we have enough data to complete? */
            if (need >= rec_size) {
                /* don't have complete PES hdr; save what we have and return */
                memcpy(ty->pes_buffer + ty->pes_buf_cnt, ty->chunk + ty->cur_chunk_pos, rec_size);
                ty->cur_chunk_pos += rec_size;
                ty->pes_buf_cnt += rec_size;
                return 0;
            }

            /* we have enough; reconstruct this frame with the new hdr */
            memcpy(ty->pes_buffer + ty->pes_buf_cnt, ty->chunk + ty->cur_chunk_pos, need);
            ty->cur_chunk_pos += need;
            /* get the PTS out of this PES header (MPEG or AC3) */
            if (ty->audio_type == TIVO_AUDIO_MPEG) {
                es_offset1 = find_es_header(ty_MPEGAudioPacket,
                        ty->pes_buffer, 5);
            } else {
                es_offset1 = find_es_header(ty_AC3AudioPacket,
                        ty->pes_buffer, 5);
            }
            if (es_offset1 < 0) {
                ff_dlog(s, "Can't find audio PES header in packet.\n");
            } else {
                ty->last_audio_pts = ff_parse_pes_pts(
                    &ty->pes_buffer[es_offset1 + ty->pts_offset]);
                pkt->pts = ty->last_audio_pts;
            }
            ty->pes_buf_cnt = 0;

        }
        if ((ret = av_new_packet(pkt, rec_size - need)) < 0)
            return ret;
        memcpy(pkt->data, ty->chunk + ty->cur_chunk_pos, rec_size - need);
        ty->cur_chunk_pos += rec_size - need;
        pkt->stream_index = 1;

        /* S2 DTivo has AC3 packets with 2 padding bytes at end.  This is
         * not allowed in the AC3 spec and will cause problems.  So here
         * we try to trim things. */
        /* Also, S1 DTivo has alternating short / long AC3 packets.  That
         * is, one packet is short (incomplete) and the next packet has
         * the first one's missing data, plus all of its own.  Strange. */
        if (ty->audio_type == TIVO_AUDIO_AC3 &&
                ty->tivo_series == TIVO_SERIES2) {
            if (ty->ac3_pkt_size + pkt->size > AC3_PKT_LENGTH) {
                pkt->size -= 2;
                ty->ac3_pkt_size = 0;
            } else {
                ty->ac3_pkt_size += pkt->size;
            }
        }
    } else if (subrec_type == 0x03) {
        if ((ret = av_new_packet(pkt, rec_size)) < 0)
            return ret;
        memcpy(pkt->data, ty->chunk + ty->cur_chunk_pos, rec_size);
        ty->cur_chunk_pos += rec_size;
        pkt->stream_index = 1;
        /* MPEG Audio with PES Header, either SA or DTiVo   */
        /* ================================================ */
        es_offset1 = find_es_header(ty_MPEGAudioPacket, pkt->data, 5);

        /* SA PES Header, No Audio Data                     */
        /* ================================================ */
        if ((es_offset1 == 0) && (rec_size == 16)) {
            ty->last_audio_pts = ff_parse_pes_pts(&pkt->data[SA_PTS_OFFSET]);
            if (ty->first_audio_pts == AV_NOPTS_VALUE)
                ty->first_audio_pts = ty->last_audio_pts;
            av_packet_unref(pkt);
            return 0;
        }
        /* DTiVo Audio with PES Header                      */
        /* ================================================ */

        /* Check for complete PES */
        if (check_sync_pes(s, pkt, es_offset1, rec_size) == -1) {
            /* partial PES header found, nothing else.
             * we're done. */
            av_packet_unref(pkt);
            return 0;
        }
    } else if (subrec_type == 0x04) {
        /* SA Audio with no PES Header                      */
        /* ================================================ */
        if ((ret = av_new_packet(pkt, rec_size)) < 0)
            return ret;
        memcpy(pkt->data, ty->chunk + ty->cur_chunk_pos, rec_size);
        ty->cur_chunk_pos += rec_size;
        pkt->stream_index = 1;
        pkt->pts = ty->last_audio_pts;
    } else if (subrec_type == 0x09) {
        if ((ret = av_new_packet(pkt, rec_size)) < 0)
            return ret;
        memcpy(pkt->data, ty->chunk + ty->cur_chunk_pos, rec_size);
        ty->cur_chunk_pos += rec_size ;
        pkt->stream_index = 1;

        /* DTiVo AC3 Audio Data with PES Header             */
        /* ================================================ */
        es_offset1 = find_es_header(ty_AC3AudioPacket, pkt->data, 5);

        /* Check for complete PES */
        if (check_sync_pes(s, pkt, es_offset1, rec_size) == -1) {
            /* partial PES header found, nothing else.  we're done. */
            av_packet_unref(pkt);
            return 0;
        }
        /* S2 DTivo has invalid long AC3 packets */
        if (ty->tivo_series == TIVO_SERIES2) {
            if (pkt->size > AC3_PKT_LENGTH) {
                pkt->size -= 2;
                ty->ac3_pkt_size = 0;
            } else {
                ty->ac3_pkt_size = pkt->size;
            }
        }
    } else {
        /* Unsupported/Unknown */
        ty->cur_chunk_pos += rec_size;
        return 0;
    }

    return 1;
}

static int ty_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    TYDemuxContext *ty = s->priv_data;
    AVIOContext *pb = s->pb;
    TyRecHdr *rec;
    int64_t rec_size = 0;
    int ret = 0;

    if (avio_feof(pb))
        return AVERROR_EOF;

    while (ret <= 0) {
        if (!ty->rec_hdrs || ty->first_chunk || ty->cur_rec >= ty->num_recs) {
            if (get_chunk(s) < 0 || ty->num_recs <= 0)
                return AVERROR_EOF;
        }

        rec = &ty->rec_hdrs[ty->cur_rec];
        rec_size = rec->rec_size;
        ty->cur_rec++;

        if (rec_size <= 0)
            continue;

        if (ty->cur_chunk_pos + rec->rec_size > CHUNK_SIZE)
            return AVERROR_INVALIDDATA;

        if (avio_feof(pb))
            return AVERROR_EOF;

        switch (rec->rec_type) {
        case VIDEO_ID:
            ret = demux_video(s, rec, pkt);
            break;
        case AUDIO_ID:
            ret = demux_audio(s, rec, pkt);
            break;
        default:
            ff_dlog(s, "Invalid record type 0x%02x\n", rec->rec_type);
        case 0x01:
        case 0x02:
        case 0x03: /* TiVo data services */
        case 0x05: /* unknown, but seen regularly */
            ty->cur_chunk_pos += rec->rec_size;
            break;
        }
    }

    return 0;
}

static int ty_read_close(AVFormatContext *s)
{
    TYDemuxContext *ty = s->priv_data;

    av_freep(&ty->rec_hdrs);

    return 0;
}

AVInputFormat ff_ty_demuxer = {
    .name           = "ty",
    .long_name      = NULL_IF_CONFIG_SMALL("TiVo TY Stream"),
    .priv_data_size = sizeof(TYDemuxContext),
    .read_probe     = ty_probe,
    .read_header    = ty_read_header,
    .read_packet    = ty_read_packet,
    .read_close     = ty_read_close,
    .extensions     = "ty,ty+",
    .flags          = AVFMT_TS_DISCONT,
};
