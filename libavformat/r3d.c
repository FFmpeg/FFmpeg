/*
 * R3D REDCODE demuxer
 * Copyright (c) 2008 Baptiste Coudurier <baptiste dot coudurier at gmail dot com>
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
#include "libavutil/dict.h"
#include "libavutil/mathematics.h"
#include "avformat.h"
#include "internal.h"

typedef struct R3DContext {
    unsigned video_offsets_count;
    unsigned *video_offsets;
    unsigned rdvo_offset;

    int audio_channels;
} R3DContext;

typedef struct Atom {
    unsigned size;
    uint32_t tag;
    uint64_t offset;
} Atom;

static int read_atom(AVFormatContext *s, Atom *atom)
{
    atom->offset = avio_tell(s->pb);
    atom->size = avio_rb32(s->pb);
    if (atom->size < 8)
        return -1;
    atom->tag = avio_rl32(s->pb);
    av_log(s, AV_LOG_TRACE, "atom %u %.4s offset %#"PRIx64"\n",
            atom->size, (char*)&atom->tag, atom->offset);
    return atom->size;
}

static int r3d_read_red1(AVFormatContext *s)
{
    AVStream *st = avformat_new_stream(s, NULL);
    R3DContext *r3d = s->priv_data;
    char filename[258];
    int tmp;
    int av_unused tmp2;
    AVRational framerate;

    if (!st)
        return AVERROR(ENOMEM);
    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id = AV_CODEC_ID_JPEG2000;

    tmp  = avio_r8(s->pb); // major version
    tmp2 = avio_r8(s->pb); // minor version
    av_log(s, AV_LOG_TRACE, "version %d.%d\n", tmp, tmp2);

    tmp = avio_rb16(s->pb); // unknown
    av_log(s, AV_LOG_TRACE, "unknown1 %d\n", tmp);

    tmp = avio_rb32(s->pb);
    avpriv_set_pts_info(st, 32, 1, tmp);

    tmp = avio_rb32(s->pb); // filenum
    av_log(s, AV_LOG_TRACE, "filenum %d\n", tmp);

    avio_skip(s->pb, 32); // unknown

    st->codecpar->width  = avio_rb32(s->pb);
    st->codecpar->height = avio_rb32(s->pb);

    tmp = avio_rb16(s->pb); // unknown
    av_log(s, AV_LOG_TRACE, "unknown2 %d\n", tmp);

    framerate.num = avio_rb16(s->pb);
    framerate.den = avio_rb16(s->pb);
    if (framerate.num > 0 && framerate.den > 0) {
#if FF_API_R_FRAME_RATE
        st->r_frame_rate =
#endif
        st->avg_frame_rate = framerate;
    }

    r3d->audio_channels = avio_r8(s->pb); // audio channels
    av_log(s, AV_LOG_TRACE, "audio channels %d\n", tmp);

    avio_read(s->pb, filename, 257);
    filename[sizeof(filename)-1] = 0;
    av_dict_set(&st->metadata, "filename", filename, 0);

    av_log(s, AV_LOG_TRACE, "filename %s\n", filename);
    av_log(s, AV_LOG_TRACE, "resolution %dx%d\n", st->codecpar->width, st->codecpar->height);
    av_log(s, AV_LOG_TRACE, "timescale %d\n", st->time_base.den);
    av_log(s, AV_LOG_TRACE, "frame rate %d/%d\n",
            framerate.num, framerate.den);

    return 0;
}

static int r3d_read_rdvo(AVFormatContext *s, Atom *atom)
{
    R3DContext *r3d = s->priv_data;
    AVStream *st = s->streams[0];
    int i;

    r3d->video_offsets_count = (atom->size - 8) / 4;
    r3d->video_offsets = av_malloc(atom->size);
    if (!r3d->video_offsets)
        return AVERROR(ENOMEM);

    for (i = 0; i < r3d->video_offsets_count; i++) {
        r3d->video_offsets[i] = avio_rb32(s->pb);
        if (!r3d->video_offsets[i]) {
            r3d->video_offsets_count = i;
            break;
        }
        av_log(s, AV_LOG_TRACE, "video offset %d: %#x\n", i, r3d->video_offsets[i]);
    }

    if (st->avg_frame_rate.num)
        st->duration = av_rescale_q(r3d->video_offsets_count,
                                    av_inv_q(st->avg_frame_rate),
                                    st->time_base);
    av_log(s, AV_LOG_TRACE, "duration %"PRId64"\n", st->duration);

    return 0;
}

static void r3d_read_reos(AVFormatContext *s)
{
    R3DContext *r3d = s->priv_data;
    int av_unused tmp;

    r3d->rdvo_offset = avio_rb32(s->pb);
    avio_rb32(s->pb); // rdvs offset
    avio_rb32(s->pb); // rdao offset
    avio_rb32(s->pb); // rdas offset

    tmp = avio_rb32(s->pb);
    av_log(s, AV_LOG_TRACE, "num video chunks %d\n", tmp);

    tmp = avio_rb32(s->pb);
    av_log(s, AV_LOG_TRACE, "num audio chunks %d\n", tmp);

    avio_skip(s->pb, 6*4);
}

static int r3d_read_header(AVFormatContext *s)
{
    R3DContext *r3d = s->priv_data;
    Atom atom;
    int ret;

    if (read_atom(s, &atom) < 0) {
        av_log(s, AV_LOG_ERROR, "error reading atom\n");
        return -1;
    }
    if (atom.tag == MKTAG('R','E','D','1')) {
        if ((ret = r3d_read_red1(s)) < 0) {
            av_log(s, AV_LOG_ERROR, "error parsing 'red1' atom\n");
            return ret;
        }
    } else {
        av_log(s, AV_LOG_ERROR, "could not find 'red1' atom\n");
        return -1;
    }

    /* we cannot create the audio stream now because we do not know the
     * sample rate */
    if (r3d->audio_channels)
        s->ctx_flags |= AVFMTCTX_NOHEADER;

    s->internal->data_offset = avio_tell(s->pb);
    av_log(s, AV_LOG_TRACE, "data offset %#"PRIx64"\n", s->internal->data_offset);
    if (!s->pb->seekable)
        return 0;
    // find REOB/REOF/REOS to load index
    avio_seek(s->pb, avio_size(s->pb)-48-8, SEEK_SET);
    if (read_atom(s, &atom) < 0)
        av_log(s, AV_LOG_ERROR, "error reading end atom\n");

    if (atom.tag != MKTAG('R','E','O','B') &&
        atom.tag != MKTAG('R','E','O','F') &&
        atom.tag != MKTAG('R','E','O','S'))
        goto out;

    r3d_read_reos(s);

    if (r3d->rdvo_offset) {
        avio_seek(s->pb, r3d->rdvo_offset, SEEK_SET);
        if (read_atom(s, &atom) < 0)
            av_log(s, AV_LOG_ERROR, "error reading 'rdvo' atom\n");
        if (atom.tag == MKTAG('R','D','V','O')) {
            if (r3d_read_rdvo(s, &atom) < 0)
                av_log(s, AV_LOG_ERROR, "error parsing 'rdvo' atom\n");
        }
    }

 out:
    avio_seek(s->pb, s->internal->data_offset, SEEK_SET);
    return 0;
}

static int r3d_read_redv(AVFormatContext *s, AVPacket *pkt, Atom *atom)
{
    AVStream *st = s->streams[0];
    int tmp;
    int av_unused tmp2;
    int64_t pos = avio_tell(s->pb);
    unsigned dts;
    int ret;

    dts = avio_rb32(s->pb);

    tmp = avio_rb32(s->pb);
    av_log(s, AV_LOG_TRACE, "frame num %d\n", tmp);

    tmp  = avio_r8(s->pb); // major version
    tmp2 = avio_r8(s->pb); // minor version
    av_log(s, AV_LOG_TRACE, "version %d.%d\n", tmp, tmp2);

    tmp = avio_rb16(s->pb); // unknown
    av_log(s, AV_LOG_TRACE, "unknown %d\n", tmp);

    if (tmp > 4) {
        tmp = avio_rb16(s->pb); // unknown
        av_log(s, AV_LOG_TRACE, "unknown %d\n", tmp);

        tmp = avio_rb16(s->pb); // unknown
        av_log(s, AV_LOG_TRACE, "unknown %d\n", tmp);

        tmp = avio_rb32(s->pb);
        av_log(s, AV_LOG_TRACE, "width %d\n", tmp);
        tmp = avio_rb32(s->pb);
        av_log(s, AV_LOG_TRACE, "height %d\n", tmp);

        tmp = avio_rb32(s->pb);
        av_log(s, AV_LOG_TRACE, "metadata len %d\n", tmp);
    }
    tmp = atom->size - 8 - (avio_tell(s->pb) - pos);
    if (tmp < 0)
        return -1;
    ret = av_get_packet(s->pb, pkt, tmp);
    if (ret < 0) {
        av_log(s, AV_LOG_ERROR, "error reading video packet\n");
        return -1;
    }

    pkt->stream_index = 0;
    pkt->dts = dts;
    if (st->avg_frame_rate.num)
        pkt->duration = (uint64_t)st->time_base.den*
            st->avg_frame_rate.den/st->avg_frame_rate.num;
    av_log(s, AV_LOG_TRACE, "pkt dts %"PRId64" duration %"PRId64"\n", pkt->dts, pkt->duration);

    return 0;
}

static int r3d_read_reda(AVFormatContext *s, AVPacket *pkt, Atom *atom)
{
    R3DContext *r3d = s->priv_data;
    AVStream *st;
    int av_unused tmp, tmp2;
    int samples, size;
    int64_t pos = avio_tell(s->pb);
    unsigned dts;
    int ret;

    if (s->nb_streams < 2) {
        st = avformat_new_stream(s, NULL);
        if (!st)
            return AVERROR(ENOMEM);
        st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
        st->codecpar->codec_id = AV_CODEC_ID_PCM_S32BE;
        st->codecpar->channels = r3d->audio_channels;
        avpriv_set_pts_info(st, 32, 1, s->streams[0]->time_base.den);
    } else {
        st = s->streams[1];
    }

    dts = avio_rb32(s->pb);

    st->codecpar->sample_rate = avio_rb32(s->pb);
    if (st->codecpar->sample_rate <= 0) {
        av_log(s, AV_LOG_ERROR, "Bad sample rate\n");
        return AVERROR_INVALIDDATA;
    }

    samples = avio_rb32(s->pb);

    tmp = avio_rb32(s->pb);
    av_log(s, AV_LOG_TRACE, "packet num %d\n", tmp);

    tmp = avio_rb16(s->pb); // unknown
    av_log(s, AV_LOG_TRACE, "unknown %d\n", tmp);

    tmp  = avio_r8(s->pb); // major version
    tmp2 = avio_r8(s->pb); // minor version
    av_log(s, AV_LOG_TRACE, "version %d.%d\n", tmp, tmp2);

    tmp = avio_rb32(s->pb); // unknown
    av_log(s, AV_LOG_TRACE, "unknown %d\n", tmp);

    size = atom->size - 8 - (avio_tell(s->pb) - pos);
    if (size < 0)
        return -1;
    ret = av_get_packet(s->pb, pkt, size);
    if (ret < 0) {
        av_log(s, AV_LOG_ERROR, "error reading audio packet\n");
        return ret;
    }

    pkt->stream_index = 1;
    pkt->dts = dts;
    if (st->codecpar->sample_rate)
        pkt->duration = av_rescale(samples, st->time_base.den, st->codecpar->sample_rate);
    av_log(s, AV_LOG_TRACE, "pkt dts %"PRId64" duration %"PRId64" samples %d sample rate %d\n",
            pkt->dts, pkt->duration, samples, st->codecpar->sample_rate);

    return 0;
}

static int r3d_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    R3DContext *r3d = s->priv_data;
    Atom atom;
    int err = 0;

    while (!err) {
        if (read_atom(s, &atom) < 0) {
            err = -1;
            break;
        }
        switch (atom.tag) {
        case MKTAG('R','E','D','V'):
            if (s->streams[0]->discard == AVDISCARD_ALL)
                goto skip;
            if (!(err = r3d_read_redv(s, pkt, &atom)))
                return 0;
            break;
        case MKTAG('R','E','D','A'):
            if (!r3d->audio_channels)
                return -1;
            if (s->nb_streams >= 2 && s->streams[1]->discard == AVDISCARD_ALL)
                goto skip;
            if (!(err = r3d_read_reda(s, pkt, &atom)))
                return 0;
            break;
        default:
        skip:
            avio_skip(s->pb, atom.size-8);
        }
    }
    return err;
}

static int r3d_probe(AVProbeData *p)
{
    if (AV_RL32(p->buf + 4) == MKTAG('R','E','D','1'))
        return AVPROBE_SCORE_MAX;
    return 0;
}

static int r3d_seek(AVFormatContext *s, int stream_index, int64_t sample_time, int flags)
{
    AVStream *st = s->streams[0]; // video stream
    R3DContext *r3d = s->priv_data;
    int frame_num;

    if (!st->avg_frame_rate.num)
        return -1;

    frame_num = av_rescale_q(sample_time, st->time_base,
                             av_inv_q(st->avg_frame_rate));
    av_log(s, AV_LOG_TRACE, "seek frame num %d timestamp %"PRId64"\n",
            frame_num, sample_time);

    if (frame_num < r3d->video_offsets_count) {
        if (avio_seek(s->pb, r3d->video_offsets_count, SEEK_SET) < 0)
            return -1;
    } else {
        av_log(s, AV_LOG_ERROR, "could not seek to frame %d\n", frame_num);
        return -1;
    }

    return 0;
}

static int r3d_close(AVFormatContext *s)
{
    R3DContext *r3d = s->priv_data;

    av_freep(&r3d->video_offsets);

    return 0;
}

AVInputFormat ff_r3d_demuxer = {
    .name           = "r3d",
    .long_name      = NULL_IF_CONFIG_SMALL("REDCODE R3D"),
    .priv_data_size = sizeof(R3DContext),
    .read_probe     = r3d_probe,
    .read_header    = r3d_read_header,
    .read_packet    = r3d_read_packet,
    .read_close     = r3d_close,
    .read_seek      = r3d_seek,
};
