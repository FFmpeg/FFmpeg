/*
 * Copyright (c) 2011 Anton Khirnov <anton@khirnov.net>
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
 * libcdio CD grabbing
 */

#include <cdio/cdda.h>
#include <cdio/paranoia.h>

#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"

#include "libavformat/avformat.h"
#include "libavformat/internal.h"

/* cdio returns some malloced strings that need to be free()d */
#undef free

typedef struct CDIOContext {
    cdrom_drive_t       *drive;
    cdrom_paranoia_t *paranoia;
    int32_t last_sector;

    /* private options */
    int speed;
    int paranoia_mode;
} CDIOContext;

static av_cold int read_header(AVFormatContext *ctx)
{
    CDIOContext *s = ctx->priv_data;
    AVStream *st;
    int ret, i;
    char *err = NULL;

    if (!(st = avformat_new_stream(ctx, NULL)))
        return AVERROR(ENOMEM);
    s->drive = cdio_cddap_identify(ctx->filename, CDDA_MESSAGE_LOGIT, &err);
    if (!s->drive) {
        av_log(ctx, AV_LOG_ERROR, "Could not open drive %s.\n", ctx->filename);
        return AVERROR(EINVAL);
    }
    if (err) {
        av_log(ctx, AV_LOG_VERBOSE, "%s\n", err);
        free(err);
    }
    if ((ret = cdio_cddap_open(s->drive)) < 0 || !s->drive->opened) {
        av_log(ctx, AV_LOG_ERROR, "Could not open disk in drive %s.\n", ctx->filename);
        return AVERROR(EINVAL);
    }

    cdio_cddap_verbose_set(s->drive, CDDA_MESSAGE_LOGIT, CDDA_MESSAGE_LOGIT);
    if (s->speed)
        cdio_cddap_speed_set(s->drive, s->speed);

    s->paranoia = cdio_paranoia_init(s->drive);
    if (!s->paranoia) {
        av_log(ctx, AV_LOG_ERROR, "Could not init paranoia.\n");
        return AVERROR(EINVAL);
    }
    cdio_paranoia_modeset(s->paranoia, s->paranoia_mode);

    st->codec->codec_type      = AVMEDIA_TYPE_AUDIO;
    if (s->drive->bigendianp)
        st->codec->codec_id    = CODEC_ID_PCM_S16BE;
    else
        st->codec->codec_id    = CODEC_ID_PCM_S16LE;
    st->codec->sample_rate     = 44100;
    st->codec->channels        = 2;
    if (s->drive->audio_last_sector != CDIO_INVALID_LSN &&
        s->drive->audio_first_sector != CDIO_INVALID_LSN)
        st->duration           = s->drive->audio_last_sector - s->drive->audio_first_sector;
    else if (s->drive->tracks)
        st->duration = s->drive->disc_toc[s->drive->tracks].dwStartSector;
    avpriv_set_pts_info(st, 64, CDIO_CD_FRAMESIZE_RAW, 2*st->codec->channels*st->codec->sample_rate);

    for (i = 0; i < s->drive->tracks; i++) {
        char title[16];
        snprintf(title, sizeof(title), "track %02d", s->drive->disc_toc[i].bTrack);
        avpriv_new_chapter(ctx, i, st->time_base, s->drive->disc_toc[i].dwStartSector,
                       s->drive->disc_toc[i+1].dwStartSector, title);
    }

    s->last_sector = cdio_cddap_disc_lastsector(s->drive);

    return 0;
}

static int read_packet(AVFormatContext *ctx, AVPacket *pkt)
{
    CDIOContext *s = ctx->priv_data;
    int ret;
    uint16_t *buf;
    char *err = NULL;

    if (ctx->streams[0]->cur_dts > s->last_sector)
        return AVERROR_EOF;

    buf = cdio_paranoia_read(s->paranoia, NULL);
    if (!buf)
        return AVERROR_EOF;

    if (err = cdio_cddap_errors(s->drive)) {
        av_log(ctx, AV_LOG_ERROR, "%s\n", err);
        free(err);
        err = NULL;
    }
    if (err = cdio_cddap_messages(s->drive)) {
        av_log(ctx, AV_LOG_VERBOSE, "%s\n", err);
        free(err);
        err = NULL;
    }

    if ((ret = av_new_packet(pkt, CDIO_CD_FRAMESIZE_RAW)) < 0)
        return ret;
    memcpy(pkt->data, buf, CDIO_CD_FRAMESIZE_RAW);
    return 0;
}

static av_cold int read_close(AVFormatContext *ctx)
{
    CDIOContext *s = ctx->priv_data;
    cdio_paranoia_free(s->paranoia);
    cdio_cddap_close(s->drive);
    return 0;
}

static int read_seek(AVFormatContext *ctx, int stream_index, int64_t timestamp,
                     int flags)
{
    CDIOContext *s = ctx->priv_data;
    AVStream *st = ctx->streams[0];

    cdio_paranoia_seek(s->paranoia, timestamp, SEEK_SET);
    st->cur_dts = timestamp;
    return 0;
}

#define OFFSET(x) offsetof(CDIOContext, x)
#define DEC AV_OPT_FLAG_DECODING_PARAM
static const AVOption options[] = {
    { "speed",              "Drive reading speed.", OFFSET(speed),         AV_OPT_TYPE_INT,   { 0 }, 0,       INT_MAX, DEC },
    { "paranoia_mode",      "Error recovery mode.", OFFSET(paranoia_mode), AV_OPT_TYPE_FLAGS, { 0 }, INT_MIN, INT_MAX, DEC, "paranoia_mode" },
        { "verify",         "Verify data integrity in overlap area", 0,    AV_OPT_TYPE_CONST, { PARANOIA_MODE_VERIFY },    0, 0, DEC, "paranoia_mode" },
        { "overlap",        "Perform overlapped reads.",             0,    AV_OPT_TYPE_CONST, { PARANOIA_MODE_OVERLAP },   0, 0, DEC, "paranoia_mode" },
        { "neverskip",      "Do not skip failed reads.",             0,    AV_OPT_TYPE_CONST, { PARANOIA_MODE_NEVERSKIP }, 0, 0, DEC, "paranoia_mode" },
    { NULL },
};

static const AVClass libcdio_class = {
    .class_name = "libcdio indev",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVInputFormat ff_libcdio_demuxer = {
    .name           = "libcdio",
    .read_header    = read_header,
    .read_packet    = read_packet,
    .read_close     = read_close,
    .read_seek      = read_seek,
    .priv_data_size = sizeof(CDIOContext),
    .flags          = AVFMT_NOFILE,
    .priv_class     = &libcdio_class,
};
