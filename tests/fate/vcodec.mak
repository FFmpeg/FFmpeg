fate-vsynth1-%: SRC = tests/data/vsynth1.yuv
fate-vsynth2-%: SRC = tests/data/vsynth2.yuv
fate-vsynth%: CODEC = $(word 3, $(subst -, ,$(@)))
fate-vsynth%: FMT = avi
fate-vsynth%: CMD = enc_dec "rawvideo -s 352x288 -pix_fmt yuv420p $(RAWDECOPTS)" $(SRC) $(FMT) "-c $(CODEC) $(ENCOPTS)" rawvideo "-s 352x288 -pix_fmt yuv420p -vsync 0 $(DECOPTS)" -keep "$(DECINOPTS)"
fate-vsynth%: CMP_UNIT = 1
fate-vsynth%: REF = $(SRC_PATH)/tests/ref/vsynth/$(@:fate-%=%)

FATE_VCODEC-$(call ENCDEC, AMV, AVI) += amv

FATE_VCODEC-$(call ENCDEC, ASV1, AVI)   += asv1
fate-vsynth%-asv1:               ENCOPTS = -qscale 10

FATE_VCODEC-$(call ENCDEC, ASV2, AVI)   += asv2
fate-vsynth%-asv2:               ENCOPTS = -qscale 10

FATE_VCODEC-$(call ENCDEC, CLJR, AVI)   += cljr

FATE_VCODEC-$(call ENCDEC, DNXHD, DNXHD) += dnxhd-720p                  \
                                            dnxhd-720p-rd               \
                                            dnxhd-720p-10bit

fate-vsynth%-dnxhd-720p:         ENCOPTS = -s hd720 -b 90M              \
                                           -pix_fmt yuv422p -frames 5 -qmax 8
fate-vsynth%-dnxhd-720p:         FMT     = dnxhd

fate-vsynth%-dnxhd-720p-rd:      ENCOPTS = -s hd720 -b 90M -threads 4 -mbd rd \
                                           -pix_fmt yuv422p -frames 5 -qmax 8
fate-vsynth%-dnxhd-720p-rd:      FMT     = dnxhd

fate-vsynth%-dnxhd-720p-10bit:   ENCOPTS = -s hd720 -b 90M              \
                                           -pix_fmt yuv422p10 -frames 5 -qmax 8
fate-vsynth%-dnxhd-720p-10bit:   FMT     = dnxhd

FATE_VCODEC-$(call ENCDEC, DNXHD, MOV)  += dnxhd-1080i
fate-vsynth%-dnxhd-1080i:        ENCOPTS = -s hd1080 -b 120M -flags +ildct \
                                           -pix_fmt yuv422p -frames 5 -qmax 8
fate-vsynth%-dnxhd-1080i:        FMT     = mov

FATE_VCODEC-$(call ENCDEC, DVVIDEO, DV) += dv dv-411 dv-50
fate-vsynth%-dv:                 CODEC   = dvvideo
fate-vsynth%-dv:                 ENCOPTS = -dct int -s pal
fate-vsynth%-dv:                 FMT     = dv

fate-vsynth%-dv-411:             CODEC   = dvvideo
fate-vsynth%-dv-411:             ENCOPTS = -dct int -s pal -pix_fmt yuv411p \
                                           -sws_flags area
fate-vsynth%-dv-411:             DECOPTS = -sws_flags area
fate-vsynth%-dv-411:             FMT     = dv

fate-vsynth%-dv-50:              CODEC   = dvvideo
fate-vsynth%-dv-50:              ENCOPTS = -dct int -s pal -pix_fmt yuv422p \
                                           -sws_flags neighbor
fate-vsynth%-dv-50:              DECOPTS = -sws_flags neighbor
fate-vsynth%-dv-50:              FMT     = dv

FATE_VCODEC-$(call ENCDEC, FFV1, AVI)   += ffv1
fate-vsynth%-ffv1:               ENCOPTS = -slices 4 -strict -2

FATE_VCODEC-$(call ENCDEC, FFVHUFF, AVI) += ffvhuff

FATE_VCODEC-$(call ENCDEC, FLASHSV, FLV) += flashsv
fate-vsynth%-flashsv:            ENCOPTS = -sws_flags neighbor+full_chroma_int
fate-vsynth%-flashsv:            DECOPTS = -sws_flags area
fate-vsynth%-flashsv:            FMT     = flv

FATE_VCODEC-$(call ENCDEC, FLASHSV2, FLV) += flashsv2
fate-vsynth%-flashsv2:           ENCOPTS = -sws_flags neighbor+full_chroma_int -strict experimental -compression_level 0
fate-vsynth%-flashsv2:           DECOPTS = -sws_flags area
fate-vsynth%-flashsv2:           FMT     = flv

FATE_VCODEC-$(call ENCDEC, FLV, FLV)    += flv
fate-vsynth%-flv:                ENCOPTS = -qscale 10
fate-vsynth%-flv:                FMT     = flv

FATE_VCODEC-$(call ENCDEC, H261, AVI)   += h261
fate-vsynth%-h261:               ENCOPTS = -qscale 11

FATE_VCODEC-$(call ENCDEC, H263, AVI)   += h263 h263-obmc h263p
fate-vsynth%-h263:               ENCOPTS = -qscale 10
fate-vsynth%-h263-obmc:          ENCOPTS = -qscale 10 -obmc 1
fate-vsynth%-h263p:              ENCOPTS = -qscale 2 -flags +aic -umv 1 -aiv 1 -ps 300

FATE_VCODEC-$(call ENCDEC, HUFFYUV, AVI) += huffyuv
fate-vsynth%-huffyuv:            ENCOPTS = -pix_fmt yuv422p -sws_flags neighbor
fate-vsynth%-huffyuv:            DECOPTS = -strict -2 -sws_flags neighbor

FATE_VCODEC-$(call ENCDEC, JPEGLS, AVI) += jpegls
fate-vsynth%-jpegls:             ENCOPTS = -sws_flags neighbor+full_chroma_int
fate-vsynth%-jpegls:             DECOPTS = -sws_flags area

FATE_VCODEC-$(call ENCDEC, JPEG2000, AVI) += jpeg2000 jpeg2000-97
fate-vsynth%-jpeg2000:                ENCOPTS = -qscale 7 -strict experimental -pred 1 -pix_fmt rgb24
fate-vsynth%-jpeg2000:                DECINOPTS = -vcodec jpeg2000
fate-vsynth%-jpeg2000-97:             ENCOPTS = -qscale 7 -strict experimental -pix_fmt rgb24
fate-vsynth%-jpeg2000-97:             DECINOPTS = -vcodec jpeg2000

FATE_VCODEC-$(call ENCDEC, LJPEG MJPEG, AVI) += ljpeg
fate-vsynth%-ljpeg:              ENCOPTS = -strict -1

FATE_VCODEC-$(call ENCDEC, MJPEG, AVI)  += mjpeg mjpeg-422 mjpeg-444
fate-vsynth%-mjpeg:              ENCOPTS = -qscale 9 -pix_fmt yuvj420p
fate-vsynth%-mjpeg-422:          ENCOPTS = -qscale 9 -pix_fmt yuvj422p
fate-vsynth%-mjpeg-444:          ENCOPTS = -qscale 9 -pix_fmt yuvj444p

FATE_VCODEC-$(call ENCDEC, MPEG1VIDEO, MPEG1VIDEO MPEGVIDEO) += mpeg1 mpeg1b
fate-vsynth%-mpeg1:              FMT     = mpeg1video
fate-vsynth%-mpeg1:              CODEC   = mpeg1video
fate-vsynth%-mpeg1:              ENCOPTS = -qscale 10

fate-vsynth%-mpeg1b:             CODEC   = mpeg1video
fate-vsynth%-mpeg1b:             ENCOPTS = -qscale 8 -bf 3 -ps 200
fate-vsynth%-mpeg1b:             FMT     = mpeg1video

FATE_MPEG2 = mpeg2                                                      \
             mpeg2-422                                                  \
             mpeg2-idct-int                                             \
             mpeg2-ilace                                                \
             mpeg2-ivlc-qprd                                            \
             mpeg2-thread                                               \
             mpeg2-thread-ivlc

FATE_VCODEC-$(call ENCDEC, MPEG2VIDEO, MPEG2VIDEO MPEGVIDEO) += $(FATE_MPEG2)

$(FATE_MPEG2:%=fate-vsynth\%-%): FMT    = mpeg2video
$(FATE_MPEG2:%=fate-vsynth\%-%): CODEC  = mpeg2video

fate-vsynth%-mpeg2:              ENCOPTS = -qscale 10
fate-vsynth%-mpeg2-422:          ENCOPTS = -b:v 1000k                   \
                                           -bf 2                        \
                                           -trellis 1                   \
                                           -flags +mv0+ildct+ilme       \
                                           -mpv_flags +qp_rd            \
                                           -intra_vlc 1                 \
                                           -mbd rd                      \
                                           -pix_fmt yuv422p
fate-vsynth%-mpeg2-idct-int:     ENCOPTS = -qscale 10 -idct int -dct int
fate-vsynth%-mpeg2-ilace:        ENCOPTS = -qscale 10 -flags +ildct+ilme
fate-vsynth%-mpeg2-ivlc-qprd:    ENCOPTS = -b:v 500k                    \
                                           -bf 2                        \
                                           -trellis 1                   \
                                           -flags +mv0                  \
                                           -mpv_flags +qp_rd            \
                                           -intra_vlc 1                 \
                                           -cmp 2 -subcmp 2             \
                                           -mbd rd
fate-vsynth%-mpeg2-thread:       ENCOPTS = -qscale 10 -bf 2 -flags +ildct+ilme \
                                           -threads 2 -slices 2
fate-vsynth%-mpeg2-thread-ivlc:  ENCOPTS = -qscale 10 -bf 2 -flags +ildct+ilme \
                                           -intra_vlc 1 -threads 2 -slices 2

FATE_MPEG4_MP4 = mpeg4
FATE_MPEG4_AVI = mpeg4-rc                                               \
                 mpeg4-adv                                              \
                 mpeg4-qprd                                             \
                 mpeg4-adap                                             \
                 mpeg4-qpel                                             \
                 mpeg4-thread                                           \
                 mpeg4-error                                            \
                 mpeg4-nr

FATE_VCODEC-$(call ENCDEC, MPEG4, MP4 MOV) += $(FATE_MPEG4_MP4)
FATE_VCODEC-$(call ENCDEC, MPEG4, AVI)     += $(FATE_MPEG4_AVI)

fate-vsynth%-mpeg4:              ENCOPTS = -qscale 10 -flags +mv4 -mbd bits
fate-vsynth%-mpeg4:              FMT     = mp4

fate-vsynth%-mpeg4-adap:         ENCOPTS = -b 550k -bf 2 -flags +mv4+mv0 \
                                           -trellis 1 -cmp 1 -subcmp 2   \
                                           -mbd rd -scplx_mask 0.3

fate-vsynth%-mpeg4-adv:          ENCOPTS = -qscale 9 -flags +mv4+aic       \
                                           -data_partitioning 1 -trellis 1 \
                                           -mbd bits -ps 200

fate-vsynth%-mpeg4-error:        ENCOPTS = -qscale 7 -flags +mv4+aic    \
                                           -data_partitioning 1 -mbd rd \
                                           -ps 250 -error 10

fate-vsynth%-mpeg4-nr:           ENCOPTS = -qscale 8 -flags +mv4 -mbd rd -nr 200

fate-vsynth%-mpeg4-qpel:         ENCOPTS = -qscale 7 -flags +mv4+qpel -mbd 2 \
                                           -bf 2 -cmp 1 -subcmp 2

fate-vsynth%-mpeg4-qprd:         ENCOPTS = -b 450k -bf 2 -trellis 1          \
                                           -flags +mv4+mv0 -mpv_flags +qp_rd \
                                           -cmp 2 -subcmp 2 -mbd rd

fate-vsynth%-mpeg4-rc:           ENCOPTS = -b 400k -bf 2

fate-vsynth%-mpeg4-thread:       ENCOPTS = -b 500k -flags +mv4+aic         \
                                           -data_partitioning 1 -trellis 1 \
                                           -mbd bits -ps 200 -bf 2         \
                                           -threads 2 -slices 2

FATE_VCODEC-$(call ENCDEC, MSMPEG4V3, AVI) += msmpeg4
fate-vsynth%-msmpeg4:            ENCOPTS = -qscale 10

FATE_VCODEC-$(call ENCDEC, MSMPEG4V2, AVI) += msmpeg4v2
fate-vsynth%-msmpeg4v2:          ENCOPTS = -qscale 10

FATE_VCODEC-$(call ENCDEC, PNG, AVI)    += mpng
fate-vsynth%-mpng:               CODEC   = png

FATE_VCODEC-$(call ENCDEC, MSVIDEO1, AVI) += msvideo1

FATE_VCODEC-$(call ENCDEC, PRORES, MOV) += prores prores_ks
fate-vsynth%-prores:             FMT     = mov

fate-vsynth%-prores_ks:          ENCOPTS = -profile hq
fate-vsynth%-prores_ks:          FMT     = mov

FATE_VCODEC-$(call ENCDEC, QTRLE, MOV)  += qtrle qtrlegray
fate-vsynth%-qtrle:              FMT     = mov

fate-vsynth%-qtrlegray:          CODEC   = qtrle
fate-vsynth%-qtrlegray:          ENCOPTS = -pix_fmt gray
fate-vsynth%-qtrlegray:          FMT     = mov

FATE_VCODEC-$(call ENCDEC, RAWVIDEO, AVI) += rgb
fate-vsynth%-rgb:                CODEC   = rawvideo
fate-vsynth%-rgb:                ENCOPTS = -pix_fmt bgr24

FATE_VCODEC-$(call ENCDEC, ROQ, ROQ)    += roqvideo
fate-vsynth%-roqvideo:           CODEC   = roqvideo
fate-vsynth%-roqvideo:           ENCOPTS = -frames 5
fate-vsynth%-roqvideo:           RAWDECOPTS = -r 30
fate-vsynth%-roqvideo:           FMT     = roq

FATE_VCODEC-$(call ENCDEC, RV10, RM)    += rv10
fate-vsynth%-rv10:               ENCOPTS = -qscale 10
fate-vsynth%-rv10:               FMT     = rm

FATE_VCODEC-$(call ENCDEC, RV20, RM)    += rv20
fate-vsynth%-rv20:               ENCOPTS = -qscale 10
fate-vsynth%-rv20:               FMT     = rm

FATE_VCODEC-$(call ENCDEC, SNOW, AVI)   += snow snow-hpel snow-ll
fate-vsynth%-snow:               ENCOPTS = -strict -2 -qscale 2 -flags +qpel \
                                           -me_method iter -dia_size 2       \
                                           -cmp 12 -subcmp 12 -s 128x64

fate-vsynth%-snow-hpel:          ENCOPTS = -strict -2 -qscale 2              \
                                           -me_method iter -dia_size 2       \
                                           -cmp 12 -subcmp 12 -s 128x64

fate-vsynth%-snow-ll:            ENCOPTS = -strict -2 -qscale .001 -pred 1 \
                                           -flags +mv4+qpel

FATE_VCODEC-$(call ENCDEC, SVQ1, MOV)   += svq1
fate-vsynth%-svq1:               ENCOPTS = -qscale 3 -pix_fmt yuv410p
fate-vsynth%-svq1:               FMT     = mov

FATE_VCODEC-$(call ENCDEC, R210, AVI)   += r210

FATE_VCODEC-$(call ENCDEC, V210, AVI)   += v210

FATE_VCODEC-$(call ENCDEC, V308, AVI)   += v308

FATE_VCODEC-$(call ENCDEC, V408, AVI)   += v408
fate-vsynth%-v408:               ENCOPTS = -sws_flags neighbor+bitexact
fate-vsynth%-v408:               DECOPTS = -sws_flags neighbor+bitexact

FATE_VCODEC-$(call ENCDEC, AVUI, MOV)   += avui
fate-vsynth%-avui:               ENCOPTS = -s pal -strict experimental -sws_flags neighbor+bitexact
fate-vsynth%-avui:               DECOPTS = -sws_flags neighbor+bitexact
fate-vsynth%-avui:               FMT     = mov

FATE_VCODEC-$(call ENCDEC, WMV1, AVI)   += wmv1
fate-vsynth%-wmv1:               ENCOPTS = -qscale 10

FATE_VCODEC-$(call ENCDEC, WMV2, AVI)   += wmv2
fate-vsynth%-wmv2:               ENCOPTS = -qscale 10

FATE_VCODEC-$(call ENCDEC, RAWVIDEO, AVI) += yuv
fate-vsynth%-yuv:                CODEC = rawvideo

FATE_VCODEC-$(call ENCDEC, YUV4, AVI) += yuv4

FATE_VCODEC-$(call ENCDEC, Y41P, AVI) += y41p

FATE_VCODEC-$(call ENCDEC, ZLIB, AVI) += zlib

FATE_VCODEC += $(FATE_VCODEC-yes)
FATE_VSYNTH1 = $(FATE_VCODEC:%=fate-vsynth1-%)
FATE_VSYNTH2 = $(FATE_VCODEC:%=fate-vsynth2-%)

$(FATE_VSYNTH1): tests/data/vsynth1.yuv
$(FATE_VSYNTH2): tests/data/vsynth2.yuv

FATE_AVCONV += $(FATE_VSYNTH1) $(FATE_VSYNTH2)

fate-vsynth1: $(FATE_VSYNTH1)
fate-vsynth2: $(FATE_VSYNTH2)
fate-vcodec:  fate-vsynth1 fate-vsynth2
