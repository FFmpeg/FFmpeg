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

#include "config.h"
#include "libavutil/thread.h"
#include "avdevice.h"

#if FF_API_NEXT
#include "libavformat/internal.h"
#endif

/* devices */
extern AVInputFormat  ff_alsa_demuxer;
extern AVOutputFormat ff_alsa_muxer;
extern AVInputFormat  ff_avfoundation_demuxer;
extern AVInputFormat  ff_bktr_demuxer;
extern AVOutputFormat ff_caca_muxer;
extern AVInputFormat  ff_decklink_demuxer;
extern AVOutputFormat ff_decklink_muxer;
extern AVInputFormat  ff_libndi_newtek_demuxer;
extern AVOutputFormat ff_libndi_newtek_muxer;
extern AVInputFormat  ff_dshow_demuxer;
extern AVInputFormat  ff_fbdev_demuxer;
extern AVOutputFormat ff_fbdev_muxer;
extern AVInputFormat  ff_gdigrab_demuxer;
extern AVInputFormat  ff_iec61883_demuxer;
extern AVInputFormat  ff_jack_demuxer;
extern AVInputFormat  ff_kmsgrab_demuxer;
extern AVInputFormat  ff_lavfi_demuxer;
extern AVInputFormat  ff_openal_demuxer;
extern AVOutputFormat ff_opengl_muxer;
extern AVInputFormat  ff_oss_demuxer;
extern AVOutputFormat ff_oss_muxer;
extern AVInputFormat  ff_pulse_demuxer;
extern AVOutputFormat ff_pulse_muxer;
extern AVOutputFormat ff_sdl2_muxer;
extern AVInputFormat  ff_sndio_demuxer;
extern AVOutputFormat ff_sndio_muxer;
extern AVInputFormat  ff_v4l2_demuxer;
extern AVOutputFormat ff_v4l2_muxer;
extern AVInputFormat  ff_vfwcap_demuxer;
extern AVInputFormat  ff_xcbgrab_demuxer;
extern AVOutputFormat ff_xv_muxer;

/* external libraries */
extern AVInputFormat  ff_libcdio_demuxer;
extern AVInputFormat  ff_libdc1394_demuxer;

#include "libavdevice/outdev_list.c"
#include "libavdevice/indev_list.c"

const AVOutputFormat *av_outdev_iterate(void **opaque)
{
    uintptr_t i = (uintptr_t)*opaque;
    const AVOutputFormat *f = outdev_list[i];

    if (f)
        *opaque = (void*)(i + 1);
    return f;
}

const AVInputFormat *av_indev_iterate(void **opaque)
{
    uintptr_t i = (uintptr_t)*opaque;
    const AVInputFormat *f = indev_list[i];

    if (f)
        *opaque = (void*)(i + 1);
    return f;
}

#if FF_API_NEXT
FF_DISABLE_DEPRECATION_WARNINGS
static AVOnce av_device_next_init = AV_ONCE_INIT;

static void av_device_init_next(void)
{
    AVOutputFormat *prevout = NULL, *out;
    AVInputFormat *previn = NULL, *in;
    void *i = 0;

    while ((out = (AVOutputFormat*)av_outdev_iterate(&i))) {
        if (prevout)
            prevout->next = out;
        prevout = out;
    }

    i = 0;
    while ((in = (AVInputFormat*)av_indev_iterate(&i))) {
        if (previn)
            previn->next = in;
        previn = in;
    }

    avpriv_register_devices(outdev_list, indev_list);
}

void avdevice_register_all(void)
{
    ff_thread_once(&av_device_next_init, av_device_init_next);
}

static void *device_next(void *prev, int output,
                         AVClassCategory c1, AVClassCategory c2)
{
    const AVClass *pc;
    AVClassCategory category = AV_CLASS_CATEGORY_NA;

    ff_thread_once(&av_device_next_init, av_device_init_next);

    if (!prev && !(prev = (output ? (void*)outdev_list[0] : (void*)indev_list[0])))
        return NULL;

    do {
        if (output) {
            if (!(prev = ((AVOutputFormat *)prev)->next))
                break;
            pc = ((AVOutputFormat *)prev)->priv_class;
        } else {
            if (!(prev = ((AVInputFormat *)prev)->next))
                break;
            pc = ((AVInputFormat *)prev)->priv_class;
        }
        if (!pc)
            continue;
        category = pc->category;
    } while (category != c1 && category != c2);
    return prev;
}

AVInputFormat *av_input_audio_device_next(AVInputFormat  *d)
{
    return device_next(d, 0, AV_CLASS_CATEGORY_DEVICE_AUDIO_INPUT,
                       AV_CLASS_CATEGORY_DEVICE_INPUT);
}

AVInputFormat *av_input_video_device_next(AVInputFormat  *d)
{
    return device_next(d, 0, AV_CLASS_CATEGORY_DEVICE_VIDEO_INPUT,
                       AV_CLASS_CATEGORY_DEVICE_INPUT);
}

AVOutputFormat *av_output_audio_device_next(AVOutputFormat *d)
{
    return device_next(d, 1, AV_CLASS_CATEGORY_DEVICE_AUDIO_OUTPUT,
                       AV_CLASS_CATEGORY_DEVICE_OUTPUT);
}

AVOutputFormat *av_output_video_device_next(AVOutputFormat *d)
{
    return device_next(d, 1, AV_CLASS_CATEGORY_DEVICE_VIDEO_OUTPUT,
                       AV_CLASS_CATEGORY_DEVICE_OUTPUT);
}
FF_DISABLE_DEPRECATION_WARNINGS
#endif

