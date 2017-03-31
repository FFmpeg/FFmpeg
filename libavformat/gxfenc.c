/*
 * GXF muxer.
 * Copyright (c) 2006 SmartJog S.A., Baptiste Coudurier <baptiste dot coudurier at smartjog dot com>
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
#include "libavutil/intfloat.h"
#include "libavutil/opt.h"
#include "libavutil/mathematics.h"
#include "libavutil/timecode.h"
#include "avformat.h"
#include "avio_internal.h"
#include "internal.h"
#include "gxf.h"

#define GXF_SAMPLES_PER_FRAME 32768
#define GXF_AUDIO_PACKET_SIZE 65536

#define GXF_TIMECODE(c, d, h, m, s, f) \
    ((c) << 30 | (d) << 29 | (h) << 24 | (m) << 16 | (s) << 8 | (f))

typedef struct GXFTimecode{
    int hh;
    int mm;
    int ss;
    int ff;
    int color;
    int drop;
} GXFTimecode;

typedef struct GXFStreamContext {
    int64_t pkt_cnt;
    uint32_t track_type;
    uint32_t sample_size;
    uint32_t sample_rate;
    uint16_t media_type;
    uint16_t media_info;
    int frame_rate_index;
    int lines_index;
    int fields;
    int iframes;
    int pframes;
    int bframes;
    int p_per_gop;
    int b_per_i_or_p; ///< number of B-frames per I-frame or P-frame
    int first_gop_closed;
    unsigned order;   ///< interleaving order
} GXFStreamContext;

typedef struct GXFContext {
    AVClass *av_class;
    uint32_t nb_fields;
    uint16_t audio_tracks;
    uint16_t mpeg_tracks;
    int64_t creation_time;
    uint32_t umf_start_offset;
    uint32_t umf_track_offset;
    uint32_t umf_media_offset;
    uint32_t umf_length;
    uint16_t umf_track_size;
    uint16_t umf_media_size;
    AVRational time_base;
    int flags;
    GXFStreamContext timecode_track;
    unsigned *flt_entries;    ///< offsets of packets /1024, starts after 2nd video field
    unsigned flt_entries_nb;
    uint64_t *map_offsets;    ///< offset of map packets
    unsigned map_offsets_nb;
    unsigned packet_count;
    GXFTimecode tc;
} GXFContext;

static const struct {
    int height, index;
} gxf_lines_tab[] = {
    { 480,  1 }, /* NTSC */
    { 512,  1 }, /* NTSC + VBI */
    { 576,  2 }, /* PAL */
    { 608,  2 }, /* PAL + VBI */
    { 1080, 4 },
    { 720,  6 },
};

static const AVCodecTag gxf_media_types[] = {
    { AV_CODEC_ID_MJPEG     ,   3 }, /* NTSC */
    { AV_CODEC_ID_MJPEG     ,   4 }, /* PAL */
    { AV_CODEC_ID_PCM_S24LE ,   9 },
    { AV_CODEC_ID_PCM_S16LE ,  10 },
    { AV_CODEC_ID_MPEG2VIDEO,  11 }, /* NTSC */
    { AV_CODEC_ID_MPEG2VIDEO,  12 }, /* PAL */
    { AV_CODEC_ID_DVVIDEO   ,  13 }, /* NTSC */
    { AV_CODEC_ID_DVVIDEO   ,  14 }, /* PAL */
    { AV_CODEC_ID_DVVIDEO   ,  15 }, /* 50M NTSC */
    { AV_CODEC_ID_DVVIDEO   ,  16 }, /* 50M PAL */
    { AV_CODEC_ID_AC3       ,  17 },
    //{ AV_CODEC_ID_NONE,  ,   18 }, /* Non compressed 24 bit audio */
    { AV_CODEC_ID_MPEG2VIDEO,  20 }, /* MPEG HD */
    { AV_CODEC_ID_MPEG1VIDEO,  22 }, /* NTSC */
    { AV_CODEC_ID_MPEG1VIDEO,  23 }, /* PAL */
    { AV_CODEC_ID_NONE,         0 },
};

#define SERVER_PATH "EXT:/PDR/default/"
#define ES_NAME_PATTERN "EXT:/PDR/default/ES."

static int gxf_find_lines_index(AVStream *st)
{
    GXFStreamContext *sc = st->priv_data;
    int i;

    for (i = 0; i < 6; ++i) {
        if (st->codecpar->height == gxf_lines_tab[i].height) {
            sc->lines_index = gxf_lines_tab[i].index;
            return 0;
        }
    }
    return -1;
}

static void gxf_write_padding(AVIOContext *pb, int64_t to_pad)
{
    ffio_fill(pb, 0, to_pad);
}

static int64_t updatePacketSize(AVIOContext *pb, int64_t pos)
{
    int64_t curpos;
    int size;

    size = avio_tell(pb) - pos;
    if (size % 4) {
        gxf_write_padding(pb, 4 - size % 4);
        size = avio_tell(pb) - pos;
    }
    curpos = avio_tell(pb);
    avio_seek(pb, pos + 6, SEEK_SET);
    avio_wb32(pb, size);
    avio_seek(pb, curpos, SEEK_SET);
    return curpos - pos;
}

static int64_t updateSize(AVIOContext *pb, int64_t pos)
{
    int64_t curpos;

    curpos = avio_tell(pb);
    avio_seek(pb, pos, SEEK_SET);
    avio_wb16(pb, curpos - pos - 2);
    avio_seek(pb, curpos, SEEK_SET);
    return curpos - pos;
}

static void gxf_write_packet_header(AVIOContext *pb, GXFPktType type)
{
    avio_wb32(pb, 0);  /* packet leader for synchro */
    avio_w8(pb, 1);
    avio_w8(pb, type); /* map packet */
    avio_wb32(pb, 0);  /* size */
    avio_wb32(pb, 0);  /* reserved */
    avio_w8(pb, 0xE1); /* trailer 1 */
    avio_w8(pb, 0xE2); /* trailer 2 */
}

static int gxf_write_mpeg_auxiliary(AVIOContext *pb, AVStream *st)
{
    GXFStreamContext *sc = st->priv_data;
    char buffer[1024];
    int size, starting_line;

    if (sc->iframes) {
        sc->p_per_gop = sc->pframes / sc->iframes;
        if (sc->pframes % sc->iframes)
            sc->p_per_gop++;
        if (sc->pframes) {
            sc->b_per_i_or_p = sc->bframes / sc->pframes;
            if (sc->bframes % sc->pframes)
                sc->b_per_i_or_p++;
        }
        if (sc->p_per_gop > 9)
            sc->p_per_gop = 9; /* ensure value won't take more than one char */
        if (sc->b_per_i_or_p > 9)
            sc->b_per_i_or_p = 9; /* ensure value won't take more than one char */
    }
    if (st->codecpar->height == 512 || st->codecpar->height == 608)
        starting_line = 7; // VBI
    else if (st->codecpar->height == 480)
        starting_line = 20;
    else
        starting_line = 23; // default PAL

    size = snprintf(buffer, sizeof(buffer), "Ver 1\nBr %.6f\nIpg 1\nPpi %d\nBpiop %d\n"
                    "Pix 0\nCf %d\nCg %d\nSl %d\nnl16 %d\nVi 1\nf1 1\n",
                    (float)st->codecpar->bit_rate, sc->p_per_gop, sc->b_per_i_or_p,
                    st->codecpar->format == AV_PIX_FMT_YUV422P ? 2 : 1, sc->first_gop_closed == 1,
                    starting_line, (st->codecpar->height + 15) / 16);
    av_assert0(size < sizeof(buffer));
    avio_w8(pb, TRACK_MPG_AUX);
    avio_w8(pb, size + 1);
    avio_write(pb, (uint8_t *)buffer, size + 1);
    return size + 3;
}

static int gxf_write_dv_auxiliary(AVIOContext *pb, AVStream *st)
{
    int64_t track_aux_data = 0;

    avio_w8(pb, TRACK_AUX);
    avio_w8(pb, 8);
    if (st->codecpar->format == AV_PIX_FMT_YUV420P)
        track_aux_data |= 0x01;     /* marks stream as DVCAM instead of DVPRO */
    track_aux_data |= 0x40000000;   /* aux data is valid */
    avio_wl64(pb, track_aux_data);
    return 8;
}

static int gxf_write_timecode_auxiliary(AVIOContext *pb, GXFContext *gxf)
{
    uint32_t timecode = GXF_TIMECODE(gxf->tc.color, gxf->tc.drop,
                                     gxf->tc.hh, gxf->tc.mm,
                                     gxf->tc.ss, gxf->tc.ff);

    avio_w8(pb, TRACK_AUX);
    avio_w8(pb, 8);
    avio_wl32(pb, timecode);
    /* reserved */
    avio_wl32(pb, 0);
    return 8;
}

static int gxf_write_track_description(AVFormatContext *s, GXFStreamContext *sc, int index)
{
    GXFContext *gxf = s->priv_data;
    AVIOContext *pb = s->pb;
    int64_t pos;

    /* track description section */
    avio_w8(pb, sc->media_type + 0x80);
    avio_w8(pb, index + 0xC0);

    pos = avio_tell(pb);
    avio_wb16(pb, 0); /* size */

    /* media file name */
    avio_w8(pb, TRACK_NAME);
    avio_w8(pb, strlen(ES_NAME_PATTERN) + 3);
    avio_write(pb, ES_NAME_PATTERN, sizeof(ES_NAME_PATTERN) - 1);
    avio_wb16(pb, sc->media_info);
    avio_w8(pb, 0);

    switch (sc->track_type) {
        case 3:     /* timecode */
            gxf_write_timecode_auxiliary(pb, gxf);
            break;
        case 4:     /* MPEG2 */
        case 9:     /* MPEG1 */
            gxf_write_mpeg_auxiliary(pb, s->streams[index]);
            break;
        case 5:     /* DV25 */
        case 6:     /* DV50 */
            gxf_write_dv_auxiliary(pb, s->streams[index]);
            break;
        default:
            avio_w8(pb, TRACK_AUX);
            avio_w8(pb, 8);
            avio_wl64(pb, 0);
    }

    /* file system version */
    avio_w8(pb, TRACK_VER);
    avio_w8(pb, 4);
    avio_wb32(pb, 0);

    /* frame rate */
    avio_w8(pb, TRACK_FPS);
    avio_w8(pb, 4);
    avio_wb32(pb, sc->frame_rate_index);

    /* lines per frame */
    avio_w8(pb, TRACK_LINES);
    avio_w8(pb, 4);
    avio_wb32(pb, sc->lines_index);

    /* fields per frame */
    avio_w8(pb, TRACK_FPF);
    avio_w8(pb, 4);
    avio_wb32(pb, sc->fields);

    return updateSize(pb, pos);
}

static int gxf_write_material_data_section(AVFormatContext *s)
{
    GXFContext *gxf = s->priv_data;
    AVIOContext *pb = s->pb;
    int64_t pos;
    int len;
    const char *filename = strrchr(s->url, '/');

    pos = avio_tell(pb);
    avio_wb16(pb, 0); /* size */

    /* name */
    if (filename)
        filename++;
    else
        filename = s->url;
    len = strlen(filename);

    avio_w8(pb, MAT_NAME);
    avio_w8(pb, strlen(SERVER_PATH) + len + 1);
    avio_write(pb, SERVER_PATH, sizeof(SERVER_PATH) - 1);
    avio_write(pb, filename, len);
    avio_w8(pb, 0);

    /* first field */
    avio_w8(pb, MAT_FIRST_FIELD);
    avio_w8(pb, 4);
    avio_wb32(pb, 0);

    /* last field */
    avio_w8(pb, MAT_LAST_FIELD);
    avio_w8(pb, 4);
    avio_wb32(pb, gxf->nb_fields);

    /* reserved */
    avio_w8(pb, MAT_MARK_IN);
    avio_w8(pb, 4);
    avio_wb32(pb, 0);

    avio_w8(pb, MAT_MARK_OUT);
    avio_w8(pb, 4);
    avio_wb32(pb, gxf->nb_fields);

    /* estimated size */
    avio_w8(pb, MAT_SIZE);
    avio_w8(pb, 4);
    avio_wb32(pb, avio_size(pb) / 1024);

    return updateSize(pb, pos);
}

static int gxf_write_track_description_section(AVFormatContext *s)
{
    GXFContext *gxf = s->priv_data;
    AVIOContext *pb = s->pb;
    int64_t pos;
    int i;

    pos = avio_tell(pb);
    avio_wb16(pb, 0); /* size */
    for (i = 0; i < s->nb_streams; ++i)
        gxf_write_track_description(s, s->streams[i]->priv_data, i);

    gxf_write_track_description(s, &gxf->timecode_track, s->nb_streams);

    return updateSize(pb, pos);
}

static int gxf_write_map_packet(AVFormatContext *s, int rewrite)
{
    GXFContext *gxf = s->priv_data;
    AVIOContext *pb = s->pb;
    int64_t pos = avio_tell(pb);

    if (!rewrite) {
        if (!(gxf->map_offsets_nb % 30)) {
            int err;
            if ((err = av_reallocp_array(&gxf->map_offsets,
                                         gxf->map_offsets_nb + 30,
                                         sizeof(*gxf->map_offsets))) < 0) {
                gxf->map_offsets_nb = 0;
                av_log(s, AV_LOG_ERROR, "could not realloc map offsets\n");
                return err;
            }
        }
        gxf->map_offsets[gxf->map_offsets_nb++] = pos; // do not increment here
    }

    gxf_write_packet_header(pb, PKT_MAP);

    /* preamble */
    avio_w8(pb, 0xE0); /* version */
    avio_w8(pb, 0xFF); /* reserved */

    gxf_write_material_data_section(s);
    gxf_write_track_description_section(s);

    return updatePacketSize(pb, pos);
}

static int gxf_write_flt_packet(AVFormatContext *s)
{
    GXFContext *gxf = s->priv_data;
    AVIOContext *pb = s->pb;
    int64_t pos = avio_tell(pb);
    int fields_per_flt = (gxf->nb_fields+1) / 1000 + 1;
    int flt_entries = gxf->nb_fields / fields_per_flt;
    int i = 0;

    gxf_write_packet_header(pb, PKT_FLT);

    avio_wl32(pb, fields_per_flt); /* number of fields */
    avio_wl32(pb, flt_entries); /* number of active flt entries */

    if (gxf->flt_entries) {
        for (i = 0; i < flt_entries; i++)
            avio_wl32(pb, gxf->flt_entries[(i*fields_per_flt)>>1]);
    }

    ffio_fill(pb, 0, (1000 - i) * 4);

    return updatePacketSize(pb, pos);
}

static int gxf_write_umf_material_description(AVFormatContext *s)
{
    GXFContext *gxf = s->priv_data;
    AVIOContext *pb = s->pb;
    int timecode_base = gxf->time_base.den == 60000 ? 60 : 50;
    int64_t timestamp = 0;
    uint64_t nb_fields;
    uint32_t timecode_in; // timecode at mark in
    uint32_t timecode_out; // timecode at mark out

    ff_parse_creation_time_metadata(s, &timestamp, 1);

    timecode_in = GXF_TIMECODE(gxf->tc.color, gxf->tc.drop,
                               gxf->tc.hh, gxf->tc.mm,
                               gxf->tc.ss, gxf->tc.ff);

    nb_fields = gxf->nb_fields +
                gxf->tc.hh * (timecode_base * 3600) +
                gxf->tc.mm * (timecode_base * 60)   +
                gxf->tc.ss * timecode_base          +
                gxf->tc.ff;

    timecode_out = GXF_TIMECODE(gxf->tc.color, gxf->tc.drop,
                                nb_fields / (timecode_base * 3600) % 24,
                                nb_fields / (timecode_base * 60)   % 60,
                                nb_fields /  timecode_base % 60,
                                nb_fields %  timecode_base);

    avio_wl32(pb, gxf->flags);
    avio_wl32(pb, gxf->nb_fields); /* length of the longest track */
    avio_wl32(pb, gxf->nb_fields); /* length of the shortest track */
    avio_wl32(pb, 0); /* mark in */
    avio_wl32(pb, gxf->nb_fields); /* mark out */
    avio_wl32(pb, timecode_in); /* timecode mark in */
    avio_wl32(pb, timecode_out); /* timecode mark out */
    avio_wl64(pb, timestamp); /* modification time */
    avio_wl64(pb, timestamp); /* creation time */
    avio_wl16(pb, 0); /* reserved */
    avio_wl16(pb, 0); /* reserved */
    avio_wl16(pb, gxf->audio_tracks);
    avio_wl16(pb, 1); /* timecode track count */
    avio_wl16(pb, 0); /* reserved */
    avio_wl16(pb, gxf->mpeg_tracks);
    return 48;
}

static int gxf_write_umf_payload(AVFormatContext *s)
{
    GXFContext *gxf = s->priv_data;
    AVIOContext *pb = s->pb;

    avio_wl32(pb, gxf->umf_length); /* total length of the umf data */
    avio_wl32(pb, 3); /* version */
    avio_wl32(pb, s->nb_streams+1);
    avio_wl32(pb, gxf->umf_track_offset); /* umf track section offset */
    avio_wl32(pb, gxf->umf_track_size);
    avio_wl32(pb, s->nb_streams+1);
    avio_wl32(pb, gxf->umf_media_offset);
    avio_wl32(pb, gxf->umf_media_size);
    avio_wl32(pb, gxf->umf_length); /* user data offset */
    avio_wl32(pb, 0); /* user data size */
    avio_wl32(pb, 0); /* reserved */
    avio_wl32(pb, 0); /* reserved */
    return 48;
}

static int gxf_write_umf_track_description(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    GXFContext *gxf = s->priv_data;
    int64_t pos = avio_tell(pb);
    int i;

    gxf->umf_track_offset = pos - gxf->umf_start_offset;
    for (i = 0; i < s->nb_streams; ++i) {
        GXFStreamContext *sc = s->streams[i]->priv_data;
        avio_wl16(pb, sc->media_info);
        avio_wl16(pb, 1);
    }

    avio_wl16(pb, gxf->timecode_track.media_info);
    avio_wl16(pb, 1);

    return avio_tell(pb) - pos;
}

static int gxf_write_umf_media_mpeg(AVIOContext *pb, AVStream *st)
{
    GXFStreamContext *sc = st->priv_data;

    if (st->codecpar->format == AV_PIX_FMT_YUV422P)
        avio_wl32(pb, 2);
    else
        avio_wl32(pb, 1); /* default to 420 */
    avio_wl32(pb, sc->first_gop_closed == 1); /* closed = 1, open = 0, unknown = 255 */
    avio_wl32(pb, 3); /* top = 1, bottom = 2, frame = 3, unknown = 0 */
    avio_wl32(pb, 1); /* I picture per GOP */
    avio_wl32(pb, sc->p_per_gop);
    avio_wl32(pb, sc->b_per_i_or_p);
    if (st->codecpar->codec_id == AV_CODEC_ID_MPEG2VIDEO)
        avio_wl32(pb, 2);
    else if (st->codecpar->codec_id == AV_CODEC_ID_MPEG1VIDEO)
        avio_wl32(pb, 1);
    else
        avio_wl32(pb, 0);
    avio_wl32(pb, 0); /* reserved */
    return 32;
}

static int gxf_write_umf_media_timecode(AVIOContext *pb, int drop)
{
    avio_wl32(pb, drop); /* drop frame */
    ffio_fill(pb, 0, 7 * 4); /* reserved */
    return 32;
}

static int gxf_write_umf_media_dv(AVIOContext *pb, GXFStreamContext *sc, AVStream *st)
{
    int dv_umf_data = 0;

    if (st->codecpar->format == AV_PIX_FMT_YUV420P)
        dv_umf_data |= 0x20; /* marks as DVCAM instead of DVPRO */
    avio_wl32(pb, dv_umf_data);
    ffio_fill(pb, 0, 7 * 4);
    return 32;
}

static int gxf_write_umf_media_audio(AVIOContext *pb, GXFStreamContext *sc)
{
    avio_wl64(pb, av_double2int(1)); /* sound level to begin to */
    avio_wl64(pb, av_double2int(1)); /* sound level to begin to */
    avio_wl32(pb, 0); /* number of fields over which to ramp up sound level */
    avio_wl32(pb, 0); /* number of fields over which to ramp down sound level */
    avio_wl32(pb, 0); /* reserved */
    avio_wl32(pb, 0); /* reserved */
    return 32;
}

static int gxf_write_umf_media_description(AVFormatContext *s)
{
    GXFContext *gxf = s->priv_data;
    AVIOContext *pb = s->pb;
    int64_t pos;

    pos = avio_tell(pb);
    gxf->umf_media_offset = pos - gxf->umf_start_offset;
    for (unsigned i = 0; i <= s->nb_streams; ++i) {
        GXFStreamContext *sc;
        int64_t startpos, curpos;

        if (i == s->nb_streams)
            sc = &gxf->timecode_track;
        else
            sc = s->streams[i]->priv_data;

        startpos = avio_tell(pb);
        avio_wl16(pb, 0); /* length */
        avio_wl16(pb, sc->media_info);
        avio_wl16(pb, 0); /* reserved */
        avio_wl16(pb, 0); /* reserved */
        avio_wl32(pb, gxf->nb_fields);
        avio_wl32(pb, 0); /* attributes rw, ro */
        avio_wl32(pb, 0); /* mark in */
        avio_wl32(pb, gxf->nb_fields); /* mark out */
        avio_write(pb, ES_NAME_PATTERN, strlen(ES_NAME_PATTERN));
        avio_wb16(pb, sc->media_info);
        ffio_fill(pb, 0, 88 - (strlen(ES_NAME_PATTERN) + 2));
        avio_wl32(pb, sc->track_type);
        avio_wl32(pb, sc->sample_rate);
        avio_wl32(pb, sc->sample_size);
        avio_wl32(pb, 0); /* reserved */

        if (sc == &gxf->timecode_track)
            gxf_write_umf_media_timecode(pb, gxf->tc.drop);
        else {
            AVStream *st = s->streams[i];
            switch (st->codecpar->codec_id) {
            case AV_CODEC_ID_MPEG1VIDEO:
            case AV_CODEC_ID_MPEG2VIDEO:
                gxf_write_umf_media_mpeg(pb, st);
                break;
            case AV_CODEC_ID_PCM_S16LE:
                gxf_write_umf_media_audio(pb, sc);
                break;
            case AV_CODEC_ID_DVVIDEO:
                gxf_write_umf_media_dv(pb, sc, st);
                break;
            }
        }

        curpos = avio_tell(pb);
        avio_seek(pb, startpos, SEEK_SET);
        avio_wl16(pb, curpos - startpos);
        avio_seek(pb, curpos, SEEK_SET);
    }
    return avio_tell(pb) - pos;
}

static int gxf_write_umf_packet(AVFormatContext *s)
{
    GXFContext *gxf = s->priv_data;
    AVIOContext *pb = s->pb;
    int64_t pos = avio_tell(pb);

    gxf_write_packet_header(pb, PKT_UMF);

    /* preamble */
    avio_w8(pb, 3); /* first and last (only) packet */
    avio_wb32(pb, gxf->umf_length); /* data length */

    gxf->umf_start_offset = avio_tell(pb);
    gxf_write_umf_payload(s);
    gxf_write_umf_material_description(s);
    gxf->umf_track_size = gxf_write_umf_track_description(s);
    gxf->umf_media_size = gxf_write_umf_media_description(s);
    gxf->umf_length = avio_tell(pb) - gxf->umf_start_offset;
    return updatePacketSize(pb, pos);
}

static void gxf_init_timecode_track(GXFStreamContext *sc, GXFStreamContext *vsc)
{
    if (!vsc)
        return;

    sc->media_type = vsc->sample_rate == 60 ? 7 : 8;
    sc->sample_rate = vsc->sample_rate;
    sc->media_info = ('T'<<8) | '0';
    sc->track_type = 3;
    sc->frame_rate_index = vsc->frame_rate_index;
    sc->lines_index = vsc->lines_index;
    sc->sample_size = 16;
    sc->fields = vsc->fields;
}

static int gxf_init_timecode(AVFormatContext *s, GXFTimecode *tc, const char *tcstr, int fields)
{
    char c;

    if (sscanf(tcstr, "%d:%d:%d%c%d", &tc->hh, &tc->mm, &tc->ss, &c, &tc->ff) != 5) {
        av_log(s, AV_LOG_ERROR, "unable to parse timecode, "
                                "syntax: hh:mm:ss[:;.]ff\n");
        return -1;
    }

    tc->color = 0;
    tc->drop = c != ':';

    if (fields == 2)
        tc->ff = tc->ff * 2;

    return 0;
}

static int gxf_write_header(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    GXFContext *gxf = s->priv_data;
    GXFStreamContext *vsc = NULL;
    uint8_t tracks[255] = {0};
    int i, media_info = 0;
    int ret;
    AVDictionaryEntry *tcr = av_dict_get(s->metadata, "timecode", NULL, 0);

    if (!(pb->seekable & AVIO_SEEKABLE_NORMAL)) {
        av_log(s, AV_LOG_ERROR, "gxf muxer does not support streamed output, patch welcome\n");
        return -1;
    }

    gxf->flags |= 0x00080000; /* material is simple clip */
    for (i = 0; i < s->nb_streams; ++i) {
        AVStream *st = s->streams[i];
        GXFStreamContext *sc = av_mallocz(sizeof(*sc));
        if (!sc)
            return AVERROR(ENOMEM);
        st->priv_data = sc;

        sc->media_type = ff_codec_get_tag(gxf_media_types, st->codecpar->codec_id);
        if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (st->codecpar->codec_id != AV_CODEC_ID_PCM_S16LE) {
                av_log(s, AV_LOG_ERROR, "only 16 BIT PCM LE allowed for now\n");
                return -1;
            }
            if (st->codecpar->sample_rate != 48000) {
                av_log(s, AV_LOG_ERROR, "only 48000hz sampling rate is allowed\n");
                return -1;
            }
            if (st->codecpar->ch_layout.nb_channels != 1) {
                av_log(s, AV_LOG_ERROR, "only mono tracks are allowed\n");
                return -1;
            }
            ret = ff_stream_add_bitstream_filter(st, "pcm_rechunk", "n="AV_STRINGIFY(GXF_SAMPLES_PER_FRAME));
            if (ret < 0)
                return ret;
            sc->track_type = 2;
            sc->sample_rate = st->codecpar->sample_rate;
            avpriv_set_pts_info(st, 64, 1, sc->sample_rate);
            sc->sample_size = 16;
            sc->frame_rate_index = -2;
            sc->lines_index = -2;
            sc->fields = -2;
            gxf->audio_tracks++;
            gxf->flags |= 0x04000000; /* audio is 16 bit pcm */
            media_info = 'A';
        } else if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            if (i != 0) {
                av_log(s, AV_LOG_ERROR, "video stream must be the first track\n");
                return -1;
            }
            /* FIXME check from time_base ? */
            if (st->codecpar->height == 480 || st->codecpar->height == 512) { /* NTSC or NTSC+VBI */
                sc->frame_rate_index = 5;
                sc->sample_rate = 60;
                gxf->flags |= 0x00000080;
                gxf->time_base = (AVRational){ 1001, 60000 };
            } else if (st->codecpar->height == 576 || st->codecpar->height == 608) { /* PAL or PAL+VBI */
                sc->frame_rate_index = 6;
                sc->media_type++;
                sc->sample_rate = 50;
                gxf->flags |= 0x00000040;
                gxf->time_base = (AVRational){ 1, 50 };
            } else {
                av_log(s, AV_LOG_ERROR, "unsupported video resolution, "
                       "gxf muxer only accepts PAL or NTSC resolutions currently\n");
                return -1;
            }
            if (!tcr)
                tcr = av_dict_get(st->metadata, "timecode", NULL, 0);
            avpriv_set_pts_info(st, 64, gxf->time_base.num, gxf->time_base.den);
            if (gxf_find_lines_index(st) < 0)
                sc->lines_index = -1;
            sc->sample_size = st->codecpar->bit_rate;
            sc->fields = 2; /* interlaced */

            vsc = sc;

            switch (st->codecpar->codec_id) {
            case AV_CODEC_ID_MJPEG:
                sc->track_type = 1;
                gxf->flags |= 0x00004000;
                media_info = 'J';
                break;
            case AV_CODEC_ID_MPEG1VIDEO:
                sc->track_type = 9;
                gxf->mpeg_tracks++;
                media_info = 'L';
                break;
            case AV_CODEC_ID_MPEG2VIDEO:
                sc->first_gop_closed = -1;
                sc->track_type = 4;
                gxf->mpeg_tracks++;
                gxf->flags |= 0x00008000;
                media_info = 'M';
                break;
            case AV_CODEC_ID_DVVIDEO:
                if (st->codecpar->format == AV_PIX_FMT_YUV422P) {
                    sc->media_type += 2;
                    sc->track_type = 6;
                    gxf->flags |= 0x00002000;
                    media_info = 'E';
                } else {
                    sc->track_type = 5;
                    gxf->flags |= 0x00001000;
                    media_info = 'D';
                }
                break;
            default:
                av_log(s, AV_LOG_ERROR, "video codec not supported\n");
                return -1;
            }
        }
        /* FIXME first 10 audio tracks are 0 to 9 next 22 are A to V */
        sc->media_info = media_info<<8 | ('0'+tracks[media_info]++);
        sc->order = s->nb_streams - st->index;
    }

    if (tcr && vsc)
        gxf_init_timecode(s, &gxf->tc, tcr->value, vsc->fields);

    gxf_init_timecode_track(&gxf->timecode_track, vsc);
    gxf->flags |= 0x200000; // time code track is non-drop frame

    if ((ret = gxf_write_map_packet(s, 0)) < 0)
        return ret;
    gxf_write_flt_packet(s);
    gxf_write_umf_packet(s);

    gxf->packet_count = 3;

    return 0;
}

static int gxf_write_eos_packet(AVIOContext *pb)
{
    int64_t pos = avio_tell(pb);

    gxf_write_packet_header(pb, PKT_EOS);
    return updatePacketSize(pb, pos);
}

static int gxf_write_trailer(AVFormatContext *s)
{
    GXFContext *gxf = s->priv_data;
    AVIOContext *pb = s->pb;
    int64_t end;
    int i;
    int ret;

    gxf_write_eos_packet(pb);
    end = avio_tell(pb);
    avio_seek(pb, 0, SEEK_SET);
    /* overwrite map, flt and umf packets with new values */
    if ((ret = gxf_write_map_packet(s, 1)) < 0)
        return ret;
    gxf_write_flt_packet(s);
    gxf_write_umf_packet(s);
    /* update duration in all map packets */
    for (i = 1; i < gxf->map_offsets_nb; i++) {
        avio_seek(pb, gxf->map_offsets[i], SEEK_SET);
        if ((ret = gxf_write_map_packet(s, 1)) < 0)
            return ret;
    }

    avio_seek(pb, end, SEEK_SET);

    return 0;
}

static void gxf_deinit(AVFormatContext *s)
{
    GXFContext *gxf = s->priv_data;

    av_freep(&gxf->flt_entries);
    av_freep(&gxf->map_offsets);
}

static int gxf_parse_mpeg_frame(GXFStreamContext *sc, const uint8_t *buf, int size)
{
    uint32_t c=-1;
    int i;
    for(i=0; i<size-4 && c!=0x100; i++){
        c = (c<<8) + buf[i];
        if(c == 0x1B8 && sc->first_gop_closed == -1) /* GOP start code */
            sc->first_gop_closed= (buf[i+4]>>6)&1;
    }
    return (buf[i+1]>>3)&7;
}

static int gxf_write_media_preamble(AVFormatContext *s, AVPacket *pkt, int size)
{
    GXFContext *gxf = s->priv_data;
    AVIOContext *pb = s->pb;
    AVStream *st = s->streams[pkt->stream_index];
    GXFStreamContext *sc = st->priv_data;
    unsigned field_nb;
    /* If the video is frame-encoded, the frame numbers shall be represented by
     * even field numbers.
     * see SMPTE360M-2004  6.4.2.1.3 Media field number */
    if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        field_nb = gxf->nb_fields;
    } else {
        field_nb = av_rescale_rnd(pkt->dts, gxf->time_base.den,
                                  (int64_t)48000*gxf->time_base.num, AV_ROUND_UP);
    }

    avio_w8(pb, sc->media_type);
    avio_w8(pb, st->index);
    avio_wb32(pb, field_nb);
    if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
        avio_wb16(pb, 0);
        avio_wb16(pb, size / 2);
    } else if (st->codecpar->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
        int frame_type = gxf_parse_mpeg_frame(sc, pkt->data, pkt->size);
        if (frame_type == AV_PICTURE_TYPE_I) {
            avio_w8(pb, 0x0d);
            sc->iframes++;
        } else if (frame_type == AV_PICTURE_TYPE_B) {
            avio_w8(pb, 0x0f);
            sc->bframes++;
        } else {
            avio_w8(pb, 0x0e);
            sc->pframes++;
        }
        avio_wb24(pb, size);
    } else if (st->codecpar->codec_id == AV_CODEC_ID_DVVIDEO) {
        avio_w8(pb, size / 4096);
        avio_wb24(pb, 0);
    } else
        avio_wb32(pb, size);
    avio_wb32(pb, field_nb);
    avio_w8(pb, 1); /* flags */
    avio_w8(pb, 0); /* reserved */
    return 16;
}

static int gxf_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    GXFContext *gxf = s->priv_data;
    AVIOContext *pb = s->pb;
    AVStream *st = s->streams[pkt->stream_index];
    int64_t pos = avio_tell(pb);
    int padding = 0;
    unsigned packet_start_offset = avio_tell(pb) / 1024;
    int ret;

    gxf_write_packet_header(pb, PKT_MEDIA);
    if (st->codecpar->codec_id == AV_CODEC_ID_MPEG2VIDEO && pkt->size % 4) /* MPEG-2 frames must be padded */
        padding = 4 - pkt->size % 4;
    else if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        padding = GXF_AUDIO_PACKET_SIZE - pkt->size;
    gxf_write_media_preamble(s, pkt, pkt->size + padding);
    avio_write(pb, pkt->data, pkt->size);
    gxf_write_padding(pb, padding);

    if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        if (!(gxf->flt_entries_nb % 500)) {
            int err;
            if ((err = av_reallocp_array(&gxf->flt_entries,
                                         gxf->flt_entries_nb + 500,
                                         sizeof(*gxf->flt_entries))) < 0) {
                gxf->flt_entries_nb = 0;
                gxf->nb_fields = 0;
                av_log(s, AV_LOG_ERROR, "could not reallocate flt entries\n");
                return err;
            }
        }
        gxf->flt_entries[gxf->flt_entries_nb++] = packet_start_offset;
        gxf->nb_fields += 2; // count fields
    }

    updatePacketSize(pb, pos);

    gxf->packet_count++;
    if (gxf->packet_count == 100) {
        if ((ret = gxf_write_map_packet(s, 0)) < 0)
            return ret;
        gxf->packet_count = 0;
    }

    return 0;
}

static int gxf_compare_field_nb(AVFormatContext *s, const AVPacket *next,
                                                    const AVPacket *cur)
{
    GXFContext *gxf = s->priv_data;
    const AVPacket *pkt[2] = { cur, next };
    int i, field_nb[2];
    GXFStreamContext *sc[2];

    for (i = 0; i < 2; i++) {
        AVStream *st = s->streams[pkt[i]->stream_index];
        sc[i] = st->priv_data;
        if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            field_nb[i] = av_rescale_rnd(pkt[i]->dts, gxf->time_base.den,
                                         (int64_t)48000*gxf->time_base.num, AV_ROUND_UP);
            field_nb[i] &= ~1; // compare against even field number because audio must be before video
        } else
            field_nb[i] = pkt[i]->dts; // dts are field based
    }

    return field_nb[1] > field_nb[0] ||
        (field_nb[1] == field_nb[0] && sc[1]->order > sc[0]->order);
}

static int gxf_interleave_packet(AVFormatContext *s, AVPacket *pkt,
                                 int flush, int has_packet)
{
    int ret;
    if (has_packet) {
        AVStream *st = s->streams[pkt->stream_index];
        GXFStreamContext *sc = st->priv_data;
        if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
            pkt->pts = pkt->dts = sc->pkt_cnt * 2; // enforce 2 fields
        else
            pkt->pts = pkt->dts = sc->pkt_cnt * GXF_SAMPLES_PER_FRAME;
        sc->pkt_cnt++;
        if ((ret = ff_interleave_add_packet(s, pkt, gxf_compare_field_nb)) < 0)
            return ret;
    }
    return ff_interleave_packet_per_dts(s, pkt, flush, 0);
}

const AVOutputFormat ff_gxf_muxer = {
    .name              = "gxf",
    .long_name         = NULL_IF_CONFIG_SMALL("GXF (General eXchange Format)"),
    .extensions        = "gxf",
    .priv_data_size    = sizeof(GXFContext),
    .audio_codec       = AV_CODEC_ID_PCM_S16LE,
    .video_codec       = AV_CODEC_ID_MPEG2VIDEO,
    .write_header      = gxf_write_header,
    .write_packet      = gxf_write_packet,
    .write_trailer     = gxf_write_trailer,
    .deinit            = gxf_deinit,
    .interleave_packet = gxf_interleave_packet,
};
