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

#include "avformat.h"
#include "gxf.h"
#include "riff.h"
#include "audiointerleave.h"

#define GXF_AUDIO_PACKET_SIZE 65536

typedef struct GXFStreamContext {
    AudioInterleaveContext aic;
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
    int b_per_i_or_p; ///< number of B frames per I frame or P frame
    int first_gop_closed;
    unsigned order;   ///< interleaving order
} GXFStreamContext;

typedef struct GXFContext {
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
    { CODEC_ID_MJPEG     ,   3 }, /* NTSC */
    { CODEC_ID_MJPEG     ,   4 }, /* PAL */
    { CODEC_ID_PCM_S24LE ,   9 },
    { CODEC_ID_PCM_S16LE ,  10 },
    { CODEC_ID_MPEG2VIDEO,  11 }, /* NTSC */
    { CODEC_ID_MPEG2VIDEO,  12 }, /* PAL */
    { CODEC_ID_DVVIDEO   ,  13 }, /* NTSC */
    { CODEC_ID_DVVIDEO   ,  14 }, /* PAL */
    { CODEC_ID_DVVIDEO   ,  15 }, /* 50M NTSC */
    { CODEC_ID_DVVIDEO   ,  16 }, /* 50M PAL */
    { CODEC_ID_AC3       ,  17 },
    //{ CODEC_ID_NONE,  ,   18 }, /* Non compressed 24 bit audio */
    { CODEC_ID_MPEG2VIDEO,  20 }, /* MPEG HD */
    { CODEC_ID_MPEG1VIDEO,  22 }, /* NTSC */
    { CODEC_ID_MPEG1VIDEO,  23 }, /* PAL */
    { CODEC_ID_NONE,         0 },
};

#define SERVER_PATH "EXT:/PDR/default/"
#define ES_NAME_PATTERN "EXT:/PDR/default/ES."

static int gxf_find_lines_index(AVStream *st)
{
    GXFStreamContext *sc = st->priv_data;
    int i;

    for (i = 0; i < 6; ++i) {
        if (st->codec->height == gxf_lines_tab[i].height) {
            sc->lines_index = gxf_lines_tab[i].index;
            return 0;
        }
    }
    return -1;
}

static void gxf_write_padding(ByteIOContext *pb, int64_t to_pad)
{
    for (; to_pad > 0; to_pad--) {
        put_byte(pb, 0);
    }
}

static int64_t updatePacketSize(ByteIOContext *pb, int64_t pos)
{
    int64_t curpos;
    int size;

    size = url_ftell(pb) - pos;
    if (size % 4) {
        gxf_write_padding(pb, 4 - size % 4);
        size = url_ftell(pb) - pos;
    }
    curpos = url_ftell(pb);
    url_fseek(pb, pos + 6, SEEK_SET);
    put_be32(pb, size);
    url_fseek(pb, curpos, SEEK_SET);
    return curpos - pos;
}

static int64_t updateSize(ByteIOContext *pb, int64_t pos)
{
    int64_t curpos;

    curpos = url_ftell(pb);
    url_fseek(pb, pos, SEEK_SET);
    put_be16(pb, curpos - pos - 2);
    url_fseek(pb, curpos, SEEK_SET);
    return curpos - pos;
}

static void gxf_write_packet_header(ByteIOContext *pb, GXFPktType type)
{
    put_be32(pb, 0); /* packet leader for synchro */
    put_byte(pb, 1);
    put_byte(pb, type); /* map packet */
    put_be32(pb, 0); /* size */
    put_be32(pb, 0); /* reserved */
    put_byte(pb, 0xE1); /* trailer 1 */
    put_byte(pb, 0xE2); /* trailer 2 */
}

static int gxf_write_mpeg_auxiliary(ByteIOContext *pb, AVStream *st)
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
    if (st->codec->height == 512 || st->codec->height == 608)
        starting_line = 7; // VBI
    else if (st->codec->height == 480)
        starting_line = 20;
    else
        starting_line = 23; // default PAL

    size = snprintf(buffer, 1024, "Ver 1\nBr %.6f\nIpg 1\nPpi %d\nBpiop %d\n"
                    "Pix 0\nCf %d\nCg %d\nSl %d\nnl16 %d\nVi 1\nf1 1\n",
                    (float)st->codec->bit_rate, sc->p_per_gop, sc->b_per_i_or_p,
                    st->codec->pix_fmt == PIX_FMT_YUV422P ? 2 : 1, sc->first_gop_closed == 1,
                    starting_line, (st->codec->height + 15) / 16);
    put_byte(pb, TRACK_MPG_AUX);
    put_byte(pb, size + 1);
    put_buffer(pb, (uint8_t *)buffer, size + 1);
    return size + 3;
}

static int gxf_write_timecode_auxiliary(ByteIOContext *pb, GXFStreamContext *sc)
{
    put_byte(pb, 0); /* fields */
    put_byte(pb, 0);  /* seconds */
    put_byte(pb, 0); /* minutes */
    put_byte(pb, 0); /* flags + hours */
    /* reserved */
    put_be32(pb, 0);
    return 8;
}

static int gxf_write_track_description(AVFormatContext *s, GXFStreamContext *sc, int index)
{
    ByteIOContext *pb = s->pb;
    int64_t pos;
    int mpeg = sc->track_type == 4 || sc->track_type == 9;

    /* track description section */
    put_byte(pb, sc->media_type + 0x80);
    put_byte(pb, index + 0xC0);

    pos = url_ftell(pb);
    put_be16(pb, 0); /* size */

    /* media file name */
    put_byte(pb, TRACK_NAME);
    put_byte(pb, strlen(ES_NAME_PATTERN) + 3);
    put_tag(pb, ES_NAME_PATTERN);
    put_be16(pb, sc->media_info);
    put_byte(pb, 0);

    if (!mpeg) {
        /* auxiliary information */
        put_byte(pb, TRACK_AUX);
        put_byte(pb, 8);
        if (sc->track_type == 3)
            gxf_write_timecode_auxiliary(pb, sc);
        else
            put_le64(pb, 0);
    }

    /* file system version */
    put_byte(pb, TRACK_VER);
    put_byte(pb, 4);
    put_be32(pb, 0);

    if (mpeg)
        gxf_write_mpeg_auxiliary(pb, s->streams[index]);

    /* frame rate */
    put_byte(pb, TRACK_FPS);
    put_byte(pb, 4);
    put_be32(pb, sc->frame_rate_index);

    /* lines per frame */
    put_byte(pb, TRACK_LINES);
    put_byte(pb, 4);
    put_be32(pb, sc->lines_index);

    /* fields per frame */
    put_byte(pb, TRACK_FPF);
    put_byte(pb, 4);
    put_be32(pb, sc->fields);

    return updateSize(pb, pos);
}

static int gxf_write_material_data_section(AVFormatContext *s)
{
    GXFContext *gxf = s->priv_data;
    ByteIOContext *pb = s->pb;
    int64_t pos;
    const char *filename = strrchr(s->filename, '/');

    pos = url_ftell(pb);
    put_be16(pb, 0); /* size */

    /* name */
    if (filename)
        filename++;
    else
        filename = s->filename;
    put_byte(pb, MAT_NAME);
    put_byte(pb, strlen(SERVER_PATH) + strlen(filename) + 1);
    put_tag(pb, SERVER_PATH);
    put_tag(pb, filename);
    put_byte(pb, 0);

    /* first field */
    put_byte(pb, MAT_FIRST_FIELD);
    put_byte(pb, 4);
    put_be32(pb, 0);

    /* last field */
    put_byte(pb, MAT_LAST_FIELD);
    put_byte(pb, 4);
    put_be32(pb, gxf->nb_fields);

    /* reserved */
    put_byte(pb, MAT_MARK_IN);
    put_byte(pb, 4);
    put_be32(pb, 0);

    put_byte(pb, MAT_MARK_OUT);
    put_byte(pb, 4);
    put_be32(pb, gxf->nb_fields);

    /* estimated size */
    put_byte(pb, MAT_SIZE);
    put_byte(pb, 4);
    put_be32(pb, url_fsize(pb) / 1024);

    return updateSize(pb, pos);
}

static int gxf_write_track_description_section(AVFormatContext *s)
{
    GXFContext *gxf = s->priv_data;
    ByteIOContext *pb = s->pb;
    int64_t pos;
    int i;

    pos = url_ftell(pb);
    put_be16(pb, 0); /* size */
    for (i = 0; i < s->nb_streams; ++i)
        gxf_write_track_description(s, s->streams[i]->priv_data, i);

    gxf_write_track_description(s, &gxf->timecode_track, s->nb_streams);

    return updateSize(pb, pos);
}

static int gxf_write_map_packet(AVFormatContext *s, int rewrite)
{
    GXFContext *gxf = s->priv_data;
    ByteIOContext *pb = s->pb;
    int64_t pos = url_ftell(pb);

    if (!rewrite) {
        if (!(gxf->map_offsets_nb % 30)) {
            gxf->map_offsets = av_realloc(gxf->map_offsets,
                                          (gxf->map_offsets_nb+30)*sizeof(*gxf->map_offsets));
            if (!gxf->map_offsets) {
                av_log(s, AV_LOG_ERROR, "could not realloc map offsets\n");
                return -1;
            }
        }
        gxf->map_offsets[gxf->map_offsets_nb++] = pos; // do not increment here
    }

    gxf_write_packet_header(pb, PKT_MAP);

    /* preamble */
    put_byte(pb, 0xE0); /* version */
    put_byte(pb, 0xFF); /* reserved */

    gxf_write_material_data_section(s);
    gxf_write_track_description_section(s);

    return updatePacketSize(pb, pos);
}

static int gxf_write_flt_packet(AVFormatContext *s)
{
    GXFContext *gxf = s->priv_data;
    ByteIOContext *pb = s->pb;
    int64_t pos = url_ftell(pb);
    int fields_per_flt = (gxf->nb_fields+1) / 1000 + 1;
    int flt_entries = gxf->nb_fields / fields_per_flt;
    int i = 0;

    gxf_write_packet_header(pb, PKT_FLT);

    put_le32(pb, fields_per_flt); /* number of fields */
    put_le32(pb, flt_entries); /* number of active flt entries */

    if (gxf->flt_entries) {
        for (i = 0; i < flt_entries; i++)
            put_le32(pb, gxf->flt_entries[(i*fields_per_flt)>>1]);
    }

    for (; i < 1000; i++)
        put_le32(pb, 0);

    return updatePacketSize(pb, pos);
}

static int gxf_write_umf_material_description(AVFormatContext *s)
{
    GXFContext *gxf = s->priv_data;
    ByteIOContext *pb = s->pb;
    int timecode_base = gxf->time_base.den == 60000 ? 60 : 50;

    // XXX drop frame
    uint32_t timecode =
        gxf->nb_fields / (timecode_base * 3600) % 24 << 24 | // hours
        gxf->nb_fields / (timecode_base * 60) % 60   << 16 | // minutes
        gxf->nb_fields /  timecode_base % 60         <<  8 | // seconds
        gxf->nb_fields %  timecode_base;                     // fields

    put_le32(pb, gxf->flags);
    put_le32(pb, gxf->nb_fields); /* length of the longest track */
    put_le32(pb, gxf->nb_fields); /* length of the shortest track */
    put_le32(pb, 0); /* mark in */
    put_le32(pb, gxf->nb_fields); /* mark out */
    put_le32(pb, 0); /* timecode mark in */
    put_le32(pb, timecode); /* timecode mark out */
    put_le64(pb, s->timestamp); /* modification time */
    put_le64(pb, s->timestamp); /* creation time */
    put_le16(pb, 0); /* reserved */
    put_le16(pb, 0); /* reserved */
    put_le16(pb, gxf->audio_tracks);
    put_le16(pb, 1); /* timecode track count */
    put_le16(pb, 0); /* reserved */
    put_le16(pb, gxf->mpeg_tracks);
    return 48;
}

static int gxf_write_umf_payload(AVFormatContext *s)
{
    GXFContext *gxf = s->priv_data;
    ByteIOContext *pb = s->pb;

    put_le32(pb, gxf->umf_length); /* total length of the umf data */
    put_le32(pb, 3); /* version */
    put_le32(pb, s->nb_streams+1);
    put_le32(pb, gxf->umf_track_offset); /* umf track section offset */
    put_le32(pb, gxf->umf_track_size);
    put_le32(pb, s->nb_streams+1);
    put_le32(pb, gxf->umf_media_offset);
    put_le32(pb, gxf->umf_media_size);
    put_le32(pb, gxf->umf_length); /* user data offset */
    put_le32(pb, 0); /* user data size */
    put_le32(pb, 0); /* reserved */
    put_le32(pb, 0); /* reserved */
    return 48;
}

static int gxf_write_umf_track_description(AVFormatContext *s)
{
    ByteIOContext *pb = s->pb;
    GXFContext *gxf = s->priv_data;
    int64_t pos = url_ftell(pb);
    int i;

    gxf->umf_track_offset = pos - gxf->umf_start_offset;
    for (i = 0; i < s->nb_streams; ++i) {
        GXFStreamContext *sc = s->streams[i]->priv_data;
        put_le16(pb, sc->media_info);
        put_le16(pb, 1);
    }

    put_le16(pb, gxf->timecode_track.media_info);
    put_le16(pb, 1);

    return url_ftell(pb) - pos;
}

static int gxf_write_umf_media_mpeg(ByteIOContext *pb, AVStream *st)
{
    GXFStreamContext *sc = st->priv_data;

    if (st->codec->pix_fmt == PIX_FMT_YUV422P)
        put_le32(pb, 2);
    else
        put_le32(pb, 1); /* default to 420 */
    put_le32(pb, sc->first_gop_closed == 1); /* closed = 1, open = 0, unknown = 255 */
    put_le32(pb, 3); /* top = 1, bottom = 2, frame = 3, unknown = 0 */
    put_le32(pb, 1); /* I picture per GOP */
    put_le32(pb, sc->p_per_gop);
    put_le32(pb, sc->b_per_i_or_p);
    if (st->codec->codec_id == CODEC_ID_MPEG2VIDEO)
        put_le32(pb, 2);
    else if (st->codec->codec_id == CODEC_ID_MPEG1VIDEO)
        put_le32(pb, 1);
    else
        put_le32(pb, 0);
    put_le32(pb, 0); /* reserved */
    return 32;
}

static int gxf_write_umf_media_timecode(ByteIOContext *pb, GXFStreamContext *sc)
{
    put_le32(pb, 1); /* non drop frame */
    put_le32(pb, 0); /* reserved */
    put_le32(pb, 0); /* reserved */
    put_le32(pb, 0); /* reserved */
    put_le32(pb, 0); /* reserved */
    put_le32(pb, 0); /* reserved */
    put_le32(pb, 0); /* reserved */
    put_le32(pb, 0); /* reserved */
    return 32;
}

static int gxf_write_umf_media_dv(ByteIOContext *pb, GXFStreamContext *sc)
{
    int i;

    for (i = 0; i < 8; i++) {
        put_be32(pb, 0);
    }
    return 32;
}

static int gxf_write_umf_media_audio(ByteIOContext *pb, GXFStreamContext *sc)
{
    put_le64(pb, av_dbl2int(1)); /* sound level to begin to */
    put_le64(pb, av_dbl2int(1)); /* sound level to begin to */
    put_le32(pb, 0); /* number of fields over which to ramp up sound level */
    put_le32(pb, 0); /* number of fields over which to ramp down sound level */
    put_le32(pb, 0); /* reserved */
    put_le32(pb, 0); /* reserved */
    return 32;
}

#if 0
static int gxf_write_umf_media_mjpeg(ByteIOContext *pb, GXFStreamContext *sc)
{
    put_be64(pb, 0); /* FIXME FLOAT max chroma quant level */
    put_be64(pb, 0); /* FIXME FLOAT max luma quant level */
    put_be64(pb, 0); /* FIXME FLOAT min chroma quant level */
    put_be64(pb, 0); /* FIXME FLOAT min luma quant level */
    return 32;
}
#endif

static int gxf_write_umf_media_description(AVFormatContext *s)
{
    GXFContext *gxf = s->priv_data;
    ByteIOContext *pb = s->pb;
    int64_t pos;
    int i, j;

    pos = url_ftell(pb);
    gxf->umf_media_offset = pos - gxf->umf_start_offset;
    for (i = 0; i <= s->nb_streams; ++i) {
        GXFStreamContext *sc;
        int64_t startpos, curpos;

        if (i == s->nb_streams)
            sc = &gxf->timecode_track;
        else
            sc = s->streams[i]->priv_data;

        startpos = url_ftell(pb);
        put_le16(pb, 0); /* length */
        put_le16(pb, sc->media_info);
        put_le16(pb, 0); /* reserved */
        put_le16(pb, 0); /* reserved */
        put_le32(pb, gxf->nb_fields);
        put_le32(pb, 0); /* attributes rw, ro */
        put_le32(pb, 0); /* mark in */
        put_le32(pb, gxf->nb_fields); /* mark out */
        put_buffer(pb, ES_NAME_PATTERN, strlen(ES_NAME_PATTERN));
        put_be16(pb, sc->media_info);
        for (j = strlen(ES_NAME_PATTERN)+2; j < 88; j++)
            put_byte(pb, 0);
        put_le32(pb, sc->track_type);
        put_le32(pb, sc->sample_rate);
        put_le32(pb, sc->sample_size);
        put_le32(pb, 0); /* reserved */

        if (sc == &gxf->timecode_track)
            gxf_write_umf_media_timecode(pb, sc); /* 8 0bytes */
        else {
            AVStream *st = s->streams[i];
            switch (st->codec->codec_id) {
            case CODEC_ID_MPEG1VIDEO:
            case CODEC_ID_MPEG2VIDEO:
                gxf_write_umf_media_mpeg(pb, st);
                break;
            case CODEC_ID_PCM_S16LE:
                gxf_write_umf_media_audio(pb, sc);
                break;
            case CODEC_ID_DVVIDEO:
                gxf_write_umf_media_dv(pb, sc);
                break;
            }
        }

        curpos = url_ftell(pb);
        url_fseek(pb, startpos, SEEK_SET);
        put_le16(pb, curpos - startpos);
        url_fseek(pb, curpos, SEEK_SET);
    }
    return url_ftell(pb) - pos;
}

static int gxf_write_umf_packet(AVFormatContext *s)
{
    GXFContext *gxf = s->priv_data;
    ByteIOContext *pb = s->pb;
    int64_t pos = url_ftell(pb);

    gxf_write_packet_header(pb, PKT_UMF);

    /* preamble */
    put_byte(pb, 3); /* first and last (only) packet */
    put_be32(pb, gxf->umf_length); /* data length */

    gxf->umf_start_offset = url_ftell(pb);
    gxf_write_umf_payload(s);
    gxf_write_umf_material_description(s);
    gxf->umf_track_size = gxf_write_umf_track_description(s);
    gxf->umf_media_size = gxf_write_umf_media_description(s);
    gxf->umf_length = url_ftell(pb) - gxf->umf_start_offset;
    return updatePacketSize(pb, pos);
}

static const int GXF_samples_per_frame[] = { 32768, 0 };

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

static int gxf_write_header(AVFormatContext *s)
{
    ByteIOContext *pb = s->pb;
    GXFContext *gxf = s->priv_data;
    GXFStreamContext *vsc = NULL;
    uint8_t tracks[255] = {0};
    int i, media_info = 0;

    if (url_is_streamed(pb)) {
        av_log(s, AV_LOG_ERROR, "gxf muxer does not support streamed output, patch welcome");
        return -1;
    }

    gxf->flags |= 0x00080000; /* material is simple clip */
    for (i = 0; i < s->nb_streams; ++i) {
        AVStream *st = s->streams[i];
        GXFStreamContext *sc = av_mallocz(sizeof(*sc));
        if (!sc)
            return AVERROR(ENOMEM);
        st->priv_data = sc;

        sc->media_type = ff_codec_get_tag(gxf_media_types, st->codec->codec_id);
        if (st->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (st->codec->codec_id != CODEC_ID_PCM_S16LE) {
                av_log(s, AV_LOG_ERROR, "only 16 BIT PCM LE allowed for now\n");
                return -1;
            }
            if (st->codec->sample_rate != 48000) {
                av_log(s, AV_LOG_ERROR, "only 48000hz sampling rate is allowed\n");
                return -1;
            }
            if (st->codec->channels != 1) {
                av_log(s, AV_LOG_ERROR, "only mono tracks are allowed\n");
                return -1;
            }
            sc->track_type = 2;
            sc->sample_rate = st->codec->sample_rate;
            av_set_pts_info(st, 64, 1, sc->sample_rate);
            sc->sample_size = 16;
            sc->frame_rate_index = -2;
            sc->lines_index = -2;
            sc->fields = -2;
            gxf->audio_tracks++;
            gxf->flags |= 0x04000000; /* audio is 16 bit pcm */
            media_info = 'A';
        } else if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            if (i != 0) {
                av_log(s, AV_LOG_ERROR, "video stream must be the first track\n");
                return -1;
            }
            /* FIXME check from time_base ? */
            if (st->codec->height == 480 || st->codec->height == 512) { /* NTSC or NTSC+VBI */
                sc->frame_rate_index = 5;
                sc->sample_rate = 60;
                gxf->flags |= 0x00000080;
                gxf->time_base = (AVRational){ 1001, 60000 };
            } else if (st->codec->height == 576 || st->codec->height == 608) { /* PAL or PAL+VBI */
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
            av_set_pts_info(st, 64, gxf->time_base.num, gxf->time_base.den);
            if (gxf_find_lines_index(st) < 0)
                sc->lines_index = -1;
            sc->sample_size = st->codec->bit_rate;
            sc->fields = 2; /* interlaced */

            vsc = sc;

            switch (st->codec->codec_id) {
            case CODEC_ID_MJPEG:
                sc->track_type = 1;
                gxf->flags |= 0x00004000;
                media_info = 'J';
                break;
            case CODEC_ID_MPEG1VIDEO:
                sc->track_type = 9;
                gxf->mpeg_tracks++;
                media_info = 'L';
                break;
            case CODEC_ID_MPEG2VIDEO:
                sc->first_gop_closed = -1;
                sc->track_type = 4;
                gxf->mpeg_tracks++;
                gxf->flags |= 0x00008000;
                media_info = 'M';
                break;
            case CODEC_ID_DVVIDEO:
                if (st->codec->pix_fmt == PIX_FMT_YUV422P) {
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

    if (ff_audio_interleave_init(s, GXF_samples_per_frame, (AVRational){ 1, 48000 }) < 0)
        return -1;

    gxf_init_timecode_track(&gxf->timecode_track, vsc);
    gxf->flags |= 0x200000; // time code track is non-drop frame

    gxf_write_map_packet(s, 0);
    gxf_write_flt_packet(s);
    gxf_write_umf_packet(s);

    gxf->packet_count = 3;

    put_flush_packet(pb);
    return 0;
}

static int gxf_write_eos_packet(ByteIOContext *pb)
{
    int64_t pos = url_ftell(pb);

    gxf_write_packet_header(pb, PKT_EOS);
    return updatePacketSize(pb, pos);
}

static int gxf_write_trailer(AVFormatContext *s)
{
    GXFContext *gxf = s->priv_data;
    ByteIOContext *pb = s->pb;
    int64_t end;
    int i;

    ff_audio_interleave_close(s);

    gxf_write_eos_packet(pb);
    end = url_ftell(pb);
    url_fseek(pb, 0, SEEK_SET);
    /* overwrite map, flt and umf packets with new values */
    gxf_write_map_packet(s, 1);
    gxf_write_flt_packet(s);
    gxf_write_umf_packet(s);
    put_flush_packet(pb);
    /* update duration in all map packets */
    for (i = 1; i < gxf->map_offsets_nb; i++) {
        url_fseek(pb, gxf->map_offsets[i], SEEK_SET);
        gxf_write_map_packet(s, 1);
        put_flush_packet(pb);
    }

    url_fseek(pb, end, SEEK_SET);

    av_freep(&gxf->flt_entries);
    av_freep(&gxf->map_offsets);

    return 0;
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
    ByteIOContext *pb = s->pb;
    AVStream *st = s->streams[pkt->stream_index];
    GXFStreamContext *sc = st->priv_data;
    unsigned field_nb;
    /* If the video is frame-encoded, the frame numbers shall be represented by
     * even field numbers.
     * see SMPTE360M-2004  6.4.2.1.3 Media field number */
    if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
        field_nb = gxf->nb_fields;
    } else {
        field_nb = av_rescale_rnd(pkt->dts, gxf->time_base.den,
                                  (int64_t)48000*gxf->time_base.num, AV_ROUND_UP);
    }

    put_byte(pb, sc->media_type);
    put_byte(pb, st->index);
    put_be32(pb, field_nb);
    if (st->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
        put_be16(pb, 0);
        put_be16(pb, size / 2);
    } else if (st->codec->codec_id == CODEC_ID_MPEG2VIDEO) {
        int frame_type = gxf_parse_mpeg_frame(sc, pkt->data, pkt->size);
        if (frame_type == FF_I_TYPE) {
            put_byte(pb, 0x0d);
            sc->iframes++;
        } else if (frame_type == FF_B_TYPE) {
            put_byte(pb, 0x0f);
            sc->bframes++;
        } else {
            put_byte(pb, 0x0e);
            sc->pframes++;
        }
        put_be24(pb, size);
    } else if (st->codec->codec_id == CODEC_ID_DVVIDEO) {
        put_byte(pb, size / 4096);
        put_be24(pb, 0);
    } else
        put_be32(pb, size);
    put_be32(pb, field_nb);
    put_byte(pb, 1); /* flags */
    put_byte(pb, 0); /* reserved */
    return 16;
}

static int gxf_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    GXFContext *gxf = s->priv_data;
    ByteIOContext *pb = s->pb;
    AVStream *st = s->streams[pkt->stream_index];
    int64_t pos = url_ftell(pb);
    int padding = 0;
    int packet_start_offset = url_ftell(pb) / 1024;

    gxf_write_packet_header(pb, PKT_MEDIA);
    if (st->codec->codec_id == CODEC_ID_MPEG2VIDEO && pkt->size % 4) /* MPEG-2 frames must be padded */
        padding = 4 - pkt->size % 4;
    else if (st->codec->codec_type == AVMEDIA_TYPE_AUDIO)
        padding = GXF_AUDIO_PACKET_SIZE - pkt->size;
    gxf_write_media_preamble(s, pkt, pkt->size + padding);
    put_buffer(pb, pkt->data, pkt->size);
    gxf_write_padding(pb, padding);

    if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
        if (!(gxf->flt_entries_nb % 500)) {
            gxf->flt_entries = av_realloc(gxf->flt_entries,
                                          (gxf->flt_entries_nb+500)*sizeof(*gxf->flt_entries));
            if (!gxf->flt_entries) {
                av_log(s, AV_LOG_ERROR, "could not reallocate flt entries\n");
                return -1;
            }
        }
        gxf->flt_entries[gxf->flt_entries_nb++] = packet_start_offset;
        gxf->nb_fields += 2; // count fields
    }

    updatePacketSize(pb, pos);

    gxf->packet_count++;
    if (gxf->packet_count == 100) {
        gxf_write_map_packet(s, 0);
        gxf->packet_count = 0;
    }

    put_flush_packet(pb);

    return 0;
}

static int gxf_compare_field_nb(AVFormatContext *s, AVPacket *next, AVPacket *cur)
{
    GXFContext *gxf = s->priv_data;
    AVPacket *pkt[2] = { cur, next };
    int i, field_nb[2];
    GXFStreamContext *sc[2];

    for (i = 0; i < 2; i++) {
        AVStream *st = s->streams[pkt[i]->stream_index];
        sc[i] = st->priv_data;
        if (st->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
            field_nb[i] = av_rescale_rnd(pkt[i]->dts, gxf->time_base.den,
                                         (int64_t)48000*gxf->time_base.num, AV_ROUND_UP);
            field_nb[i] &= ~1; // compare against even field number because audio must be before video
        } else
            field_nb[i] = pkt[i]->dts; // dts are field based
    }

    return field_nb[1] > field_nb[0] ||
        (field_nb[1] == field_nb[0] && sc[1]->order > sc[0]->order);
}

static int gxf_interleave_packet(AVFormatContext *s, AVPacket *out, AVPacket *pkt, int flush)
{
    if (pkt && s->streams[pkt->stream_index]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
        pkt->duration = 2; // enforce 2 fields
    return ff_audio_rechunk_interleave(s, out, pkt, flush,
                               av_interleave_packet_per_dts, gxf_compare_field_nb);
}

AVOutputFormat gxf_muxer = {
    "gxf",
    NULL_IF_CONFIG_SMALL("GXF format"),
    NULL,
    "gxf",
    sizeof(GXFContext),
    CODEC_ID_PCM_S16LE,
    CODEC_ID_MPEG2VIDEO,
    gxf_write_header,
    gxf_write_packet,
    gxf_write_trailer,
    0,
    NULL,
    gxf_interleave_packet,
};
