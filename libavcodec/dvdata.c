/*
 * Constants for DV codec
 * Copyright (c) 2002 Fabrice Bellard
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
 * Constants for DV codec.
 */

#include "libavutil/rational.h"
#include "avcodec.h"
#include "dvdata.h"

static DVwork_chunk work_chunks_dv25pal   [1*12*27];
static DVwork_chunk work_chunks_dv25pal411[1*12*27];
static DVwork_chunk work_chunks_dv25ntsc  [1*10*27];
static DVwork_chunk work_chunks_dv50pal   [2*12*27];
static DVwork_chunk work_chunks_dv50ntsc  [2*10*27];
static DVwork_chunk work_chunks_dv100palp [2*12*27];
static DVwork_chunk work_chunks_dv100ntscp[2*10*27];
static DVwork_chunk work_chunks_dv100pali [4*12*27];
static DVwork_chunk work_chunks_dv100ntsci[4*10*27];

static uint32_t dv_idct_factor_sd    [2*2*22*64];
static uint32_t dv_idct_factor_hd1080[2*4*16*64];
static uint32_t dv_idct_factor_hd720 [2*4*16*64];

static const DVprofile dv_profiles[] = {
    { .dsf = 0,
      .video_stype = 0x0,
      .frame_size = 120000,        /* IEC 61834, SMPTE-314M - 525/60 (NTSC) */
      .difseg_size = 10,
      .n_difchan = 1,
      .time_base = { 1001, 30000 },
      .ltc_divisor = 30,
      .height = 480,
      .width = 720,
      .sar = {{8, 9}, {32, 27}},
      .work_chunks = &work_chunks_dv25ntsc[0],
      .idct_factor = &dv_idct_factor_sd[0],
      .pix_fmt = PIX_FMT_YUV411P,
      .bpm = 6,
      .block_sizes = block_sizes_dv2550,
      .audio_stride = 90,
      .audio_min_samples  = { 1580, 1452, 1053 }, /* for 48, 44.1 and 32kHz */
      .audio_samples_dist = { 1600, 1602, 1602, 1602, 1602 }, /* per SMPTE-314M */
      .audio_shuffle = dv_audio_shuffle525,
    },
    { .dsf = 1,
      .video_stype = 0x0,
      .frame_size = 144000,        /* IEC 61834 - 625/50 (PAL) */
      .difseg_size = 12,
      .n_difchan = 1,
      .time_base = { 1, 25 },
      .ltc_divisor = 25,
      .height = 576,
      .width = 720,
      .sar = {{16, 15}, {64, 45}},
      .work_chunks = &work_chunks_dv25pal[0],
      .idct_factor = &dv_idct_factor_sd[0],
      .pix_fmt = PIX_FMT_YUV420P,
      .bpm = 6,
      .block_sizes = block_sizes_dv2550,
      .audio_stride = 108,
      .audio_min_samples  = { 1896, 1742, 1264 }, /* for 48, 44.1 and 32kHz */
      .audio_samples_dist = { 1920, 1920, 1920, 1920, 1920 },
      .audio_shuffle = dv_audio_shuffle625,
    },
    { .dsf = 1,
      .video_stype = 0x0,
      .frame_size = 144000,        /* SMPTE-314M - 625/50 (PAL) */
      .difseg_size = 12,
      .n_difchan = 1,
      .time_base = { 1, 25 },
      .ltc_divisor = 25,
      .height = 576,
      .width = 720,
      .sar = {{16, 15}, {64, 45}},
      .work_chunks = &work_chunks_dv25pal411[0],
      .idct_factor = &dv_idct_factor_sd[0],
      .pix_fmt = PIX_FMT_YUV411P,
      .bpm = 6,
      .block_sizes = block_sizes_dv2550,
      .audio_stride = 108,
      .audio_min_samples  = { 1896, 1742, 1264 }, /* for 48, 44.1 and 32kHz */
      .audio_samples_dist = { 1920, 1920, 1920, 1920, 1920 },
      .audio_shuffle = dv_audio_shuffle625,
    },
    { .dsf = 0,
      .video_stype = 0x4,
      .frame_size = 240000,        /* SMPTE-314M - 525/60 (NTSC) 50 Mbps */
      .difseg_size = 10,           /* also known as "DVCPRO50" */
      .n_difchan = 2,
      .time_base = { 1001, 30000 },
      .ltc_divisor = 30,
      .height = 480,
      .width = 720,
      .sar = {{8, 9}, {32, 27}},
      .work_chunks = &work_chunks_dv50ntsc[0],
      .idct_factor = &dv_idct_factor_sd[0],
      .pix_fmt = PIX_FMT_YUV422P,
      .bpm = 6,
      .block_sizes = block_sizes_dv2550,
      .audio_stride = 90,
      .audio_min_samples  = { 1580, 1452, 1053 }, /* for 48, 44.1 and 32kHz */
      .audio_samples_dist = { 1600, 1602, 1602, 1602, 1602 }, /* per SMPTE-314M */
      .audio_shuffle = dv_audio_shuffle525,
    },
    { .dsf = 1,
      .video_stype = 0x4,
      .frame_size = 288000,        /* SMPTE-314M - 625/50 (PAL) 50 Mbps */
      .difseg_size = 12,           /* also known as "DVCPRO50" */
      .n_difchan = 2,
      .time_base = { 1, 25 },
      .ltc_divisor = 25,
      .height = 576,
      .width = 720,
      .sar = {{16, 15}, {64, 45}},
      .work_chunks = &work_chunks_dv50pal[0],
      .idct_factor = &dv_idct_factor_sd[0],
      .pix_fmt = PIX_FMT_YUV422P,
      .bpm = 6,
      .block_sizes = block_sizes_dv2550,
      .audio_stride = 108,
      .audio_min_samples  = { 1896, 1742, 1264 }, /* for 48, 44.1 and 32kHz */
      .audio_samples_dist = { 1920, 1920, 1920, 1920, 1920 },
      .audio_shuffle = dv_audio_shuffle625,
    },
    { .dsf = 0,
      .video_stype = 0x14,
      .frame_size = 480000,        /* SMPTE-370M - 1080i60 100 Mbps */
      .difseg_size = 10,           /* also known as "DVCPRO HD" */
      .n_difchan = 4,
      .time_base = { 1001, 30000 },
      .ltc_divisor = 30,
      .height = 1080,
      .width = 1280,
      .sar = {{1, 1}, {3, 2}},
      .work_chunks = &work_chunks_dv100ntsci[0],
      .idct_factor = &dv_idct_factor_hd1080[0],
      .pix_fmt = PIX_FMT_YUV422P,
      .bpm = 8,
      .block_sizes = block_sizes_dv100,
      .audio_stride = 90,
      .audio_min_samples  = { 1580, 1452, 1053 }, /* for 48, 44.1 and 32kHz */
      .audio_samples_dist = { 1600, 1602, 1602, 1602, 1602 }, /* per SMPTE-314M */
      .audio_shuffle = dv_audio_shuffle525,
    },
    { .dsf = 1,
      .video_stype = 0x14,
      .frame_size = 576000,        /* SMPTE-370M - 1080i50 100 Mbps */
      .difseg_size = 12,           /* also known as "DVCPRO HD" */
      .n_difchan = 4,
      .time_base = { 1, 25 },
      .ltc_divisor = 25,
      .height = 1080,
      .width = 1440,
      .sar = {{1, 1}, {4, 3}},
      .work_chunks = &work_chunks_dv100pali[0],
      .idct_factor = &dv_idct_factor_hd1080[0],
      .pix_fmt = PIX_FMT_YUV422P,
      .bpm = 8,
      .block_sizes = block_sizes_dv100,
      .audio_stride = 108,
      .audio_min_samples  = { 1896, 1742, 1264 }, /* for 48, 44.1 and 32kHz */
      .audio_samples_dist = { 1920, 1920, 1920, 1920, 1920 },
      .audio_shuffle = dv_audio_shuffle625,
    },
    { .dsf = 0,
      .video_stype = 0x18,
      .frame_size = 240000,        /* SMPTE-370M - 720p60 100 Mbps */
      .difseg_size = 10,           /* also known as "DVCPRO HD" */
      .n_difchan = 2,
      .time_base = { 1001, 60000 },
      .ltc_divisor = 60,
      .height = 720,
      .width = 960,
      .sar = {{1, 1}, {4, 3}},
      .work_chunks = &work_chunks_dv100ntscp[0],
      .idct_factor = &dv_idct_factor_hd720[0],
      .pix_fmt = PIX_FMT_YUV422P,
      .bpm = 8,
      .block_sizes = block_sizes_dv100,
      .audio_stride = 90,
      .audio_min_samples  = { 1580, 1452, 1053 }, /* for 48, 44.1 and 32kHz */
      .audio_samples_dist = { 1600, 1602, 1602, 1602, 1602 }, /* per SMPTE-314M */
      .audio_shuffle = dv_audio_shuffle525,
    },
    { .dsf = 1,
      .video_stype = 0x18,
      .frame_size = 288000,        /* SMPTE-370M - 720p50 100 Mbps */
      .difseg_size = 12,           /* also known as "DVCPRO HD" */
      .n_difchan = 2,
      .time_base = { 1, 50 },
      .ltc_divisor = 50,
      .height = 720,
      .width = 960,
      .sar = {{1, 1}, {4, 3}},
      .work_chunks = &work_chunks_dv100palp[0],
      .idct_factor = &dv_idct_factor_hd720[0],
      .pix_fmt = PIX_FMT_YUV422P,
      .bpm = 8,
      .block_sizes = block_sizes_dv100,
      .audio_stride = 90,
      .audio_min_samples  = { 1896, 1742, 1264 }, /* for 48, 44.1 and 32kHz */
      .audio_samples_dist = { 1920, 1920, 1920, 1920, 1920 },
      .audio_shuffle = dv_audio_shuffle625,
    },
    { .dsf = 1,
      .video_stype = 0x1,
      .frame_size = 144000,        /* IEC 61883-5 - 625/50 (PAL) */
      .difseg_size = 12,
      .n_difchan = 1,
      .time_base = { 1, 25 },
      .ltc_divisor = 25,
      .height = 576,
      .width = 720,
      .sar = {{16, 15}, {64, 45}},
      .work_chunks = &work_chunks_dv25pal[0],
      .idct_factor = &dv_idct_factor_sd[0],
      .pix_fmt = PIX_FMT_YUV420P,
      .bpm = 6,
      .block_sizes = block_sizes_dv2550,
      .audio_stride = 108,
      .audio_min_samples  = { 1896, 1742, 1264 }, /* for 48, 44.1 and 32kHz */
      .audio_samples_dist = { 1920, 1920, 1920, 1920, 1920 },
      .audio_shuffle = dv_audio_shuffle625,
    }
};

const DVprofile* avpriv_dv_frame_profile(const DVprofile *sys,
                                  const uint8_t* frame, unsigned buf_size)
{
   int i;

   int dsf = (frame[3] & 0x80) >> 7;

   int stype = frame[80*5 + 48 + 3] & 0x1f;

   /* 576i50 25Mbps 4:1:1 is a special case */
   if (dsf == 1 && stype == 0 && frame[4] & 0x07 /* the APT field */) {
       return &dv_profiles[2];
   }

   for (i=0; i<FF_ARRAY_ELEMS(dv_profiles); i++)
       if (dsf == dv_profiles[i].dsf && stype == dv_profiles[i].video_stype)
           return &dv_profiles[i];

   /* check if old sys matches and assumes corrupted input */
   if (sys && buf_size == sys->frame_size)
       return sys;

   return NULL;
}

const DVprofile* avpriv_dv_codec_profile(AVCodecContext* codec)
{
    int i;

    for (i=0; i<FF_ARRAY_ELEMS(dv_profiles); i++)
       if (codec->height  == dv_profiles[i].height  &&
           codec->pix_fmt == dv_profiles[i].pix_fmt &&
           codec->width   == dv_profiles[i].width)
               return &dv_profiles[i];

    return NULL;
}
