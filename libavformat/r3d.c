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

//#define DEBUG

#include "libavutil/intreadwrite.h"
#include "avformat.h"

typedef struct {
    unsigned video_offsets_count;
    unsigned *video_offsets;
    unsigned rdvo_offset;
} R3DContext;

typedef struct {
    unsigned size;
    uint32_t tag;
    uint64_t offset;
} Atom;

static int read_atom(AVFormatContext *s, Atom *atom)
{
    atom->offset = url_ftell(s->pb);
    atom->size = get_be32(s->pb);
    if (atom->size < 8)
        return -1;
    atom->tag = get_le32(s->pb);
    dprintf(s, "atom %d %.4s offset %#llx\n",
            atom->size, (char*)&atom->tag, atom->offset);
    return atom->size;
}

static int r3d_read_red1(AVFormatContext *s)
{
    AVStream *st = av_new_stream(s, 0);
    char filename[258];
    int tmp, tmp2;

    if (!st)
        return AVERROR(ENOMEM);
    st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codec->codec_id = CODEC_ID_JPEG2000;

    tmp  = get_byte(s->pb); // major version
    tmp2 = get_byte(s->pb); // minor version
    dprintf(s, "version %d.%d\n", tmp, tmp2);

    tmp = get_be16(s->pb); // unknown
    dprintf(s, "unknown1 %d\n", tmp);

    tmp = get_be32(s->pb);
    av_set_pts_info(st, 32, 1, tmp);

    tmp = get_be32(s->pb); // filenum
    dprintf(s, "filenum %d\n", tmp);

    url_fskip(s->pb, 32); // unknown

    st->codec->width  = get_be32(s->pb);
    st->codec->height = get_be32(s->pb);

    tmp = get_be16(s->pb); // unknown
    dprintf(s, "unknown2 %d\n", tmp);

    st->codec->time_base.den = get_be16(s->pb);
    st->codec->time_base.num = get_be16(s->pb);

    tmp = get_byte(s->pb); // audio channels
    dprintf(s, "audio channels %d\n", tmp);
    if (tmp > 0) {
        AVStream *ast = av_new_stream(s, 1);
        if (!ast)
            return AVERROR(ENOMEM);
        ast->codec->codec_type = AVMEDIA_TYPE_AUDIO;
        ast->codec->codec_id = CODEC_ID_PCM_S32BE;
        ast->codec->channels = tmp;
        av_set_pts_info(ast, 32, 1, st->time_base.den);
    }

    get_buffer(s->pb, filename, 257);
    filename[sizeof(filename)-1] = 0;
    av_metadata_set2(&st->metadata, "filename", filename, 0);

    dprintf(s, "filename %s\n", filename);
    dprintf(s, "resolution %dx%d\n", st->codec->width, st->codec->height);
    dprintf(s, "timescale %d\n", st->time_base.den);
    dprintf(s, "frame rate %d/%d\n",
            st->codec->time_base.num, st->codec->time_base.den);

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
        r3d->video_offsets[i] = get_be32(s->pb);
        if (!r3d->video_offsets[i]) {
            r3d->video_offsets_count = i;
            break;
        }
        dprintf(s, "video offset %d: %#x\n", i, r3d->video_offsets[i]);
    }

    if (st->codec->time_base.den)
        st->duration = (uint64_t)r3d->video_offsets_count*
            st->time_base.den*st->codec->time_base.num/st->codec->time_base.den;
    dprintf(s, "duration %lld\n", st->duration);

    return 0;
}

static void r3d_read_reos(AVFormatContext *s)
{
    R3DContext *r3d = s->priv_data;
    int tmp;

    r3d->rdvo_offset = get_be32(s->pb);
    get_be32(s->pb); // rdvs offset
    get_be32(s->pb); // rdao offset
    get_be32(s->pb); // rdas offset

    tmp = get_be32(s->pb);
    dprintf(s, "num video chunks %d\n", tmp);

    tmp = get_be32(s->pb);
    dprintf(s, "num audio chunks %d\n", tmp);

    url_fskip(s->pb, 6*4);
}

static int r3d_read_header(AVFormatContext *s, AVFormatParameters *ap)
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

    s->data_offset = url_ftell(s->pb);
    dprintf(s, "data offset %#llx\n", s->data_offset);
    if (url_is_streamed(s->pb))
        return 0;
    // find REOB/REOF/REOS to load index
    url_fseek(s->pb, url_fsize(s->pb)-48-8, SEEK_SET);
    if (read_atom(s, &atom) < 0)
        av_log(s, AV_LOG_ERROR, "error reading end atom\n");

    if (atom.tag != MKTAG('R','E','O','B') &&
        atom.tag != MKTAG('R','E','O','F') &&
        atom.tag != MKTAG('R','E','O','S'))
        goto out;

    r3d_read_reos(s);

    if (r3d->rdvo_offset) {
        url_fseek(s->pb, r3d->rdvo_offset, SEEK_SET);
        if (read_atom(s, &atom) < 0)
            av_log(s, AV_LOG_ERROR, "error reading 'rdvo' atom\n");
        if (atom.tag == MKTAG('R','D','V','O')) {
            if (r3d_read_rdvo(s, &atom) < 0)
                av_log(s, AV_LOG_ERROR, "error parsing 'rdvo' atom\n");
        }
    }

 out:
    url_fseek(s->pb, s->data_offset, SEEK_SET);
    return 0;
}

static int r3d_read_redv(AVFormatContext *s, AVPacket *pkt, Atom *atom)
{
    AVStream *st = s->streams[0];
    int tmp, tmp2;
    uint64_t pos = url_ftell(s->pb);
    unsigned dts;
    int ret;

    dts = get_be32(s->pb);

    tmp = get_be32(s->pb);
    dprintf(s, "frame num %d\n", tmp);

    tmp  = get_byte(s->pb); // major version
    tmp2 = get_byte(s->pb); // minor version
    dprintf(s, "version %d.%d\n", tmp, tmp2);

    tmp = get_be16(s->pb); // unknown
    dprintf(s, "unknown %d\n", tmp);

    if (tmp > 4) {
        tmp = get_be16(s->pb); // unknown
        dprintf(s, "unknown %d\n", tmp);

        tmp = get_be16(s->pb); // unknown
        dprintf(s, "unknown %d\n", tmp);

        tmp = get_be32(s->pb);
        dprintf(s, "width %d\n", tmp);
        tmp = get_be32(s->pb);
        dprintf(s, "height %d\n", tmp);

        tmp = get_be32(s->pb);
        dprintf(s, "metadata len %d\n", tmp);
    }
    tmp = atom->size - 8 - (url_ftell(s->pb) - pos);
    if (tmp < 0)
        return -1;
    ret = av_get_packet(s->pb, pkt, tmp);
    if (ret < 0) {
        av_log(s, AV_LOG_ERROR, "error reading video packet\n");
        return -1;
    }

    pkt->stream_index = 0;
    pkt->dts = dts;
    if (st->codec->time_base.den)
        pkt->duration = (uint64_t)st->time_base.den*
            st->codec->time_base.num/st->codec->time_base.den;
    dprintf(s, "pkt dts %lld duration %d\n", pkt->dts, pkt->duration);

    return 0;
}

static int r3d_read_reda(AVFormatContext *s, AVPacket *pkt, Atom *atom)
{
    AVStream *st = s->streams[1];
    int tmp, tmp2, samples, size;
    uint64_t pos = url_ftell(s->pb);
    unsigned dts;
    int ret;

    dts = get_be32(s->pb);

    st->codec->sample_rate = get_be32(s->pb);

    samples = get_be32(s->pb);

    tmp = get_be32(s->pb);
    dprintf(s, "packet num %d\n", tmp);

    tmp = get_be16(s->pb); // unkown
    dprintf(s, "unknown %d\n", tmp);

    tmp  = get_byte(s->pb); // major version
    tmp2 = get_byte(s->pb); // minor version
    dprintf(s, "version %d.%d\n", tmp, tmp2);

    tmp = get_be32(s->pb); // unknown
    dprintf(s, "unknown %d\n", tmp);

    size = atom->size - 8 - (url_ftell(s->pb) - pos);
    if (size < 0)
        return -1;
    ret = av_get_packet(s->pb, pkt, size);
    if (ret < 0) {
        av_log(s, AV_LOG_ERROR, "error reading audio packet\n");
        return ret;
    }

    pkt->stream_index = 1;
    pkt->dts = dts;
    pkt->duration = av_rescale(samples, st->time_base.den, st->codec->sample_rate);
    dprintf(s, "pkt dts %lld duration %d samples %d sample rate %d\n",
            pkt->dts, pkt->duration, samples, st->codec->sample_rate);

    return 0;
}

static int r3d_read_packet(AVFormatContext *s, AVPacket *pkt)
{
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
            if (s->nb_streams < 2)
                return -1;
            if (s->streams[1]->discard == AVDISCARD_ALL)
                goto skip;
            if (!(err = r3d_read_reda(s, pkt, &atom)))
                return 0;
            break;
        default:
        skip:
            url_fskip(s->pb, atom.size-8);
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

    if (!st->codec->time_base.num || !st->time_base.den)
        return -1;

    frame_num = sample_time*st->codec->time_base.den/
        ((int64_t)st->codec->time_base.num*st->time_base.den);
    dprintf(s, "seek frame num %d timestamp %lld\n", frame_num, sample_time);

    if (frame_num < r3d->video_offsets_count) {
        url_fseek(s->pb, r3d->video_offsets_count, SEEK_SET);
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

AVInputFormat r3d_demuxer = {
    "r3d",
    NULL_IF_CONFIG_SMALL("REDCODE R3D format"),
    sizeof(R3DContext),
    r3d_probe,
    r3d_read_header,
    r3d_read_packet,
    r3d_close,
    r3d_seek,
};
