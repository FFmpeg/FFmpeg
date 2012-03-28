/*
 * Constants for DV codec
 * Copyright (c) 2002 Fabrice Bellard
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

/**
 * @file
 * Constants for DV codec.
 */

#ifndef AVCODEC_DVDATA_H
#define AVCODEC_DVDATA_H

#include "libavutil/rational.h"
#include "avcodec.h"
#include "dsputil.h"
#include "get_bits.h"

typedef struct DVwork_chunk {
    uint16_t  buf_offset;
    uint16_t  mb_coordinates[5];
} DVwork_chunk;

/*
 * DVprofile is used to express the differences between various
 * DV flavors. For now it's primarily used for differentiating
 * 525/60 and 625/50, but the plans are to use it for various
 * DV specs as well (e.g. SMPTE314M vs. IEC 61834).
 */
typedef struct DVprofile {
    int              dsf;                   /* value of the dsf in the DV header */
    int              video_stype;           /* stype for VAUX source pack */
    int              frame_size;            /* total size of one frame in bytes */
    int              difseg_size;           /* number of DIF segments per DIF channel */
    int              n_difchan;             /* number of DIF channels per frame */
    AVRational       time_base;             /* 1/framerate */
    int              ltc_divisor;           /* FPS from the LTS standpoint */
    int              height;                /* picture height in pixels */
    int              width;                 /* picture width in pixels */
    AVRational       sar[2];                /* sample aspect ratios for 4:3 and 16:9 */
    DVwork_chunk    *work_chunks;           /* each thread gets its own chunk of frame to work on */
    uint32_t        *idct_factor;           /* set of iDCT factor tables */
    enum PixelFormat pix_fmt;               /* picture pixel format */
    int              bpm;                   /* blocks per macroblock */
    const uint8_t   *block_sizes;           /* AC block sizes, in bits */
    int              audio_stride;          /* size of audio_shuffle table */
    int              audio_min_samples[3];  /* min amount of audio samples */
                                            /* for 48kHz, 44.1kHz and 32kHz */
    int              audio_samples_dist[5]; /* how many samples are supposed to be */
                                            /* in each frame in a 5 frames window */
    const uint8_t  (*audio_shuffle)[9];     /* PCM shuffling table */
} DVprofile;

typedef struct DVVideoContext {
    const DVprofile *sys;
    AVFrame          picture;
    AVCodecContext  *avctx;
    uint8_t         *buf;

    uint8_t  dv_zigzag[2][64];

    void (*get_pixels)(DCTELEM *block, const uint8_t *pixels, int line_size);
    void (*fdct[2])(DCTELEM *block);
    void (*idct_put[2])(uint8_t *dest, int line_size, DCTELEM *block);
    me_cmp_func ildct_cmp;
} DVVideoContext;

enum dv_section_type {
     dv_sect_header  = 0x1f,
     dv_sect_subcode = 0x3f,
     dv_sect_vaux    = 0x56,
     dv_sect_audio   = 0x76,
     dv_sect_video   = 0x96,
};

enum dv_pack_type {
     dv_header525     = 0x3f, /* see dv_write_pack for important details on */
     dv_header625     = 0xbf, /* these two packs */
     dv_timecode      = 0x13,
     dv_audio_source  = 0x50,
     dv_audio_control = 0x51,
     dv_audio_recdate = 0x52,
     dv_audio_rectime = 0x53,
     dv_video_source  = 0x60,
     dv_video_control = 0x61,
     dv_video_recdate = 0x62,
     dv_video_rectime = 0x63,
     dv_unknown_pack  = 0xff,
};

#define DV_PROFILE_IS_HD(p) ((p)->video_stype & 0x10)
#define DV_PROFILE_IS_1080i50(p) (((p)->video_stype == 0x14) && ((p)->dsf == 1))
#define DV_PROFILE_IS_720p50(p)  (((p)->video_stype == 0x18) && ((p)->dsf == 1))

/* minimum number of bytes to read from a DV stream in order to
   determine the profile */
#define DV_PROFILE_BYTES (6*80) /* 6 DIF blocks */

/**
 * largest possible DV frame, in bytes (1080i50)
 */
#define DV_MAX_FRAME_SIZE 576000

/**
 * maximum number of blocks per macroblock in any DV format
 */
#define DV_MAX_BPM 8

#define TEX_VLC_BITS 9

extern RL_VLC_ELEM ff_dv_rl_vlc[1184];

const DVprofile* avpriv_dv_frame_profile(const DVprofile *sys,
                                  const uint8_t* frame, unsigned buf_size);
const DVprofile* avpriv_dv_frame_profile2(AVCodecContext* codec, const DVprofile *sys,
                                  const uint8_t* frame, unsigned buf_size);
const DVprofile* avpriv_dv_codec_profile(AVCodecContext* codec);

int ff_dv_init_dynamic_tables(const DVprofile *d);
int ff_dvvideo_init(AVCodecContext *avctx);

static inline int dv_work_pool_size(const DVprofile *d)
{
    int size = d->n_difchan*d->difseg_size*27;
    if (DV_PROFILE_IS_1080i50(d))
        size -= 3*27;
    if (DV_PROFILE_IS_720p50(d))
        size -= 4*27;
    return size;
}

static inline void dv_calculate_mb_xy(DVVideoContext *s, DVwork_chunk *work_chunk, int m, int *mb_x, int *mb_y)
{
     *mb_x = work_chunk->mb_coordinates[m] & 0xff;
     *mb_y = work_chunk->mb_coordinates[m] >> 8;

     /* We work with 720p frames split in half. The odd half-frame (chan==2,3) is displaced :-( */
     if (s->sys->height == 720 && !(s->buf[1]&0x0C)) {
         *mb_y -= (*mb_y>17)?18:-72; /* shifting the Y coordinate down by 72/2 macro blocks */
     }
}

/**
 *  Print all allowed DV profiles into logctx at specified logging level.
 */
void ff_dv_print_profiles(void *logctx, int loglevel);

#endif /* AVCODEC_DVDATA_H */
