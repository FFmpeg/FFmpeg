/*
 * filter registration
 * Copyright (c) 2008 Vitor Sessak
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

#include "avfilter.h"
#include "config.h"


#define REGISTER_FILTER(X,x,y) { \
          extern AVFilter avfilter_##y##_##x ; \
          if(CONFIG_##X##_FILTER )  avfilter_register(&avfilter_##y##_##x ); }

#define REGISTER_BUILTIN_FILTER(x,y) { \
          extern AVFilter avfilter_##y##_##x ; \
          avfilter_register(&avfilter_##y##_##x ); }

void avfilter_register_all(void)
{
    static int initialized;

    if (initialized)
        return;
    initialized = 1;

    REGISTER_FILTER (ACONVERT,    aconvert,    af);
    REGISTER_FILTER (AFIFO,       afifo,       af);
    REGISTER_FILTER (AFORMAT,     aformat,     af);
    REGISTER_FILTER (AMERGE,      amerge,      af);
    REGISTER_FILTER (AMIX,        amix,        af);
    REGISTER_FILTER (ANULL,       anull,       af);
    REGISTER_FILTER (ARESAMPLE,   aresample,   af);
    REGISTER_FILTER (ASENDCMD,    asendcmd,    af);
    REGISTER_FILTER (ASETNSAMPLES, asetnsamples, af);
    REGISTER_FILTER (ASETPTS,     asetpts,     af);
    REGISTER_FILTER (ASETTB,      asettb,      af);
    REGISTER_FILTER (ASHOWINFO,   ashowinfo,   af);
    REGISTER_FILTER (ASPLIT,      asplit,      af);
    REGISTER_FILTER (ASTREAMSYNC, astreamsync, af);
    REGISTER_FILTER (ASYNCTS,     asyncts,     af);
    REGISTER_FILTER (ATEMPO,      atempo,      af);
    REGISTER_FILTER (CHANNELMAP,  channelmap,  af);
    REGISTER_FILTER (CHANNELSPLIT,channelsplit,af);
    REGISTER_FILTER (EARWAX,      earwax,      af);
    REGISTER_FILTER (JOIN,        join,        af);
    REGISTER_FILTER (PAN,         pan,         af);
    REGISTER_FILTER (SILENCEDETECT, silencedetect, af);
    REGISTER_FILTER (VOLUME,      volume,      af);
    REGISTER_FILTER (VOLUMEDETECT,volumedetect,af);
    REGISTER_FILTER (RESAMPLE,    resample,    af);

    REGISTER_FILTER (AEVALSRC,    aevalsrc,    asrc);
    REGISTER_FILTER (ANULLSRC,    anullsrc,    asrc);
    REGISTER_FILTER (FLITE,       flite,       asrc);

    REGISTER_FILTER (ANULLSINK,   anullsink,   asink);

    REGISTER_FILTER (ALPHAEXTRACT, alphaextract, vf);
    REGISTER_FILTER (ALPHAMERGE,  alphamerge,  vf);
    REGISTER_FILTER (ASS,         ass,         vf);
    REGISTER_FILTER (BBOX,        bbox,        vf);
    REGISTER_FILTER (BLACKDETECT, blackdetect, vf);
    REGISTER_FILTER (BLACKFRAME,  blackframe,  vf);
    REGISTER_FILTER (BOXBLUR,     boxblur,     vf);
    REGISTER_FILTER (COLORMATRIX, colormatrix, vf);
    REGISTER_FILTER (COPY,        copy,        vf);
    REGISTER_FILTER (CROP,        crop,        vf);
    REGISTER_FILTER (CROPDETECT,  cropdetect,  vf);
    REGISTER_FILTER (DECIMATE,    decimate,    vf);
    REGISTER_FILTER (DELOGO,      delogo,      vf);
    REGISTER_FILTER (DESHAKE,     deshake,     vf);
    REGISTER_FILTER (DRAWBOX,     drawbox,     vf);
    REGISTER_FILTER (DRAWTEXT,    drawtext,    vf);
    REGISTER_FILTER (EDGEDETECT,  edgedetect,  vf);
    REGISTER_FILTER (FADE,        fade,        vf);
    REGISTER_FILTER (FIELDORDER,  fieldorder,  vf);
    REGISTER_FILTER (FIFO,        fifo,        vf);
    REGISTER_FILTER (FORMAT,      format,      vf);
    REGISTER_FILTER (FPS,         fps,         vf);
    REGISTER_FILTER (FRAMESTEP,   framestep,   vf);
    REGISTER_FILTER (FREI0R,      frei0r,      vf);
    REGISTER_FILTER (GRADFUN,     gradfun,     vf);
    REGISTER_FILTER (HFLIP,       hflip,       vf);
    REGISTER_FILTER (HQDN3D,      hqdn3d,      vf);
    REGISTER_FILTER (HUE,         hue,         vf);
    REGISTER_FILTER (IDET,        idet,        vf);
    REGISTER_FILTER (LUT,         lut,         vf);
    REGISTER_FILTER (LUTRGB,      lutrgb,      vf);
    REGISTER_FILTER (LUTYUV,      lutyuv,      vf);
    REGISTER_FILTER (MP,          mp,          vf);
    REGISTER_FILTER (NEGATE,      negate,      vf);
    REGISTER_FILTER (NOFORMAT,    noformat,    vf);
    REGISTER_FILTER (NULL,        null,        vf);
    REGISTER_FILTER (OCV,         ocv,         vf);
    REGISTER_FILTER (OVERLAY,     overlay,     vf);
    REGISTER_FILTER (PAD,         pad,         vf);
    REGISTER_FILTER (PIXDESCTEST, pixdesctest, vf);
    REGISTER_FILTER (REMOVELOGO,  removelogo,  vf);
    REGISTER_FILTER (SCALE,       scale,       vf);
    REGISTER_FILTER (SELECT,      select,      vf);
    REGISTER_FILTER (SENDCMD,     sendcmd,     vf);
    REGISTER_FILTER (SETDAR,      setdar,      vf);
    REGISTER_FILTER (SETFIELD,    setfield,    vf);
    REGISTER_FILTER (SETPTS,      setpts,      vf);
    REGISTER_FILTER (SETSAR,      setsar,      vf);
    REGISTER_FILTER (SETTB,       settb,       vf);
    REGISTER_FILTER (SHOWINFO,    showinfo,    vf);
    REGISTER_FILTER (SLICIFY,     slicify,     vf);
    REGISTER_FILTER (SMARTBLUR,   smartblur,   vf);
    REGISTER_FILTER (SPLIT,       split,       vf);
    REGISTER_FILTER (SUPER2XSAI,  super2xsai,  vf);
    REGISTER_FILTER (SWAPUV,      swapuv,      vf);
    REGISTER_FILTER (THUMBNAIL,   thumbnail,   vf);
    REGISTER_FILTER (TILE,        tile,        vf);
    REGISTER_FILTER (TINTERLACE,  tinterlace,  vf);
    REGISTER_FILTER (TRANSPOSE,   transpose,   vf);
    REGISTER_FILTER (UNSHARP,     unsharp,     vf);
    REGISTER_FILTER (VFLIP,       vflip,       vf);
    REGISTER_FILTER (YADIF,       yadif,       vf);

    REGISTER_FILTER (CELLAUTO,    cellauto,    vsrc);
    REGISTER_FILTER (COLOR,       color,       vsrc);
    REGISTER_FILTER (FREI0R,      frei0r_src,  vsrc);
    REGISTER_FILTER (LIFE,        life,        vsrc);
    REGISTER_FILTER (MANDELBROT,  mandelbrot,  vsrc);
    REGISTER_FILTER (MPTESTSRC,   mptestsrc,   vsrc);
    REGISTER_FILTER (NULLSRC,     nullsrc,     vsrc);
    REGISTER_FILTER (RGBTESTSRC,  rgbtestsrc,  vsrc);
    REGISTER_FILTER (SMPTEBARS,   smptebars,   vsrc);
    REGISTER_FILTER (TESTSRC,     testsrc,     vsrc);

    REGISTER_FILTER (NULLSINK,    nullsink,    vsink);

    /* multimedia filters */
    REGISTER_FILTER (CONCAT,      concat,      avf);
    REGISTER_FILTER (SHOWSPECTRUM,showspectrum,avf);
    REGISTER_FILTER (SHOWWAVES,   showwaves,   avf);

    /* multimedia sources */
    REGISTER_FILTER (AMOVIE,      amovie,      avsrc);
    REGISTER_FILTER (MOVIE,       movie,       avsrc);

    REGISTER_BUILTIN_FILTER (ffbuffersink,  vsink);
    REGISTER_BUILTIN_FILTER (ffabuffersink, asink);
#if !AV_HAVE_INCOMPATIBLE_FORK_ABI
    REGISTER_BUILTIN_FILTER (buffersink,    vsink);
    REGISTER_BUILTIN_FILTER (abuffersink,   asink);
#endif

    /* those filters are part of public or internal API => registered
     * unconditionally */
    {
        extern AVFilter avfilter_vsrc_buffer;
        avfilter_register(&avfilter_vsrc_buffer);
    }
    {
        extern AVFilter avfilter_asrc_abuffer;
        avfilter_register(&avfilter_asrc_abuffer);
    }
    {
        extern AVFilter avfilter_vsink_buffer;
        avfilter_register(&avfilter_vsink_buffer);
    }
    {
        extern AVFilter avfilter_asink_abuffer;
        avfilter_register(&avfilter_asink_abuffer);
    }
}
