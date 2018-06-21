/*
 * General DV demuxer
 * Copyright (c) 2003 Roman Shaposhnik
 *
 * Many thanks to Dan Dennedy <dan@dennedy.org> for providing wealth
 * of DV technical info.
 *
 * Raw DV format
 * Copyright (c) 2002 Fabrice Bellard
 *
 * 50 Mbps (DVCPRO50) and 100 Mbps (DVCPRO HD) support
 * Copyright (c) 2006 Daniel Maas <dmaas@maasdigital.com>
 * Funded by BBC Research & Development
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
#include <time.h>
#include "avformat.h"
#include "internal.h"
#include "libavcodec/dv_profile.h"
#include "libavcodec/dv.h"
#include "libavutil/channel_layout.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mathematics.h"
#include "libavutil/timecode.h"
#include "dv.h"
#include "libavutil/avassert.h"

struct DVDemuxContext {
    const AVDVProfile*  sys;    /* Current DV profile. E.g.: 525/60, 625/50 */
    AVFormatContext*  fctx;
    AVStream*         vst;
    AVStream*         ast[4];
    AVPacket          audio_pkt[4];
    uint8_t           audio_buf[4][8192];
    int               ach;
    int               frames;
    uint64_t          abytes;
};

static inline uint16_t dv_audio_12to16(uint16_t sample)
{
    uint16_t shift, result;

    sample = (sample < 0x800) ? sample : sample | 0xf000;
    shift  = (sample & 0xf00) >> 8;

    if (shift < 0x2 || shift > 0xd) {
        result = sample;
    } else if (shift < 0x8) {
        shift--;
        result = (sample - (256 * shift)) << shift;
    } else {
        shift  = 0xe - shift;
        result = ((sample + ((256 * shift) + 1)) << shift) - 1;
    }

    return result;
}

static const uint8_t *dv_extract_pack(const uint8_t *frame, enum dv_pack_type t)
{
    int offs;
    int c;

    for (c = 0; c < 10; c++) {
        switch (t) {
        case dv_audio_source:
            if (c&1)    offs = (80 * 6 + 80 * 16 * 0 + 3 + c*12000);
            else        offs = (80 * 6 + 80 * 16 * 3 + 3 + c*12000);
            break;
        case dv_audio_control:
            if (c&1)    offs = (80 * 6 + 80 * 16 * 1 + 3 + c*12000);
            else        offs = (80 * 6 + 80 * 16 * 4 + 3 + c*12000);
            break;
        case dv_video_control:
            if (c&1)    offs = (80 * 3 + 8      + c*12000);
            else        offs = (80 * 5 + 48 + 5 + c*12000);
            break;
        case dv_timecode:
            offs = (80*1 + 3 + 3);
            break;
        default:
            return NULL;
        }
        if (frame[offs] == t)
            break;
    }

    return frame[offs] == t ? &frame[offs] : NULL;
}

static const int dv_audio_frequency[3] = {
    48000, 44100, 32000,
};

/*
 * There's a couple of assumptions being made here:
 * 1. By default we silence erroneous (0x8000/16-bit 0x800/12-bit) audio samples.
 *    We can pass them upwards when libavcodec will be ready to deal with them.
 * 2. We don't do software emphasis.
 * 3. Audio is always returned as 16-bit linear samples: 12-bit nonlinear samples
 *    are converted into 16-bit linear ones.
 */
static int dv_extract_audio(const uint8_t *frame, uint8_t **ppcm,
                            const AVDVProfile *sys)
{
    int size, chan, i, j, d, of, smpls, freq, quant, half_ch;
    uint16_t lc, rc;
    const uint8_t *as_pack;
    uint8_t *pcm, ipcm;

    as_pack = dv_extract_pack(frame, dv_audio_source);
    if (!as_pack)    /* No audio ? */
        return 0;

    smpls = as_pack[1]      & 0x3f; /* samples in this frame - min. samples */
    freq  = as_pack[4] >> 3 & 0x07; /* 0 - 48kHz, 1 - 44,1kHz, 2 - 32kHz */
    quant = as_pack[4]      & 0x07; /* 0 - 16-bit linear, 1 - 12-bit nonlinear */

    if (quant > 1)
        return -1;  /* unsupported quantization */

    if (freq >= FF_ARRAY_ELEMS(dv_audio_frequency))
        return AVERROR_INVALIDDATA;

    size    = (sys->audio_min_samples[freq] + smpls) * 4; /* 2ch, 2bytes */
    half_ch = sys->difseg_size / 2;

    /* We work with 720p frames split in half, thus even frames have
     * channels 0,1 and odd 2,3. */
    ipcm = (sys->height == 720 && !(frame[1] & 0x0C)) ? 2 : 0;

    if (ipcm + sys->n_difchan > (quant == 1 ? 2 : 4)) {
        av_log(NULL, AV_LOG_ERROR, "too many dv pcm frames\n");
        return AVERROR_INVALIDDATA;
    }

    /* for each DIF channel */
    for (chan = 0; chan < sys->n_difchan; chan++) {
        av_assert0(ipcm<4);
        pcm = ppcm[ipcm++];
        if (!pcm)
            break;

        /* for each DIF segment */
        for (i = 0; i < sys->difseg_size; i++) {
            frame += 6 * 80; /* skip DIF segment header */
            if (quant == 1 && i == half_ch) {
                /* next stereo channel (12-bit mode only) */
                av_assert0(ipcm<4);
                pcm = ppcm[ipcm++];
                if (!pcm)
                    break;
            }

            /* for each AV sequence */
            for (j = 0; j < 9; j++) {
                for (d = 8; d < 80; d += 2) {
                    if (quant == 0) {  /* 16-bit quantization */
                        of = sys->audio_shuffle[i][j] +
                             (d - 8) / 2 * sys->audio_stride;
                        if (of * 2 >= size)
                            continue;

                        /* FIXME: maybe we have to admit that DV is a
                         * big-endian PCM */
                        pcm[of * 2]     = frame[d + 1];
                        pcm[of * 2 + 1] = frame[d];

                        if (pcm[of * 2 + 1] == 0x80 && pcm[of * 2] == 0x00)
                            pcm[of * 2 + 1] = 0;
                    } else {           /* 12-bit quantization */
                        lc = ((uint16_t)frame[d]     << 4) |
                             ((uint16_t)frame[d + 2] >> 4);
                        rc = ((uint16_t)frame[d + 1] << 4) |
                             ((uint16_t)frame[d + 2] & 0x0f);
                        lc = (lc == 0x800 ? 0 : dv_audio_12to16(lc));
                        rc = (rc == 0x800 ? 0 : dv_audio_12to16(rc));

                        of = sys->audio_shuffle[i % half_ch][j] +
                             (d - 8) / 3 * sys->audio_stride;
                        if (of * 2 >= size)
                            continue;

                        /* FIXME: maybe we have to admit that DV is a
                         * big-endian PCM */
                        pcm[of * 2]     = lc & 0xff;
                        pcm[of * 2 + 1] = lc >> 8;
                        of = sys->audio_shuffle[i % half_ch + half_ch][j] +
                             (d - 8) / 3 * sys->audio_stride;
                        /* FIXME: maybe we have to admit that DV is a
                         * big-endian PCM */
                        pcm[of * 2]     = rc & 0xff;
                        pcm[of * 2 + 1] = rc >> 8;
                        ++d;
                    }
                }

                frame += 16 * 80; /* 15 Video DIFs + 1 Audio DIF */
            }
        }
    }

    return size;
}

static int dv_extract_audio_info(DVDemuxContext *c, const uint8_t *frame)
{
    const uint8_t *as_pack;
    int freq, stype, smpls, quant, i, ach;

    as_pack = dv_extract_pack(frame, dv_audio_source);
    if (!as_pack || !c->sys) {    /* No audio ? */
        c->ach = 0;
        return 0;
    }

    smpls = as_pack[1]      & 0x3f; /* samples in this frame - min. samples */
    freq  = as_pack[4] >> 3 & 0x07; /* 0 - 48kHz, 1 - 44,1kHz, 2 - 32kHz */
    stype = as_pack[3]      & 0x1f; /* 0 - 2CH, 2 - 4CH, 3 - 8CH */
    quant = as_pack[4]      & 0x07; /* 0 - 16-bit linear, 1 - 12-bit nonlinear */

    if (freq >= FF_ARRAY_ELEMS(dv_audio_frequency)) {
        av_log(c->fctx, AV_LOG_ERROR,
               "Unrecognized audio sample rate index (%d)\n", freq);
        return 0;
    }

    if (stype > 3) {
        av_log(c->fctx, AV_LOG_ERROR, "stype %d is invalid\n", stype);
        c->ach = 0;
        return 0;
    }

    /* note: ach counts PAIRS of channels (i.e. stereo channels) */
    ach = ((int[4]) { 1, 0, 2, 4 })[stype];
    if (ach == 1 && quant && freq == 2)
        ach = 2;

    /* Dynamic handling of the audio streams in DV */
    for (i = 0; i < ach; i++) {
        if (!c->ast[i]) {
            c->ast[i] = avformat_new_stream(c->fctx, NULL);
            if (!c->ast[i])
                break;
            avpriv_set_pts_info(c->ast[i], 64, 1, 30000);
            c->ast[i]->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
            c->ast[i]->codecpar->codec_id   = AV_CODEC_ID_PCM_S16LE;

            av_init_packet(&c->audio_pkt[i]);
            c->audio_pkt[i].size         = 0;
            c->audio_pkt[i].data         = c->audio_buf[i];
            c->audio_pkt[i].stream_index = c->ast[i]->index;
            c->audio_pkt[i].flags       |= AV_PKT_FLAG_KEY;
        }
        c->ast[i]->codecpar->sample_rate    = dv_audio_frequency[freq];
        c->ast[i]->codecpar->channels       = 2;
        c->ast[i]->codecpar->channel_layout = AV_CH_LAYOUT_STEREO;
        c->ast[i]->codecpar->bit_rate       = 2 * dv_audio_frequency[freq] * 16;
        c->ast[i]->start_time            = 0;
    }
    c->ach = i;

    return (c->sys->audio_min_samples[freq] + smpls) * 4; /* 2ch, 2bytes */
}

static int dv_extract_video_info(DVDemuxContext *c, const uint8_t *frame)
{
    const uint8_t *vsc_pack;
    AVCodecParameters *par;
    int apt, is16_9;

    par = c->vst->codecpar;

    avpriv_set_pts_info(c->vst, 64, c->sys->time_base.num,
                        c->sys->time_base.den);
    c->vst->avg_frame_rate = av_inv_q(c->vst->time_base);

    /* finding out SAR is a little bit messy */
    vsc_pack = dv_extract_pack(frame, dv_video_control);
    apt      = frame[4] & 0x07;
    is16_9   = (vsc_pack && ((vsc_pack[2] & 0x07) == 0x02 ||
                             (!apt && (vsc_pack[2] & 0x07) == 0x07)));
    c->vst->sample_aspect_ratio = c->sys->sar[is16_9];
    par->bit_rate = av_rescale_q(c->sys->frame_size,
                                   (AVRational) { 8, 1 },
                                   c->sys->time_base);
    return c->sys->frame_size;
}

static int dv_extract_timecode(DVDemuxContext* c, const uint8_t* frame, char *tc)
{
    const uint8_t *tc_pack;

    // For PAL systems, drop frame bit is replaced by an arbitrary
    // bit so its value should not be considered. Drop frame timecode
    // is only relevant for NTSC systems.
    int prevent_df = c->sys->ltc_divisor == 25 || c->sys->ltc_divisor == 50;

    tc_pack = dv_extract_pack(frame, dv_timecode);
    if (!tc_pack)
        return 0;
    av_timecode_make_smpte_tc_string(tc, AV_RB32(tc_pack + 1), prevent_df);
    return 1;
}

/* The following 3 functions constitute our interface to the world */

DVDemuxContext *avpriv_dv_init_demux(AVFormatContext *s)
{
    DVDemuxContext *c;

    c = av_mallocz(sizeof(DVDemuxContext));
    if (!c)
        return NULL;

    c->vst = avformat_new_stream(s, NULL);
    if (!c->vst) {
        av_free(c);
        return NULL;
    }

    c->fctx                   = s;
    c->vst->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    c->vst->codecpar->codec_id   = AV_CODEC_ID_DVVIDEO;
    c->vst->codecpar->bit_rate   = 25000000;
    c->vst->start_time        = 0;

    return c;
}

int avpriv_dv_get_packet(DVDemuxContext *c, AVPacket *pkt)
{
    int size = -1;
    int i;

    for (i = 0; i < c->ach; i++) {
        if (c->ast[i] && c->audio_pkt[i].size) {
            *pkt                 = c->audio_pkt[i];
            c->audio_pkt[i].size = 0;
            size                 = pkt->size;
            break;
        }
    }

    return size;
}

int avpriv_dv_produce_packet(DVDemuxContext *c, AVPacket *pkt,
                             uint8_t *buf, int buf_size, int64_t pos)
{
    int size, i;
    uint8_t *ppcm[5] = { 0 };

    if (buf_size < DV_PROFILE_BYTES ||
        !(c->sys = av_dv_frame_profile(c->sys, buf, buf_size)) ||
        buf_size < c->sys->frame_size) {
        return -1;   /* Broken frame, or not enough data */
    }

    /* Queueing audio packet */
    /* FIXME: in case of no audio/bad audio we have to do something */
    size = dv_extract_audio_info(c, buf);
    for (i = 0; i < c->ach; i++) {
        c->audio_pkt[i].pos  = pos;
        c->audio_pkt[i].size = size;
        c->audio_pkt[i].pts  = c->abytes * 30000 * 8 /
                               c->ast[i]->codecpar->bit_rate;
        ppcm[i] = c->audio_buf[i];
    }
    if (c->ach)
        dv_extract_audio(buf, ppcm, c->sys);

    /* We work with 720p frames split in half, thus even frames have
     * channels 0,1 and odd 2,3. */
    if (c->sys->height == 720) {
        if (buf[1] & 0x0C) {
            c->audio_pkt[2].size = c->audio_pkt[3].size = 0;
        } else {
            c->audio_pkt[0].size = c->audio_pkt[1].size = 0;
            c->abytes           += size;
        }
    } else {
        c->abytes += size;
    }

    /* Now it's time to return video packet */
    size = dv_extract_video_info(c, buf);
    av_init_packet(pkt);
    pkt->data         = buf;
    pkt->pos          = pos;
    pkt->size         = size;
    pkt->flags       |= AV_PKT_FLAG_KEY;
    pkt->stream_index = c->vst->index;
    pkt->pts          = c->frames;

    c->frames++;

    return size;
}

static int64_t dv_frame_offset(AVFormatContext *s, DVDemuxContext *c,
                               int64_t timestamp, int flags)
{
    // FIXME: sys may be wrong if last dv_read_packet() failed (buffer is junk)
    const int frame_size = c->sys->frame_size;
    int64_t offset;
    int64_t size       = avio_size(s->pb) - s->internal->data_offset;
    int64_t max_offset = ((size - 1) / frame_size) * frame_size;

    offset = frame_size * timestamp;

    if (size >= 0 && offset > max_offset)
        offset = max_offset;
    else if (offset < 0)
        offset = 0;

    return offset + s->internal->data_offset;
}

void ff_dv_offset_reset(DVDemuxContext *c, int64_t frame_offset)
{
    c->frames = frame_offset;
    if (c->ach) {
        if (c->sys) {
        c->abytes = av_rescale_q(c->frames, c->sys->time_base,
                                 (AVRational) { 8, c->ast[0]->codecpar->bit_rate });
        } else
            av_log(c->fctx, AV_LOG_ERROR, "cannot adjust audio bytes\n");
    }
    c->audio_pkt[0].size = c->audio_pkt[1].size = 0;
    c->audio_pkt[2].size = c->audio_pkt[3].size = 0;
}

/************************************************************
 * Implementation of the easiest DV storage of all -- raw DV.
 ************************************************************/

typedef struct RawDVContext {
    DVDemuxContext *dv_demux;
    uint8_t         buf[DV_MAX_FRAME_SIZE];
} RawDVContext;

static int dv_read_timecode(AVFormatContext *s) {
    int ret;
    char timecode[AV_TIMECODE_STR_SIZE];
    int64_t pos = avio_tell(s->pb);

    // Read 3 DIF blocks: Header block and 2 Subcode blocks.
    int partial_frame_size = 3 * 80;
    uint8_t *partial_frame = av_mallocz(sizeof(*partial_frame) *
                                        partial_frame_size);

    RawDVContext *c = s->priv_data;
    if (!partial_frame)
        return AVERROR(ENOMEM);

    ret = avio_read(s->pb, partial_frame, partial_frame_size);
    if (ret < 0)
        goto finish;

    if (ret < partial_frame_size) {
        ret = -1;
        goto finish;
    }

    ret = dv_extract_timecode(c->dv_demux, partial_frame, timecode);
    if (ret)
        av_dict_set(&s->metadata, "timecode", timecode, 0);
    else
        av_log(s, AV_LOG_ERROR, "Detected timecode is invalid\n");

finish:
    av_free(partial_frame);
    avio_seek(s->pb, pos, SEEK_SET);
    return ret;
}

static int dv_read_header(AVFormatContext *s)
{
    unsigned state, marker_pos = 0;
    RawDVContext *c = s->priv_data;

    c->dv_demux = avpriv_dv_init_demux(s);
    if (!c->dv_demux)
        return -1;

    state = avio_rb32(s->pb);
    while ((state & 0xffffff7f) != 0x1f07003f) {
        if (avio_feof(s->pb)) {
            av_log(s, AV_LOG_ERROR, "Cannot find DV header.\n");
            return -1;
        }
        if (state == 0x003f0700 || state == 0xff3f0700)
            marker_pos = avio_tell(s->pb);
        if (state == 0xff3f0701 && avio_tell(s->pb) - marker_pos == 80) {
            avio_seek(s->pb, -163, SEEK_CUR);
            state = avio_rb32(s->pb);
            break;
        }
        state = (state << 8) | avio_r8(s->pb);
    }
    AV_WB32(c->buf, state);

    if (avio_read(s->pb, c->buf + 4, DV_PROFILE_BYTES - 4) != DV_PROFILE_BYTES - 4 ||
        avio_seek(s->pb, -DV_PROFILE_BYTES, SEEK_CUR) < 0)
        return AVERROR(EIO);

    c->dv_demux->sys = av_dv_frame_profile(c->dv_demux->sys,
                                           c->buf,
                                           DV_PROFILE_BYTES);
    if (!c->dv_demux->sys) {
        av_log(s, AV_LOG_ERROR,
               "Can't determine profile of DV input stream.\n");
        return -1;
    }

    s->bit_rate = av_rescale_q(c->dv_demux->sys->frame_size,
                               (AVRational) { 8, 1 },
                               c->dv_demux->sys->time_base);

    if (s->pb->seekable & AVIO_SEEKABLE_NORMAL)
        dv_read_timecode(s);

    return 0;
}

static int dv_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int size;
    RawDVContext *c = s->priv_data;

    size = avpriv_dv_get_packet(c->dv_demux, pkt);

    if (size < 0) {
        int ret;
        int64_t pos = avio_tell(s->pb);
        if (!c->dv_demux->sys)
            return AVERROR(EIO);
        size = c->dv_demux->sys->frame_size;
        ret = avio_read(s->pb, c->buf, size);
        if (ret < 0) {
            return ret;
        } else if (ret == 0) {
            return AVERROR(EIO);
        }

        size = avpriv_dv_produce_packet(c->dv_demux, pkt, c->buf, size, pos);
    }

    return size;
}

static int dv_read_seek(AVFormatContext *s, int stream_index,
                        int64_t timestamp, int flags)
{
    RawDVContext *r   = s->priv_data;
    DVDemuxContext *c = r->dv_demux;
    int64_t offset    = dv_frame_offset(s, c, timestamp, flags);

    if (avio_seek(s->pb, offset, SEEK_SET) < 0)
        return -1;

    ff_dv_offset_reset(c, offset / c->sys->frame_size);
    return 0;
}

static int dv_read_close(AVFormatContext *s)
{
    RawDVContext *c = s->priv_data;
    av_freep(&c->dv_demux);
    return 0;
}

static int dv_probe(AVProbeData *p)
{
    unsigned marker_pos = 0;
    int i;
    int matches           = 0;
    int firstmatch        = 0;
    int secondary_matches = 0;

    if (p->buf_size < 5)
        return 0;

    for (i = 0; i < p->buf_size-4; i++) {
        unsigned state = AV_RB32(p->buf+i);
        if ((state & 0x0007f840) == 0x00070000) {
            // any section header, also with seq/chan num != 0,
            // should appear around every 12000 bytes, at least 10 per frame
            if ((state & 0xff07ff7f) == 0x1f07003f) {
                secondary_matches++;
                if ((state & 0xffffff7f) == 0x1f07003f) {
                    matches++;
                    if (!i)
                        firstmatch = 1;
                }
            }
            if (state == 0x003f0700 || state == 0xff3f0700)
                marker_pos = i;
            if (state == 0xff3f0701 && i - marker_pos == 80)
                matches++;
        }
    }

    if (matches && p->buf_size / matches < 1024 * 1024) {
        if (matches > 4 || firstmatch ||
            (secondary_matches >= 10 &&
             p->buf_size / secondary_matches < 24000))
            // not max to avoid dv in mov to match
            return AVPROBE_SCORE_MAX * 3 / 4;
        return AVPROBE_SCORE_MAX / 4;
    }
    return 0;
}

AVInputFormat ff_dv_demuxer = {
    .name           = "dv",
    .long_name      = NULL_IF_CONFIG_SMALL("DV (Digital Video)"),
    .priv_data_size = sizeof(RawDVContext),
    .read_probe     = dv_probe,
    .read_header    = dv_read_header,
    .read_packet    = dv_read_packet,
    .read_close     = dv_read_close,
    .read_seek      = dv_read_seek,
    .extensions     = "dv,dif",
};
