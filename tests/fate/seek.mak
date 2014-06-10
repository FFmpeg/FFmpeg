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

# files from fate-vsynth2

FATE_SEEK_VSYNTH2-$(call ENCDEC, ASV1,          AVI)     += asv1
FATE_SEEK_VSYNTH2-$(call ENCDEC, ASV2,          AVI)     += asv2
FATE_SEEK_VSYNTH2-$(call ENCDEC, DNXHD,         DNXHD)   += dnxhd-720p
FATE_SEEK_VSYNTH2-$(call ENCDEC, DNXHD,         DNXHD)   += dnxhd-720p-rd
FATE_SEEK_VSYNTH2-$(call ENCDEC, DNXHD,         MOV)     += dnxhd-1080i
FATE_SEEK_VSYNTH2-$(call ENCDEC, DVVIDEO,       DV)      += dv
FATE_SEEK_VSYNTH2-$(call ENCDEC, DVVIDEO,       DV)      += dv-411
FATE_SEEK_VSYNTH2-$(call ENCDEC, DVVIDEO,       DV)      += dv-50
FATE_SEEK_VSYNTH2-$(call ENCDEC, FFV1,          AVI)     += ffv1
FATE_SEEK_VSYNTH2-$(call ENCDEC, FLASHSV,       FLV)     += flashsv
FATE_SEEK_VSYNTH2-$(call ENCDEC, FLV,           FLV)     += flv
FATE_SEEK_VSYNTH2-$(call ENCDEC, H261,          AVI)     += h261
FATE_SEEK_VSYNTH2-$(call ENCDEC, H263,          AVI)     += h263
FATE_SEEK_VSYNTH2-$(call ENCDEC, H263,          AVI)     += h263p
FATE_SEEK_VSYNTH2-$(call ENCDEC, HUFFYUV,       AVI)     += huffyuv
FATE_SEEK_VSYNTH2-$(call ENCDEC, JPEGLS,        AVI)     += jpegls
FATE_SEEK_VSYNTH2-$(call ENCDEC, LJPEG MJPEG,   AVI)     += ljpeg
FATE_SEEK_VSYNTH2-$(call ENCDEC, MJPEG,         AVI)     += mjpeg

FATE_SEEK_VSYNTH2-$(call ENCDEC, MPEG1VIDEO, MPEG1VIDEO MPEGVIDEO) +=          \
                                                    mpeg1                      \
                                                    mpeg1b

FATE_SEEK_VSYNTH2-$(call ENCDEC, MPEG2VIDEO, MPEG2VIDEO MPEGVIDEO) +=          \
                                                    mpeg2-422                  \
                                                    mpeg2-idct-int             \
                                                    mpeg2-ilace                \
                                                    mpeg2-ivlc-qprd            \
                                                    mpeg2-thread               \
                                                    mpeg2-thread-ivlc

FATE_SEEK_VSYNTH2-$(call ENCDEC, MPEG4,         MP4 MOV) += mpeg4
FATE_SEEK_VSYNTH2-$(call ENCDEC, MPEG4, AVI)             += $(FATE_MPEG4_AVI)
FATE_SEEK_VSYNTH2-$(call ENCDEC, MSMPEG4V3,     AVI)     += msmpeg4
FATE_SEEK_VSYNTH2-$(call ENCDEC, MSMPEG4V2,     AVI)     += msmpeg4v2
FATE_SEEK_VSYNTH2-$(call ENCDEC, RAWVIDEO,      AVI)     += rgb
FATE_SEEK_VSYNTH2-$(call ENCDEC, ROQ,           ROQ)     += roqvideo
FATE_SEEK_VSYNTH2-$(call ENCDEC, RV10,          RM)      += rv10
FATE_SEEK_VSYNTH2-$(call ENCDEC, RV20,          RM)      += rv20
FATE_SEEK_VSYNTH2-$(call ENCDEC, SNOW,          AVI)     += snow
FATE_SEEK_VSYNTH2-$(call ENCDEC, SNOW,          AVI)     += snow-ll
FATE_SEEK_VSYNTH2-$(call ENCDEC, SVQ1,          MOV)     += svq1
FATE_SEEK_VSYNTH2-$(call ENCDEC, WMV1,          AVI)     += wmv1
FATE_SEEK_VSYNTH2-$(call ENCDEC, WMV2,          AVI)     += wmv2
FATE_SEEK_VSYNTH2-$(call ENCDEC, RAWVIDEO,      AVI)     += yuv

fate-seek-vsynth2-asv1:              SRC = fate/vsynth2-asv1.avi
fate-seek-vsynth2-asv2:              SRC = fate/vsynth2-asv2.avi
fate-seek-vsynth2-dnxhd-1080i:       SRC = fate/vsynth2-dnxhd-1080i.mov
fate-seek-vsynth2-dnxhd-720p:        SRC = fate/vsynth2-dnxhd-720p.dnxhd
fate-seek-vsynth2-dnxhd-720p-rd:     SRC = fate/vsynth2-dnxhd-720p.dnxhd
fate-seek-vsynth2-dv:                SRC = fate/vsynth2-dv.dv
fate-seek-vsynth2-dv-411:            SRC = fate/vsynth2-dv-411.dv
fate-seek-vsynth2-dv-50:             SRC = fate/vsynth2-dv-50.dv
fate-seek-vsynth2-ffv1:              SRC = fate/vsynth2-ffv1.avi
fate-seek-vsynth2-flashsv:           SRC = fate/vsynth2-flashsv.flv
fate-seek-vsynth2-flv:               SRC = fate/vsynth2-flv.flv
fate-seek-vsynth2-h261:              SRC = fate/vsynth2-h261.avi
fate-seek-vsynth2-h263:              SRC = fate/vsynth2-h263.avi
fate-seek-vsynth2-h263p:             SRC = fate/vsynth2-h263p.avi
fate-seek-vsynth2-huffyuv:           SRC = fate/vsynth2-huffyuv.avi
fate-seek-vsynth2-jpegls:            SRC = fate/vsynth2-jpegls.avi
fate-seek-vsynth2-ljpeg:             SRC = fate/vsynth2-ljpeg.avi
fate-seek-vsynth2-mjpeg:             SRC = fate/vsynth2-mjpeg.avi
fate-seek-vsynth2-mpeg1:             SRC = fate/vsynth2-mpeg1.mpeg1video
fate-seek-vsynth2-mpeg1b:            SRC = fate/vsynth2-mpeg1b.mpeg1video
fate-seek-vsynth2-mpeg2-422:         SRC = fate/vsynth2-mpeg2-422.mpeg2video
fate-seek-vsynth2-mpeg2-idct-int:    SRC = fate/vsynth2-mpeg2-idct-int.mpeg2video
fate-seek-vsynth2-mpeg2-ilace:       SRC = fate/vsynth2-mpeg2-ilace.mpeg2video
fate-seek-vsynth2-mpeg2-ivlc-qprd:   SRC = fate/vsynth2-mpeg2-ivlc-qprd.mpeg2video
fate-seek-vsynth2-mpeg2-thread:      SRC = fate/vsynth2-mpeg2-thread.mpeg2video
fate-seek-vsynth2-mpeg2-thread-ivlc: SRC = fate/vsynth2-mpeg2-thread-ivlc.mpeg2video
fate-seek-vsynth2-mpeg4:             SRC = fate/vsynth2-mpeg4.mp4
fate-seek-vsynth2-mpeg4-adap:        SRC = fate/vsynth2-mpeg4-adap.avi
fate-seek-vsynth2-mpeg4-adv:         SRC = fate/vsynth2-mpeg4-adv.avi
fate-seek-vsynth2-mpeg4-error:       SRC = fate/vsynth2-mpeg4-error.avi
fate-seek-vsynth2-mpeg4-nr:          SRC = fate/vsynth2-mpeg4-nr.avi
fate-seek-vsynth2-mpeg4-nsse:        SRC = fate/vsynth2-mpeg4-nsse.avi
fate-seek-vsynth2-mpeg4-qpel:        SRC = fate/vsynth2-mpeg4-qpel.avi
fate-seek-vsynth2-mpeg4-qprd:        SRC = fate/vsynth2-mpeg4-qprd.avi
fate-seek-vsynth2-mpeg4-rc:          SRC = fate/vsynth2-mpeg4-rc.avi
fate-seek-vsynth2-mpeg4-thread:      SRC = fate/vsynth2-mpeg4-thread.avi
fate-seek-vsynth2-msmpeg4:           SRC = fate/vsynth2-msmpeg4.avi
fate-seek-vsynth2-msmpeg4v2:         SRC = fate/vsynth2-msmpeg4v2.avi
fate-seek-vsynth2-rgb:               SRC = fate/vsynth2-rgb.avi
fate-seek-vsynth2-roqvideo:          SRC = fate/vsynth2-roqvideo.roq
fate-seek-vsynth2-rv10:              SRC = fate/vsynth2-rv10.rm
fate-seek-vsynth2-rv20:              SRC = fate/vsynth2-rv20.rm
fate-seek-vsynth2-snow:              SRC = fate/vsynth2-snow.avi
fate-seek-vsynth2-snow-ll:           SRC = fate/vsynth2-snow-ll.avi
fate-seek-vsynth2-svq1:              SRC = fate/vsynth2-svq1.mov
fate-seek-vsynth2-wmv1:              SRC = fate/vsynth2-wmv1.avi
fate-seek-vsynth2-wmv2:              SRC = fate/vsynth2-wmv2.avi
fate-seek-vsynth2-yuv:               SRC = fate/vsynth2-yuv.avi

FATE_SEEK += $(FATE_SEEK_VSYNTH2-yes:%=fate-seek-vsynth2-%)

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

$(FATE_SEEK): libavformat/seek-test$(EXESUF)
$(FATE_SEEK): CMD = run libavformat/seek-test$(EXESUF) $(TARGET_PATH)/tests/data/$(SRC)
$(FATE_SEEK): fate-seek-%: fate-%
fate-seek-%: REF = $(SRC_PATH)/tests/ref/seek/$(@:fate-seek-%=%)

FATE_AVCONV += $(FATE_SEEK)
fate-seek:     $(FATE_SEEK)
