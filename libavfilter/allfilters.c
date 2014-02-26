/*
 * filter registration
 * Copyright (c) 2008 Vitor Sessak
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

#include "avfilter.h"
#include "config.h"


#define REGISTER_FILTER(X, x, y)                                        \
    {                                                                   \
        extern AVFilter ff_##y##_##x;                                   \
        if (CONFIG_##X##_FILTER)                                        \
            avfilter_register(&ff_##y##_##x);                           \
    }

#define REGISTER_FILTER_UNCONDITIONAL(x)                                \
    {                                                                   \
        extern AVFilter ff_##x;                                         \
        avfilter_register(&ff_##x);                                     \
    }

void avfilter_register_all(void)
{
    static int initialized;

    if (initialized)
        return;
    initialized = 1;

    REGISTER_FILTER(AFORMAT,        aformat,        af);
    REGISTER_FILTER(AMIX,           amix,           af);
    REGISTER_FILTER(ANULL,          anull,          af);
    REGISTER_FILTER(ASETPTS,        asetpts,        af);
    REGISTER_FILTER(ASHOWINFO,      ashowinfo,      af);
    REGISTER_FILTER(ASPLIT,         asplit,         af);
    REGISTER_FILTER(ASYNCTS,        asyncts,        af);
    REGISTER_FILTER(ATRIM,          atrim,          af);
    REGISTER_FILTER(CHANNELMAP,     channelmap,     af);
    REGISTER_FILTER(CHANNELSPLIT,   channelsplit,   af);
    REGISTER_FILTER(COMPAND,        compand,        af);
    REGISTER_FILTER(JOIN,           join,           af);
    REGISTER_FILTER(RESAMPLE,       resample,       af);
    REGISTER_FILTER(VOLUME,         volume,         af);

    REGISTER_FILTER(ANULLSRC,       anullsrc,       asrc);

    REGISTER_FILTER(ANULLSINK,      anullsink,      asink);

    REGISTER_FILTER(BLACKFRAME,     blackframe,     vf);
    REGISTER_FILTER(BOXBLUR,        boxblur,        vf);
    REGISTER_FILTER(COPY,           copy,           vf);
    REGISTER_FILTER(CROP,           crop,           vf);
    REGISTER_FILTER(CROPDETECT,     cropdetect,     vf);
    REGISTER_FILTER(DELOGO,         delogo,         vf);
    REGISTER_FILTER(DRAWBOX,        drawbox,        vf);
    REGISTER_FILTER(DRAWTEXT,       drawtext,       vf);
    REGISTER_FILTER(FADE,           fade,           vf);
    REGISTER_FILTER(FIELDORDER,     fieldorder,     vf);
    REGISTER_FILTER(FORMAT,         format,         vf);
    REGISTER_FILTER(FPS,            fps,            vf);
    REGISTER_FILTER(FRAMEPACK,      framepack,      vf);
    REGISTER_FILTER(FREI0R,         frei0r,         vf);
    REGISTER_FILTER(GRADFUN,        gradfun,        vf);
    REGISTER_FILTER(HFLIP,          hflip,          vf);
    REGISTER_FILTER(HQDN3D,         hqdn3d,         vf);
    REGISTER_FILTER(INTERLACE,      interlace,      vf);
    REGISTER_FILTER(LUT,            lut,            vf);
    REGISTER_FILTER(LUTRGB,         lutrgb,         vf);
    REGISTER_FILTER(LUTYUV,         lutyuv,         vf);
    REGISTER_FILTER(NEGATE,         negate,         vf);
    REGISTER_FILTER(NOFORMAT,       noformat,       vf);
    REGISTER_FILTER(NULL,           null,           vf);
    REGISTER_FILTER(OCV,            ocv,            vf);
    REGISTER_FILTER(OVERLAY,        overlay,        vf);
    REGISTER_FILTER(PAD,            pad,            vf);
    REGISTER_FILTER(PIXDESCTEST,    pixdesctest,    vf);
    REGISTER_FILTER(SCALE,          scale,          vf);
    REGISTER_FILTER(SELECT,         select,         vf);
    REGISTER_FILTER(SETDAR,         setdar,         vf);
    REGISTER_FILTER(SETPTS,         setpts,         vf);
    REGISTER_FILTER(SETSAR,         setsar,         vf);
    REGISTER_FILTER(SETTB,          settb,          vf);
    REGISTER_FILTER(SHOWINFO,       showinfo,       vf);
    REGISTER_FILTER(SPLIT,          split,          vf);
    REGISTER_FILTER(TRANSPOSE,      transpose,      vf);
    REGISTER_FILTER(TRIM,           trim,           vf);
    REGISTER_FILTER(UNSHARP,        unsharp,        vf);
    REGISTER_FILTER(VFLIP,          vflip,          vf);
    REGISTER_FILTER(YADIF,          yadif,          vf);

    REGISTER_FILTER(COLOR,          color,          vsrc);
    REGISTER_FILTER(FREI0R,         frei0r_src,     vsrc);
    REGISTER_FILTER(MOVIE,          movie,          vsrc);
    REGISTER_FILTER(NULLSRC,        nullsrc,        vsrc);
    REGISTER_FILTER(RGBTESTSRC,     rgbtestsrc,     vsrc);
    REGISTER_FILTER(TESTSRC,        testsrc,        vsrc);

    REGISTER_FILTER(NULLSINK,       nullsink,       vsink);

    /* those filters are part of public or internal API => registered
     * unconditionally */
    REGISTER_FILTER_UNCONDITIONAL(asrc_abuffer);
    REGISTER_FILTER_UNCONDITIONAL(vsrc_buffer);
    REGISTER_FILTER_UNCONDITIONAL(asink_abuffer);
    REGISTER_FILTER_UNCONDITIONAL(vsink_buffer);
    REGISTER_FILTER_UNCONDITIONAL(af_afifo);
    REGISTER_FILTER_UNCONDITIONAL(vf_fifo);
}
