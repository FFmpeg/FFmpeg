/*
 * Register all the formats and protocols
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard
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

#include "avformat.h"
#include "rtp.h"
#include "rdt.h"
#include "url.h"
#include "version.h"

#define REGISTER_MUXER(X, x)                                            \
    {                                                                   \
        extern AVOutputFormat ff_##x##_muxer;                           \
        if (CONFIG_##X##_MUXER)                                         \
            av_register_output_format(&ff_##x##_muxer);                 \
    }

#define REGISTER_DEMUXER(X, x)                                          \
    {                                                                   \
        extern AVInputFormat ff_##x##_demuxer;                          \
        if (CONFIG_##X##_DEMUXER)                                       \
            av_register_input_format(&ff_##x##_demuxer);                \
    }

#define REGISTER_MUXDEMUX(X, x) REGISTER_MUXER(X, x); REGISTER_DEMUXER(X, x)

void av_register_all(void)
{
    static int initialized;

    if (initialized)
        return;

    avcodec_register_all();

    /* (de)muxers */
    REGISTER_MUXER   (A64,              a64);
    REGISTER_DEMUXER (AA,               aa);
    REGISTER_DEMUXER (AAC,              aac);
    REGISTER_MUXDEMUX(AC3,              ac3);
    REGISTER_DEMUXER (ACM,              acm);
    REGISTER_DEMUXER (ACT,              act);
    REGISTER_DEMUXER (ADF,              adf);
    REGISTER_DEMUXER (ADP,              adp);
    REGISTER_DEMUXER (ADS,              ads);
    REGISTER_MUXER   (ADTS,             adts);
    REGISTER_MUXDEMUX(ADX,              adx);
    REGISTER_DEMUXER (AEA,              aea);
    REGISTER_DEMUXER (AFC,              afc);
    REGISTER_MUXDEMUX(AIFF,             aiff);
    REGISTER_DEMUXER (AIX,              aix);
    REGISTER_MUXDEMUX(AMR,              amr);
    REGISTER_DEMUXER (ANM,              anm);
    REGISTER_DEMUXER (APC,              apc);
    REGISTER_DEMUXER (APE,              ape);
    REGISTER_MUXDEMUX(APNG,             apng);
    REGISTER_DEMUXER (AQTITLE,          aqtitle);
    REGISTER_MUXDEMUX(ASF,              asf);
    REGISTER_DEMUXER (ASF_O,            asf_o);
    REGISTER_MUXDEMUX(ASS,              ass);
    REGISTER_MUXDEMUX(AST,              ast);
    REGISTER_MUXER   (ASF_STREAM,       asf_stream);
    REGISTER_MUXDEMUX(AU,               au);
    REGISTER_MUXDEMUX(AVI,              avi);
    REGISTER_DEMUXER (AVISYNTH,         avisynth);
    REGISTER_MUXER   (AVM2,             avm2);
    REGISTER_DEMUXER (AVR,              avr);
    REGISTER_DEMUXER (AVS,              avs);
    REGISTER_DEMUXER (BETHSOFTVID,      bethsoftvid);
    REGISTER_DEMUXER (BFI,              bfi);
    REGISTER_DEMUXER (BINTEXT,          bintext);
    REGISTER_DEMUXER (BINK,             bink);
    REGISTER_MUXDEMUX(BIT,              bit);
    REGISTER_DEMUXER (BMV,              bmv);
    REGISTER_DEMUXER (BFSTM,            bfstm);
    REGISTER_DEMUXER (BRSTM,            brstm);
    REGISTER_DEMUXER (BOA,              boa);
    REGISTER_DEMUXER (C93,              c93);
    REGISTER_MUXDEMUX(CAF,              caf);
    REGISTER_MUXDEMUX(CAVSVIDEO,        cavsvideo);
    REGISTER_DEMUXER (CDG,              cdg);
    REGISTER_DEMUXER (CDXL,             cdxl);
    REGISTER_DEMUXER (CINE,             cine);
    REGISTER_DEMUXER (CONCAT,           concat);
    REGISTER_MUXER   (CRC,              crc);
    REGISTER_MUXER   (DASH,             dash);
    REGISTER_MUXDEMUX(DATA,             data);
    REGISTER_MUXDEMUX(DAUD,             daud);
    REGISTER_DEMUXER (DCSTR,            dcstr);
    REGISTER_DEMUXER (DFA,              dfa);
    REGISTER_MUXDEMUX(DIRAC,            dirac);
    REGISTER_MUXDEMUX(DNXHD,            dnxhd);
    REGISTER_DEMUXER (DSF,              dsf);
    REGISTER_DEMUXER (DSICIN,           dsicin);
    REGISTER_DEMUXER (DSS,              dss);
    REGISTER_MUXDEMUX(DTS,              dts);
    REGISTER_DEMUXER (DTSHD,            dtshd);
    REGISTER_MUXDEMUX(DV,               dv);
    REGISTER_DEMUXER (DVBSUB,           dvbsub);
    REGISTER_DEMUXER (DVBTXT,           dvbtxt);
    REGISTER_DEMUXER (DXA,              dxa);
    REGISTER_DEMUXER (EA,               ea);
    REGISTER_DEMUXER (EA_CDATA,         ea_cdata);
    REGISTER_MUXDEMUX(EAC3,             eac3);
    REGISTER_DEMUXER (EPAF,             epaf);
    REGISTER_MUXER   (F4V,              f4v);
    REGISTER_MUXDEMUX(FFM,              ffm);
    REGISTER_MUXDEMUX(FFMETADATA,       ffmetadata);
    REGISTER_MUXER   (FIFO,             fifo);
    REGISTER_MUXDEMUX(FILMSTRIP,        filmstrip);
    REGISTER_MUXDEMUX(FLAC,             flac);
    REGISTER_DEMUXER (FLIC,             flic);
    REGISTER_MUXDEMUX(FLV,              flv);
    REGISTER_DEMUXER (LIVE_FLV,         live_flv);
    REGISTER_DEMUXER (FOURXM,           fourxm);
    REGISTER_MUXER   (FRAMECRC,         framecrc);
    REGISTER_MUXER   (FRAMEHASH,        framehash);
    REGISTER_MUXER   (FRAMEMD5,         framemd5);
    REGISTER_DEMUXER (FRM,              frm);
    REGISTER_DEMUXER (FSB,              fsb);
    REGISTER_MUXDEMUX(G722,             g722);
    REGISTER_MUXDEMUX(G723_1,           g723_1);
    REGISTER_DEMUXER (G729,             g729);
    REGISTER_DEMUXER (GENH,             genh);
    REGISTER_MUXDEMUX(GIF,              gif);
    REGISTER_MUXDEMUX(GSM,              gsm);
    REGISTER_MUXDEMUX(GXF,              gxf);
    REGISTER_MUXDEMUX(H261,             h261);
    REGISTER_MUXDEMUX(H263,             h263);
    REGISTER_MUXDEMUX(H264,             h264);
    REGISTER_MUXER   (HASH,             hash);
    REGISTER_MUXER   (HDS,              hds);
    REGISTER_MUXDEMUX(HEVC,             hevc);
    REGISTER_MUXDEMUX(HLS,              hls);
    REGISTER_DEMUXER (HNM,              hnm);
    REGISTER_MUXDEMUX(ICO,              ico);
    REGISTER_DEMUXER (IDCIN,            idcin);
    REGISTER_DEMUXER (IDF,              idf);
    REGISTER_DEMUXER (IFF,              iff);
    REGISTER_MUXDEMUX(ILBC,             ilbc);
    REGISTER_MUXDEMUX(IMAGE2,           image2);
    REGISTER_MUXDEMUX(IMAGE2PIPE,       image2pipe);
    REGISTER_DEMUXER (IMAGE2_ALIAS_PIX, image2_alias_pix);
    REGISTER_DEMUXER (IMAGE2_BRENDER_PIX, image2_brender_pix);
    REGISTER_DEMUXER (INGENIENT,        ingenient);
    REGISTER_DEMUXER (IPMOVIE,          ipmovie);
    REGISTER_MUXER   (IPOD,             ipod);
    REGISTER_MUXDEMUX(IRCAM,            ircam);
    REGISTER_MUXER   (ISMV,             ismv);
    REGISTER_DEMUXER (ISS,              iss);
    REGISTER_DEMUXER (IV8,              iv8);
    REGISTER_MUXDEMUX(IVF,              ivf);
    REGISTER_DEMUXER (IVR,              ivr);
    REGISTER_MUXDEMUX(JACOSUB,          jacosub);
    REGISTER_DEMUXER (JV,               jv);
    REGISTER_MUXER   (LATM,             latm);
    REGISTER_DEMUXER (LMLM4,            lmlm4);
    REGISTER_DEMUXER (LOAS,             loas);
    REGISTER_MUXDEMUX(LRC,              lrc);
    REGISTER_DEMUXER (LVF,              lvf);
    REGISTER_DEMUXER (LXF,              lxf);
    REGISTER_MUXDEMUX(M4V,              m4v);
    REGISTER_MUXER   (MD5,              md5);
    REGISTER_MUXDEMUX(MATROSKA,         matroska);
    REGISTER_MUXER   (MATROSKA_AUDIO,   matroska_audio);
    REGISTER_DEMUXER (MGSTS,            mgsts);
    REGISTER_MUXDEMUX(MICRODVD,         microdvd);
    REGISTER_MUXDEMUX(MJPEG,            mjpeg);
    REGISTER_MUXDEMUX(MLP,              mlp);
    REGISTER_DEMUXER (MLV,              mlv);
    REGISTER_DEMUXER (MM,               mm);
    REGISTER_MUXDEMUX(MMF,              mmf);
    REGISTER_MUXDEMUX(MOV,              mov);
    REGISTER_MUXER   (MP2,              mp2);
    REGISTER_MUXDEMUX(MP3,              mp3);
    REGISTER_MUXER   (MP4,              mp4);
    REGISTER_DEMUXER (MPC,              mpc);
    REGISTER_DEMUXER (MPC8,             mpc8);
    REGISTER_MUXER   (MPEG1SYSTEM,      mpeg1system);
    REGISTER_MUXER   (MPEG1VCD,         mpeg1vcd);
    REGISTER_MUXER   (MPEG1VIDEO,       mpeg1video);
    REGISTER_MUXER   (MPEG2DVD,         mpeg2dvd);
    REGISTER_MUXER   (MPEG2SVCD,        mpeg2svcd);
    REGISTER_MUXER   (MPEG2VIDEO,       mpeg2video);
    REGISTER_MUXER   (MPEG2VOB,         mpeg2vob);
    REGISTER_DEMUXER (MPEGPS,           mpegps);
    REGISTER_MUXDEMUX(MPEGTS,           mpegts);
    REGISTER_DEMUXER (MPEGTSRAW,        mpegtsraw);
    REGISTER_DEMUXER (MPEGVIDEO,        mpegvideo);
    REGISTER_MUXDEMUX(MPJPEG,           mpjpeg);
    REGISTER_DEMUXER (MPL2,             mpl2);
    REGISTER_DEMUXER (MPSUB,            mpsub);
    REGISTER_DEMUXER (MSF,              msf);
    REGISTER_DEMUXER (MSNWC_TCP,        msnwc_tcp);
    REGISTER_DEMUXER (MTAF,             mtaf);
    REGISTER_DEMUXER (MTV,              mtv);
    REGISTER_DEMUXER (MUSX,             musx);
    REGISTER_DEMUXER (MV,               mv);
    REGISTER_DEMUXER (MVI,              mvi);
    REGISTER_MUXDEMUX(MXF,              mxf);
    REGISTER_MUXER   (MXF_D10,          mxf_d10);
    REGISTER_MUXER   (MXF_OPATOM,       mxf_opatom);
    REGISTER_DEMUXER (MXG,              mxg);
    REGISTER_DEMUXER (NC,               nc);
    REGISTER_DEMUXER (NISTSPHERE,       nistsphere);
    REGISTER_DEMUXER (NSV,              nsv);
    REGISTER_MUXER   (NULL,             null);
    REGISTER_MUXDEMUX(NUT,              nut);
    REGISTER_DEMUXER (NUV,              nuv);
    REGISTER_MUXER   (OGA,              oga);
    REGISTER_MUXDEMUX(OGG,              ogg);
    REGISTER_MUXER   (OGV,              ogv);
    REGISTER_MUXDEMUX(OMA,              oma);
    REGISTER_MUXER   (OPUS,             opus);
    REGISTER_DEMUXER (PAF,              paf);
    REGISTER_MUXDEMUX(PCM_ALAW,         pcm_alaw);
    REGISTER_MUXDEMUX(PCM_MULAW,        pcm_mulaw);
    REGISTER_MUXDEMUX(PCM_F64BE,        pcm_f64be);
    REGISTER_MUXDEMUX(PCM_F64LE,        pcm_f64le);
    REGISTER_MUXDEMUX(PCM_F32BE,        pcm_f32be);
    REGISTER_MUXDEMUX(PCM_F32LE,        pcm_f32le);
    REGISTER_MUXDEMUX(PCM_S32BE,        pcm_s32be);
    REGISTER_MUXDEMUX(PCM_S32LE,        pcm_s32le);
    REGISTER_MUXDEMUX(PCM_S24BE,        pcm_s24be);
    REGISTER_MUXDEMUX(PCM_S24LE,        pcm_s24le);
    REGISTER_MUXDEMUX(PCM_S16BE,        pcm_s16be);
    REGISTER_MUXDEMUX(PCM_S16LE,        pcm_s16le);
    REGISTER_MUXDEMUX(PCM_S8,           pcm_s8);
    REGISTER_MUXDEMUX(PCM_U32BE,        pcm_u32be);
    REGISTER_MUXDEMUX(PCM_U32LE,        pcm_u32le);
    REGISTER_MUXDEMUX(PCM_U24BE,        pcm_u24be);
    REGISTER_MUXDEMUX(PCM_U24LE,        pcm_u24le);
    REGISTER_MUXDEMUX(PCM_U16BE,        pcm_u16be);
    REGISTER_MUXDEMUX(PCM_U16LE,        pcm_u16le);
    REGISTER_MUXDEMUX(PCM_U8,           pcm_u8);
    REGISTER_DEMUXER (PJS,              pjs);
    REGISTER_DEMUXER (PMP,              pmp);
    REGISTER_MUXER   (PSP,              psp);
    REGISTER_DEMUXER (PVA,              pva);
    REGISTER_DEMUXER (PVF,              pvf);
    REGISTER_DEMUXER (QCP,              qcp);
    REGISTER_DEMUXER (R3D,              r3d);
    REGISTER_MUXDEMUX(RAWVIDEO,         rawvideo);
    REGISTER_DEMUXER (REALTEXT,         realtext);
    REGISTER_DEMUXER (REDSPARK,         redspark);
    REGISTER_DEMUXER (RL2,              rl2);
    REGISTER_MUXDEMUX(RM,               rm);
    REGISTER_MUXDEMUX(ROQ,              roq);
    REGISTER_DEMUXER (RPL,              rpl);
    REGISTER_DEMUXER (RSD,              rsd);
    REGISTER_MUXDEMUX(RSO,              rso);
    REGISTER_MUXDEMUX(RTP,              rtp);
    REGISTER_MUXER   (RTP_MPEGTS,       rtp_mpegts);
    REGISTER_MUXDEMUX(RTSP,             rtsp);
    REGISTER_DEMUXER (SAMI,             sami);
    REGISTER_MUXDEMUX(SAP,              sap);
    REGISTER_DEMUXER (SBG,              sbg);
    REGISTER_DEMUXER (SDP,              sdp);
    REGISTER_DEMUXER (SDR2,             sdr2);
#if CONFIG_RTPDEC
    ff_register_rtp_dynamic_payload_handlers();
    ff_register_rdt_dynamic_payload_handlers();
#endif
    REGISTER_DEMUXER (SEGAFILM,         segafilm);
    REGISTER_MUXER   (SEGMENT,          segment);
    REGISTER_MUXER   (SEGMENT,          stream_segment);
    REGISTER_DEMUXER (SHORTEN,          shorten);
    REGISTER_DEMUXER (SIFF,             siff);
    REGISTER_MUXER   (SINGLEJPEG,       singlejpeg);
    REGISTER_DEMUXER (SLN,              sln);
    REGISTER_DEMUXER (SMACKER,          smacker);
    REGISTER_MUXDEMUX(SMJPEG,           smjpeg);
    REGISTER_MUXER   (SMOOTHSTREAMING,  smoothstreaming);
    REGISTER_DEMUXER (SMUSH,            smush);
    REGISTER_DEMUXER (SOL,              sol);
    REGISTER_MUXDEMUX(SOX,              sox);
    REGISTER_MUXER   (SPX,              spx);
    REGISTER_MUXDEMUX(SPDIF,            spdif);
    REGISTER_MUXDEMUX(SRT,              srt);
    REGISTER_DEMUXER (STR,              str);
    REGISTER_DEMUXER (STL,              stl);
    REGISTER_DEMUXER (SUBVIEWER1,       subviewer1);
    REGISTER_DEMUXER (SUBVIEWER,        subviewer);
    REGISTER_DEMUXER (SUP,              sup);
    REGISTER_DEMUXER (SVAG,             svag);
    REGISTER_MUXDEMUX(SWF,              swf);
    REGISTER_DEMUXER (TAK,              tak);
    REGISTER_MUXER   (TEE,              tee);
    REGISTER_DEMUXER (TEDCAPTIONS,      tedcaptions);
    REGISTER_MUXER   (TG2,              tg2);
    REGISTER_MUXER   (TGP,              tgp);
    REGISTER_DEMUXER (THP,              thp);
    REGISTER_DEMUXER (THREEDOSTR,       threedostr);
    REGISTER_DEMUXER (TIERTEXSEQ,       tiertexseq);
    REGISTER_MUXER   (MKVTIMESTAMP_V2,  mkvtimestamp_v2);
    REGISTER_DEMUXER (TMV,              tmv);
    REGISTER_MUXDEMUX(TRUEHD,           truehd);
    REGISTER_MUXDEMUX(TTA,              tta);
    REGISTER_DEMUXER (TXD,              txd);
    REGISTER_DEMUXER (TTY,              tty);
    REGISTER_MUXER   (UNCODEDFRAMECRC,  uncodedframecrc);
    REGISTER_DEMUXER (V210,             v210);
    REGISTER_DEMUXER (V210X,            v210x);
    REGISTER_DEMUXER (VAG,              vag);
    REGISTER_MUXDEMUX(VC1,              vc1);
    REGISTER_MUXDEMUX(VC1T,             vc1t);
    REGISTER_DEMUXER (VIVO,             vivo);
    REGISTER_DEMUXER (VMD,              vmd);
    REGISTER_DEMUXER (VOBSUB,           vobsub);
    REGISTER_MUXDEMUX(VOC,              voc);
    REGISTER_DEMUXER (VPK,              vpk);
    REGISTER_DEMUXER (VPLAYER,          vplayer);
    REGISTER_DEMUXER (VQF,              vqf);
    REGISTER_MUXDEMUX(W64,              w64);
    REGISTER_MUXDEMUX(WAV,              wav);
    REGISTER_DEMUXER (WC3,              wc3);
    REGISTER_MUXER   (WEBM,             webm);
    REGISTER_MUXDEMUX(WEBM_DASH_MANIFEST, webm_dash_manifest);
    REGISTER_MUXER   (WEBM_CHUNK,       webm_chunk);
    REGISTER_MUXER   (WEBP,             webp);
    REGISTER_MUXDEMUX(WEBVTT,           webvtt);
    REGISTER_DEMUXER (WSAUD,            wsaud);
    REGISTER_DEMUXER (WSD,              wsd);
    REGISTER_DEMUXER (WSVQA,            wsvqa);
    REGISTER_MUXDEMUX(WTV,              wtv);
    REGISTER_DEMUXER (WVE,              wve);
    REGISTER_MUXDEMUX(WV,               wv);
    REGISTER_DEMUXER (XA,               xa);
    REGISTER_DEMUXER (XBIN,             xbin);
    REGISTER_DEMUXER (XMV,              xmv);
    REGISTER_DEMUXER (XVAG,             xvag);
    REGISTER_DEMUXER (XWMA,             xwma);
    REGISTER_DEMUXER (YOP,              yop);
    REGISTER_MUXDEMUX(YUV4MPEGPIPE,     yuv4mpegpipe);

    /* image demuxers */
    REGISTER_DEMUXER (IMAGE_BMP_PIPE,        image_bmp_pipe);
    REGISTER_DEMUXER (IMAGE_DDS_PIPE,        image_dds_pipe);
    REGISTER_DEMUXER (IMAGE_DPX_PIPE,        image_dpx_pipe);
    REGISTER_DEMUXER (IMAGE_EXR_PIPE,        image_exr_pipe);
    REGISTER_DEMUXER (IMAGE_J2K_PIPE,        image_j2k_pipe);
    REGISTER_DEMUXER (IMAGE_JPEG_PIPE,       image_jpeg_pipe);
    REGISTER_DEMUXER (IMAGE_JPEGLS_PIPE,     image_jpegls_pipe);
    REGISTER_DEMUXER (IMAGE_PAM_PIPE,        image_pam_pipe);
    REGISTER_DEMUXER (IMAGE_PBM_PIPE,        image_pbm_pipe);
    REGISTER_DEMUXER (IMAGE_PCX_PIPE,        image_pcx_pipe);
    REGISTER_DEMUXER (IMAGE_PGMYUV_PIPE,     image_pgmyuv_pipe);
    REGISTER_DEMUXER (IMAGE_PGM_PIPE,        image_pgm_pipe);
    REGISTER_DEMUXER (IMAGE_PICTOR_PIPE,     image_pictor_pipe);
    REGISTER_DEMUXER (IMAGE_PNG_PIPE,        image_png_pipe);
    REGISTER_DEMUXER (IMAGE_PPM_PIPE,        image_ppm_pipe);
    REGISTER_DEMUXER (IMAGE_QDRAW_PIPE,      image_qdraw_pipe);
    REGISTER_DEMUXER (IMAGE_SGI_PIPE,        image_sgi_pipe);
    REGISTER_DEMUXER (IMAGE_SUNRAST_PIPE,    image_sunrast_pipe);
    REGISTER_DEMUXER (IMAGE_TIFF_PIPE,       image_tiff_pipe);
    REGISTER_DEMUXER (IMAGE_WEBP_PIPE,       image_webp_pipe);

    /* external libraries */
    REGISTER_MUXER   (CHROMAPRINT,      chromaprint);
    REGISTER_DEMUXER (LIBGME,           libgme);
    REGISTER_DEMUXER (LIBMODPLUG,       libmodplug);
    REGISTER_MUXDEMUX(LIBNUT,           libnut);
    REGISTER_DEMUXER (LIBOPENMPT,       libopenmpt);

    initialized = 1;
}
