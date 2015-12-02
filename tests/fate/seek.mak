# files from fate-acodec

FATE_SEEK_ACODEC-$(call ENCDEC, ADPCM_IMA_QT,  AIFF)    += adpcm-ima_qt \
                                                           adpcm-ima_qt-trellis
FATE_SEEK_ACODEC-$(call ENCDEC, ADPCM_IMA_WAV, WAV)     += adpcm-ima_wav \
                                                           adpcm-ima_wav-trellis
FATE_SEEK_ACODEC-$(call ENCDEC, ADPCM_MS,      WAV)     += adpcm-ms      \
                                                           adpcm-ms-trellis
FATE_SEEK_ACODEC-$(call ENCDEC, ADPCM_SWF,     FLV)     += adpcm-swf     \
                                                           adpcm-swf-trellis
FATE_SEEK_ACODEC-$(call ENCDEC, ADPCM_YAMAHA,  WAV)     += adpcm-yamaha  \
                                                           adpcm-yamaha-trellis
FATE_SEEK_ACODEC-$(call ENCDEC, ALAC,          MOV)     += alac
FATE_SEEK_ACODEC-$(call ENCDEC, FLAC,          FLAC)    += flac
FATE_SEEK_ACODEC-$(call ENCDEC, MP2,           MP2 MP3) += mp2
FATE_SEEK_ACODEC-$(call ENCDEC, PCM_ALAW,      WAV)     += pcm-alaw
FATE_SEEK_ACODEC-$(call ENCDEC, PCM_MULAW,     WAV)     += pcm-mulaw
FATE_SEEK_ACODEC-$(call ENCDEC, PCM_S8,        MOV)     += pcm-s8
FATE_SEEK_ACODEC-$(call ENCDEC, PCM_U8,        WAV)     += pcm-u8
FATE_SEEK_ACODEC-$(call ENCDEC, PCM_S16BE,     MOV)     += pcm-s16be
FATE_SEEK_ACODEC-$(call ENCDEC, PCM_S16LE,     WAV)     += pcm-s16le
FATE_SEEK_ACODEC-$(call ENCDEC, PCM_S24BE,     MOV)     += pcm-s24be
FATE_SEEK_ACODEC-$(call ENCDEC, PCM_S24LE,     WAV)     += pcm-s24le
FATE_SEEK_ACODEC-$(call ENCDEC, PCM_S32BE,     MOV)     += pcm-s32be
FATE_SEEK_ACODEC-$(call ENCDEC, PCM_S32LE,     WAV)     += pcm-s32le
FATE_SEEK_ACODEC-$(call ENCDEC, PCM_F32BE,     AU)      += pcm-f32be
FATE_SEEK_ACODEC-$(call ENCDEC, PCM_F32LE,     WAV)     += pcm-f32le
FATE_SEEK_ACODEC-$(call ENCDEC, PCM_F64BE,     AU)      += pcm-f64be
FATE_SEEK_ACODEC-$(call ENCDEC, PCM_F64LE,     WAV)     += pcm-f64le

fate-seek-acodec-adpcm-ima_qt:  SRC = fate/acodec-adpcm-ima_qt.aiff
fate-seek-acodec-adpcm-ima_wav: SRC = fate/acodec-adpcm-ima_wav.wav
fate-seek-acodec-adpcm-ms:      SRC = fate/acodec-adpcm-ms.wav
fate-seek-acodec-adpcm-swf:     SRC = fate/acodec-adpcm-swf.flv
fate-seek-acodec-adpcm-yamaha:  SRC = fate/acodec-adpcm-yamaha.wav
fate-seek-acodec-adpcm-ima_qt-trellis:  SRC = fate/acodec-adpcm-ima_qt-trellis.aiff
fate-seek-acodec-adpcm-ima_wav-trellis: SRC = fate/acodec-adpcm-ima_wav-trellis.wav
fate-seek-acodec-adpcm-ms-trellis:      SRC = fate/acodec-adpcm-ms-trellis.wav
fate-seek-acodec-adpcm-swf-trellis:     SRC = fate/acodec-adpcm-swf-trellis.flv
fate-seek-acodec-adpcm-yamaha-trellis:  SRC = fate/acodec-adpcm-yamaha-trellis.wav
fate-seek-acodec-alac:          SRC = fate/acodec-alac.mov
fate-seek-acodec-flac:          SRC = fate/acodec-flac.flac
fate-seek-acodec-mp2:           SRC = fate/acodec-mp2.mp2
fate-seek-acodec-pcm-alaw:      SRC = fate/acodec-pcm-alaw.wav
fate-seek-acodec-pcm-f32be:     SRC = fate/acodec-pcm-f32be.au
fate-seek-acodec-pcm-f32le:     SRC = fate/acodec-pcm-f32le.wav
fate-seek-acodec-pcm-f64be:     SRC = fate/acodec-pcm-f64be.au
fate-seek-acodec-pcm-f64le:     SRC = fate/acodec-pcm-f64le.wav
fate-seek-acodec-pcm-mulaw:     SRC = fate/acodec-pcm-mulaw.wav
fate-seek-acodec-pcm-s16be:     SRC = fate/acodec-pcm-s16be.mov
fate-seek-acodec-pcm-s16le:     SRC = fate/acodec-pcm-s16le.wav
fate-seek-acodec-pcm-s24be:     SRC = fate/acodec-pcm-s24be.mov
fate-seek-acodec-pcm-s24le:     SRC = fate/acodec-pcm-s24le.wav
fate-seek-acodec-pcm-s32be:     SRC = fate/acodec-pcm-s32be.mov
fate-seek-acodec-pcm-s32le:     SRC = fate/acodec-pcm-s32le.wav
fate-seek-acodec-pcm-s8:        SRC = fate/acodec-pcm-s8.mov
fate-seek-acodec-pcm-u8:        SRC = fate/acodec-pcm-u8.wav

FATE_SEEK += $(FATE_SEEK_ACODEC-yes:%=fate-seek-acodec-%)

# files from fate-vsynth_lena

FATE_SEEK_VSYNTH_LENA-$(call ENCDEC, ASV1,          AVI)     += asv1
FATE_SEEK_VSYNTH_LENA-$(call ENCDEC, ASV2,          AVI)     += asv2
FATE_SEEK_VSYNTH_LENA-$(call ENCDEC, DNXHD,         DNXHD)   += dnxhd-720p
FATE_SEEK_VSYNTH_LENA-$(call ENCDEC, DNXHD,         DNXHD)   += dnxhd-720p-rd
FATE_SEEK_VSYNTH_LENA-$(call ENCDEC, DNXHD,         MOV)     += dnxhd-1080i
FATE_SEEK_VSYNTH_LENA-$(call ENCDEC, DVVIDEO,       DV)      += dv
FATE_SEEK_VSYNTH_LENA-$(call ENCDEC, DVVIDEO,       DV)      += dv-411
FATE_SEEK_VSYNTH_LENA-$(call ENCDEC, DVVIDEO,       DV)      += dv-50
FATE_SEEK_VSYNTH_LENA-$(call ENCDEC, FFV1,          AVI)     += ffv1
FATE_SEEK_VSYNTH_LENA-$(call ENCDEC, FLASHSV,       FLV)     += flashsv
FATE_SEEK_VSYNTH_LENA-$(call ENCDEC, FLV,           FLV)     += flv
FATE_SEEK_VSYNTH_LENA-$(call ENCDEC, H261,          AVI)     += h261
FATE_SEEK_VSYNTH_LENA-$(call ENCDEC, H263,          AVI)     += h263
FATE_SEEK_VSYNTH_LENA-$(call ENCDEC, H263,          AVI)     += h263p
FATE_SEEK_VSYNTH_LENA-$(call ENCDEC, HUFFYUV,       AVI)     += huffyuv
FATE_SEEK_VSYNTH_LENA-$(call ENCDEC, JPEGLS,        AVI)     += jpegls
FATE_SEEK_VSYNTH_LENA-$(call ENCDEC, LJPEG MJPEG,   AVI)     += ljpeg
FATE_SEEK_VSYNTH_LENA-$(call ENCDEC, MJPEG,         AVI)     += mjpeg

FATE_SEEK_VSYNTH_LENA-$(call ENCDEC, MPEG1VIDEO, MPEG1VIDEO MPEGVIDEO) +=          \
                                                    mpeg1                      \
                                                    mpeg1b

FATE_SEEK_VSYNTH_LENA-$(call ENCDEC, MPEG2VIDEO, MPEG2VIDEO MPEGVIDEO) +=          \
                                                    mpeg2-422                  \
                                                    mpeg2-idct-int             \
                                                    mpeg2-ilace                \
                                                    mpeg2-ivlc-qprd            \
                                                    mpeg2-thread               \
                                                    mpeg2-thread-ivlc

FATE_SEEK_VSYNTH_LENA-$(call ENCDEC, MPEG4,         MP4 MOV) += mpeg4
FATE_SEEK_VSYNTH_LENA-$(call ENCDEC, MPEG4, AVI)             += $(FATE_MPEG4_AVI)
FATE_SEEK_VSYNTH_LENA-$(call ENCDEC, MSMPEG4V3,     AVI)     += msmpeg4
FATE_SEEK_VSYNTH_LENA-$(call ENCDEC, MSMPEG4V2,     AVI)     += msmpeg4v2
FATE_SEEK_VSYNTH_LENA-$(call ENCDEC, RAWVIDEO,      AVI)     += rgb
FATE_SEEK_VSYNTH_LENA-$(call ENCDEC, ROQ,           ROQ)     += roqvideo
FATE_SEEK_VSYNTH_LENA-$(call ENCDEC, RV10,          RM)      += rv10
FATE_SEEK_VSYNTH_LENA-$(call ENCDEC, RV20,          RM)      += rv20
FATE_SEEK_VSYNTH_LENA-$(call ENCDEC, SNOW,          AVI)     += snow
FATE_SEEK_VSYNTH_LENA-$(call ENCDEC, SNOW,          AVI)     += snow-ll
FATE_SEEK_VSYNTH_LENA-$(call ENCDEC, SVQ1,          MOV)     += svq1
FATE_SEEK_VSYNTH_LENA-$(call ENCDEC, WMV1,          AVI)     += wmv1
FATE_SEEK_VSYNTH_LENA-$(call ENCDEC, WMV2,          AVI)     += wmv2
FATE_SEEK_VSYNTH_LENA-$(call ENCDEC, RAWVIDEO,      AVI)     += yuv

fate-seek-vsynth_lena-asv1:              SRC = fate/vsynth_lena-asv1.avi
fate-seek-vsynth_lena-asv2:              SRC = fate/vsynth_lena-asv2.avi
fate-seek-vsynth_lena-dnxhd-1080i:       SRC = fate/vsynth_lena-dnxhd-1080i.mov
fate-seek-vsynth_lena-dnxhd-720p:        SRC = fate/vsynth_lena-dnxhd-720p.dnxhd
fate-seek-vsynth_lena-dnxhd-720p-rd:     SRC = fate/vsynth_lena-dnxhd-720p.dnxhd
fate-seek-vsynth_lena-dv:                SRC = fate/vsynth_lena-dv.dv
fate-seek-vsynth_lena-dv-411:            SRC = fate/vsynth_lena-dv-411.dv
fate-seek-vsynth_lena-dv-50:             SRC = fate/vsynth_lena-dv-50.dv
fate-seek-vsynth_lena-ffv1:              SRC = fate/vsynth_lena-ffv1.avi
fate-seek-vsynth_lena-flashsv:           SRC = fate/vsynth_lena-flashsv.flv
fate-seek-vsynth_lena-flv:               SRC = fate/vsynth_lena-flv.flv
fate-seek-vsynth_lena-h261:              SRC = fate/vsynth_lena-h261.avi
fate-seek-vsynth_lena-h263:              SRC = fate/vsynth_lena-h263.avi
fate-seek-vsynth_lena-h263p:             SRC = fate/vsynth_lena-h263p.avi
fate-seek-vsynth_lena-huffyuv:           SRC = fate/vsynth_lena-huffyuv.avi
fate-seek-vsynth_lena-jpegls:            SRC = fate/vsynth_lena-jpegls.avi
fate-seek-vsynth_lena-ljpeg:             SRC = fate/vsynth_lena-ljpeg.avi
fate-seek-vsynth_lena-mjpeg:             SRC = fate/vsynth_lena-mjpeg.avi
fate-seek-vsynth_lena-mpeg1:             SRC = fate/vsynth_lena-mpeg1.mpeg1video
fate-seek-vsynth_lena-mpeg1b:            SRC = fate/vsynth_lena-mpeg1b.mpeg1video
fate-seek-vsynth_lena-mpeg2-422:         SRC = fate/vsynth_lena-mpeg2-422.mpeg2video
fate-seek-vsynth_lena-mpeg2-idct-int:    SRC = fate/vsynth_lena-mpeg2-idct-int.mpeg2video
fate-seek-vsynth_lena-mpeg2-ilace:       SRC = fate/vsynth_lena-mpeg2-ilace.mpeg2video
fate-seek-vsynth_lena-mpeg2-ivlc-qprd:   SRC = fate/vsynth_lena-mpeg2-ivlc-qprd.mpeg2video
fate-seek-vsynth_lena-mpeg2-thread:      SRC = fate/vsynth_lena-mpeg2-thread.mpeg2video
fate-seek-vsynth_lena-mpeg2-thread-ivlc: SRC = fate/vsynth_lena-mpeg2-thread-ivlc.mpeg2video
fate-seek-vsynth_lena-mpeg4:             SRC = fate/vsynth_lena-mpeg4.mp4
fate-seek-vsynth_lena-mpeg4-adap:        SRC = fate/vsynth_lena-mpeg4-adap.avi
fate-seek-vsynth_lena-mpeg4-adv:         SRC = fate/vsynth_lena-mpeg4-adv.avi
fate-seek-vsynth_lena-mpeg4-error:       SRC = fate/vsynth_lena-mpeg4-error.avi
fate-seek-vsynth_lena-mpeg4-nr:          SRC = fate/vsynth_lena-mpeg4-nr.avi
fate-seek-vsynth_lena-mpeg4-nsse:        SRC = fate/vsynth_lena-mpeg4-nsse.avi
fate-seek-vsynth_lena-mpeg4-qpel:        SRC = fate/vsynth_lena-mpeg4-qpel.avi
fate-seek-vsynth_lena-mpeg4-qprd:        SRC = fate/vsynth_lena-mpeg4-qprd.avi
fate-seek-vsynth_lena-mpeg4-rc:          SRC = fate/vsynth_lena-mpeg4-rc.avi
fate-seek-vsynth_lena-mpeg4-thread:      SRC = fate/vsynth_lena-mpeg4-thread.avi
fate-seek-vsynth_lena-msmpeg4:           SRC = fate/vsynth_lena-msmpeg4.avi
fate-seek-vsynth_lena-msmpeg4v2:         SRC = fate/vsynth_lena-msmpeg4v2.avi
fate-seek-vsynth_lena-rgb:               SRC = fate/vsynth_lena-rgb.avi
fate-seek-vsynth_lena-roqvideo:          SRC = fate/vsynth_lena-roqvideo.roq
fate-seek-vsynth_lena-rv10:              SRC = fate/vsynth_lena-rv10.rm
fate-seek-vsynth_lena-rv20:              SRC = fate/vsynth_lena-rv20.rm
fate-seek-vsynth_lena-snow:              SRC = fate/vsynth_lena-snow.avi
fate-seek-vsynth_lena-snow-ll:           SRC = fate/vsynth_lena-snow-ll.avi
fate-seek-vsynth_lena-svq1:              SRC = fate/vsynth_lena-svq1.mov
fate-seek-vsynth_lena-wmv1:              SRC = fate/vsynth_lena-wmv1.avi
fate-seek-vsynth_lena-wmv2:              SRC = fate/vsynth_lena-wmv2.avi
fate-seek-vsynth_lena-yuv:               SRC = fate/vsynth_lena-yuv.avi

FATE_SAMPLES_SEEK += $(FATE_SEEK_VSYNTH_LENA-yes:%=fate-seek-vsynth_lena-%)

# files from fate-lavf

FATE_SEEK_LAVF-$(call ENCDEC,  PCM_S16BE,             AIFF)        += aiff
FATE_SEEK_LAVF-$(call ENCDEC,  PCM_ALAW,              PCM_ALAW)    += alaw
FATE_SEEK_LAVF-$(call ENCDEC2, MSMPEG4V3,  MP2,       ASF)         += asf
FATE_SEEK_LAVF-$(call ENCDEC,  PCM_S16BE,             AU)          += au
FATE_SEEK_LAVF-$(call ENCDEC2, MPEG4,      MP2,       AVI)         += avi
FATE_SEEK_LAVF-$(call ENCDEC,  BMP,                   IMAGE2)      += bmp
FATE_SEEK_LAVF-$(call ENCDEC2, DVVIDEO,    PCM_S16LE, AVI)         += dv_fmt
FATE_SEEK_LAVF-$(call ENCDEC2, MPEG1VIDEO, MP2,       FFM)         += ffm
FATE_SEEK_LAVF-$(call ENCDEC,  FLV,                   FLV)         += flv_fmt
FATE_SEEK_LAVF-$(call ENCDEC,  GIF,                   IMAGE2)      += gif
FATE_SEEK_LAVF-$(call ENCDEC2, MPEG2VIDEO, PCM_S16LE, GXF)         += gxf
FATE_SEEK_LAVF-$(call ENCDEC,  MJPEG,                 IMAGE2)      += jpg
FATE_SEEK_LAVF-$(call ENCDEC2, MPEG4,      MP2,       MATROSKA)    += mkv
FATE_SEEK_LAVF-$(call ENCDEC,  ADPCM_YAMAHA,          MMF)         += mmf
FATE_SEEK_LAVF-$(call ENCDEC2, MPEG4,      PCM_ALAW,  MOV)         += mov
FATE_SEEK_LAVF-$(call ENCDEC2, MPEG1VIDEO, MP2,       MPEG1SYSTEM MPEGPS) += mpg
FATE_SEEK_LAVF-$(call ENCDEC,  PCM_MULAW,             PCM_MULAW)   += mulaw
FATE_SEEK_LAVF-$(call ENCDEC2, MPEG2VIDEO, PCM_S16LE, MXF)         += mxf
FATE_SEEK_LAVF-$(call ENCDEC2, MPEG2VIDEO, PCM_S16LE, MXF_D10 MXF) += mxf_d10
FATE_SEEK_LAVF-$(call ENCDEC2, DNXHD,      PCM_S16LE, MXF_OPATOM MXF) += mxf_opatom
FATE_SEEK_LAVF-$(call ENCDEC2, DNXHD,      PCM_S16LE, MXF_OPATOM MXF) += mxf_opatom_audio
FATE_SEEK_LAVF-$(call ENCDEC2, MPEG4,      MP2,       NUT)         += nut
FATE_SEEK_LAVF-$(call ENCDEC,  FLAC,                  OGG)         += ogg
FATE_SEEK_LAVF-$(call ENCDEC,  PBM,                   IMAGE2PIPE)  += pbmpipe
FATE_SEEK_LAVF-$(call ENCDEC,  PCX,                   IMAGE2)      += pcx
FATE_SEEK_LAVF-$(call ENCDEC,  PGM,                   IMAGE2)      += pgm
FATE_SEEK_LAVF-$(call ENCDEC,  PGM,                   IMAGE2PIPE)  += pgmpipe
FATE_SEEK_LAVF-$(call ENCDEC,  PPM,                   IMAGE2)      += ppm
FATE_SEEK_LAVF-$(call ENCDEC,  PPM,                   IMAGE2PIPE)  += ppmpipe
FATE_SEEK_LAVF-$(call ENCMUX,  RV10 AC3_FIXED,        RM)          += rm
FATE_SEEK_LAVF-$(call ENCDEC,  SGI,                   IMAGE2)      += sgi
FATE_SEEK_LAVF-$(call ENCDEC,  FLV,                   SWF)         += swf
FATE_SEEK_LAVF-$(call ENCDEC,  TARGA,                 IMAGE2)      += tga
FATE_SEEK_LAVF-$(call ENCDEC,  TIFF,                  IMAGE2)      += tiff
FATE_SEEK_LAVF-$(call ENCDEC2, MPEG2VIDEO, MP2,       MPEGTS)      += ts
FATE_SEEK_LAVF-$(call ENCDEC,  PCM_U8,                VOC)         += voc
FATE_SEEK_LAVF-$(call ENCDEC,  PCM_S16LE,             WAV)         += wav
FATE_SEEK_LAVF-$(call ENCDEC,  MP2,                   WTV)         += wtv
FATE_SEEK_LAVF-$(CONFIG_YUV4MPEGPIPE_MUXER)                        += yuv4mpeg

fate-seek-lavf-aiff:     SRC = lavf/lavf.aif
fate-seek-lavf-alaw:     SRC = lavf/lavf.al
fate-seek-lavf-asf:      SRC = lavf/lavf.asf
fate-seek-lavf-au:       SRC = lavf/lavf.au
fate-seek-lavf-avi:      SRC = lavf/lavf.avi
fate-seek-lavf-bmp:      SRC = images/bmp/%02d.bmp
fate-seek-lavf-dv_fmt:   SRC = lavf/lavf.dv
fate-seek-lavf-ffm:      SRC = lavf/lavf.ffm
fate-seek-lavf-flv_fmt:  SRC = lavf/lavf.flv
fate-seek-lavf-gif:      SRC = lavf/lavf.gif
fate-seek-lavf-gxf:      SRC = lavf/lavf.gxf
fate-seek-lavf-jpg:      SRC = images/jpg/%02d.jpg
fate-seek-lavf-mkv:      SRC = lavf/lavf.mkv
fate-seek-lavf-mmf:      SRC = lavf/lavf.mmf
fate-seek-lavf-mov:      SRC = lavf/lavf.mov
fate-seek-lavf-mpg:      SRC = lavf/lavf.mpg
fate-seek-lavf-mulaw:    SRC = lavf/lavf.ul
fate-seek-lavf-mxf:      SRC = lavf/lavf.mxf
fate-seek-lavf-mxf_d10:  SRC = lavf/lavf.mxf_d10
fate-seek-lavf-mxf_opatom: SRC = lavf/lavf.mxf_opatom
fate-seek-lavf-mxf_opatom_audio: SRC = lavf/lavf.mxf_opatom_audio
fate-seek-lavf-nut:      SRC = lavf/lavf.nut
fate-seek-lavf-ogg:      SRC = lavf/lavf.ogg
fate-seek-lavf-pbmpipe:  SRC = lavf/pbmpipe.pbm
fate-seek-lavf-pcx:      SRC = images/pcx/%02d.pcx
fate-seek-lavf-pgm:      SRC = images/pgm/%02d.pgm
fate-seek-lavf-pgmpipe:  SRC = lavf/pgmpipe.pgm
fate-seek-lavf-ppm:      SRC = images/ppm/%02d.ppm
fate-seek-lavf-ppmpipe:  SRC = lavf/ppmpipe.ppm
fate-seek-lavf-rm:       SRC = lavf/lavf.rm
fate-seek-lavf-sgi:      SRC = images/sgi/%02d.sgi
fate-seek-lavf-swf:      SRC = lavf/lavf.swf
fate-seek-lavf-tga:      SRC = images/tga/%02d.tga
fate-seek-lavf-tiff:     SRC = images/tiff/%02d.tiff
fate-seek-lavf-ts:       SRC = lavf/lavf.ts
fate-seek-lavf-voc:      SRC = lavf/lavf.voc
fate-seek-lavf-wav:      SRC = lavf/lavf.wav
fate-seek-lavf-wtv:      SRC = lavf/lavf.wtv
fate-seek-lavf-yuv4mpeg: SRC = lavf/lavf.y4m

FATE_SEEK += $(FATE_SEEK_LAVF-yes:%=fate-seek-lavf-%)

# extra files

FATE_SEEK_EXTRA-$(CONFIG_MP3_DEMUXER)   += fate-seek-extra-mp3
fate-seek-extra-mp3:  CMD = run libavformat/seek-test$(EXESUF) $(TARGET_SAMPLES)/gapless/gapless.mp3 -fastseek 1
FATE_SEEK_EXTRA += $(FATE_SEEK_EXTRA-yes)


$(FATE_SEEK) $(FATE_SAMPLES_SEEK) $(FATE_SEEK_EXTRA): libavformat/seek-test$(EXESUF)
$(FATE_SEEK) $(FATE_SAMPLES_SEEK): CMD = run libavformat/seek-test$(EXESUF) $(TARGET_PATH)/tests/data/$(SRC)
$(FATE_SEEK) $(FATE_SAMPLES_SEEK): fate-seek-%: fate-%
fate-seek-%: REF = $(SRC_PATH)/tests/ref/seek/$(@:fate-seek-%=%)

FATE_AVCONV += $(FATE_SEEK)
FATE_SAMPLES_AVCONV += $(FATE_SAMPLES_SEEK) $(FATE_SEEK_EXTRA)
fate-seek:     $(FATE_SEEK) $(FATE_SAMPLES_SEEK) $(FATE_SEEK_EXTRA)
