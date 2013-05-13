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
#include "opencl_allkernels.h"


#define REGISTER_FILTER(X, x, y)                                        \
    {                                                                   \
        extern AVFilter avfilter_##y##_##x;                             \
        if (CONFIG_##X##_FILTER)                                        \
            avfilter_register(&avfilter_##y##_##x);                     \
    }

#define REGISTER_FILTER_UNCONDITIONAL(x)                                \
    {                                                                   \
        extern AVFilter avfilter_##x;                                   \
        avfilter_register(&avfilter_##x);                               \
    }

void avfilter_register_all(void)
{
    static int initialized;

    if (initialized)
        return;
    initialized = 1;

#if FF_API_ACONVERT_FILTER
    REGISTER_FILTER(ACONVERT,       aconvert,       af);
#endif
    REGISTER_FILTER(AFADE,          afade,          af);
    REGISTER_FILTER(AFORMAT,        aformat,        af);
    REGISTER_FILTER(AINTERLEAVE,    ainterleave,    af);
    REGISTER_FILTER(ALLPASS,        allpass,        af);
    REGISTER_FILTER(AMERGE,         amerge,         af);
    REGISTER_FILTER(AMIX,           amix,           af);
    REGISTER_FILTER(ANULL,          anull,          af);
    REGISTER_FILTER(APAD,           apad,           af);
    REGISTER_FILTER(APERMS,         aperms,         af);
    REGISTER_FILTER(APHASER,        aphaser,        af);
    REGISTER_FILTER(ARESAMPLE,      aresample,      af);
    REGISTER_FILTER(ASELECT,        aselect,        af);
    REGISTER_FILTER(ASENDCMD,       asendcmd,       af);
    REGISTER_FILTER(ASETNSAMPLES,   asetnsamples,   af);
    REGISTER_FILTER(ASETPTS,        asetpts,        af);
    REGISTER_FILTER(ASETRATE,       asetrate,       af);
    REGISTER_FILTER(ASETTB,         asettb,         af);
    REGISTER_FILTER(ASHOWINFO,      ashowinfo,      af);
    REGISTER_FILTER(ASPLIT,         asplit,         af);
    REGISTER_FILTER(ASTATS,         astats,         af);
    REGISTER_FILTER(ASTREAMSYNC,    astreamsync,    af);
    REGISTER_FILTER(ASYNCTS,        asyncts,        af);
    REGISTER_FILTER(ATEMPO,         atempo,         af);
    REGISTER_FILTER(ATRIM,          atrim,          af);
    REGISTER_FILTER(BANDPASS,       bandpass,       af);
    REGISTER_FILTER(BANDREJECT,     bandreject,     af);
    REGISTER_FILTER(BASS,           bass,           af);
    REGISTER_FILTER(BIQUAD,         biquad,         af);
    REGISTER_FILTER(CHANNELMAP,     channelmap,     af);
    REGISTER_FILTER(CHANNELSPLIT,   channelsplit,   af);
    REGISTER_FILTER(EARWAX,         earwax,         af);
    REGISTER_FILTER(EBUR128,        ebur128,        af);
    REGISTER_FILTER(EQUALIZER,      equalizer,      af);
    REGISTER_FILTER(HIGHPASS,       highpass,       af);
    REGISTER_FILTER(JOIN,           join,           af);
    REGISTER_FILTER(LOWPASS,        lowpass,        af);
    REGISTER_FILTER(PAN,            pan,            af);
    REGISTER_FILTER(RESAMPLE,       resample,       af);
    REGISTER_FILTER(SILENCEDETECT,  silencedetect,  af);
    REGISTER_FILTER(TREBLE,         treble,         af);
    REGISTER_FILTER(VOLUME,         volume,         af);
    REGISTER_FILTER(VOLUMEDETECT,   volumedetect,   af);

    REGISTER_FILTER(AEVALSRC,       aevalsrc,       asrc);
    REGISTER_FILTER(ANULLSRC,       anullsrc,       asrc);
    REGISTER_FILTER(FLITE,          flite,          asrc);
    REGISTER_FILTER(SINE,           sine,           asrc);

    REGISTER_FILTER(ANULLSINK,      anullsink,      asink);

    REGISTER_FILTER(ALPHAEXTRACT,   alphaextract,   vf);
    REGISTER_FILTER(ALPHAMERGE,     alphamerge,     vf);
    REGISTER_FILTER(ASS,            ass,            vf);
    REGISTER_FILTER(BBOX,           bbox,           vf);
    REGISTER_FILTER(BLACKDETECT,    blackdetect,    vf);
    REGISTER_FILTER(BLACKFRAME,     blackframe,     vf);
    REGISTER_FILTER(BLEND,          blend,          vf);
    REGISTER_FILTER(BOXBLUR,        boxblur,        vf);
    REGISTER_FILTER(COLORBALANCE,   colorbalance,   vf);
    REGISTER_FILTER(COLORCHANNELMIXER, colorchannelmixer, vf);
    REGISTER_FILTER(COLORMATRIX,    colormatrix,    vf);
    REGISTER_FILTER(COPY,           copy,           vf);
    REGISTER_FILTER(CROP,           crop,           vf);
    REGISTER_FILTER(CROPDETECT,     cropdetect,     vf);
    REGISTER_FILTER(CURVES,         curves,         vf);
    REGISTER_FILTER(DECIMATE,       decimate,       vf);
    REGISTER_FILTER(DELOGO,         delogo,         vf);
    REGISTER_FILTER(DESHAKE,        deshake,        vf);
    REGISTER_FILTER(DRAWBOX,        drawbox,        vf);
    REGISTER_FILTER(DRAWTEXT,       drawtext,       vf);
    REGISTER_FILTER(EDGEDETECT,     edgedetect,     vf);
    REGISTER_FILTER(EXTRACTPLANES,  extractplanes,  vf);
    REGISTER_FILTER(FADE,           fade,           vf);
    REGISTER_FILTER(FIELD,          field,          vf);
    REGISTER_FILTER(FIELDMATCH,     fieldmatch,     vf);
    REGISTER_FILTER(FIELDORDER,     fieldorder,     vf);
    REGISTER_FILTER(FORMAT,         format,         vf);
    REGISTER_FILTER(FPS,            fps,            vf);
    REGISTER_FILTER(FRAMESTEP,      framestep,      vf);
    REGISTER_FILTER(FREI0R,         frei0r,         vf);
    REGISTER_FILTER(GEQ,            geq,            vf);
    REGISTER_FILTER(GRADFUN,        gradfun,        vf);
    REGISTER_FILTER(HFLIP,          hflip,          vf);
    REGISTER_FILTER(HISTEQ,         histeq,         vf);
    REGISTER_FILTER(HISTOGRAM,      histogram,      vf);
    REGISTER_FILTER(HQDN3D,         hqdn3d,         vf);
    REGISTER_FILTER(HUE,            hue,            vf);
    REGISTER_FILTER(IDET,           idet,           vf);
    REGISTER_FILTER(IL,             il,             vf);
    REGISTER_FILTER(INTERLACE,      interlace,      vf);
    REGISTER_FILTER(INTERLEAVE,     interleave,     vf);
    REGISTER_FILTER(KERNDEINT,      kerndeint,      vf);
    REGISTER_FILTER(LUT,            lut,            vf);
    REGISTER_FILTER(LUTRGB,         lutrgb,         vf);
    REGISTER_FILTER(LUTYUV,         lutyuv,         vf);
    REGISTER_FILTER(MP,             mp,             vf);
    REGISTER_FILTER(MPDECIMATE,     mpdecimate,     vf);
    REGISTER_FILTER(NEGATE,         negate,         vf);
    REGISTER_FILTER(NOFORMAT,       noformat,       vf);
    REGISTER_FILTER(NOISE,          noise,          vf);
    REGISTER_FILTER(NULL,           null,           vf);
    REGISTER_FILTER(OCV,            ocv,            vf);
    REGISTER_FILTER(OVERLAY,        overlay,        vf);
    REGISTER_FILTER(PAD,            pad,            vf);
    REGISTER_FILTER(PERMS,          perms,          vf);
    REGISTER_FILTER(PIXDESCTEST,    pixdesctest,    vf);
    REGISTER_FILTER(PP,             pp,             vf);
    REGISTER_FILTER(REMOVELOGO,     removelogo,     vf);
    REGISTER_FILTER(SCALE,          scale,          vf);
    REGISTER_FILTER(SELECT,         select,         vf);
    REGISTER_FILTER(SENDCMD,        sendcmd,        vf);
    REGISTER_FILTER(SEPARATEFIELDS, separatefields, vf);
    REGISTER_FILTER(SETDAR,         setdar,         vf);
    REGISTER_FILTER(SETFIELD,       setfield,       vf);
    REGISTER_FILTER(SETPTS,         setpts,         vf);
    REGISTER_FILTER(SETSAR,         setsar,         vf);
    REGISTER_FILTER(SETTB,          settb,          vf);
    REGISTER_FILTER(SHOWINFO,       showinfo,       vf);
    REGISTER_FILTER(SMARTBLUR,      smartblur,      vf);
    REGISTER_FILTER(SPLIT,          split,          vf);
    REGISTER_FILTER(STEREO3D,       stereo3d,       vf);
    REGISTER_FILTER(SUBTITLES,      subtitles,      vf);
    REGISTER_FILTER(SUPER2XSAI,     super2xsai,     vf);
    REGISTER_FILTER(SWAPUV,         swapuv,         vf);
    REGISTER_FILTER(TELECINE,       telecine,       vf);
    REGISTER_FILTER(THUMBNAIL,      thumbnail,      vf);
    REGISTER_FILTER(TILE,           tile,           vf);
    REGISTER_FILTER(TINTERLACE,     tinterlace,     vf);
    REGISTER_FILTER(TRANSPOSE,      transpose,      vf);
    REGISTER_FILTER(TRIM,           trim,           vf);
    REGISTER_FILTER(UNSHARP,        unsharp,        vf);
    REGISTER_FILTER(VFLIP,          vflip,          vf);
    REGISTER_FILTER(VIDSTABDETECT,  vidstabdetect,  vf);
    REGISTER_FILTER(VIDSTABTRANSFORM, vidstabtransform, vf);
    REGISTER_FILTER(YADIF,          yadif,          vf);

    REGISTER_FILTER(CELLAUTO,       cellauto,       vsrc);
    REGISTER_FILTER(COLOR,          color,          vsrc);
    REGISTER_FILTER(FREI0R,         frei0r_src,     vsrc);
    REGISTER_FILTER(LIFE,           life,           vsrc);
    REGISTER_FILTER(MANDELBROT,     mandelbrot,     vsrc);
    REGISTER_FILTER(MPTESTSRC,      mptestsrc,      vsrc);
    REGISTER_FILTER(NULLSRC,        nullsrc,        vsrc);
    REGISTER_FILTER(RGBTESTSRC,     rgbtestsrc,     vsrc);
    REGISTER_FILTER(SMPTEBARS,      smptebars,      vsrc);
    REGISTER_FILTER(SMPTEHDBARS,    smptehdbars,    vsrc);
    REGISTER_FILTER(TESTSRC,        testsrc,        vsrc);

    REGISTER_FILTER(NULLSINK,       nullsink,       vsink);

    /* multimedia filters */
    REGISTER_FILTER(CONCAT,         concat,         avf);
    REGISTER_FILTER(SHOWSPECTRUM,   showspectrum,   avf);
    REGISTER_FILTER(SHOWWAVES,      showwaves,      avf);

    /* multimedia sources */
    REGISTER_FILTER(AMOVIE,         amovie,         avsrc);
    REGISTER_FILTER(MOVIE,          movie,          avsrc);

#if FF_API_AVFILTERBUFFER
    REGISTER_FILTER_UNCONDITIONAL(vsink_ffbuffersink);
    REGISTER_FILTER_UNCONDITIONAL(asink_ffabuffersink);
#endif

    /* those filters are part of public or internal API => registered
     * unconditionally */
    REGISTER_FILTER_UNCONDITIONAL(asrc_abuffer);
    REGISTER_FILTER_UNCONDITIONAL(vsrc_buffer);
    REGISTER_FILTER_UNCONDITIONAL(asink_abuffer);
    REGISTER_FILTER_UNCONDITIONAL(vsink_buffer);
    REGISTER_FILTER_UNCONDITIONAL(af_afifo);
    REGISTER_FILTER_UNCONDITIONAL(vf_fifo);
    ff_opencl_register_filter_kernel_code_all();
}
