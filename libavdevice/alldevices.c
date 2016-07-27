/*
 * Register all the grabbing devices.
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

#include "config.h"
#include "libavformat/avformat.h"
#include "avdevice.h"

#define REGISTER_OUTDEV(X, x)                                           \
    {                                                                   \
        extern AVOutputFormat ff_##x##_muxer;                           \
        if (CONFIG_##X##_OUTDEV)                                        \
            av_register_output_format(&ff_##x##_muxer);                 \
    }

#define REGISTER_INDEV(X, x)                                            \
    {                                                                   \
        extern AVInputFormat ff_##x##_demuxer;                          \
        if (CONFIG_##X##_INDEV)                                         \
            av_register_input_format(&ff_##x##_demuxer);                \
    }

#define REGISTER_INOUTDEV(X, x) REGISTER_OUTDEV(X, x); REGISTER_INDEV(X, x)

void avdevice_register_all(void)
{
    static int initialized;

    if (initialized)
        return;
    initialized = 1;

    /* devices */
    REGISTER_INOUTDEV(ALSA,             alsa);
    REGISTER_INDEV   (AVFOUNDATION,     avfoundation);
    REGISTER_INDEV   (BKTR,             bktr);
    REGISTER_INDEV   (DV1394,           dv1394);
    REGISTER_INDEV   (FBDEV,            fbdev);
    REGISTER_INDEV   (JACK,             jack);
    REGISTER_INOUTDEV(OSS,              oss);
    REGISTER_INDEV   (PULSE,            pulse);
    REGISTER_INOUTDEV(SNDIO,            sndio);
    REGISTER_INDEV   (V4L2,             v4l2);
    REGISTER_INDEV   (VFWCAP,           vfwcap);
    REGISTER_INDEV   (XCBGRAB,          xcbgrab);

    /* external libraries */
    REGISTER_INDEV   (LIBCDIO,          libcdio);
    REGISTER_INDEV   (LIBDC1394,        libdc1394);
}
