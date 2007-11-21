/*
 * GXF muxer.
 * Copyright (c) 2006 SmartJog S.A., Baptiste Coudurier <baptiste dot coudurier at smartjog dot com>.
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
#include "fifo.h"

#define GXF_AUDIO_PACKET_SIZE 65536

typedef struct GXFStreamContext {
    AVCodecContext *codec;
    AVFifoBuffer audio_buffer;
    uint32_t track_type;
    uint32_t sample_size;
    uint32_t sample_rate;
    uint16_t media_type;
    uint16_t media_info;
    uint8_t index;
    int frame_rate_index;
    int lines_index;
    int fields;
    int iframes;
    int pframes;
    int bframes;
    int p_per_gop;
    int b_per_gop;
    int first_gop_closed;
    int64_t current_dts;
    int dts_delay;
} GXFStreamContext;

typedef struct GXFContext {
    uint32_t nb_frames;
    uint32_t material_flags;
    uint16_t audio_tracks;
    uint16_t mpeg_tracks;
    int64_t creation_time;
    uint32_t umf_start_offset;
    uint32_t umf_track_offset;
    uint32_t umf_media_offset;
    uint32_t umf_user_data_offset;
    uint32_t umf_user_data_size;
    uint32_t umf_length;
    uint16_t umf_track_size;
    uint16_t umf_media_size;
    int audio_written;
    int sample_rate;
    int flags;
    AVFormatContext *fc;
    GXFStreamContext streams[48];
} GXFContext;

typedef struct GXF_Lines {
    int height;
    int index;
} GXF_Lines;


/* FIXME check if it is relevant */
static const GXF_Lines gxf_lines_tab[] = {
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
    { 0, 0 },
};

#define SERVER_PATH "/space/"
#define ES_NAME_PATTERN "ES."

static int gxf_find_lines_index(GXFStreamContext *ctx)
{
    int i;

    for (i = 0; i < 6; ++i) {
        if (ctx->codec->height == gxf_lines_tab[i].height) {
            ctx->lines_index = gxf_lines_tab[i].index;
            return 0;
        }
    }
    return -1;
}

static void gxf_write_padding(ByteIOContext *pb, offset_t to_pad)
{
    for (; to_pad > 0; to_pad--) {
        put_byte(pb, 0);
    }
}

static offset_t updatePacketSize(ByteIOContext *pb, offset_t pos)
{
    offset_t curpos;
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

static offset_t updateSize(ByteIOContext *pb, offset_t pos)
{
    offset_t curpos;

    curpos = url_ftell(pb);
    url_fseek(pb, pos, SEEK_SET);
    put_be16(pb, curpos - pos - 2);
    url_fseek(pb, curpos, SEEK_SET);
    return curpos - pos;
}

static void gxf_write_packet_header(ByteIOContext *pb, pkt_type_t type)
{
    put_be32(pb, 0); /* packet leader for synchro */
    put_byte(pb, 1);
    put_byte(pb, type); /* map packet */
    put_be32(pb, 0); /* size */
    put_be32(pb, 0); /* reserved */
    put_byte(pb, 0xE1); /* trailer 1 */
    put_byte(pb, 0xE2); /* trailer 2 */
}

static int gxf_write_mpeg_auxiliary(ByteIOContext *pb, GXFStreamContext *ctx)
{
    char buffer[1024];
    int size;

    if (ctx->iframes) {
        ctx->p_per_gop = ctx->pframes / ctx->iframes;
        if (ctx->pframes % ctx->iframes)
            ctx->p_per_gop++;
        if (ctx->pframes)
            ctx->b_per_gop = ctx->bframes / ctx->pframes;
        if (ctx->p_per_gop > 9)
            ctx->p_per_gop = 9; /* ensure value won't take more than one char */
        if (ctx->b_per_gop > 9)
            ctx->b_per_gop = 9; /* ensure value won't take more than one char */
    }
    size = snprintf(buffer, 1024, "Ver 1\nBr %.6f\nIpg 1\nPpi %d\nBpiop %d\n"
                    "Pix 0\nCf %d\nCg %d\nSl 7\nnl16 %d\nVi 1\nf1 1\n",
                    (float)ctx->codec->bit_rate, ctx->p_per_gop, ctx->b_per_gop,
                    ctx->codec->pix_fmt == PIX_FMT_YUV422P ? 2 : 1, ctx->first_gop_closed == 1,
                    ctx->codec->height / 16);
    put_byte(pb, TRACK_MPG_AUX);
    put_byte(pb, size + 1);
    put_buffer(pb, (uint8_t *)buffer, size + 1);
    return size + 3;
}

static int gxf_write_timecode_auxiliary(ByteIOContext *pb, GXFStreamContext *ctx)
{
    /* FIXME implement that */
    put_byte(pb, 0); /* fields */
    put_byte(pb, 0);  /* seconds */
    put_byte(pb, 0); /* minutes */
    put_byte(pb, 0); /* flags + hours */
    /* reserved */
    put_be32(pb, 0);
    return 8;
}

static int gxf_write_track_description(ByteIOContext *pb, GXFStreamContext *stream)
{
    offset_t pos;

    /* track description section */
    put_byte(pb, stream->media_type + 0x80);
    put_byte(pb, stream->index + 0xC0);

    pos = url_ftell(pb);
    put_be16(pb, 0); /* size */

    /* media file name */
    put_byte(pb, TRACK_NAME);
    put_byte(pb, strlen(ES_NAME_PATTERN) + 3);
    put_tag(pb, ES_NAME_PATTERN);
    put_be16(pb, stream->media_info);
    put_byte(pb, 0);

    if (stream->codec->codec_id != CODEC_ID_MPEG2VIDEO) {
        /* auxiliary information */
        put_byte(pb, TRACK_AUX);
        put_byte(pb, 8);
        if (stream->codec->codec_id == CODEC_ID_NONE)
            gxf_write_timecode_auxiliary(pb, stream);
        else
            put_le64(pb, 0);
    }

    /* file system version */
    put_byte(pb, TRACK_VER);
    put_byte(pb, 4);
    put_be32(pb, 0);

    if (stream->codec->codec_id == CODEC_ID_MPEG2VIDEO)
        gxf_write_mpeg_auxiliary(pb, stream);

    /* frame rate */
    put_byte(pb, TRACK_FPS);
    put_byte(pb, 4);
    put_be32(pb, stream->frame_rate_index);

    /* lines per frame */
    put_byte(pb, TRACK_LINES);
    put_byte(pb, 4);
    put_be32(pb, stream->lines_index);

    /* fields per frame */
    put_byte(pb, TRACK_FPF);
    put_byte(pb, 4);
    put_be32(pb, stream->fields);

    return updateSize(pb, pos);
}

static int gxf_write_material_data_section(ByteIOContext *pb, GXFContext *ctx)
{
    offset_t pos;
    const char *filename = strrchr(ctx->fc->filename, '/');

    pos = url_ftell(pb);
    put_be16(pb, 0); /* size */

    /* name */
    if (filename)
        filename++;
    else
        filename = ctx->fc->filename;
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
    put_be32(pb, ctx->nb_frames);

    /* reserved */
    put_byte(pb, MAT_MARK_IN);
    put_byte(pb, 4);
    put_be32(pb, 0);

    put_byte(pb, MAT_MARK_OUT);
    put_byte(pb, 4);
    put_be32(pb, ctx->nb_frames);

    /* estimated size */
    put_byte(pb, MAT_SIZE);
    put_byte(pb, 4);
    put_be32(pb, url_fsize(pb) / 1024);

    return updateSize(pb, pos);
}

static int gxf_write_track_description_section(ByteIOContext *pb, GXFContext *ctx)
{
    offset_t pos;
    int i;

    pos = url_ftell(pb);
    put_be16(pb, 0); /* size */
    for (i = 0; i < ctx->fc->nb_streams; ++i)
        gxf_write_track_description(pb, &ctx->streams[i]);
    return updateSize(pb, pos);
}

static int gxf_write_map_packet(ByteIOContext *pb, GXFContext *ctx)
{
    offset_t pos = url_ftell(pb);

    gxf_write_packet_header(pb, PKT_MAP);

    /* preamble */
    put_byte(pb, 0xE0); /* version */
    put_byte(pb, 0xFF); /* reserved */

    gxf_write_material_data_section(pb, ctx);
    gxf_write_track_description_section(pb, ctx);

    return updatePacketSize(pb, pos);
}

#if 0
static int gxf_write_flt_packet(ByteIOContext *pb, GXFContext *ctx)
{
    offset_t pos = url_ftell(pb);
    int i;

    gxf_write_packet_header(pb, PKT_FLT);

    put_le32(pb, 1000); /* number of fields */
    put_le32(pb, 0); /* number of active flt entries */

    for (i = 0; i < 1000; ++i) {
        put_le32(pb, 0);
    }
    return updatePacketSize(pb, pos);
}
#endif

static int gxf_write_umf_material_description(ByteIOContext *pb, GXFContext *ctx)
{
    put_le32(pb, ctx->flags);
    put_le32(pb, ctx->nb_frames); /* length of the longest track */
    put_le32(pb, ctx->nb_frames); /* length of the shortest track */
    put_le32(pb, 0); /* mark in */
    put_le32(pb, ctx->nb_frames); /* mark out */
    put_le32(pb, 0); /* timecode mark in */
    put_le32(pb, ctx->nb_frames); /* timecode mark out */
    put_le64(pb, ctx->fc->timestamp); /* modification time */
    put_le64(pb, ctx->fc->timestamp); /* creation time */
    put_le16(pb, 0); /* reserved */
    put_le16(pb, 0); /* reserved */
    put_le16(pb, ctx->audio_tracks);
    put_le16(pb, 0); /* timecode track count */
    put_le16(pb, 0); /* reserved */
    put_le16(pb, ctx->mpeg_tracks);
    return 48;
}

static int gxf_write_umf_payload(ByteIOContext *pb, GXFContext *ctx)
{
    put_le32(pb, ctx->umf_length); /* total length of the umf data */
    put_le32(pb, 3); /* version */
    put_le32(pb, ctx->fc->nb_streams);
    put_le32(pb, ctx->umf_track_offset); /* umf track section offset */
    put_le32(pb, ctx->umf_track_size);
    put_le32(pb, ctx->fc->nb_streams);
    put_le32(pb, ctx->umf_media_offset);
    put_le32(pb, ctx->umf_media_size);
    put_le32(pb, ctx->umf_user_data_offset); /* user data offset */
    put_le32(pb, ctx->umf_user_data_size); /* user data size */
    put_le32(pb, 0); /* reserved */
    put_le32(pb, 0); /* reserved */
    return 48;
}

static int gxf_write_umf_track_description(ByteIOContext *pb, GXFContext *ctx)
{
    offset_t pos = url_ftell(pb);
    int tracks[255]={0};
    int i;

    ctx->umf_track_offset = pos - ctx->umf_start_offset;
    for (i = 0; i < ctx->fc->nb_streams; ++i) {
        AVStream *st = ctx->fc->streams[i];
        GXFStreamContext *sc = &ctx->streams[i];
        int id = 0;

        switch (st->codec->codec_id) {
        case CODEC_ID_MPEG1VIDEO: id= 'L'; break;
        case CODEC_ID_MPEG2VIDEO: id= 'M'; break;
        case CODEC_ID_PCM_S16LE:  id= 'A'; break;
        case CODEC_ID_DVVIDEO:    id= sc->track_type == 6 ? 'E' : 'D'; break;
        case CODEC_ID_MJPEG:      id= 'V'; break;
        default:                  break;
        }
        sc->media_info= id << 8;
        /* FIXME first 10 audio tracks are 0 to 9 next 22 are A to V */
        sc->media_info |= '0' + (tracks[id]++);
        put_le16(pb, sc->media_info);
        put_le16(pb, 1);
    }
    return url_ftell(pb) - pos;
}

static int gxf_write_umf_media_mpeg(ByteIOContext *pb, GXFStreamContext *stream)
{
    if (stream->codec->pix_fmt == PIX_FMT_YUV422P)
        put_le32(pb, 2);
    else
        put_le32(pb, 1); /* default to 420 */
    put_le32(pb, stream->first_gop_closed == 1); /* closed = 1, open = 0, unknown = 255 */
    put_le32(pb, 3); /* top = 1, bottom = 2, frame = 3, unknown = 0 */
    put_le32(pb, 1); /* I picture per GOP */
    put_le32(pb, stream->p_per_gop);
    put_le32(pb, stream->b_per_gop);
    if (stream->codec->codec_id == CODEC_ID_MPEG2VIDEO)
        put_le32(pb, 2);
    else if (stream->codec->codec_id == CODEC_ID_MPEG1VIDEO)
        put_le32(pb, 1);
    else
        put_le32(pb, 0);
    put_le32(pb, 0); /* reserved */
    return 32;
}

static int gxf_write_umf_media_timecode(ByteIOContext *pb, GXFStreamContext *track)
{
    /* FIXME implement */
    put_be32(pb, 0); /* drop frame flag */
    put_be32(pb, 0); /* reserved */
    put_be32(pb, 0); /* reserved */
    put_be32(pb, 0); /* reserved */
    put_be32(pb, 0); /* reserved */
    put_be32(pb, 0); /* reserved */
    put_be32(pb, 0); /* reserved */
    put_be32(pb, 0); /* reserved */
    return 32;
}

static int gxf_write_umf_media_dv(ByteIOContext *pb, GXFStreamContext *track)
{
    int i;

    for (i = 0; i < 8; i++) {
        put_be32(pb, 0);
    }
    return 32;
}

static int gxf_write_umf_media_audio(ByteIOContext *pb, GXFStreamContext *track)
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
static int gxf_write_umf_media_mjpeg(ByteIOContext *pb, GXFStreamContext *track)
{
    put_be64(pb, 0); /* FIXME FLOAT max chroma quant level */
    put_be64(pb, 0); /* FIXME FLOAT max luma quant level */
    put_be64(pb, 0); /* FIXME FLOAT min chroma quant level */
    put_be64(pb, 0); /* FIXME FLOAT min luma quant level */
    return 32;
}
#endif

static int gxf_write_umf_media_description(ByteIOContext *pb, GXFContext *ctx)
{
    offset_t pos;
    int i;

    pos = url_ftell(pb);
    ctx->umf_media_offset = pos - ctx->umf_start_offset;
    for (i = 0; i < ctx->fc->nb_streams; ++i) {
        GXFStreamContext *sc = &ctx->streams[i];
        char buffer[88];
        offset_t startpos, curpos;
        int path_size = strlen(ES_NAME_PATTERN);

        memset(buffer, 0, 88);
        startpos = url_ftell(pb);
        put_le16(pb, 0); /* length */
        put_le16(pb, sc->media_info);
        put_le16(pb, 0); /* reserved */
        put_le16(pb, 0); /* reserved */
        put_le32(pb, ctx->nb_frames);
        put_le32(pb, 0); /* attributes rw, ro */
        put_le32(pb, 0); /* mark in */
        put_le32(pb, ctx->nb_frames); /* mark out */
        strncpy(buffer, ES_NAME_PATTERN, path_size);
        put_buffer(pb, (uint8_t *)buffer, path_size);
        put_be16(pb, sc->media_info);
        put_buffer(pb, (uint8_t *)buffer + path_size + 2, 88 - path_size - 2);
        put_le32(pb, sc->track_type);
        put_le32(pb, sc->sample_rate);
        put_le32(pb, sc->sample_size);
        put_le32(pb, 0); /* reserved */
        switch (sc->codec->codec_id) {
        case CODEC_ID_MPEG2VIDEO:
            gxf_write_umf_media_mpeg(pb, sc);
            break;
        case CODEC_ID_PCM_S16LE:
            gxf_write_umf_media_audio(pb, sc);
            break;
        case CODEC_ID_DVVIDEO:
            gxf_write_umf_media_dv(pb, sc);
            break;
        default:
            gxf_write_umf_media_timecode(pb, sc); /* 8 0bytes */
        }
        curpos = url_ftell(pb);
        url_fseek(pb, startpos, SEEK_SET);
        put_le16(pb, curpos - startpos);
        url_fseek(pb, curpos, SEEK_SET);
    }
    return url_ftell(pb) - pos;
}

static int gxf_write_umf_user_data(ByteIOContext *pb, GXFContext *ctx)
{
    offset_t pos = url_ftell(pb);
    ctx->umf_user_data_offset = pos - ctx->umf_start_offset;
    put_le32(pb, 20);
    put_le32(pb,  0);
    put_le16(pb,  0);
    put_le16(pb,  0);
    put_le32(pb,  0);
    put_byte(pb,  0);
    put_byte(pb,  0);
    put_byte(pb,  0);
    put_byte(pb,  0);
    return 20;
}

static int gxf_write_umf_packet(ByteIOContext *pb, GXFContext *ctx)
{
    offset_t pos = url_ftell(pb);

    gxf_write_packet_header(pb, PKT_UMF);

    /* preamble */
    put_byte(pb, 3); /* first and last (only) packet */
    put_be32(pb, ctx->umf_length); /* data length */

    ctx->umf_start_offset = url_ftell(pb);
    gxf_write_umf_payload(pb, ctx);
    gxf_write_umf_material_description(pb, ctx);
    ctx->umf_track_size = gxf_write_umf_track_description(pb, ctx);
    ctx->umf_media_size = gxf_write_umf_media_description(pb, ctx);
    ctx->umf_user_data_size = gxf_write_umf_user_data(pb, ctx);
    ctx->umf_length = url_ftell(pb) - ctx->umf_start_offset;
    return updatePacketSize(pb, pos);
}

#define GXF_NODELAY -5000

static int gxf_write_header(AVFormatContext *s)
{
    ByteIOContext *pb = s->pb;
    GXFContext *gxf = s->priv_data;
    int i;

    gxf->fc = s;
    gxf->flags |= 0x00080000; /* material is simple clip */
    for (i = 0; i < s->nb_streams; ++i) {
        AVStream *st = s->streams[i];
        GXFStreamContext *sc = &gxf->streams[i];

        sc->codec = st->codec;
        sc->index = i;
        sc->media_type = codec_get_tag(gxf_media_types, sc->codec->codec_id);
        if (st->codec->codec_type == CODEC_TYPE_AUDIO) {
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
            av_fifo_init(&sc->audio_buffer, 3*GXF_AUDIO_PACKET_SIZE);
        } else if (sc->codec->codec_type == CODEC_TYPE_VIDEO) {
            /* FIXME check from time_base ? */
            if (sc->codec->height == 480 || sc->codec->height == 512) { /* NTSC or NTSC+VBI */
                sc->frame_rate_index = 5;
                sc->sample_rate = 60;
                gxf->flags |= 0x00000080;
            } else { /* assume PAL */
                sc->frame_rate_index = 6;
                sc->media_type++;
                sc->sample_rate = 50;
                gxf->flags |= 0x00000040;
            }
            gxf->sample_rate = sc->sample_rate;
            av_set_pts_info(st, 64, 1, st->codec->time_base.den);
            sc->dts_delay = GXF_NODELAY;
            if (gxf_find_lines_index(sc) < 0)
                sc->lines_index = -1;
            sc->sample_size = st->codec->bit_rate;
            sc->fields = 2; /* interlaced */
            switch (sc->codec->codec_id) {
            case CODEC_ID_MPEG2VIDEO:
                sc->first_gop_closed = -1;
                sc->track_type = 4;
                gxf->mpeg_tracks++;
                gxf->flags |= 0x00008000;
                break;
            case CODEC_ID_DVVIDEO:
                if (sc->codec->pix_fmt == PIX_FMT_YUV422P) {
                    sc->media_type += 2;
                    sc->track_type = 6;
                    gxf->flags |= 0x00002000;
                } else {
                    sc->track_type = 5;
                    gxf->flags |= 0x00001000;
                }
                break;
            default:
                av_log(s, AV_LOG_ERROR, "video codec not supported\n");
                return -1;
            }
        }
    }
    gxf_write_map_packet(pb, gxf);
    //gxf_write_flt_packet(pb, gxf);
    gxf_write_umf_packet(pb, gxf);
    put_flush_packet(pb);
    return 0;
}

static int gxf_write_eos_packet(ByteIOContext *pb, GXFContext *ctx)
{
    offset_t pos = url_ftell(pb);

    gxf_write_packet_header(pb, PKT_EOS);
    return updatePacketSize(pb, pos);
}

static int gxf_write_trailer(AVFormatContext *s)
{
    ByteIOContext *pb = s->pb;
    GXFContext *gxf = s->priv_data;
    offset_t end;
    int i;

    for (i = 0; i < s->nb_streams; ++i) {
        if (s->streams[i]->codec->codec_type == CODEC_TYPE_AUDIO) {
            av_fifo_free(&gxf->streams[i].audio_buffer);
        } else if (s->streams[i]->codec->codec_type == CODEC_TYPE_VIDEO) {
            gxf->nb_frames = 2 * s->streams[i]->codec->frame_number;
        }
    }

    gxf_write_eos_packet(pb, gxf);
    end = url_ftell(pb);
    url_fseek(pb, 0, SEEK_SET);
    /* overwrite map and umf packets with new values */
    gxf_write_map_packet(pb, gxf);
    //gxf_write_flt_packet(pb, gxf);
    gxf_write_umf_packet(pb, gxf);
    url_fseek(pb, end, SEEK_SET);
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

static int gxf_write_media_preamble(ByteIOContext *pb, GXFContext *ctx, AVPacket *pkt, int size)
{
    GXFStreamContext *sc = &ctx->streams[pkt->stream_index];
    int64_t dts = av_rescale_rnd(pkt->dts, ctx->sample_rate, sc->codec->time_base.den, AV_ROUND_UP);

    put_byte(pb, sc->media_type);
    put_byte(pb, sc->index);
    put_be32(pb, dts);
    if (sc->codec->codec_type == CODEC_TYPE_AUDIO) {
        put_be16(pb, 0);
        put_be16(pb, size / 2);
    } else if (sc->codec->codec_id == CODEC_ID_MPEG2VIDEO) {
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
    } else if (sc->codec->codec_id == CODEC_ID_DVVIDEO) {
        put_byte(pb, size / 4096);
        put_be24(pb, 0);
    } else
        put_be32(pb, size);
    put_be32(pb, dts);
    put_byte(pb, 1); /* flags */
    put_byte(pb, 0); /* reserved */
    return 16;
}

static int gxf_write_media_packet(ByteIOContext *pb, GXFContext *ctx, AVPacket *pkt)
{
    GXFStreamContext *sc = &ctx->streams[pkt->stream_index];
    offset_t pos = url_ftell(pb);
    int padding = 0;

    gxf_write_packet_header(pb, PKT_MEDIA);
    if (sc->codec->codec_id == CODEC_ID_MPEG2VIDEO && pkt->size % 4) /* MPEG-2 frames must be padded */
        padding = 4 - pkt->size % 4;
    else if (sc->codec->codec_type == CODEC_TYPE_AUDIO)
        padding = GXF_AUDIO_PACKET_SIZE - pkt->size;
    gxf_write_media_preamble(pb, ctx, pkt, pkt->size + padding);
    put_buffer(pb, pkt->data, pkt->size);
    gxf_write_padding(pb, padding);
    return updatePacketSize(pb, pos);
}

static int gxf_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    GXFContext *gxf = s->priv_data;

    gxf_write_media_packet(s->pb, gxf, pkt);
    put_flush_packet(s->pb);
    return 0;
}

static int gxf_new_audio_packet(GXFContext *gxf, GXFStreamContext *sc, AVPacket *pkt, int flush)
{
    int size = flush ? av_fifo_size(&sc->audio_buffer) : GXF_AUDIO_PACKET_SIZE;

    if (!size)
        return 0;
    av_new_packet(pkt, size);
    av_fifo_read(&sc->audio_buffer, pkt->data, size);
    pkt->stream_index = sc->index;
    pkt->dts = sc->current_dts;
    sc->current_dts += size / 2; /* we only support 16 bit pcm mono for now */
    return size;
}

static int gxf_interleave_packet(AVFormatContext *s, AVPacket *out, AVPacket *pkt, int flush)
{
    GXFContext *gxf = s->priv_data;
    AVPacket new_pkt;
    int i;

    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        GXFStreamContext *sc = &gxf->streams[i];
        if (st->codec->codec_type == CODEC_TYPE_AUDIO) {
            if (pkt && pkt->stream_index == i) {
                av_fifo_write(&sc->audio_buffer, pkt->data, pkt->size);
                pkt = NULL;
            }
            if (flush || av_fifo_size(&sc->audio_buffer) >= GXF_AUDIO_PACKET_SIZE) {
                if (!pkt && gxf_new_audio_packet(gxf, sc, &new_pkt, flush) > 0) {
                    pkt = &new_pkt;
                    break; /* add pkt right now into list */
                }
            }
        } else if (pkt && pkt->stream_index == i) {
            if (sc->dts_delay == GXF_NODELAY) /* adjust dts if needed */
                sc->dts_delay = pkt->dts;
            pkt->dts -= sc->dts_delay;
        }
    }
    return av_interleave_packet_per_dts(s, out, pkt, flush);
}

AVOutputFormat gxf_muxer = {
    "gxf",
    "GXF format",
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
