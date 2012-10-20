# files from fate-acodec

FATE_SEEK-$(call ENCDEC, PCM_ALAW,      WAV)     += pcm_alaw_wav
FATE_SEEK-$(call ENCDEC, PCM_MULAW,     WAV)     += pcm_mulaw_wav
FATE_SEEK-$(call ENCDEC, PCM_S8,        MOV)     += pcm_s8_mov
FATE_SEEK-$(call ENCDEC, PCM_U8,        WAV)     += pcm_u8_wav
FATE_SEEK-$(call ENCDEC, PCM_S16BE,     MOV)     += pcm_s16be_mov
FATE_SEEK-$(call ENCDEC, PCM_S16LE,     WAV)     += pcm_s16le_wav
FATE_SEEK-$(call ENCDEC, PCM_S24BE,     MOV)     += pcm_s24be_mov
FATE_SEEK-$(call ENCDEC, PCM_S24LE,     WAV)     += pcm_s24le_wav
FATE_SEEK-$(call ENCDEC, PCM_S32BE,     MOV)     += pcm_s32be_mov
FATE_SEEK-$(call ENCDEC, PCM_S32LE,     WAV)     += pcm_s32le_wav
FATE_SEEK-$(call ENCDEC, PCM_F32BE,     AU)      += pcm_f32be_au
FATE_SEEK-$(call ENCDEC, PCM_F32LE,     WAV)     += pcm_f32le_wav
FATE_SEEK-$(call ENCDEC, PCM_F64BE,     AU)      += pcm_f64be_au
FATE_SEEK-$(call ENCDEC, PCM_F64LE,     WAV)     += pcm_f64le_wav
FATE_SEEK-$(call ENCDEC, ADPCM_IMA_QT,  AIFF)    += adpcm_ima_qt_aiff
FATE_SEEK-$(call ENCDEC, ADPCM_IMA_WAV, WAV)     += adpcm_ima_wav_wav
FATE_SEEK-$(call ENCDEC, ADPCM_MS,      WAV)     += adpcm_ms_wav
FATE_SEEK-$(call ENCDEC, ADPCM_SWF,     FLV)     += adpcm_swf_flv
FATE_SEEK-$(call ENCDEC, ADPCM_YAMAHA,  WAV)     += adpcm_yamaha_wav
FATE_SEEK-$(call ENCDEC, ALAC,          MOV)     += alac_mov
FATE_SEEK-$(call ENCDEC, FLAC,          FLAC)    += flac_flac
FATE_SEEK-$(call ENCDEC, MP2,           MP2 MP3) += mp2_mp2

# files from fate-vsynth2

FATE_SEEK-$(call ENCDEC, ASV1,          AVI)     += asv1_avi
FATE_SEEK-$(call ENCDEC, ASV2,          AVI)     += asv2_avi
FATE_SEEK-$(call ENCDEC, DNXHD,         DNXHD)   += dnxhd_720p_dnxhd
FATE_SEEK-$(call ENCDEC, DNXHD,         DNXHD)   += dnxhd_720p_rd_dnxhd
FATE_SEEK-$(call ENCDEC, DNXHD,         MOV)     += dnxhd_1080i_mov
FATE_SEEK-$(call ENCDEC, DVVIDEO,       DV)      += dv_dv
FATE_SEEK-$(call ENCDEC, DVVIDEO,       DV)      += dv_411_dv
FATE_SEEK-$(call ENCDEC, DVVIDEO,       DV)      += dv_50_dv
FATE_SEEK-$(call ENCDEC, FFV1,          AVI)     += ffv1_avi
FATE_SEEK-$(call ENCDEC, FLASHSV,       FLV)     += flashsv_flv
FATE_SEEK-$(call ENCDEC, FLV,           FLV)     += flv_flv
FATE_SEEK-$(call ENCDEC, H261,          AVI)     += h261_avi
FATE_SEEK-$(call ENCDEC, H263,          AVI)     += h263_avi
FATE_SEEK-$(call ENCDEC, H263,          AVI)     += h263p_avi
FATE_SEEK-$(call ENCDEC, HUFFYUV,       AVI)     += huffyuv_avi
FATE_SEEK-$(call ENCDEC, JPEGLS,        AVI)     += jpegls_avi
FATE_SEEK-$(call ENCDEC, LJPEG MJPEG,   AVI)     += ljpeg_avi
FATE_SEEK-$(call ENCDEC, MJPEG,         AVI)     += mjpeg_avi

FATE_SEEK-$(call ENCDEC, MPEG1VIDEO, MPEG1VIDEO MPEGVIDEO) +=           \
                                                    mpeg1_mpeg1video    \
                                                    mpeg1b_mpeg1video

FATE_SEEK-$(call ENCDEC, MPEG2VIDEO, MPEG2VIDEO MPEGVIDEO) +=                  \
                                                    mpeg2_422_mpeg2video       \
                                                    mpeg2_idct_int_mpeg2video  \
                                                    mpeg2_ilace_mpeg2video     \
                                                    mpeg2_ivlc_qprd_mpeg2video \
                                                    mpeg2_thread_mpeg2video    \
                                                    mpeg2_thread_ivlc_mpeg2video

FATE_SEEK-$(call ENCDEC, MPEG4,         MP4 MOV) += mpeg4_mp4
FATE_SEEK-$(call ENCDEC, MPEG4, AVI) += $(subst -,_,$(FATE_MPEG4_AVI:%=%_avi))
FATE_SEEK-$(call ENCDEC, MSMPEG4V3,     AVI)     += msmpeg4_avi
FATE_SEEK-$(call ENCDEC, MSMPEG4V2,     AVI)     += msmpeg4v2_avi
FATE_SEEK-$(call ENCDEC, RAWVIDEO,      AVI)     += rgb_avi
FATE_SEEK-$(call ENCDEC, ROQ,           ROQ)     += roqvideo_roq
FATE_SEEK-$(call ENCDEC, RV10,          RM)      += rv10_rm
FATE_SEEK-$(call ENCDEC, RV20,          RM)      += rv20_rm
FATE_SEEK-$(call ENCDEC, SNOW,          AVI)     += snow_avi
FATE_SEEK-$(call ENCDEC, SNOW,          AVI)     += snow_ll_avi
FATE_SEEK-$(call ENCDEC, SVQ1,          MOV)     += svq1_mov
FATE_SEEK-$(call ENCDEC, WMV1,          AVI)     += wmv1_avi
FATE_SEEK-$(call ENCDEC, WMV2,          AVI)     += wmv2_avi
FATE_SEEK-$(call ENCDEC, RAWVIDEO,      AVI)     += yuv_avi

# files from fate-lavf

FATE_SEEK-$(call ENCDEC,  PCM_S16BE,             AIFF)        += lavf_aif
FATE_SEEK-$(call ENCDEC,  PCM_ALAW,              PCM_ALAW)    += lavf_al
FATE_SEEK-$(call ENCDEC2, MSMPEG4V3,  MP2,       ASF)         += lavf_asf
FATE_SEEK-$(call ENCDEC,  PCM_S16BE,             AU)          += lavf_au
FATE_SEEK-$(call ENCDEC2, MPEG4,      MP2,       AVI)         += lavf_avi
FATE_SEEK-$(call ENCDEC,  BMP,                   IMAGE2)      += image_bmp
FATE_SEEK-$(call ENCDEC2, DVVIDEO,    PCM_S16LE, AVI)         += lavf_dv
FATE_SEEK-$(call ENCDEC2, MPEG1VIDEO, MP2,       FFM)         += lavf_ffm
FATE_SEEK-$(call ENCDEC,  FLV,                   FLV)         += lavf_flv
FATE_SEEK-$(call ENCDEC,  GIF,                   IMAGE2)      += lavf_gif
FATE_SEEK-$(call ENCDEC2, MPEG2VIDEO, PCM_S16LE, GXF)         += lavf_gxf
FATE_SEEK-$(call ENCDEC,  MJPEG,                 IMAGE2)      += image_jpg
FATE_SEEK-$(call ENCDEC2, MPEG4,      MP2,       MATROSKA)    += lavf_mkv
FATE_SEEK-$(call ENCDEC,  ADPCM_YAMAHA,          MMF)         += lavf_mmf
FATE_SEEK-$(call ENCDEC2, MPEG4,      PCM_ALAW,  MOV)         += lavf_mov
FATE_SEEK-$(call ENCDEC2, MPEG1VIDEO, MP2,       MPEG1SYSTEM MPEGPS) += lavf_mpg
FATE_SEEK-$(call ENCDEC,  PCM_MULAW,             PCM_MULAW)   += lavf_ul
FATE_SEEK-$(call ENCDEC2, MPEG2VIDEO, PCM_S16LE, MXF)         += lavf_mxf
FATE_SEEK-$(call ENCDEC2, MPEG2VIDEO, PCM_S16LE, MXF_D10 MXF) += lavf_mxf_d10
FATE_SEEK-$(call ENCDEC2, MPEG4,      MP2,       NUT)         += lavf_nut
FATE_SEEK-$(call ENCDEC,  FLAC,                  OGG)         += lavf_ogg
FATE_SEEK-$(call ENCDEC,  PBM,                   IMAGE2PIPE)  += pbmpipe_pbm
FATE_SEEK-$(call ENCDEC,  PCX,                   IMAGE2)      += image_pcx
FATE_SEEK-$(call ENCDEC,  PGM,                   IMAGE2)      += image_pgm
FATE_SEEK-$(call ENCDEC,  PGM,                   IMAGE2PIPE)  += pgmpipe_pgm
FATE_SEEK-$(call ENCDEC,  PPM,                   IMAGE2)      += image_ppm
FATE_SEEK-$(call ENCDEC,  PPM,                   IMAGE2PIPE)  += ppmpipe_ppm
FATE_SEEK-$(call ENCMUX,  RV10 AC3_FIXED,        RM)          += lavf_rm
FATE_SEEK-$(call ENCDEC,  SGI,                   IMAGE2)      += image_sgi
FATE_SEEK-$(call ENCDEC,  FLV,                   SWF)         += lavf_swf
FATE_SEEK-$(call ENCDEC,  TARGA,                 IMAGE2)      += image_tga
FATE_SEEK-$(call ENCDEC,  TIFF,                  IMAGE2)      += image_tiff
FATE_SEEK-$(call ENCDEC2, MPEG2VIDEO, MP2,       MPEGTS)      += lavf_ts
FATE_SEEK-$(call ENCDEC,  PCM_U8,                VOC)         += lavf_voc
FATE_SEEK-$(call ENCDEC,  PCM_S16LE,             WAV)         += lavf_wav
FATE_SEEK-$(CONFIG_YUV4MPEGPIPE_MUXER)                        += lavf_y4m

FATE_SEEK += $(FATE_SEEK-yes:%=fate-seek-%)

$(FATE_SEEK): fate-acodec fate-vsynth2 fate-lavf libavformat/seek-test$(EXESUF)
$(FATE_SEEK): CMD = seektest

FATE_AVCONV += $(FATE_SEEK)
fate-seek:     $(FATE_SEEK)
