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

    REGISTER_FILTER(ACOMPRESSOR,    acompressor,    af);
    REGISTER_FILTER(ACROSSFADE,     acrossfade,     af);
    REGISTER_FILTER(ADELAY,         adelay,         af);
    REGISTER_FILTER(AECHO,          aecho,          af);
    REGISTER_FILTER(AEMPHASIS,      aemphasis,      af);
    REGISTER_FILTER(AEVAL,          aeval,          af);
    REGISTER_FILTER(AFADE,          afade,          af);
    REGISTER_FILTER(AFFTFILT,       afftfilt,       af);
    REGISTER_FILTER(AFORMAT,        aformat,        af);
    REGISTER_FILTER(AGATE,          agate,          af);
    REGISTER_FILTER(AINTERLEAVE,    ainterleave,    af);
    REGISTER_FILTER(ALIMITER,       alimiter,       af);
    REGISTER_FILTER(ALLPASS,        allpass,        af);
    REGISTER_FILTER(AMERGE,         amerge,         af);
    REGISTER_FILTER(AMETADATA,      ametadata,      af);
    REGISTER_FILTER(AMIX,           amix,           af);
    REGISTER_FILTER(ANEQUALIZER,    anequalizer,    af);
    REGISTER_FILTER(ANULL,          anull,          af);
    REGISTER_FILTER(APAD,           apad,           af);
    REGISTER_FILTER(APERMS,         aperms,         af);
    REGISTER_FILTER(APHASER,        aphaser,        af);
    REGISTER_FILTER(APULSATOR,      apulsator,      af);
    REGISTER_FILTER(AREALTIME,      arealtime,      af);
    REGISTER_FILTER(ARESAMPLE,      aresample,      af);
    REGISTER_FILTER(AREVERSE,       areverse,       af);
    REGISTER_FILTER(ASELECT,        aselect,        af);
    REGISTER_FILTER(ASENDCMD,       asendcmd,       af);
    REGISTER_FILTER(ASETNSAMPLES,   asetnsamples,   af);
    REGISTER_FILTER(ASETPTS,        asetpts,        af);
    REGISTER_FILTER(ASETRATE,       asetrate,       af);
    REGISTER_FILTER(ASETTB,         asettb,         af);
    REGISTER_FILTER(ASHOWINFO,      ashowinfo,      af);
    REGISTER_FILTER(ASPLIT,         asplit,         af);
    REGISTER_FILTER(ASTATS,         astats,         af);
    REGISTER_FILTER(ASTREAMSELECT,  astreamselect,  af);
    REGISTER_FILTER(ASYNCTS,        asyncts,        af);
    REGISTER_FILTER(ATEMPO,         atempo,         af);
    REGISTER_FILTER(ATRIM,          atrim,          af);
    REGISTER_FILTER(AZMQ,           azmq,           af);
    REGISTER_FILTER(BANDPASS,       bandpass,       af);
    REGISTER_FILTER(BANDREJECT,     bandreject,     af);
    REGISTER_FILTER(BASS,           bass,           af);
    REGISTER_FILTER(BIQUAD,         biquad,         af);
    REGISTER_FILTER(BS2B,           bs2b,           af);
    REGISTER_FILTER(CHANNELMAP,     channelmap,     af);
    REGISTER_FILTER(CHANNELSPLIT,   channelsplit,   af);
    REGISTER_FILTER(CHORUS,         chorus,         af);
    REGISTER_FILTER(COMPAND,        compand,        af);
    REGISTER_FILTER(COMPENSATIONDELAY, compensationdelay, af);
    REGISTER_FILTER(DCSHIFT,        dcshift,        af);
    REGISTER_FILTER(DYNAUDNORM,     dynaudnorm,     af);
    REGISTER_FILTER(EARWAX,         earwax,         af);
    REGISTER_FILTER(EBUR128,        ebur128,        af);
    REGISTER_FILTER(EQUALIZER,      equalizer,      af);
    REGISTER_FILTER(EXTRASTEREO,    extrastereo,    af);
    REGISTER_FILTER(FLANGER,        flanger,        af);
    REGISTER_FILTER(HIGHPASS,       highpass,       af);
    REGISTER_FILTER(JOIN,           join,           af);
    REGISTER_FILTER(LADSPA,         ladspa,         af);
    REGISTER_FILTER(LOWPASS,        lowpass,        af);
    REGISTER_FILTER(PAN,            pan,            af);
    REGISTER_FILTER(REPLAYGAIN,     replaygain,     af);
    REGISTER_FILTER(RESAMPLE,       resample,       af);
    REGISTER_FILTER(RUBBERBAND,     rubberband,     af);
    REGISTER_FILTER(SIDECHAINCOMPRESS, sidechaincompress, af);
    REGISTER_FILTER(SIDECHAINGATE,  sidechaingate,  af);
    REGISTER_FILTER(SILENCEDETECT,  silencedetect,  af);
    REGISTER_FILTER(SILENCEREMOVE,  silenceremove,  af);
    REGISTER_FILTER(SOFALIZER,      sofalizer,      af);
    REGISTER_FILTER(STEREOTOOLS,    stereotools,    af);
    REGISTER_FILTER(STEREOWIDEN,    stereowiden,    af);
    REGISTER_FILTER(TREBLE,         treble,         af);
    REGISTER_FILTER(TREMOLO,        tremolo,        af);
    REGISTER_FILTER(VIBRATO,        vibrato,        af);
    REGISTER_FILTER(VOLUME,         volume,         af);
    REGISTER_FILTER(VOLUMEDETECT,   volumedetect,   af);

    REGISTER_FILTER(AEVALSRC,       aevalsrc,       asrc);
    REGISTER_FILTER(ANOISESRC,      anoisesrc,      asrc);
    REGISTER_FILTER(ANULLSRC,       anullsrc,       asrc);
    REGISTER_FILTER(FLITE,          flite,          asrc);
    REGISTER_FILTER(SINE,           sine,           asrc);

    REGISTER_FILTER(ANULLSINK,      anullsink,      asink);

    REGISTER_FILTER(ALPHAEXTRACT,   alphaextract,   vf);
    REGISTER_FILTER(ALPHAMERGE,     alphamerge,     vf);
    REGISTER_FILTER(ATADENOISE,     atadenoise,     vf);
    REGISTER_FILTER(ASS,            ass,            vf);
    REGISTER_FILTER(BBOX,           bbox,           vf);
    REGISTER_FILTER(BLACKDETECT,    blackdetect,    vf);
    REGISTER_FILTER(BLACKFRAME,     blackframe,     vf);
    REGISTER_FILTER(BLEND,          blend,          vf);
    REGISTER_FILTER(BOXBLUR,        boxblur,        vf);
    REGISTER_FILTER(CHROMAKEY,      chromakey,      vf);
    REGISTER_FILTER(CODECVIEW,      codecview,      vf);
    REGISTER_FILTER(COLORBALANCE,   colorbalance,   vf);
    REGISTER_FILTER(COLORCHANNELMIXER, colorchannelmixer, vf);
    REGISTER_FILTER(COLORKEY,       colorkey,       vf);
    REGISTER_FILTER(COLORLEVELS,    colorlevels,    vf);
    REGISTER_FILTER(COLORMATRIX,    colormatrix,    vf);
    REGISTER_FILTER(CONVOLUTION,    convolution,    vf);
    REGISTER_FILTER(COPY,           copy,           vf);
    REGISTER_FILTER(COVER_RECT,     cover_rect,     vf);
    REGISTER_FILTER(CROP,           crop,           vf);
    REGISTER_FILTER(CROPDETECT,     cropdetect,     vf);
    REGISTER_FILTER(CURVES,         curves,         vf);
    REGISTER_FILTER(DCTDNOIZ,       dctdnoiz,       vf);
    REGISTER_FILTER(DEBAND,         deband,         vf);
    REGISTER_FILTER(DECIMATE,       decimate,       vf);
    REGISTER_FILTER(DEFLATE,        deflate,        vf);
    REGISTER_FILTER(DEJUDDER,       dejudder,       vf);
    REGISTER_FILTER(DELOGO,         delogo,         vf);
    REGISTER_FILTER(DESHAKE,        deshake,        vf);
    REGISTER_FILTER(DETELECINE,     detelecine,     vf);
    REGISTER_FILTER(DILATION,       dilation,       vf);
    REGISTER_FILTER(DISPLACE,       displace,       vf);
    REGISTER_FILTER(DRAWBOX,        drawbox,        vf);
    REGISTER_FILTER(DRAWGRAPH,      drawgraph,      vf);
    REGISTER_FILTER(DRAWGRID,       drawgrid,       vf);
    REGISTER_FILTER(DRAWTEXT,       drawtext,       vf);
    REGISTER_FILTER(EDGEDETECT,     edgedetect,     vf);
    REGISTER_FILTER(ELBG,           elbg,           vf);
    REGISTER_FILTER(EQ,             eq,             vf);
    REGISTER_FILTER(EROSION,        erosion,        vf);
    REGISTER_FILTER(EXTRACTPLANES,  extractplanes,  vf);
    REGISTER_FILTER(FADE,           fade,           vf);
    REGISTER_FILTER(FFTFILT,        fftfilt,        vf);
    REGISTER_FILTER(FIELD,          field,          vf);
    REGISTER_FILTER(FIELDMATCH,     fieldmatch,     vf);
    REGISTER_FILTER(FIELDORDER,     fieldorder,     vf);
    REGISTER_FILTER(FIND_RECT,      find_rect,      vf);
    REGISTER_FILTER(FORMAT,         format,         vf);
    REGISTER_FILTER(FPS,            fps,            vf);
    REGISTER_FILTER(FRAMEPACK,      framepack,      vf);
    REGISTER_FILTER(FRAMERATE,      framerate,      vf);
    REGISTER_FILTER(FRAMESTEP,      framestep,      vf);
    REGISTER_FILTER(FREI0R,         frei0r,         vf);
    REGISTER_FILTER(FSPP,           fspp,           vf);
    REGISTER_FILTER(GEQ,            geq,            vf);
    REGISTER_FILTER(GRADFUN,        gradfun,        vf);
    REGISTER_FILTER(HALDCLUT,       haldclut,       vf);
    REGISTER_FILTER(HFLIP,          hflip,          vf);
    REGISTER_FILTER(HISTEQ,         histeq,         vf);
    REGISTER_FILTER(HISTOGRAM,      histogram,      vf);
    REGISTER_FILTER(HQDN3D,         hqdn3d,         vf);
    REGISTER_FILTER(HQX,            hqx,            vf);
    REGISTER_FILTER(HSTACK,         hstack,         vf);
    REGISTER_FILTER(HUE,            hue,            vf);
    REGISTER_FILTER(IDET,           idet,           vf);
    REGISTER_FILTER(IL,             il,             vf);
    REGISTER_FILTER(INFLATE,        inflate,        vf);
    REGISTER_FILTER(INTERLACE,      interlace,      vf);
    REGISTER_FILTER(INTERLEAVE,     interleave,     vf);
    REGISTER_FILTER(KERNDEINT,      kerndeint,      vf);
    REGISTER_FILTER(LENSCORRECTION, lenscorrection, vf);
    REGISTER_FILTER(LUT3D,          lut3d,          vf);
    REGISTER_FILTER(LUT,            lut,            vf);
    REGISTER_FILTER(LUTRGB,         lutrgb,         vf);
    REGISTER_FILTER(LUTYUV,         lutyuv,         vf);
    REGISTER_FILTER(MASKEDMERGE,    maskedmerge,    vf);
    REGISTER_FILTER(MCDEINT,        mcdeint,        vf);
    REGISTER_FILTER(MERGEPLANES,    mergeplanes,    vf);
    REGISTER_FILTER(METADATA,       metadata,       vf);
    REGISTER_FILTER(MPDECIMATE,     mpdecimate,     vf);
    REGISTER_FILTER(NEGATE,         negate,         vf);
    REGISTER_FILTER(NNEDI,          nnedi,          vf);
    REGISTER_FILTER(NOFORMAT,       noformat,       vf);
    REGISTER_FILTER(NOISE,          noise,          vf);
    REGISTER_FILTER(NULL,           null,           vf);
    REGISTER_FILTER(OCR,            ocr,            vf);
    REGISTER_FILTER(OCV,            ocv,            vf);
    REGISTER_FILTER(OVERLAY,        overlay,        vf);
    REGISTER_FILTER(OWDENOISE,      owdenoise,      vf);
    REGISTER_FILTER(PAD,            pad,            vf);
    REGISTER_FILTER(PALETTEGEN,     palettegen,     vf);
    REGISTER_FILTER(PALETTEUSE,     paletteuse,     vf);
    REGISTER_FILTER(PERMS,          perms,          vf);
    REGISTER_FILTER(PERSPECTIVE,    perspective,    vf);
    REGISTER_FILTER(PHASE,          phase,          vf);
    REGISTER_FILTER(PIXDESCTEST,    pixdesctest,    vf);
    REGISTER_FILTER(PP,             pp,             vf);
    REGISTER_FILTER(PP7,            pp7,            vf);
    REGISTER_FILTER(PSNR,           psnr,           vf);
    REGISTER_FILTER(PULLUP,         pullup,         vf);
    REGISTER_FILTER(QP,             qp,             vf);
    REGISTER_FILTER(RANDOM,         random,         vf);
    REGISTER_FILTER(REALTIME,       realtime,       vf);
    REGISTER_FILTER(REMOVEGRAIN,    removegrain,    vf);
    REGISTER_FILTER(REMOVELOGO,     removelogo,     vf);
    REGISTER_FILTER(REPEATFIELDS,   repeatfields,   vf);
    REGISTER_FILTER(REVERSE,        reverse,        vf);
    REGISTER_FILTER(ROTATE,         rotate,         vf);
    REGISTER_FILTER(SAB,            sab,            vf);
    REGISTER_FILTER(SCALE,          scale,          vf);
    REGISTER_FILTER(SCALE2REF,      scale2ref,      vf);
    REGISTER_FILTER(SELECT,         select,         vf);
    REGISTER_FILTER(SELECTIVECOLOR, selectivecolor, vf);
    REGISTER_FILTER(SENDCMD,        sendcmd,        vf);
    REGISTER_FILTER(SEPARATEFIELDS, separatefields, vf);
    REGISTER_FILTER(SETDAR,         setdar,         vf);
    REGISTER_FILTER(SETFIELD,       setfield,       vf);
    REGISTER_FILTER(SETPTS,         setpts,         vf);
    REGISTER_FILTER(SETSAR,         setsar,         vf);
    REGISTER_FILTER(SETTB,          settb,          vf);
    REGISTER_FILTER(SHOWINFO,       showinfo,       vf);
    REGISTER_FILTER(SHOWPALETTE,    showpalette,    vf);
    REGISTER_FILTER(SHUFFLEFRAMES,  shuffleframes,  vf);
    REGISTER_FILTER(SHUFFLEPLANES,  shuffleplanes,  vf);
    REGISTER_FILTER(SIGNALSTATS,    signalstats,    vf);
    REGISTER_FILTER(SMARTBLUR,      smartblur,      vf);
    REGISTER_FILTER(SPLIT,          split,          vf);
    REGISTER_FILTER(SPP,            spp,            vf);
    REGISTER_FILTER(SSIM,           ssim,           vf);
    REGISTER_FILTER(STEREO3D,       stereo3d,       vf);
    REGISTER_FILTER(STREAMSELECT,   streamselect,   vf);
    REGISTER_FILTER(SUBTITLES,      subtitles,      vf);
    REGISTER_FILTER(SUPER2XSAI,     super2xsai,     vf);
    REGISTER_FILTER(SWAPRECT,       swaprect,       vf);
    REGISTER_FILTER(SWAPUV,         swapuv,         vf);
    REGISTER_FILTER(TBLEND,         tblend,         vf);
    REGISTER_FILTER(TELECINE,       telecine,       vf);
    REGISTER_FILTER(THUMBNAIL,      thumbnail,      vf);
    REGISTER_FILTER(TILE,           tile,           vf);
    REGISTER_FILTER(TINTERLACE,     tinterlace,     vf);
    REGISTER_FILTER(TRANSPOSE,      transpose,      vf);
    REGISTER_FILTER(TRIM,           trim,           vf);
    REGISTER_FILTER(UNSHARP,        unsharp,        vf);
    REGISTER_FILTER(USPP,           uspp,           vf);
    REGISTER_FILTER(VECTORSCOPE,    vectorscope,    vf);
    REGISTER_FILTER(VFLIP,          vflip,          vf);
    REGISTER_FILTER(VIDSTABDETECT,  vidstabdetect,  vf);
    REGISTER_FILTER(VIDSTABTRANSFORM, vidstabtransform, vf);
    REGISTER_FILTER(VIGNETTE,       vignette,       vf);
    REGISTER_FILTER(VSTACK,         vstack,         vf);
    REGISTER_FILTER(W3FDIF,         w3fdif,         vf);
    REGISTER_FILTER(WAVEFORM,       waveform,       vf);
    REGISTER_FILTER(XBR,            xbr,            vf);
    REGISTER_FILTER(YADIF,          yadif,          vf);
    REGISTER_FILTER(ZMQ,            zmq,            vf);
    REGISTER_FILTER(ZOOMPAN,        zoompan,        vf);
    REGISTER_FILTER(ZSCALE,         zscale,         vf);

    REGISTER_FILTER(ALLRGB,         allrgb,         vsrc);
    REGISTER_FILTER(ALLYUV,         allyuv,         vsrc);
    REGISTER_FILTER(CELLAUTO,       cellauto,       vsrc);
    REGISTER_FILTER(COLOR,          color,          vsrc);
    REGISTER_FILTER(FREI0R,         frei0r_src,     vsrc);
    REGISTER_FILTER(HALDCLUTSRC,    haldclutsrc,    vsrc);
    REGISTER_FILTER(LIFE,           life,           vsrc);
    REGISTER_FILTER(MANDELBROT,     mandelbrot,     vsrc);
    REGISTER_FILTER(MPTESTSRC,      mptestsrc,      vsrc);
    REGISTER_FILTER(NULLSRC,        nullsrc,        vsrc);
    REGISTER_FILTER(RGBTESTSRC,     rgbtestsrc,     vsrc);
    REGISTER_FILTER(SMPTEBARS,      smptebars,      vsrc);
    REGISTER_FILTER(SMPTEHDBARS,    smptehdbars,    vsrc);
    REGISTER_FILTER(TESTSRC,        testsrc,        vsrc);
    REGISTER_FILTER(TESTSRC2,       testsrc2,       vsrc);

    REGISTER_FILTER(NULLSINK,       nullsink,       vsink);

    /* multimedia filters */
    REGISTER_FILTER(ADRAWGRAPH,     adrawgraph,     avf);
    REGISTER_FILTER(AHISTOGRAM,     ahistogram,     avf);
    REGISTER_FILTER(APHASEMETER,    aphasemeter,    avf);
    REGISTER_FILTER(AVECTORSCOPE,   avectorscope,   avf);
    REGISTER_FILTER(CONCAT,         concat,         avf);
    REGISTER_FILTER(SHOWCQT,        showcqt,        avf);
    REGISTER_FILTER(SHOWFREQS,      showfreqs,      avf);
    REGISTER_FILTER(SHOWSPECTRUM,   showspectrum,   avf);
    REGISTER_FILTER(SHOWSPECTRUMPIC, showspectrumpic, avf);
    REGISTER_FILTER(SHOWVOLUME,     showvolume,     avf);
    REGISTER_FILTER(SHOWWAVES,      showwaves,      avf);
    REGISTER_FILTER(SHOWWAVESPIC,   showwavespic,   avf);
    REGISTER_FILTER(SPECTRUMSYNTH,  spectrumsynth,  vaf);

    /* multimedia sources */
    REGISTER_FILTER(AMOVIE,         amovie,         avsrc);
    REGISTER_FILTER(MOVIE,          movie,          avsrc);

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
