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
#include "libavformat/avformat.h"
#include "avdevice.h"

unsigned avdevice_version(void)
{
    return LIBAVDEVICE_VERSION_INT;
}

#define REGISTER_MUXER(X,x) { \
          extern AVOutputFormat x##_muxer; \
          if(CONFIG_##X##_MUXER)   av_register_output_format(&x##_muxer); }
#define REGISTER_DEMUXER(X,x) { \
          extern AVInputFormat x##_demuxer; \
          if(CONFIG_##X##_DEMUXER) av_register_input_format(&x##_demuxer); }
#define REGISTER_MUXDEMUX(X,x)  REGISTER_MUXER(X,x); REGISTER_DEMUXER(X,x)

void avdevice_register_all(void)
{
    static int initialized;

    if (initialized)
        return;
    initialized = 1;

    /* devices */
    REGISTER_MUXDEMUX (ALSA, alsa);
    REGISTER_MUXDEMUX (AUDIO_BEOS, audio_beos);
    REGISTER_DEMUXER  (BKTR, bktr);
    REGISTER_DEMUXER  (DV1394, dv1394);
    REGISTER_DEMUXER  (JACK, jack);
    REGISTER_MUXDEMUX (OSS, oss);
    REGISTER_DEMUXER  (V4L2, v4l2);
    REGISTER_DEMUXER  (V4L, v4l);
    REGISTER_DEMUXER  (VFWCAP, vfwcap);
    REGISTER_DEMUXER  (X11_GRAB_DEVICE, x11_grab_device);

    /* external libraries */
    REGISTER_DEMUXER  (LIBDC1394, libdc1394);
}
