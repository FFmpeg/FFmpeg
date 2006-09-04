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
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
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

struct DVMuxContext {
    const DVprofile*  sys;    /* Current DV profile. E.g.: 525/60, 625/50 */
    int         n_ast;        /* Number of stereo audio streams (up to 2) */
    AVStream   *ast[2];       /* Stereo audio streams */
    FifoBuffer  audio_data[2]; /* Fifo for storing excessive amounts of PCM */
    int         frames;       /* Number of a current frame */
    time_t      start_time;   /* Start time of recording */
    int         has_audio;    /* frame under contruction has audio */
    int         has_video;    /* frame under contruction has video */
    uint8_t     frame_buf[DV_MAX_FRAME_SIZE]; /* frame under contruction */
};

static const int dv_aaux_packs_dist[12][9] = {
    { 0xff, 0xff, 0xff, 0x50, 0x51, 0x52, 0x53, 0xff, 0xff },
    { 0x50, 0x51, 0x52, 0x53, 0xff, 0xff, 0xff, 0xff, 0xff },
    { 0xff, 0xff, 0xff, 0x50, 0x51, 0x52, 0x53, 0xff, 0xff },
    { 0x50, 0x51, 0x52, 0x53, 0xff, 0xff, 0xff, 0xff, 0xff },
    { 0xff, 0xff, 0xff, 0x50, 0x51, 0x52, 0x53, 0xff, 0xff },
    { 0x50, 0x51, 0x52, 0x53, 0xff, 0xff, 0xff, 0xff, 0xff },
    { 0xff, 0xff, 0xff, 0x50, 0x51, 0x52, 0x53, 0xff, 0xff },
    { 0x50, 0x51, 0x52, 0x53, 0xff, 0xff, 0xff, 0xff, 0xff },
    { 0xff, 0xff, 0xff, 0x50, 0x51, 0x52, 0x53, 0xff, 0xff },
    { 0x50, 0x51, 0x52, 0x53, 0xff, 0xff, 0xff, 0xff, 0xff },
    { 0xff, 0xff, 0xff, 0x50, 0x51, 0x52, 0x53, 0xff, 0xff },
    { 0x50, 0x51, 0x52, 0x53, 0xff, 0xff, 0xff, 0xff, 0xff },
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

static int dv_audio_frame_size(const DVprofile* sys, int frame)
{
    return sys->audio_samples_dist[frame % (sizeof(sys->audio_samples_dist)/
                                            sizeof(sys->audio_samples_dist[0]))];
}

static int dv_write_pack(enum dv_pack_type pack_id, DVMuxContext *c, uint8_t* buf)
{
    struct tm tc;
    time_t ct;
    int ltc_frame;

    /* Its hard to tell what SMPTE requires w.r.t. APT, but Quicktime needs it.
     * We set it based on pix_fmt value but it really should be per DV profile */
    int apt = (c->sys->pix_fmt == PIX_FMT_YUV422P ? 1 : 0);

    buf[0] = (uint8_t)pack_id;
    switch (pack_id) {
    case dv_timecode:
          ct = (time_t)(c->frames / ((float)c->sys->frame_rate /
                                     (float)c->sys->frame_rate_base));
          brktimegm(ct, &tc);
          /*
           * LTC drop-frame frame counter drops two frames (0 and 1) every
           * minute, unless it is exactly divisible by 10
           */
          ltc_frame = (c->frames + 2*ct/60 - 2*ct/600) % c->sys->ltc_divisor;
          buf[1] = (0 << 7) | /* Color fame: 0 - unsync; 1 - sync mode */
                   (1 << 6) | /* Drop frame timecode: 0 - nondrop; 1 - drop */
                   ((ltc_frame / 10) << 4) | /* Tens of frames */
                   (ltc_frame % 10);         /* Units of frames */
          buf[2] = (1 << 7) | /* Biphase mark polarity correction: 0 - even; 1 - odd */
                   ((tc.tm_sec / 10) << 4) | /* Tens of seconds */
                   (tc.tm_sec % 10);         /* Units of seconds */
          buf[3] = (1 << 7) | /* Binary group flag BGF0 */
                   ((tc.tm_min / 10) << 4) | /* Tens of minutes */
                   (tc.tm_min % 10);         /* Units of minutes */
          buf[4] = (1 << 7) | /* Binary group flag BGF2 */
                   (1 << 6) | /* Binary group flag BGF1 */
                   ((tc.tm_hour / 10) << 4) | /* Tens of hours */
                   (tc.tm_hour % 10);         /* Units of hours */
          break;
    case dv_audio_source:  /* AAUX source pack */
          buf[1] = (0 << 7) | /* locked mode       */
                   (1 << 6) | /* reserved -- always 1 */
                   (dv_audio_frame_size(c->sys, c->frames) -
                    c->sys->audio_min_samples[0]);
                              /* # of samples      */
          buf[2] = (0 << 7) | /* multi-stereo      */
                   (0 << 5) | /* #of audio channels per block: 0 -- 1 channel */
                   (0 << 4) | /* pair bit: 0 -- one pair of channels */
                    0;        /* audio mode        */
          buf[3] = (1 << 7) | /* res               */
                   (1 << 6) | /* multi-language flag */
                   (c->sys->dsf << 5) | /*  system: 60fields/50fields */
                   (apt << 1);/* definition: 0 -- 25Mbps, 2 -- 50Mbps */
          buf[4] = (1 << 7) | /* emphasis: 1 -- off */
                   (0 << 6) | /* emphasis time constant: 0 -- reserved */
                   (0 << 3) | /* frequency: 0 -- 48Khz, 1 -- 44,1Khz, 2 -- 32Khz */
                    0;        /* quantization: 0 -- 16bit linear, 1 -- 12bit nonlinear */
          break;
    case dv_audio_control:
          buf[1] = (0 << 6) | /* copy protection: 0 -- unrestricted */
                   (1 << 4) | /* input source: 1 -- digital input */
                   (3 << 2) | /* compression: 3 -- no information */
                    0;        /* misc. info/SMPTE emphasis off */
          buf[2] = (1 << 7) | /* recording start point: 1 -- no */
                   (1 << 6) | /* recording end point: 1 -- no */
                   (1 << 3) | /* recording mode: 1 -- original */
                    7;
          buf[3] = (1 << 7) | /* direction: 1 -- forward */
                    0x20;     /* speed */
          buf[4] = (1 << 7) | /* reserved -- always 1 */
                    0x7f;     /* genre category */
          break;
    case dv_audio_recdate:
    case dv_video_recdate:  /* VAUX recording date */
          ct = c->start_time + (time_t)(c->frames /
               ((float)c->sys->frame_rate / (float)c->sys->frame_rate_base));
          brktimegm(ct, &tc);
          buf[1] = 0xff; /* ds, tm, tens of time zone, units of time zone */
                         /* 0xff is very likely to be "unknown" */
          buf[2] = (3 << 6) | /* reserved -- always 1 */
                   ((tc.tm_mday / 10) << 4) | /* Tens of day */
                   (tc.tm_mday % 10);         /* Units of day */
          buf[3] = /* we set high 4 bits to 0, shouldn't we set them to week? */
                   ((tc.tm_mon / 10) << 4) |    /* Tens of month */
                   (tc.tm_mon  % 10);           /* Units of month */
          buf[4] = (((tc.tm_year % 100) / 10) << 4) | /* Tens of year */
                   (tc.tm_year % 10);                 /* Units of year */
          break;
    case dv_audio_rectime:  /* AAUX recording time */
    case dv_video_rectime:  /* VAUX recording time */
          ct = c->start_time + (time_t)(c->frames /
               ((float)c->sys->frame_rate / (float)c->sys->frame_rate_base));
          brktimegm(ct, &tc);
          buf[1] = (3 << 6) | /* reserved -- always 1 */
                   0x3f; /* tens of frame, units of frame: 0x3f - "unknown" ? */
          buf[2] = (1 << 7) | /* reserved -- always 1 */
                   ((tc.tm_sec / 10) << 4) | /* Tens of seconds */
                   (tc.tm_sec % 10);         /* Units of seconds */
          buf[3] = (1 << 7) | /* reserved -- always 1 */
                   ((tc.tm_min / 10) << 4) | /* Tens of minutes */
                   (tc.tm_min % 10);         /* Units of minutes */
          buf[4] = (3 << 6) | /* reserved -- always 1 */
                   ((tc.tm_hour / 10) << 4) | /* Tens of hours */
                   (tc.tm_hour % 10);         /* Units of hours */
          break;
    default:
          buf[1] = buf[2] = buf[3] = buf[4] = 0xff;
    }
    return 5;
}

static void dv_inject_audio(DVMuxContext *c, int channel, uint8_t* frame_ptr)
{
    int i, j, d, of, size;
    size = 4 * dv_audio_frame_size(c->sys, c->frames);
    frame_ptr += channel * c->sys->difseg_size * 150 * 80;
    for (i = 0; i < c->sys->difseg_size; i++) {
       frame_ptr += 6 * 80; /* skip DIF segment header */
       for (j = 0; j < 9; j++) {
          dv_write_pack(dv_aaux_packs_dist[i][j], c, &frame_ptr[3]);
          for (d = 8; d < 80; d+=2) {
             of = c->sys->audio_shuffle[i][j] + (d - 8)/2 * c->sys->audio_stride;
             if (of*2 >= size)
                 continue;

             frame_ptr[d] = fifo_peek(&c->audio_data[channel], of*2+1); // FIXME: may be we have to admit
             frame_ptr[d+1] = fifo_peek(&c->audio_data[channel], of*2); //        that DV is a big endian PCM
          }
          frame_ptr += 16 * 80; /* 15 Video DIFs + 1 Audio DIF */
       }
    }
}

static void dv_inject_metadata(DVMuxContext *c, uint8_t* frame)
{
    int j, k;
    uint8_t* buf;

    for (buf = frame; buf < frame + c->sys->frame_size; buf += 150 * 80) {
        /* DV subcode: 2nd and 3d DIFs */
        for (j = 80; j < 80 * 3; j += 80) {
            for (k = 6; k < 6 * 8; k += 8)
                dv_write_pack(dv_timecode, c, &buf[j+k]);

            if (((long)(buf-frame)/(c->sys->frame_size/(c->sys->difseg_size*c->sys->n_difchan))%c->sys->difseg_size) > 5) { /* FIXME: is this really needed ? */
                dv_write_pack(dv_video_recdate, c, &buf[j+14]);
                dv_write_pack(dv_video_rectime, c, &buf[j+22]);
                dv_write_pack(dv_video_recdate, c, &buf[j+38]);
                dv_write_pack(dv_video_rectime, c, &buf[j+46]);
            }
        }

        /* DV VAUX: 4th, 5th and 6th 3DIFs */
        for (j = 80*3 + 3; j < 80*6; j += 80) {
            dv_write_pack(dv_video_recdate, c, &buf[j+5*2]);
            dv_write_pack(dv_video_rectime, c, &buf[j+5*3]);
            dv_write_pack(dv_video_recdate, c, &buf[j+5*11]);
            dv_write_pack(dv_video_rectime, c, &buf[j+5*12]);
        }
    }
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
 * The following 6 functions constitute our interface to the world
 */

int dv_assemble_frame(DVMuxContext *c, AVStream* st,
                      const uint8_t* data, int data_size, uint8_t** frame)
{
    int i, reqasize;

    *frame = &c->frame_buf[0];
    reqasize = 4 * dv_audio_frame_size(c->sys, c->frames);

    switch (st->codec->codec_type) {
    case CODEC_TYPE_VIDEO:
          /* FIXME: we have to have more sensible approach than this one */
          if (c->has_video)
              av_log(st->codec, AV_LOG_ERROR, "Can't process DV frame #%d. Insufficient audio data or severe sync problem.\n", c->frames);

          memcpy(*frame, data, c->sys->frame_size);
          c->has_video = 1;
          break;
    case CODEC_TYPE_AUDIO:
          for (i = 0; i < c->n_ast && st != c->ast[i]; i++);

          /* FIXME: we have to have more sensible approach than this one */
          if (fifo_size(&c->audio_data[i], c->audio_data[i].rptr) + data_size >= 100*AVCODEC_MAX_AUDIO_FRAME_SIZE)
              av_log(st->codec, AV_LOG_ERROR, "Can't process DV frame #%d. Insufficient video data or severe sync problem.\n", c->frames);
          fifo_write(&c->audio_data[i], data, data_size, &c->audio_data[i].wptr);

          /* Lets see if we've got enough audio for one DV frame */
          c->has_audio |= ((reqasize <= fifo_size(&c->audio_data[i], c->audio_data[i].rptr)) << i);

          break;
    default:
          break;
    }

    /* Lets see if we have enough data to construct one DV frame */
    if (c->has_video == 1 && c->has_audio + 1 == 1<<c->n_ast) {
        dv_inject_metadata(c, *frame);
        for (i=0; i<c->n_ast; i++) {
             dv_inject_audio(c, i, *frame);
             fifo_drain(&c->audio_data[i], reqasize);
        }

        c->has_video = 0;
        c->has_audio = 0;
        c->frames++;

        return c->sys->frame_size;
    }

    return 0;
}

DVMuxContext* dv_init_mux(AVFormatContext* s)
{
    DVMuxContext *c;
    AVStream *vst = NULL;
    int i;

    /* we support at most 1 video and 2 audio streams */
    if (s->nb_streams > 3)
        return NULL;

    c = av_mallocz(sizeof(DVMuxContext));
    if (!c)
        return NULL;

    c->n_ast = 0;
    c->ast[0] = c->ast[1] = NULL;

    /* We have to sort out where audio and where video stream is */
    for (i=0; i<s->nb_streams; i++) {
         switch (s->streams[i]->codec->codec_type) {
         case CODEC_TYPE_VIDEO:
               vst = s->streams[i];
               break;
         case CODEC_TYPE_AUDIO:
             c->ast[c->n_ast++] = s->streams[i];
             break;
         default:
               goto bail_out;
         }
    }

    /* Some checks -- DV format is very picky about its incoming streams */
    if (!vst || vst->codec->codec_id != CODEC_ID_DVVIDEO)
        goto bail_out;
    for (i=0; i<c->n_ast; i++) {
        if (c->ast[i] && (c->ast[i]->codec->codec_id != CODEC_ID_PCM_S16LE ||
                          c->ast[i]->codec->sample_rate != 48000 ||
                          c->ast[i]->codec->channels != 2))
            goto bail_out;
    }
    c->sys = dv_codec_profile(vst->codec);
    if (!c->sys)
        goto bail_out;

    if((c->n_ast > 1) && (c->sys->n_difchan < 2)) {
        /* only 1 stereo pair is allowed in 25Mbps mode */
        goto bail_out;
    }

    /* Ok, everything seems to be in working order */
    c->frames = 0;
    c->has_audio = 0;
    c->has_video = 0;
    c->start_time = (time_t)s->timestamp;

    for (i=0; i<c->n_ast; i++) {
        if (c->ast[i] && fifo_init(&c->audio_data[i], 100*AVCODEC_MAX_AUDIO_FRAME_SIZE) < 0) {
            while (i>0) {
                i--;
                fifo_free(&c->audio_data[i]);
            }
            goto bail_out;
        }
    }

    return c;

bail_out:
    av_free(c);
    return NULL;
}

void dv_delete_mux(DVMuxContext *c)
{
    int i;
    for (i=0; i < c->n_ast; i++)
        fifo_free(&c->audio_data[i]);
}

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

void dv_flush_audio_packets(DVDemuxContext *c)
{
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

    c->frames= offset / c->sys->frame_size;
    if (c->ach)
        c->abytes= av_rescale(c->frames,
                          c->ast[0]->codec->bit_rate * (int64_t)c->sys->frame_rate_base,
                          8*c->sys->frame_rate);

    dv_flush_audio_packets(c);
    return url_fseek(&s->pb, offset, SEEK_SET);
}

static int dv_read_close(AVFormatContext *s)
{
    RawDVContext *c = s->priv_data;
    av_free(c->dv_demux);
    return 0;
}

#ifdef CONFIG_MUXERS
static int dv_write_header(AVFormatContext *s)
{
    s->priv_data = dv_init_mux(s);
    if (!s->priv_data) {
        av_log(s, AV_LOG_ERROR, "Can't initialize DV format!\n"
                    "Make sure that you supply exactly two streams:\n"
                    "     video: 25fps or 29.97fps, audio: 2ch/48Khz/PCM\n"
                    "     (50Mbps allows an optional second audio stream)\n");
        return -1;
    }
    return 0;
}

static int dv_write_packet(struct AVFormatContext *s, AVPacket *pkt)
{
    uint8_t* frame;
    int fsize;

    fsize = dv_assemble_frame((DVMuxContext *)s->priv_data, s->streams[pkt->stream_index],
                              pkt->data, pkt->size, &frame);
    if (fsize > 0) {
        put_buffer(&s->pb, frame, fsize);
        put_flush_packet(&s->pb);
    }
    return 0;
}

/*
 * We might end up with some extra A/V data without matching counterpart.
 * E.g. video data without enough audio to write the complete frame.
 * Currently we simply drop the last frame. I don't know whether this
 * is the best strategy of all
 */
static int dv_write_trailer(struct AVFormatContext *s)
{
    dv_delete_mux((DVMuxContext *)s->priv_data);
    return 0;
}
#endif /* CONFIG_MUXERS */

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
#ifdef CONFIG_DV_MUXER
AVOutputFormat dv_muxer = {
    "dv",
    "DV video format",
    NULL,
    "dv",
    sizeof(DVMuxContext),
    CODEC_ID_PCM_S16LE,
    CODEC_ID_DVVIDEO,
    dv_write_header,
    dv_write_packet,
    dv_write_trailer,
};
#endif
