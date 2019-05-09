fate-vsynth1-%: SRC = tests/data/vsynth1.yuv
fate-vsynth2-%: SRC = tests/data/vsynth2.yuv
fate-vsynth_lena-%: SRC = tests/data/vsynth_lena.yuv
fate-vsynth3-%: SRC = tests/data/vsynth3.yuv
fate-vsynth%: CODEC = $(word 3, $(subst -, ,$(@)))
fate-vsynth%: FMT = avi
fate-vsynth%: CMD = enc_dec "rawvideo -s 352x288 -pix_fmt yuv420p $(RAWDECOPTS)" $(SRC) $(FMT) "-c $(CODEC) $(ENCOPTS)" rawvideo "-s 352x288 -pix_fmt yuv420p -vsync 0 $(DECOPTS)" -keep "$(DECINOPTS)"
fate-vsynth3-%: CMD = enc_dec "rawvideo -s $(FATEW)x$(FATEH) -pix_fmt yuv420p $(RAWDECOPTS)" $(SRC) $(FMT) "-c $(CODEC) $(ENCOPTS)" rawvideo "-s $(FATEW)x$(FATEH) -pix_fmt yuv420p -vsync 0 $(DECOPTS)" -keep "$(DECINOPTS)"
fate-vsynth%: CMP_UNIT = 1
fate-vsynth%: REF = $(SRC_PATH)/tests/ref/vsynth/$(@:fate-%=%)

FATE_VCODEC-$(call ENCDEC, AMV, AVI) += amv
fate-vsynth%-amv:                ENCOPTS = -strict -1

FATE_VCODEC-$(call ENCDEC, ASV1, AVI)   += asv1
fate-vsynth%-asv1:               ENCOPTS = -qscale 10

FATE_VCODEC-$(call ENCDEC, ASV2, AVI)   += asv2
fate-vsynth%-asv2:               ENCOPTS = -qscale 10

FATE_VCODEC-$(call ENCDEC, CINEPAK, AVI) += cinepak
fate-vsynth%-cinepak:            ENCOPTS = -s sqcif -strip_number_adaptivity 1
fate-vsynth%-cinepak:            DECOPTS = -s sqcif

FATE_VCODEC-$(call ENCDEC, CLJR, AVI)   += cljr
fate-vsynth%-cljr:               ENCOPTS = -strict -1

FATE_VCODEC-$(call ENCDEC, DNXHD, DNXHD) += dnxhd-720p                  \
                                            dnxhd-720p-rd               \
                                            dnxhd-720p-10bit            \
                                            dnxhd-720p-hr-lb            \
                                            dnxhd-4k-hr-lb              \
                                            dnxhd-uhd-hr-sq             \
                                            dnxhd-2k-hr-hq              \
                                            dnxhd-edge1-hr              \
                                            dnxhd-edge2-hr              \
                                            dnxhd-edge3-hr

FATE_VCODEC-$(call ENCDEC, VC2 DIRAC, MOV) += vc2-420p vc2-420p10 vc2-420p12 \
                                              vc2-422p vc2-422p10 vc2-422p12 \
                                              vc2-444p vc2-444p10 vc2-444p12 \
                                              vc2-thaar vc2-t5_3
fate-vsynth1-vc2-4%:             FMT      = mov
fate-vsynth1-vc2-4%:             ENCOPTS = -pix_fmt yuv$(@:fate-vsynth1-vc2-%=%) \
                                           -c:v vc2 -frames 5 -strict -1
fate-vsynth2-vc2-4%:             FMT      = mov
fate-vsynth2-vc2-4%:             ENCOPTS = -pix_fmt yuv$(@:fate-vsynth2-vc2-%=%) \
                                           -c:v vc2 -frames 5 -strict -1
fate-vsynth_lena-vc2-4%:         FMT      = mov
fate-vsynth_lena-vc2-4%:         ENCOPTS = -pix_fmt yuv$(@:fate-vsynth_lena-vc2-%=%) \
                                           -c:v vc2 -frames 5 -strict -1

fate-vsynth1-vc2-t%:             FMT     = mov
fate-vsynth1-vc2-t%:             ENCOPTS = -pix_fmt yuv422p10 -c:v vc2 -frames 5 -strict -1 -wavelet_type $(@:fate-vsynth1-vc2-t%=%)
fate-vsynth2-vc2-t%:             FMT     = mov
fate-vsynth2-vc2-t%:             ENCOPTS = -pix_fmt yuv422p10 -c:v vc2 -frames 5 -strict -1 -wavelet_type $(@:fate-vsynth2-vc2-t%=%)
fate-vsynth_lena-vc2-t%:         FMT     = mov
fate-vsynth_lena-vc2-t%:         ENCOPTS = -pix_fmt yuv422p10 -c:v vc2 -frames 5 -strict -1 -wavelet_type $(@:fate-vsynth_lena-vc2-t%=%)

fate-vsynth%-dnxhd-720p:         ENCOPTS = -s hd720 -b 90M              \
                                           -pix_fmt yuv422p -frames 5 -qmax 8
fate-vsynth%-dnxhd-720p:         FMT     = dnxhd

fate-vsynth%-dnxhd-720p-rd:      ENCOPTS = -s hd720 -b 90M -threads 4 -mbd rd \
                                           -pix_fmt yuv422p -frames 5 -qmax 8
fate-vsynth%-dnxhd-720p-rd:      FMT     = dnxhd

fate-vsynth%-dnxhd-720p-10bit:   ENCOPTS = -s hd720 -b 90M              \
                                           -pix_fmt yuv422p10 -frames 5 -qmax 8
fate-vsynth%-dnxhd-720p-10bit:   FMT     = dnxhd

fate-vsynth%-dnxhd-720p-hr-lb: ENCOPTS   = -s hd720 -profile:v dnxhr_lb \
                                           -pix_fmt yuv422p -frames 5
fate-vsynth%-dnxhd-720p-hr-lb: DECOPTS    = -sws_flags area+accurate_rnd+bitexact
fate-vsynth%-dnxhd-720p-hr-lb: FMT       = dnxhd

fate-vsynth%-dnxhd-4k-hr-lb:  ENCOPTS    = -s 4k -profile:v dnxhr_lb \
                                           -pix_fmt yuv422p -frames 5
fate-vsynth%-dnxhd-4k-hr-lb:  DECOPTS    = -sws_flags area+accurate_rnd+bitexact
fate-vsynth%-dnxhd-4k-hr-lb:  FMT        = dnxhd

fate-vsynth%-dnxhd-uhd-hr-sq: ENCOPTS    = -s uhd2160 -profile:v dnxhr_sq \
                                           -pix_fmt yuv422p -frames 5
fate-vsynth%-dnxhd-uhd-hr-sq: DECOPTS    = -sws_flags area+accurate_rnd+bitexact
fate-vsynth%-dnxhd-uhd-hr-sq: FMT        = dnxhd

fate-vsynth%-dnxhd-2k-hr-hq:  ENCOPTS    = -s 2k -profile:v dnxhr_hq \
                                         -pix_fmt yuv422p -frames 5
fate-vsynth%-dnxhd-2k-hr-hq:  DECOPTS    = -sws_flags area+accurate_rnd+bitexact
fate-vsynth%-dnxhd-2k-hr-hq:  FMT        = dnxhd

fate-vsynth%-dnxhd-edge1-hr:  ENCOPTS    = -s 264x128 -profile:v dnxhr_hq \
                                           -pix_fmt yuv422p -frames 5
fate-vsynth%-dnxhd-edge1-hr:  DECOPTS    = -sws_flags area+accurate_rnd+bitexact
fate-vsynth%-dnxhd-edge1-hr:  FMT        = dnxhd

fate-vsynth%-dnxhd-edge2-hr:  ENCOPTS    = -s 271x135 -profile:v dnxhr_hq \
                                           -pix_fmt yuv422p -frames 5
fate-vsynth%-dnxhd-edge2-hr:  DECOPTS    = -sws_flags area+accurate_rnd+bitexact
fate-vsynth%-dnxhd-edge2-hr:  FMT        = dnxhd

fate-vsynth%-dnxhd-edge3-hr:  ENCOPTS    = -s 257x121 -profile:v dnxhr_hq \
                                           -pix_fmt yuv422p -frames 5
fate-vsynth%-dnxhd-edge3-hr:  DECOPTS    = -sws_flags area+accurate_rnd+bitexact
fate-vsynth%-dnxhd-edge3-hr:  FMT        = dnxhd

FATE_VCODEC-$(call ENCDEC, DNXHD, MOV)  += dnxhd-1080i dnxhd-1080i-10bit dnxhd-1080i-colr \
                                           dnxhd-hr-lb-mov dnxhd-hr-sq-mov dnxhd-hr-hq-mov
fate-vsynth%-dnxhd-1080i:        ENCOPTS = -s hd1080 -b 120M -flags +ildct \
                                           -pix_fmt yuv422p -frames 5 -qmax 8
fate-vsynth%-dnxhd-1080i:        FMT     = mov

fate-vsynth%-dnxhd-1080i-10bit:  ENCOPTS = -s hd1080 -b 185M -flags +ildct \
                                           -pix_fmt yuv422p10 -frames 5 -qmax 8
fate-vsynth%-dnxhd-1080i-10bit:  DECOPTS = -sws_flags area+accurate_rnd+bitexact
fate-vsynth%-dnxhd-1080i-10bit:  FMT     = mov

fate-vsynth%-dnxhd-1080i-colr:   ENCOPTS = -s hd1080 -b 120M -flags +ildct -movflags write_colr \
                                           -pix_fmt yuv422p -frames 5 -qmax 8
fate-vsynth%-dnxhd-1080i-colr:   DECOPTS = -sws_flags area+accurate_rnd+bitexact
fate-vsynth%-dnxhd-1080i-colr:   FMT     = mov

fate-vsynth%-dnxhd-hr-lb-mov:   ENCOPTS = -s uhd2160 -profile:v dnxhr_lb \
                                           -pix_fmt yuv422p -frames 5
fate-vsynth%-dnxhd-hr-lb-mov:   DECOPTS = -sws_flags area+accurate_rnd+bitexact
fate-vsynth%-dnxhd-hr-lb-mov:   FMT     = mov

fate-vsynth%-dnxhd-hr-sq-mov:   ENCOPTS = -s 2kscope -profile:v dnxhr_sq \
                                           -pix_fmt yuv422p -frames 5
fate-vsynth%-dnxhd-hr-sq-mov:   DECOPTS = -sws_flags area+accurate_rnd+bitexact
fate-vsynth%-dnxhd-hr-sq-mov:   FMT     = mov

fate-vsynth%-dnxhd-hr-hq-mov:   ENCOPTS = -s 2kflat -profile:v dnxhr_hq \
                                           -pix_fmt yuv422p -frames 5
fate-vsynth%-dnxhd-hr-hq-mov:   DECOPTS = -sws_flags area+accurate_rnd+bitexact
fate-vsynth%-dnxhd-hr-hq-mov:   FMT     = mov

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

FATE_VCODEC-$(call ENCDEC, FFV1, AVI)   += ffv1 ffv1-v0 \
                                           ffv1-v3-yuv420p ffv1-v3-yuv422p10 ffv1-v3-yuv444p16 \
                                           ffv1-v3-bgr0 ffv1-v3-rgb48
fate-vsynth%-ffv1:               ENCOPTS = -slices 4
fate-vsynth%-ffv1-v0:            CODEC   = ffv1
fate-vsynth%-ffv1-v3-yuv420p:    ENCOPTS = -level 3 -pix_fmt yuv420p
fate-vsynth%-ffv1-v3-yuv422p10:  ENCOPTS = -level 3 -pix_fmt yuv422p10 \
                                           -sws_flags neighbor+bitexact
fate-vsynth%-ffv1-v3-yuv422p10:  DECOPTS = -sws_flags neighbor+bitexact
fate-vsynth%-ffv1-v3-yuv444p16:  ENCOPTS = -level 3 -pix_fmt yuv444p16 \
                                           -sws_flags neighbor+bitexact
fate-vsynth%-ffv1-v3-yuv444p16:  DECOPTS = -sws_flags neighbor+bitexact
fate-vsynth%-ffv1-v3-bgr0:       ENCOPTS = -level 3 -pix_fmt bgr0 \
                                           -sws_flags neighbor+bitexact
fate-vsynth%-ffv1-v3-bgr0:       DECOPTS = -sws_flags neighbor+bitexact
fate-vsynth%-ffv1-v3-rgb48:      ENCOPTS = -level 3 -pix_fmt rgb48 -strict -2 \
                                           -sws_flags neighbor+bitexact
fate-vsynth%-ffv1-v3-rgb48:      DECOPTS = -sws_flags neighbor+bitexact

FATE_VCODEC-$(call ENCDEC, FFVHUFF, AVI) += ffvhuff ffvhuff444 ffvhuff420p12 ffvhuff422p10left ffvhuff444p16
fate-vsynth%-ffvhuff444:         ENCOPTS = -c:v ffvhuff -pix_fmt yuv444p
fate-vsynth%-ffvhuff420p12:      ENCOPTS = -c:v ffvhuff -pix_fmt yuv420p12le
fate-vsynth%-ffvhuff422p10left:  ENCOPTS = -c:v ffvhuff -pix_fmt yuv422p10le -pred left
fate-vsynth%-ffvhuff444p16:      ENCOPTS = -c:v ffvhuff -pix_fmt yuv444p16le -pred plane

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

FATE_VCODEC-$(call ENCDEC, H261, AVI)   += h261 h261-trellis
fate-vsynth%-h261:               ENCOPTS = -qscale 11
fate-vsynth%-h261-trellis:       ENCOPTS = -qscale 12 -trellis 1 -mbd rd

FATE_VCODEC-$(call ENCDEC, H263, AVI)   += h263 h263-obmc h263p
fate-vsynth%-h263:               ENCOPTS = -qscale 10
fate-vsynth%-h263-obmc:          ENCOPTS = -qscale 10 -obmc 1
fate-vsynth%-h263p:              ENCOPTS = -qscale 2 -flags +aic -umv 1 -aiv 1 -ps 300

FATE_VCODEC-$(call ENCDEC, HUFFYUV, AVI) += huffyuv huffyuvbgr24 huffyuvbgra
fate-vsynth%-huffyuv:            ENCOPTS = -c:v huffyuv -pix_fmt yuv422p -sws_flags neighbor
fate-vsynth%-huffyuv:            DECOPTS = -sws_flags neighbor
fate-vsynth%-huffyuvbgr24:       ENCOPTS = -c:v huffyuv -pix_fmt bgr24 -sws_flags neighbor
fate-vsynth%-huffyuvbgr24:       DECOPTS = -sws_flags neighbor
fate-vsynth%-huffyuvbgra:        ENCOPTS = -c:v huffyuv -pix_fmt bgr32 -sws_flags neighbor
fate-vsynth%-huffyuvbgra:        DECOPTS = -sws_flags neighbor

FATE_VCODEC-$(call ENCDEC, JPEGLS, AVI) += jpegls
fate-vsynth%-jpegls:             ENCOPTS = -sws_flags neighbor+full_chroma_int
fate-vsynth%-jpegls:             DECOPTS = -sws_flags area

FATE_VCODEC-$(call ENCDEC, JPEG2000, AVI) += jpeg2000 jpeg2000-97
fate-vsynth%-jpeg2000:                ENCOPTS = -qscale 7 -strict experimental -pred 1 -pix_fmt rgb24
fate-vsynth%-jpeg2000:                DECINOPTS = -c:v jpeg2000
fate-vsynth%-jpeg2000-97:             ENCOPTS = -qscale 7 -strict experimental -pix_fmt rgb24
fate-vsynth%-jpeg2000-97:             DECINOPTS = -c:v jpeg2000

FATE_VCODEC-$(call ENCDEC, LJPEG MJPEG, AVI) += ljpeg
fate-vsynth%-ljpeg:              ENCOPTS = -strict -1

FATE_VCODEC-$(call ENCDEC, MJPEG, AVI)  += mjpeg mjpeg-422 mjpeg-444 mjpeg-trell mjpeg-huffman mjpeg-trell-huffman
fate-vsynth%-mjpeg:                   ENCOPTS = -qscale 9 -pix_fmt yuvj420p
fate-vsynth%-mjpeg-422:               ENCOPTS = -qscale 9 -pix_fmt yuvj422p
fate-vsynth%-mjpeg-444:               ENCOPTS = -qscale 9 -pix_fmt yuvj444p
fate-vsynth%-mjpeg-trell:             ENCOPTS = -qscale 9 -pix_fmt yuvj420p -trellis 1
fate-vsynth%-mjpeg-huffman:           ENCOPTS = -qscale 9 -pix_fmt yuvj420p -huffman optimal
fate-vsynth%-mjpeg-trell-huffman:     ENCOPTS = -qscale 9 -pix_fmt yuvj420p -trellis 1 -huffman optimal

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
                                           -flags +ildct+ilme           \
                                           -mpv_flags +qp_rd+mv0        \
                                           -intra_vlc 1                 \
                                           -mbd rd                      \
                                           -pix_fmt yuv422p
fate-vsynth%-mpeg2-idct-int:     ENCOPTS = -qscale 10 -idct int -dct int
fate-vsynth%-mpeg2-ilace:        ENCOPTS = -qscale 10 -flags +ildct+ilme
fate-vsynth%-mpeg2-ivlc-qprd:    ENCOPTS = -b:v 500k                    \
                                           -bf 2                        \
                                           -trellis 1                   \
                                           -mpv_flags +qp_rd+mv0        \
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
                 mpeg4-nr                                               \
                 mpeg4-nsse

FATE_VCODEC-$(call ENCDEC, MPEG4, MP4 MOV) += $(FATE_MPEG4_MP4)
FATE_VCODEC-$(call ENCDEC, MPEG4, AVI)     += $(FATE_MPEG4_AVI)

fate-vsynth%-mpeg4:              ENCOPTS = -qscale 10 -flags +mv4 -mbd bits
fate-vsynth%-mpeg4:              FMT     = mp4

fate-vsynth%-mpeg4-adap:         ENCOPTS = -b 550k -bf 2 -flags +mv4     \
                                           -trellis 1 -cmp 1 -subcmp 2   \
                                           -mbd rd -scplx_mask 0.3       \
                                           -mpv_flags +mv0

fate-vsynth%-mpeg4-adv:          ENCOPTS = -qscale 9 -flags +mv4+aic       \
                                           -data_partitioning 1 -trellis 1 \
                                           -mbd bits -ps 200

fate-vsynth%-mpeg4-error:        ENCOPTS = -qscale 7 -flags +mv4+aic    \
                                           -data_partitioning 1 -mbd rd \
                                           -ps 250 -error_rate 10

fate-vsynth%-mpeg4-nr:           ENCOPTS = -qscale 8 -flags +mv4 -mbd rd \
                                           -noise_reduction 200

fate-vsynth%-mpeg4-nsse:         ENCOPTS = -qscale 7 -cmp nsse -subcmp nsse \
                                           -mbcmp nsse -precmp nsse         \
                                           -skipcmp nsse

fate-vsynth%-mpeg4-qpel:         ENCOPTS = -qscale 7 -flags +mv4+qpel -mbd 2 \
                                           -bf 2 -cmp 1 -subcmp 2

fate-vsynth%-mpeg4-qprd:         ENCOPTS = -b 450k -bf 2 -trellis 1          \
                                           -flags +mv4 -mpv_flags +qp_rd+mv0 \
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

FATE_VCODEC-$(call ENCDEC, PRORES, MOV) += prores prores_int prores_444 prores_444_int prores_ks
fate-vsynth%-prores:             FMT     = mov

fate-vsynth%-prores_int:         CODEC   = prores
fate-vsynth%-prores_int:         ENCOPTS = -flags +ildct
fate-vsynth%-prores_int:         FMT     = mov

fate-vsynth%-prores_444:         CODEC   = prores
fate-vsynth%-prores_444:         ENCOPTS = -pix_fmt yuv444p10
fate-vsynth%-prores_444:         FMT     = mov

fate-vsynth%-prores_444_int:     CODEC   = prores
fate-vsynth%-prores_444_int:     ENCOPTS = -pix_fmt yuv444p10 -flags +ildct
fate-vsynth%-prores_444_int:     FMT     = mov

fate-vsynth%-prores_ks:          ENCOPTS = -profile hq
fate-vsynth%-prores_ks:          FMT     = mov

FATE_VCODEC-$(call ENCDEC, QTRLE, MOV)  += qtrle qtrlegray
fate-vsynth%-qtrle:              FMT     = mov

fate-vsynth%-qtrlegray:          CODEC   = qtrle
fate-vsynth%-qtrlegray:          ENCOPTS = -pix_fmt gray
fate-vsynth%-qtrlegray:          FMT     = mov

FATE_VCODEC-$(call ENCDEC, RAWVIDEO, AVI) += rgb bpp1 bpp15
fate-vsynth%-rgb:                CODEC   = rawvideo
fate-vsynth%-rgb:                ENCOPTS = -pix_fmt bgr24
fate-vsynth%-bpp1:               CODEC   = rawvideo
fate-vsynth%-bpp1:               ENCOPTS = -pix_fmt monow
fate-vsynth%-bpp15:              CODEC   = rawvideo
fate-vsynth%-bpp15:              ENCOPTS = -pix_fmt bgr555le

FATE_VCODEC-$(call ENCDEC, RAWVIDEO, MOV) += mov-bgr24 mov-bpp15 mov-bpp16
fate-vsynth%-mov-bgr24:          CODEC   = rawvideo
fate-vsynth%-mov-bgr24:          ENCOPTS = -pix_fmt bgr24
fate-vsynth%-mov-bgr24:          FMT      = mov
fate-vsynth%-mov-bpp15:          CODEC   = rawvideo
fate-vsynth%-mov-bpp15:          ENCOPTS = -pix_fmt rgb555le
fate-vsynth%-mov-bpp15:          FMT      = mov
fate-vsynth%-mov-bpp16:          CODEC   = rawvideo
fate-vsynth%-mov-bpp16:          ENCOPTS = -pix_fmt rgb565le
fate-vsynth%-mov-bpp16:          FMT      = mov

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
fate-vsynth%-snow:               ENCOPTS = -qscale 2 -flags +qpel \
                                           -motion_est iter -dia_size 2      \
                                           -cmp 12 -subcmp 12 -s 128x64

fate-vsynth%-snow-hpel:          ENCOPTS = -qscale 2              \
                                           -motion_est iter -dia_size 2      \
                                           -cmp 12 -subcmp 12 -s 128x64

fate-vsynth%-snow-ll:            ENCOPTS = -qscale .001 -pred 1 \
                                           -flags +mv4+qpel

FATE_VCODEC-$(call ENCDEC, SVQ1, MOV)   += svq1
fate-vsynth%-svq1:               ENCOPTS = -qscale 3 -pix_fmt yuv410p
fate-vsynth%-svq1:               FMT     = mov

FATE_VCODEC-$(call ENCDEC, R210, AVI)   += r210

FATE_VCODEC-$(call ENCDEC, V210, AVI)   += v210 v210-10
fate-vsynth%-v210-10:            ENCOPTS = -pix_fmt yuv422p10

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

FATE_VCODEC-$(call ENCDEC, XFACE, NUT) += xface
fate-vsynth%-xface:              ENCOPTS = -s 48x48 -sws_flags neighbor+bitexact
fate-vsynth%-xface:              DECOPTS = -sws_flags neighbor+bitexact
fate-vsynth%-xface:              FMT = nut

FATE_VCODEC-$(call ENCDEC, YUV4, AVI) += yuv4

FATE_VCODEC-$(call ENCDEC, Y41P, AVI) += y41p

FATE_VCODEC-$(call ENCDEC, ZLIB, AVI) += zlib

FATE_VCODEC += $(FATE_VCODEC-yes)
FATE_VSYNTH1 = $(FATE_VCODEC:%=fate-vsynth1-%)
FATE_VSYNTH2 = $(FATE_VCODEC:%=fate-vsynth2-%)
FATE_VSYNTH_LENA = $(FATE_VCODEC:%=fate-vsynth_lena-%)
# Redundant tests because they just resize the input
RESIZE_OFF   = dnxhd-720p dnxhd-720p-rd dnxhd-720p-10bit dnxhd-1080i \
               dv dv-411 dv-50 avui snow snow-hpel snow-ll vc2-420p \
               vc2-420p10 vc2-420p12 vc2-422p vc2-422p10 vc2-422p12 \
               vc2-444p vc2-444p10 vc2-444p12 vc2-thaar vc2-t5_3
# Incorrect parameters - usually size or color format restrictions
INC_PAR_OFF  = cinepak h261 h261-trellis h263 h263p h263-obmc msvideo1 \
               roqvideo rv10 rv20 y41p qtrlegray
VSYNTH3_OFF  = $(RESIZE_OFF) $(INC_PAR_OFF)

FATE_VCODEC3 = $(filter-out $(VSYNTH3_OFF),$(FATE_VCODEC))
FATE_VSYNTH3 = $(FATE_VCODEC3:%=fate-vsynth3-%)

$(FATE_VSYNTH1): tests/data/vsynth1.yuv
$(FATE_VSYNTH2): tests/data/vsynth2.yuv
$(FATE_VSYNTH_LENA): tests/data/vsynth_lena.yuv
$(FATE_VSYNTH3): tests/data/vsynth3.yuv

FATE_AVCONV += $(FATE_VSYNTH1) $(FATE_VSYNTH2) $(FATE_VSYNTH3)
FATE_SAMPLES_AVCONV += $(FATE_VSYNTH_LENA)

fate-vsynth1: $(FATE_VSYNTH1)
fate-vsynth2: $(FATE_VSYNTH2)
fate-vsynth_lena: $(FATE_VSYNTH_LENA)
fate-vsynth3: $(FATE_VSYNTH3)
fate-vcodec:  fate-vsynth1 fate-vsynth_lena fate-vsynth2 fate-vsynth3
