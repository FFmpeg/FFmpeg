fate-vsynth1-%: SRC = tests/data/vsynth1.yuv
fate-vsynth2-%: SRC = tests/data/vsynth2.yuv
fate-vsynth%: CODEC = $(word 3, $(subst -, ,$(@)))
fate-vsynth%: FMT = avi
fate-vsynth%: CMD = enc_dec "rawvideo -s 352x288 -pix_fmt yuv420p $(RAWDECOPTS)" $(SRC) $(FMT) "-c $(CODEC) $(ENCOPTS)" rawvideo "-s 352x288 -pix_fmt yuv420p -vsync 0 $(DECOPTS)" -keep "$(DECINOPTS)"
fate-vsynth%: CMP_UNIT = 1

FATE_VCODEC += amv

FATE_VCODEC += asv1
fate-vsynth%-asv1:               ENCOPTS = -qscale 10

FATE_VCODEC += asv2
fate-vsynth%-asv2:               ENCOPTS = -qscale 10

FATE_VCODEC += cljr

FATE_VCODEC += dnxhd-720p
fate-vsynth%-dnxhd-720p:         ENCOPTS = -s hd720 -b 90M              \
                                           -pix_fmt yuv422p -frames 5 -qmax 8
fate-vsynth%-dnxhd-720p:         FMT     = dnxhd

FATE_VCODEC += dnxhd-720p-rd
fate-vsynth%-dnxhd-720p-rd:      ENCOPTS = -s hd720 -b 90M -threads 4 -mbd rd \
                                           -pix_fmt yuv422p -frames 5 -qmax 8
fate-vsynth%-dnxhd-720p-rd:      FMT     = dnxhd

FATE_VCODEC += dnxhd-720p-10bit
fate-vsynth%-dnxhd-720p-10bit:   ENCOPTS = -s hd720 -b 90M              \
                                           -pix_fmt yuv422p10 -frames 5 -qmax 8
fate-vsynth%-dnxhd-720p-10bit:   FMT     = dnxhd

FATE_VCODEC += dnxhd-1080i
fate-vsynth%-dnxhd-1080i:        ENCOPTS = -s hd1080 -b 120M -flags +ildct \
                                           -pix_fmt yuv422p -frames 5 -qmax 8
fate-vsynth%-dnxhd-1080i:        FMT     = mov

FATE_VCODEC += dv
fate-vsynth%-dv:                 CODEC   = dvvideo
fate-vsynth%-dv:                 ENCOPTS = -dct int -s pal
fate-vsynth%-dv:                 FMT     = dv

FATE_VCODEC += dv-411
fate-vsynth%-dv-411:             CODEC   = dvvideo
fate-vsynth%-dv-411:             ENCOPTS = -dct int -s pal -pix_fmt yuv411p \
                                           -sws_flags area
fate-vsynth%-dv-411:             DECOPTS = -sws_flags area
fate-vsynth%-dv-411:             FMT     = dv

FATE_VCODEC += dv-50
fate-vsynth%-dv-50:              CODEC   = dvvideo
fate-vsynth%-dv-50:              ENCOPTS = -dct int -s pal -pix_fmt yuv422p \
                                           -sws_flags neighbor
fate-vsynth%-dv-50:              DECOPTS = -sws_flags neighbor
fate-vsynth%-dv-50:              FMT     = dv

FATE_VCODEC += ffv1
fate-vsynth%-ffv1:               ENCOPTS = -strict -2

FATE_VCODEC += ffvhuff

FATE_VCODEC += flashsv
fate-vsynth%-flashsv:            ENCOPTS = -sws_flags neighbor+full_chroma_int
fate-vsynth%-flashsv:            DECOPTS = -sws_flags area
fate-vsynth%-flashsv:            FMT     = flv

FATE_VCODEC += flashsv2
fate-vsynth%-flashsv2:           ENCOPTS = -sws_flags neighbor+full_chroma_int -strict experimental -compression_level 0
fate-vsynth%-flashsv2:           DECOPTS = -sws_flags area
fate-vsynth%-flashsv2:           FMT     = flv

FATE_VCODEC += flv
fate-vsynth%-flv:                ENCOPTS = -qscale 10
fate-vsynth%-flv:                FMT     = flv

FATE_VCODEC += h261
fate-vsynth%-h261:               ENCOPTS = -qscale 11

FATE_VCODEC += h263
fate-vsynth%-h263:               ENCOPTS = -qscale 10

FATE_VCODEC += h263p
fate-vsynth%-h263p:              ENCOPTS = -qscale 2 -flags +aic -umv 1 -aiv 1 -ps 300

FATE_VCODEC += huffyuv
fate-vsynth%-huffyuv:            ENCOPTS = -pix_fmt yuv422p -sws_flags neighbor
fate-vsynth%-huffyuv:            DECOPTS = -strict -2 -sws_flags neighbor

FATE_VCODEC += jpegls
fate-vsynth%-jpegls:             ENCOPTS = -sws_flags neighbor+full_chroma_int
fate-vsynth%-jpegls:             DECOPTS = -sws_flags area

FATE_VCODEC += j2k
fate-vsynth%-j2k:                ENCOPTS = -qscale 7 -strict experimental -pix_fmt rgb24
fate-vsynth%-j2k:                DECINOPTS = -vcodec j2k -strict experimental

FATE_VCODEC += ljpeg
fate-vsynth%-ljpeg:              ENCOPTS = -strict -1

FATE_VCODEC += mjpeg
fate-vsynth%-mjpeg:              ENCOPTS = -qscale 9 -pix_fmt yuvj420p

FATE_VCODEC += mpeg1
fate-vsynth%-mpeg1:              FMT     = mpeg1video
fate-vsynth%-mpeg1:              CODEC   = mpeg1video
fate-vsynth%-mpeg1:              ENCOPTS = -qscale 10

FATE_VCODEC += mpeg1b
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

FATE_VCODEC += $(FATE_MPEG2)

$(FATE_MPEG2:%=fate-vsynth\%-%): FMT    = mpeg2video
$(FATE_MPEG2:%=fate-vsynth\%-%): CODEC  = mpeg2video

fate-vsynth%-mpeg2:              ENCOPTS = -qscale 10
fate-vsynth%-mpeg2-422:          ENCOPTS = -vb 1000k                    \
                                           -bf 2                        \
                                           -trellis 1                   \
                                           -flags +mv0+ildct+ilme       \
                                           -mpv_flags +qp_rd            \
                                           -intra_vlc 1                 \
                                           -mbd rd                      \
                                           -pix_fmt yuv422p
fate-vsynth%-mpeg2-idct-int:     ENCOPTS = -qscale 10 -idct int -dct int
fate-vsynth%-mpeg2-ilace:        ENCOPTS = -qscale 10 -flags +ildct+ilme
fate-vsynth%-mpeg2-ivlc-qprd:    ENCOPTS = -vb 500k                     \
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

FATE_VCODEC += mpeg4
fate-vsynth%-mpeg4:              ENCOPTS = -qscale 10 -flags +mv4 -mbd bits
fate-vsynth%-mpeg4:              FMT     = mp4

FATE_VCODEC += mpeg4-rc
fate-vsynth%-mpeg4-rc:           ENCOPTS = -b 400k -bf 2

FATE_VCODEC += mpeg4-adv
fate-vsynth%-mpeg4-adv:          ENCOPTS = -qscale 9 -flags +mv4+aic       \
                                           -data_partitioning 1 -trellis 1 \
                                           -mbd bits -ps 200

FATE_VCODEC += mpeg4-qprd
fate-vsynth%-mpeg4-qprd:         ENCOPTS = -b 450k -bf 2 -trellis 1          \
                                           -flags +mv4+mv0 -mpv_flags +qp_rd \
                                           -cmp 2 -subcmp 2 -mbd rd

FATE_VCODEC += mpeg4-adap
fate-vsynth%-mpeg4-adap:         ENCOPTS = -b 550k -bf 2 -flags +mv4+mv0 \
                                           -trellis 1 -cmp 1 -subcmp 2   \
                                           -mbd rd -scplx_mask 0.3

FATE_VCODEC += mpeg4-qpel
fate-vsynth%-mpeg4-qpel:         ENCOPTS = -qscale 7 -flags +mv4+qpel -mbd 2 \
                                           -bf 2 -cmp 1 -subcmp 2

FATE_VCODEC += mpeg4-thread
fate-vsynth%-mpeg4-thread:       ENCOPTS = -b 500k -flags +mv4+aic         \
                                           -data_partitioning 1 -trellis 1 \
                                           -mbd bits -ps 200 -bf 2         \
                                           -threads 2 -slices 2

FATE_VCODEC += mpeg4-error
fate-vsynth%-mpeg4-error:        ENCOPTS = -qscale 7 -flags +mv4+aic    \
                                           -data_partitioning 1 -mbd rd \
                                           -ps 250 -error 10

FATE_VCODEC += mpeg4-nr
fate-vsynth%-mpeg4-nr:           ENCOPTS = -qscale 8 -flags +mv4 -mbd rd -nr 200

FATE_VCODEC += msmpeg4
fate-vsynth%-msmpeg4:            ENCOPTS = -qscale 10

FATE_VCODEC += msmpeg4v2
fate-vsynth%-msmpeg4v2:          ENCOPTS = -qscale 10

FATE_VCODEC += mpng
fate-vsynth%-mpng:               CODEC   = png

FATE_VCODEC += msvideo1

FATE_VCODEC += prores
fate-vsynth%-prores:             FMT     = mov

FATE_VCODEC += prores_kostya
fate-vsynth%-prores_kostya:             ENCOPTS = -profile hq
fate-vsynth%-prores_kostya:             FMT     = mov

FATE_VCODEC += qtrle
fate-vsynth%-qtrle:              FMT     = mov

FATE_VCODEC += qtrlegray
fate-vsynth%-qtrlegray:          CODEC   = qtrle
fate-vsynth%-qtrlegray:          ENCOPTS = -pix_fmt gray
fate-vsynth%-qtrlegray:          FMT     = mov

FATE_VCODEC += rgb
fate-vsynth%-rgb:                CODEC   = rawvideo
fate-vsynth%-rgb:                ENCOPTS = -pix_fmt bgr24

FATE_VCODEC += roqvideo
fate-vsynth%-roqvideo:           CODEC   = roqvideo
fate-vsynth%-roqvideo:           ENCOPTS = -frames 5
fate-vsynth%-roqvideo:           RAWDECOPTS = -r 30
fate-vsynth%-roqvideo:           FMT     = roq

FATE_VCODEC += rv10
fate-vsynth%-rv10:               ENCOPTS = -qscale 10
fate-vsynth%-rv10:               FMT     = rm

FATE_VCODEC += rv20
fate-vsynth%-rv20:               ENCOPTS = -qscale 10
fate-vsynth%-rv20:               FMT     = rm

FATE_VCODEC += snow
fate-vsynth%-snow:               ENCOPTS = -strict -2 -qscale 2 -flags +qpel \
                                           -me_method iter -dia_size 2       \
                                           -cmp 12 -subcmp 12 -s 128x64

FATE_VCODEC += snow-hpel
fate-vsynth%-snow-hpel:          ENCOPTS = -strict -2 -qscale 2              \
                                           -me_method iter -dia_size 2       \
                                           -cmp 12 -subcmp 12 -s 128x64

FATE_VCODEC += snow-ll
fate-vsynth%-snow-ll:            ENCOPTS = -strict -2 -qscale .001 -pred 1 \
                                           -flags +mv4+qpel

FATE_VCODEC += svq1
fate-vsynth%-svq1:               ENCOPTS = -qscale 3 -pix_fmt yuv410p
fate-vsynth%-svq1:               FMT     = mov

FATE_VCODEC += r210

FATE_VCODEC += v210

FATE_VCODEC += v308

FATE_VCODEC += v408
fate-vsynth%-v408:               ENCOPTS = -sws_flags neighbor+bitexact
fate-vsynth%-v408:               DECOPTS = -sws_flags neighbor+bitexact

FATE_VCODEC += avui
fate-vsynth%-avui:               ENCOPTS = -s pal -strict experimental -sws_flags neighbor+bitexact
fate-vsynth%-avui:               DECOPTS = -sws_flags neighbor+bitexact
fate-vsynth%-avui:               FMT     = mov

FATE_VCODEC += wmv1
fate-vsynth%-wmv1:               ENCOPTS = -qscale 10

FATE_VCODEC += wmv2
fate-vsynth%-wmv2:               ENCOPTS = -qscale 10

FATE_VCODEC += yuv
fate-vsynth%-yuv:                CODEC = rawvideo

FATE_VCODEC += yuv4

FATE_VCODEC += y41p

FATE_VCODEC += zlib


FATE_VSYNTH1 = $(FATE_VCODEC:%=fate-vsynth1-%)
FATE_VSYNTH2 = $(FATE_VCODEC:%=fate-vsynth2-%)

$(FATE_VSYNTH1): tests/data/vsynth1.yuv
$(FATE_VSYNTH2): tests/data/vsynth2.yuv

FATE_AVCONV += $(FATE_VSYNTH1) $(FATE_VSYNTH2)

fate-vsynth1: $(FATE_VSYNTH1)
fate-vsynth2: $(FATE_VSYNTH2)
fate-vcodec:  fate-vsynth1 fate-vsynth2
