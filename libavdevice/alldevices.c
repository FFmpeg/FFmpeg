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
#include "libavformat/internal.h"
#include "avdevice.h"

/* devices */
extern AVInputFormat  ff_alsa_demuxer;
extern AVOutputFormat ff_alsa_muxer;
extern AVInputFormat  ff_android_camera_demuxer;
extern AVInputFormat  ff_avfoundation_demuxer;
extern AVInputFormat  ff_bktr_demuxer;
extern AVOutputFormat ff_caca_muxer;
extern AVInputFormat  ff_decklink_demuxer;
extern AVOutputFormat ff_decklink_muxer;
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

void avdevice_register_all(void)
{
    avpriv_register_devices(outdev_list, indev_list);
}
