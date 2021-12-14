/*
 * Register all the grabbing devices.
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

#include "libavformat/internal.h"
#include "avdevice.h"

/* devices */
extern const AVInputFormat  ff_alsa_demuxer;
extern const AVOutputFormat ff_alsa_muxer;
extern const AVInputFormat  ff_android_camera_demuxer;
extern const AVOutputFormat ff_audiotoolbox_muxer;
extern const AVInputFormat  ff_avfoundation_demuxer;
extern const AVInputFormat  ff_bktr_demuxer;
extern const AVOutputFormat ff_caca_muxer;
extern const AVInputFormat  ff_decklink_demuxer;
extern const AVOutputFormat ff_decklink_muxer;
extern const AVInputFormat  ff_dshow_demuxer;
extern const AVInputFormat  ff_fbdev_demuxer;
extern const AVOutputFormat ff_fbdev_muxer;
extern const AVInputFormat  ff_gdigrab_demuxer;
extern const AVInputFormat  ff_iec61883_demuxer;
extern const AVInputFormat  ff_jack_demuxer;
extern const AVInputFormat  ff_kmsgrab_demuxer;
extern const AVInputFormat  ff_lavfi_demuxer;
extern const AVInputFormat  ff_openal_demuxer;
extern const AVOutputFormat ff_opengl_muxer;
extern const AVInputFormat  ff_oss_demuxer;
extern const AVOutputFormat ff_oss_muxer;
extern const AVInputFormat  ff_pulse_demuxer;
extern const AVOutputFormat ff_pulse_muxer;
extern const AVOutputFormat ff_sdl2_muxer;
extern const AVInputFormat  ff_sndio_demuxer;
extern const AVOutputFormat ff_sndio_muxer;
extern const AVInputFormat  ff_v4l2_demuxer;
extern const AVOutputFormat ff_v4l2_muxer;
extern const AVInputFormat  ff_vfwcap_demuxer;
extern const AVInputFormat  ff_xcbgrab_demuxer;
extern const AVOutputFormat ff_xv_muxer;

/* external libraries */
extern const AVInputFormat  ff_libcdio_demuxer;
extern const AVInputFormat  ff_libdc1394_demuxer;

#include "libavdevice/outdev_list.c"
#include "libavdevice/indev_list.c"

void avdevice_register_all(void)
{
    avpriv_register_devices(outdev_list, indev_list);
}

static const void *next_input(const AVInputFormat *prev, AVClassCategory c2)
{
    const AVClass *pc;
    const AVClassCategory c1 = AV_CLASS_CATEGORY_DEVICE_INPUT;
    AVClassCategory category = AV_CLASS_CATEGORY_NA;
    const AVInputFormat *fmt = NULL;
    int i = 0;

    while (prev && (fmt = indev_list[i])) {
        i++;
        if (prev == fmt)
            break;
    }

    do {
        fmt = indev_list[i++];
        if (!fmt)
            break;
        pc = fmt->priv_class;
        if (!pc)
            continue;
        category = pc->category;
    } while (category != c1 && category != c2);
    return fmt;
}

static const void *next_output(const AVOutputFormat *prev, AVClassCategory c2)
{
    const AVClass *pc;
    const AVClassCategory c1 = AV_CLASS_CATEGORY_DEVICE_OUTPUT;
    AVClassCategory category = AV_CLASS_CATEGORY_NA;
    const AVOutputFormat *fmt = NULL;
    int i = 0;

    while (prev && (fmt = outdev_list[i])) {
        i++;
        if (prev == fmt)
            break;
    }

    do {
        fmt = outdev_list[i++];
        if (!fmt)
            break;
        pc = fmt->priv_class;
        if (!pc)
            continue;
        category = pc->category;
    } while (category != c1 && category != c2);
    return fmt;
}

const AVInputFormat *av_input_audio_device_next(const AVInputFormat  *d)
{
    return next_input(d, AV_CLASS_CATEGORY_DEVICE_AUDIO_INPUT);
}

const AVInputFormat *av_input_video_device_next(const AVInputFormat  *d)
{
    return next_input(d, AV_CLASS_CATEGORY_DEVICE_VIDEO_INPUT);
}

const AVOutputFormat *av_output_audio_device_next(const AVOutputFormat *d)
{
    return next_output(d, AV_CLASS_CATEGORY_DEVICE_AUDIO_OUTPUT);
}

const AVOutputFormat *av_output_video_device_next(const AVOutputFormat *d)
{
    return next_output(d, AV_CLASS_CATEGORY_DEVICE_VIDEO_OUTPUT);
}
