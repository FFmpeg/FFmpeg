/*
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

#include "config.h"

#include <stdint.h>

#include "libavutil/common.h"
#include "libavutil/log.h"
#include "libavutil/pixdesc.h"

#include "avcodec.h"
#include "dv_profile.h"
#include "dv_profile_internal.h"

#if CONFIG_DVPROFILE

static const uint8_t dv_audio_shuffle525[10][9] = {
    {  0, 30, 60, 20, 50, 80, 10, 40, 70 }, /* 1st channel */
    {  6, 36, 66, 26, 56, 86, 16, 46, 76 },
    { 12, 42, 72,  2, 32, 62, 22, 52, 82 },
    { 18, 48, 78,  8, 38, 68, 28, 58, 88 },
    { 24, 54, 84, 14, 44, 74,  4, 34, 64 },

    {  1, 31, 61, 21, 51, 81, 11, 41, 71 }, /* 2nd channel */
    {  7, 37, 67, 27, 57, 87, 17, 47, 77 },
    { 13, 43, 73,  3, 33, 63, 23, 53, 83 },
    { 19, 49, 79,  9, 39, 69, 29, 59, 89 },
    { 25, 55, 85, 15, 45, 75,  5, 35, 65 },
};

static const uint8_t dv_audio_shuffle625[12][9] = {
    {  0, 36,  72, 26, 62,  98, 16, 52,  88 }, /* 1st channel */
    {  6, 42,  78, 32, 68, 104, 22, 58,  94 },
    { 12, 48,  84,  2, 38,  74, 28, 64, 100 },
    { 18, 54,  90,  8, 44,  80, 34, 70, 106 },
    { 24, 60,  96, 14, 50,  86,  4, 40,  76 },
    { 30, 66, 102, 20, 56,  92, 10, 46,  82 },

    {  1, 37,  73, 27, 63,  99, 17, 53,  89 }, /* 2nd channel */
    {  7, 43,  79, 33, 69, 105, 23, 59,  95 },
    { 13, 49,  85,  3, 39,  75, 29, 65, 101 },
    { 19, 55,  91,  9, 45,  81, 35, 71, 107 },
    { 25, 61,  97, 15, 51,  87,  5, 41,  77 },
    { 31, 67, 103, 21, 57,  93, 11, 47,  83 },
};

/* macroblock bit budgets */
static const uint8_t block_sizes_dv2550[8] = {
    112, 112, 112, 112, 80, 80, 0, 0,
};

static const uint8_t block_sizes_dv100[8] = {
    80, 80, 80, 80, 80, 80, 64, 64,
};

static const AVDVProfile dv_profiles[] = {
    { .dsf                = 0,
      .video_stype        = 0x0,
      .frame_size         = 120000, /* IEC 61834, SMPTE-314M - 525/60 (NTSC) */
      .difseg_size        = 10,
      .n_difchan          = 1,
      .time_base          = { 1001, 30000 },
      .ltc_divisor        = 30,
      .height             = 480,
      .width              = 720,
      .sar                = { { 8, 9 }, { 32, 27 } },
      .pix_fmt            = AV_PIX_FMT_YUV411P,
      .bpm                = 6,
      .block_sizes        = block_sizes_dv2550,
      .audio_stride       = 90,
      .audio_min_samples  = { 1580, 1452, 1053 }, /* for 48, 44.1 and 32kHz */
      .audio_samples_dist = { 1600, 1602, 1602, 1602, 1602 },  /* per SMPTE-314M */
      .audio_shuffle      = dv_audio_shuffle525, },
    { .dsf                = 1,
      .video_stype        = 0x0,
      .frame_size         = 144000, /* IEC 61834 - 625/50 (PAL) */
      .difseg_size        = 12,
      .n_difchan          = 1,
      .time_base          = { 1, 25 },
      .ltc_divisor        = 25,
      .height             = 576,
      .width              = 720,
      .sar                = { { 16, 15 }, { 64, 45 } },
      .pix_fmt            = AV_PIX_FMT_YUV420P,
      .bpm                = 6,
      .block_sizes        = block_sizes_dv2550,
      .audio_stride       = 108,
      .audio_min_samples  = { 1896, 1742, 1264 }, /* for 48, 44.1 and 32kHz */
      .audio_samples_dist = { 1920, 1920, 1920, 1920, 1920 },
      .audio_shuffle      = dv_audio_shuffle625, },
    { .dsf                = 1,
      .video_stype        = 0x0,
      .frame_size         = 144000, /* SMPTE-314M - 625/50 (PAL) */
      .difseg_size        = 12,
      .n_difchan          = 1,
      .time_base          = { 1, 25 },
      .ltc_divisor        = 25,
      .height             = 576,
      .width              = 720,
      .sar                = { { 16, 15 }, { 64, 45 } },
      .pix_fmt            = AV_PIX_FMT_YUV411P,
      .bpm                = 6,
      .block_sizes        = block_sizes_dv2550,
      .audio_stride       = 108,
      .audio_min_samples  = { 1896, 1742, 1264 }, /* for 48, 44.1 and 32kHz */
      .audio_samples_dist = { 1920, 1920, 1920, 1920, 1920 },
      .audio_shuffle      = dv_audio_shuffle625, },
    { .dsf                = 0,
      .video_stype        = 0x4,
      .frame_size         = 240000, /* SMPTE-314M - 525/60 (NTSC) 50 Mbps */
      .difseg_size        = 10,    /* also known as "DVCPRO50" */
      .n_difchan          = 2,
      .time_base          = { 1001, 30000 },
      .ltc_divisor        = 30,
      .height             = 480,
      .width              = 720,
      .sar                = { { 8, 9 }, { 32, 27 } },
      .pix_fmt            = AV_PIX_FMT_YUV422P,
      .bpm                = 6,
      .block_sizes        = block_sizes_dv2550,
      .audio_stride       = 90,
      .audio_min_samples  = { 1580, 1452, 1053 }, /* for 48, 44.1 and 32kHz */
      .audio_samples_dist = { 1600, 1602, 1602, 1602, 1602 },  /* per SMPTE-314M */
      .audio_shuffle      = dv_audio_shuffle525, },
    { .dsf                = 1,
      .video_stype        = 0x4,
      .frame_size         = 288000, /* SMPTE-314M - 625/50 (PAL) 50 Mbps */
      .difseg_size        = 12,    /* also known as "DVCPRO50" */
      .n_difchan          = 2,
      .time_base          = { 1, 25 },
      .ltc_divisor        = 25,
      .height             = 576,
      .width              = 720,
      .sar                = { { 16, 15 }, { 64, 45 } },
      .pix_fmt            = AV_PIX_FMT_YUV422P,
      .bpm                = 6,
      .block_sizes        = block_sizes_dv2550,
      .audio_stride       = 108,
      .audio_min_samples  = { 1896, 1742, 1264 }, /* for 48, 44.1 and 32kHz */
      .audio_samples_dist = { 1920, 1920, 1920, 1920, 1920 },
      .audio_shuffle      = dv_audio_shuffle625, },
    { .dsf                = 0,
      .video_stype        = 0x14,
      .frame_size         = 480000, /* SMPTE-370M - 1080i60 100 Mbps */
      .difseg_size        = 10,    /* also known as "DVCPRO HD" */
      .n_difchan          = 4,
      .time_base          = { 1001, 30000 },
      .ltc_divisor        = 30,
      .height             = 1080,
      .width              = 1280,
      .sar                = { { 1, 1 }, { 3, 2 } },
      .pix_fmt            = AV_PIX_FMT_YUV422P,
      .bpm                = 8,
      .block_sizes        = block_sizes_dv100,
      .audio_stride       = 90,
      .audio_min_samples  = { 1580, 1452, 1053 }, /* for 48, 44.1 and 32kHz */
      .audio_samples_dist = { 1600, 1602, 1602, 1602, 1602 },  /* per SMPTE-314M */
      .audio_shuffle      = dv_audio_shuffle525, },
    { .dsf                = 1,
      .video_stype        = 0x14,
      .frame_size         = 576000, /* SMPTE-370M - 1080i50 100 Mbps */
      .difseg_size        = 12,    /* also known as "DVCPRO HD" */
      .n_difchan          = 4,
      .time_base          = { 1, 25 },
      .ltc_divisor        = 25,
      .height             = 1080,
      .width              = 1440,
      .sar                = { { 1, 1 }, { 4, 3 } },
      .pix_fmt            = AV_PIX_FMT_YUV422P,
      .bpm                = 8,
      .block_sizes        = block_sizes_dv100,
      .audio_stride       = 108,
      .audio_min_samples  = { 1896, 1742, 1264 }, /* for 48, 44.1 and 32kHz */
      .audio_samples_dist = { 1920, 1920, 1920, 1920, 1920 },
      .audio_shuffle      = dv_audio_shuffle625, },
    { .dsf                = 0,
      .video_stype        = 0x18,
      .frame_size         = 240000, /* SMPTE-370M - 720p60 100 Mbps */
      .difseg_size        = 10,    /* also known as "DVCPRO HD" */
      .n_difchan          = 2,
      .time_base          = { 1001, 60000 },
      .ltc_divisor        = 60,
      .height             = 720,
      .width              = 960,
      .sar                = { { 1, 1 }, { 4, 3 } },
      .pix_fmt            = AV_PIX_FMT_YUV422P,
      .bpm                = 8,
      .block_sizes        = block_sizes_dv100,
      .audio_stride       = 90,
      .audio_min_samples  = { 1580, 1452, 1053 }, /* for 48, 44.1 and 32kHz */
      .audio_samples_dist = { 1600, 1602, 1602, 1602, 1602 },  /* per SMPTE-314M */
      .audio_shuffle      = dv_audio_shuffle525, },
    { .dsf                = 1,
      .video_stype        = 0x18,
      .frame_size         = 288000, /* SMPTE-370M - 720p50 100 Mbps */
      .difseg_size        = 12,    /* also known as "DVCPRO HD" */
      .n_difchan          = 2,
      .time_base          = { 1, 50 },
      .ltc_divisor        = 50,
      .height             = 720,
      .width              = 960,
      .sar                = { { 1, 1 }, { 4, 3 } },
      .pix_fmt            = AV_PIX_FMT_YUV422P,
      .bpm                = 8,
      .block_sizes        = block_sizes_dv100,
      .audio_stride       = 90,
      .audio_min_samples  = { 1896, 1742, 1264 }, /* for 48, 44.1 and 32kHz */
      .audio_samples_dist = { 1920, 1920, 1920, 1920, 1920 },
      .audio_shuffle      = dv_audio_shuffle625, },
    { .dsf                = 1,
      .video_stype        = 0x1,
      .frame_size         = 144000, /* IEC 61883-5 - 625/50 (PAL) */
      .difseg_size        = 12,
      .n_difchan          = 1,
      .time_base          = { 1, 25 },
      .ltc_divisor        = 25,
      .height             = 576,
      .width              = 720,
      .sar                = { { 16, 15 }, { 64, 45 } },
      .pix_fmt            = AV_PIX_FMT_YUV420P,
      .bpm                = 6,
      .block_sizes        = block_sizes_dv2550,
      .audio_stride       = 108,
      .audio_min_samples  = { 1896, 1742, 1264 }, /* for 48, 44.1 and 32kHz */
      .audio_samples_dist = { 1920, 1920, 1920, 1920, 1920 },
      .audio_shuffle      = dv_audio_shuffle625, }
};

void ff_dv_print_profiles(void *logctx, int loglevel)
{
    int i;
    for (i = 0; i < FF_ARRAY_ELEMS(dv_profiles); i++) {
        const AVDVProfile *p = &dv_profiles[i];
        av_log(logctx, loglevel,
               "Frame size: %dx%d; pixel format: %s, framerate: %d/%d\n",
               p->width, p->height, av_get_pix_fmt_name(p->pix_fmt),
               p->time_base.den, p->time_base.num);
    }
}

#endif /* CONFIG_DVPROFILE */

const AVDVProfile *av_dv_frame_profile(const AVDVProfile *sys,
                                       const uint8_t *frame, unsigned buf_size)
{
#if CONFIG_DVPROFILE
    int i, dsf, stype;

    if (buf_size < 80 * 5 + 48 + 4)
        return NULL;

    dsf   = (frame[3] & 0x80) >> 7;
    stype = frame[80 * 5 + 48 + 3] & 0x1f;

    /* 576i50 25Mbps 4:1:1 is a special case */
    if (dsf == 1 && stype == 0 && frame[4] & 0x07 /* the APT field */)
        return &dv_profiles[2];

    for (i = 0; i < FF_ARRAY_ELEMS(dv_profiles); i++)
        if (dsf == dv_profiles[i].dsf && stype == dv_profiles[i].video_stype)
            return &dv_profiles[i];

    /* check if old sys matches and assumes corrupted input */
    if (sys && buf_size == sys->frame_size)
        return sys;
#endif

    return NULL;
}

const AVDVProfile *av_dv_codec_profile(int width, int height,
                                       enum AVPixelFormat pix_fmt)
{
#if CONFIG_DVPROFILE
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(dv_profiles); i++)
        if (height  == dv_profiles[i].height  &&
            pix_fmt == dv_profiles[i].pix_fmt &&
            width   == dv_profiles[i].width)
            return &dv_profiles[i];
#endif

    return NULL;
}
