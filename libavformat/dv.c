/*
 * General DV muxer/demuxer
 * Copyright (c) 2003 Roman Shaposhnik
 *
 * Many thanks to Dan Dennedy <dan@dennedy.org> for providing wealth
 * of DV technical info.
 *
 * Raw DV format
 * Copyright (c) 2002 Fabrice Bellard.
 *
 * 50 Mbps (DVCPRO50) support
 * Copyright (c) 2006 Daniel Maas <dmaas@maasdigital.com>
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
#include "dvdata.h"
#include "dv.h"

struct DVDemuxContext {
    const DVprofile*  sys;    /* Current DV profile. E.g.: 525/60, 625/50 */
    AVFormatContext* fctx;
    AVStream*        vst;
    AVStream*        ast[2];
    AVPacket         audio_pkt[2];
    uint8_t          audio_buf[2][8192];
    int              ach;
    int              frames;
    uint64_t         abytes;
};

static inline uint16_t dv_audio_12to16(uint16_t sample)
{
    uint16_t shift, result;

    sample = (sample < 0x800) ? sample : sample | 0xf000;
    shift = (sample & 0xf00) >> 8;

    if (shift < 0x2 || shift > 0xd) {
        result = sample;
    } else if (shift < 0x8) {
        shift--;
        result = (sample - (256 * shift)) << shift;
    } else {
        shift = 0xe - shift;
        result = ((sample + ((256 * shift) + 1)) << shift) - 1;
    }

    return result;
}

/*
 * This is the dumbest implementation of all -- it simply looks at
 * a fixed offset and if pack isn't there -- fails. We might want
 * to have a fallback mechanism for complete search of missing packs.
 */
static const uint8_t* dv_extract_pack(uint8_t* frame, enum dv_pack_type t)
{
    int offs;

    switch (t) {
    case dv_audio_source:
          offs = (80*6 + 80*16*3 + 3);
          break;
    case dv_audio_control:
          offs = (80*6 + 80*16*4 + 3);
          break;
    case dv_video_control:
          offs = (80*5 + 48 + 5);
          break;
    default:
          return NULL;
    }

    return (frame[offs] == t ? &frame[offs] : NULL);
}

/*
 * There's a couple of assumptions being made here:
 * 1. By default we silence erroneous (0x8000/16bit 0x800/12bit) audio samples.
 *    We can pass them upwards when ffmpeg will be ready to deal with them.
 * 2. We don't do software emphasis.
 * 3. Audio is always returned as 16bit linear samples: 12bit nonlinear samples
 *    are converted into 16bit linear ones.
 */
static int dv_extract_audio(uint8_t* frame, uint8_t* pcm, uint8_t* pcm2,
                            const DVprofile *sys)
{
    int size, chan, i, j, d, of, smpls, freq, quant, half_ch;
    uint16_t lc, rc;
    const uint8_t* as_pack;

    as_pack = dv_extract_pack(frame, dv_audio_source);
    if (!as_pack)    /* No audio ? */
        return 0;

    smpls = as_pack[1] & 0x3f; /* samples in this frame - min. samples */
    freq = (as_pack[4] >> 3) & 0x07; /* 0 - 48KHz, 1 - 44,1kHz, 2 - 32 kHz */
    quant = as_pack[4] & 0x07; /* 0 - 16bit linear, 1 - 12bit nonlinear */

    if (quant > 1)
        return -1; /* Unsupported quantization */

    size = (sys->audio_min_samples[freq] + smpls) * 4; /* 2ch, 2bytes */
    half_ch = sys->difseg_size/2;

    /* for each DIF channel */
    for (chan = 0; chan < sys->n_difchan; chan++) {
        /* for each DIF segment */
        for (i = 0; i < sys->difseg_size; i++) {
            frame += 6 * 80; /* skip DIF segment header */
            if (quant == 1 && i == half_ch) {
                /* next stereo channel (12bit mode only) */
                if (!pcm2)
                    break;
                else
                    pcm = pcm2;
            }

            /* for each AV sequence */
            for (j = 0; j < 9; j++) {
                for (d = 8; d < 80; d += 2) {
                    if (quant == 0) {  /* 16bit quantization */
                        of = sys->audio_shuffle[i][j] + (d - 8)/2 * sys->audio_stride;
                        if (of*2 >= size)
                            continue;

                        pcm[of*2] = frame[d+1]; // FIXME: may be we have to admit
                        pcm[of*2+1] = frame[d]; //        that DV is a big endian PCM
                        if (pcm[of*2+1] == 0x80 && pcm[of*2] == 0x00)
                            pcm[of*2+1] = 0;
                    } else {           /* 12bit quantization */
                        lc = ((uint16_t)frame[d] << 4) |
                             ((uint16_t)frame[d+2] >> 4);
                        rc = ((uint16_t)frame[d+1] << 4) |
                             ((uint16_t)frame[d+2] & 0x0f);
                        lc = (lc == 0x800 ? 0 : dv_audio_12to16(lc));
                        rc = (rc == 0x800 ? 0 : dv_audio_12to16(rc));

                        of = sys->audio_shuffle[i%half_ch][j] + (d - 8)/3 * sys->audio_stride;
                        if (of*2 >= size)
                            continue;

                        pcm[of*2] = lc & 0xff; // FIXME: may be we have to admit
                        pcm[of*2+1] = lc >> 8; //        that DV is a big endian PCM
                        of = sys->audio_shuffle[i%half_ch+half_ch][j] +
                            (d - 8)/3 * sys->audio_stride;
                        pcm[of*2] = rc & 0xff; // FIXME: may be we have to admit
                        pcm[of*2+1] = rc >> 8; //        that DV is a big endian PCM
                        ++d;
                    }
                }

                frame += 16 * 80; /* 15 Video DIFs + 1 Audio DIF */
            }
        }

        /* next stereo channel (50Mbps only) */
        if(!pcm2)
            break;
        pcm = pcm2;
    }

    return size;
}

static int dv_extract_audio_info(DVDemuxContext* c, uint8_t* frame)
{
    const uint8_t* as_pack;
    int freq, stype, smpls, quant, i, ach;

    as_pack = dv_extract_pack(frame, dv_audio_source);
    if (!as_pack || !c->sys) {    /* No audio ? */
        c->ach = 0;
        return 0;
    }

    smpls = as_pack[1] & 0x3f; /* samples in this frame - min. samples */
    freq = (as_pack[4] >> 3) & 0x07; /* 0 - 48KHz, 1 - 44,1kHz, 2 - 32 kHz */
    stype = (as_pack[3] & 0x1f); /* 0 - 2CH, 2 - 4CH */
    quant = as_pack[4] & 0x07; /* 0 - 16bit linear, 1 - 12bit nonlinear */

    /* note: ach counts PAIRS of channels (i.e. stereo channels) */
    ach = (stype == 2 || (quant && (freq == 2))) ? 2 : 1;

    /* Dynamic handling of the audio streams in DV */
    for (i=0; i<ach; i++) {
       if (!c->ast[i]) {
           c->ast[i] = av_new_stream(c->fctx, 0);
           if (!c->ast[i])
               break;
           av_set_pts_info(c->ast[i], 64, 1, 30000);
           c->ast[i]->codec->codec_type = CODEC_TYPE_AUDIO;
           c->ast[i]->codec->codec_id = CODEC_ID_PCM_S16LE;

           av_init_packet(&c->audio_pkt[i]);
           c->audio_pkt[i].size     = 0;
           c->audio_pkt[i].data     = c->audio_buf[i];
           c->audio_pkt[i].stream_index = c->ast[i]->index;
           c->audio_pkt[i].flags |= PKT_FLAG_KEY;
       }
       c->ast[i]->codec->sample_rate = dv_audio_frequency[freq];
       c->ast[i]->codec->channels = 2;
       c->ast[i]->codec->bit_rate = 2 * dv_audio_frequency[freq] * 16;
       c->ast[i]->start_time = 0;
    }
    c->ach = i;

    return (c->sys->audio_min_samples[freq] + smpls) * 4; /* 2ch, 2bytes */;
}

static int dv_extract_video_info(DVDemuxContext *c, uint8_t* frame)
{
    const uint8_t* vsc_pack;
    AVCodecContext* avctx;
    int apt, is16_9;
    int size = 0;

    if (c->sys) {
        avctx = c->vst->codec;

        av_set_pts_info(c->vst, 64, c->sys->frame_rate_base, c->sys->frame_rate);
        avctx->time_base= (AVRational){c->sys->frame_rate_base, c->sys->frame_rate};
        if(!avctx->width){
            avctx->width = c->sys->width;
            avctx->height = c->sys->height;
        }
        avctx->pix_fmt = c->sys->pix_fmt;

        /* finding out SAR is a little bit messy */
        vsc_pack = dv_extract_pack(frame, dv_video_control);
        apt = frame[4] & 0x07;
        is16_9 = (vsc_pack && ((vsc_pack[2] & 0x07) == 0x02 ||
                               (!apt && (vsc_pack[2] & 0x07) == 0x07)));
        avctx->sample_aspect_ratio = c->sys->sar[is16_9];
        avctx->bit_rate = av_rescale(c->sys->frame_size * 8,
                                     c->sys->frame_rate,
                                     c->sys->frame_rate_base);
        size = c->sys->frame_size;
    }
    return size;
}

/*
 * The following 3 functions constitute our interface to the world
 */

DVDemuxContext* dv_init_demux(AVFormatContext *s)
{
    DVDemuxContext *c;

    c = av_mallocz(sizeof(DVDemuxContext));
    if (!c)
        return NULL;

    c->vst = av_new_stream(s, 0);
    if (!c->vst) {
        av_free(c);
        return NULL;
    }

    c->sys = NULL;
    c->fctx = s;
    c->ast[0] = c->ast[1] = NULL;
    c->ach = 0;
    c->frames = 0;
    c->abytes = 0;

    c->vst->codec->codec_type = CODEC_TYPE_VIDEO;
    c->vst->codec->codec_id = CODEC_ID_DVVIDEO;
    c->vst->codec->bit_rate = 25000000;
    c->vst->start_time = 0;

    return c;
}

int dv_get_packet(DVDemuxContext *c, AVPacket *pkt)
{
    int size = -1;
    int i;

    for (i=0; i<c->ach; i++) {
       if (c->ast[i] && c->audio_pkt[i].size) {
           *pkt = c->audio_pkt[i];
           c->audio_pkt[i].size = 0;
           size = pkt->size;
           break;
       }
    }

    return size;
}

int dv_produce_packet(DVDemuxContext *c, AVPacket *pkt,
                      uint8_t* buf, int buf_size)
{
    int size, i;

    if (buf_size < DV_PROFILE_BYTES ||
        !(c->sys = dv_frame_profile(buf)) ||
        buf_size < c->sys->frame_size) {
          return -1;   /* Broken frame, or not enough data */
    }

    /* Queueing audio packet */
    /* FIXME: in case of no audio/bad audio we have to do something */
    size = dv_extract_audio_info(c, buf);
    for (i=0; i<c->ach; i++) {
       c->audio_pkt[i].size = size;
       c->audio_pkt[i].pts  = c->abytes * 30000*8 / c->ast[i]->codec->bit_rate;
    }
    dv_extract_audio(buf, c->audio_buf[0], c->audio_buf[1], c->sys);
    c->abytes += size;

    /* Now it's time to return video packet */
    size = dv_extract_video_info(c, buf);
    av_init_packet(pkt);
    pkt->data     = buf;
    pkt->size     = size;
    pkt->flags   |= PKT_FLAG_KEY;
    pkt->stream_index = c->vst->id;
    pkt->pts      = c->frames;

    c->frames++;

    return size;
}

static int64_t dv_frame_offset(AVFormatContext *s, DVDemuxContext *c,
                              int64_t timestamp, int flags)
{
    // FIXME: sys may be wrong if last dv_read_packet() failed (buffer is junk)
    const DVprofile* sys = dv_codec_profile(c->vst->codec);
    int64_t offset;
    int64_t size = url_fsize(&s->pb);
    int64_t max_offset = ((size-1) / sys->frame_size) * sys->frame_size;

    offset = sys->frame_size * timestamp;

    if (offset > max_offset) offset = max_offset;
    else if (offset < 0) offset = 0;

    return offset;
}

void dv_offset_reset(DVDemuxContext *c, int64_t frame_offset)
{
    c->frames= frame_offset;
    if (c->ach)
        c->abytes= av_rescale(c->frames,
                          c->ast[0]->codec->bit_rate * (int64_t)c->sys->frame_rate_base,
                          8*c->sys->frame_rate);
    c->audio_pkt[0].size = c->audio_pkt[1].size = 0;
}

/************************************************************
 * Implementation of the easiest DV storage of all -- raw DV.
 ************************************************************/

typedef struct RawDVContext {
    DVDemuxContext* dv_demux;
    uint8_t         buf[DV_MAX_FRAME_SIZE];
} RawDVContext;

static int dv_read_header(AVFormatContext *s,
                          AVFormatParameters *ap)
{
    RawDVContext *c = s->priv_data;

    c->dv_demux = dv_init_demux(s);
    if (!c->dv_demux)
        return -1;

    if (get_buffer(&s->pb, c->buf, DV_PROFILE_BYTES) <= 0 ||
        url_fseek(&s->pb, -DV_PROFILE_BYTES, SEEK_CUR) < 0)
        return AVERROR_IO;

    c->dv_demux->sys = dv_frame_profile(c->buf);
    s->bit_rate = av_rescale(c->dv_demux->sys->frame_size * 8,
                             c->dv_demux->sys->frame_rate,
                             c->dv_demux->sys->frame_rate_base);

    return 0;
}


static int dv_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int size;
    RawDVContext *c = s->priv_data;

    size = dv_get_packet(c->dv_demux, pkt);

    if (size < 0) {
        size = c->dv_demux->sys->frame_size;
        if (get_buffer(&s->pb, c->buf, size) <= 0)
            return AVERROR_IO;

        size = dv_produce_packet(c->dv_demux, pkt, c->buf, size);
    }

    return size;
}

static int dv_read_seek(AVFormatContext *s, int stream_index,
                       int64_t timestamp, int flags)
{
    RawDVContext *r = s->priv_data;
    DVDemuxContext *c = r->dv_demux;
    int64_t offset= dv_frame_offset(s, c, timestamp, flags);

    dv_offset_reset(c, offset / c->sys->frame_size);

    return url_fseek(&s->pb, offset, SEEK_SET);
}

static int dv_read_close(AVFormatContext *s)
{
    RawDVContext *c = s->priv_data;
    av_free(c->dv_demux);
    return 0;
}

#ifdef CONFIG_DV_DEMUXER
AVInputFormat dv_demuxer = {
    "dv",
    "DV video format",
    sizeof(RawDVContext),
    NULL,
    dv_read_header,
    dv_read_packet,
    dv_read_close,
    dv_read_seek,
    .extensions = "dv,dif",
};
#endif
