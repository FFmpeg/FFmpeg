FATE_4XM += fate-4xm-1
fate-4xm-1: CMD = framecrc -i $(TARGET_SAMPLES)/4xm/version1.4xm -pix_fmt rgb24 -an

FATE_4XM += fate-4xm-2
fate-4xm-2: CMD = framecrc -i $(TARGET_SAMPLES)/4xm/version2.4xm -pix_fmt rgb24 -an

FATE_VIDEO-$(call DEMDEC, FOURXM, FOURXM) += $(FATE_4XM)
fate-4xm: $(FATE_4XM)

FATE_VIDEO-$(call DEMDEC, AVI, AASC) += fate-aasc
fate-aasc: CMD = framecrc -i $(TARGET_SAMPLES)/aasc/AASC-1.5MB.AVI -pix_fmt rgb24

FATE_VIDEO-$(call DEMDEC, MOV, AIC) += fate-aic
fate-aic: CMD = framecrc -idct simple -i $(TARGET_SAMPLES)/aic/small_apple_intermediate_codec.mov -an -vframes 15

FATE_VIDEO-$(call DEMDEC, MOV, AIC) += fate-aic-oddsize
fate-aic-oddsize: CMD = framecrc -idct simple -i $(TARGET_SAMPLES)/aic/aic_odd_dimensions.mov

FATE_VIDEO-$(call DEMDEC, MM, MMVIDEO) += fate-alg-mm
fate-alg-mm: CMD = framecrc -i $(TARGET_SAMPLES)/alg-mm/ibmlogo.mm -an -pix_fmt rgb24

FATE_VIDEO-$(call DEMDEC, AVI, AMV) += fate-amv
fate-amv: CMD = framecrc -idct simple -i $(TARGET_SAMPLES)/amv/MTV_high_res_320x240_sample_Penguin_Joke_MTV_from_WMV.amv -t 10 -an

FATE_VIDEO-$(call DEMDEC, TTY, ANSI) += fate-ansi
fate-ansi: CMD = framecrc -chars_per_frame 44100 -i $(TARGET_SAMPLES)/ansi/TRE-IOM5.ANS -pix_fmt rgb24

FATE_VIDEO-$(call DEMDEC, TTY, ANSI) += fate-ansi256
fate-ansi256: CMD = framecrc -chars_per_frame 44100 -i $(TARGET_SAMPLES)/ansi/ansi256.ans -pix_fmt rgb24

FATE_VIDEO-$(call DEMDEC, RPL, ESCAPE124) += fate-armovie-escape124
fate-armovie-escape124: CMD = framecrc -i $(TARGET_SAMPLES)/rpl/ESCAPE.RPL -pix_fmt rgb24

FATE_VIDEO-$(call DEMDEC, RPL, ESCAPE130) += fate-armovie-escape130
fate-armovie-escape130: CMD = framecrc -i $(TARGET_SAMPLES)/rpl/landing.rpl -an

FATE_VIDEO-$(call DEMDEC, AVI, AURA) += fate-auravision-v1
fate-auravision-v1: CMD = framecrc -i $(TARGET_SAMPLES)/auravision/SOUVIDEO.AVI -an

FATE_VIDEO-$(call DEMDEC, AVI, AURA2) += fate-auravision-v2
fate-auravision-v2: CMD = framecrc -i $(TARGET_SAMPLES)/auravision/salma-hayek-in-ugly-betty-partial-avi -an

FATE_VIDEO-$(call DEMDEC, BETHSOFTVID, BETHSOFTVID) += fate-bethsoft-vid
fate-bethsoft-vid: CMD = framecrc -i $(TARGET_SAMPLES)/bethsoft-vid/ANIM0001.VID -t 5 -pix_fmt rgb24

FATE_VIDEO-$(call DEMDEC, BFI, BFI) += fate-bfi
fate-bfi: CMD = framecrc -i $(TARGET_SAMPLES)/bfi/2287.bfi -pix_fmt rgb24

FATE_BINK_VIDEO += fate-bink-video-b
fate-bink-video-b: CMD = framecrc -i $(TARGET_SAMPLES)/bink/RISE.BIK -frames 30

FATE_BINK_VIDEO += fate-bink-video-f
fate-bink-video-f: CMD = framecrc -i $(TARGET_SAMPLES)/bink/hol2br.bik

FATE_BINK_VIDEO += fate-bink-video-i
fate-bink-video-i: CMD = framecrc -i $(TARGET_SAMPLES)/bink/RazOnBull.bik -an

FATE_VIDEO-$(call DEMDEC, BINK, BINK) += $(FATE_BINK_VIDEO)

FATE_VIDEO-$(call DEMDEC, BMV, BMV_VIDEO) += fate-bmv-video
fate-bmv-video: CMD = framecrc -i $(TARGET_SAMPLES)/bmv/SURFING-partial.BMV -pix_fmt rgb24 -an

FATE_CANOPUS_HQ_HQA += fate-canopus-hq_hqa-hq
fate-canopus-hq_hqa-hq: CMD = framecrc -i $(TARGET_SAMPLES)/canopus/hq.avi

FATE_CANOPUS_HQ_HQA += fate-canopus-hq_hqa-hqa
fate-canopus-hq_hqa-hqa: CMD = framecrc -i $(TARGET_SAMPLES)/canopus/hqa.avi

FATE_CANOPUS_HQ_HQA += fate-canopus-hq_hqa-inter
fate-canopus-hq_hqa-inter: CMD = framecrc -i $(TARGET_SAMPLES)/canopus/hq25i.avi

FATE_SAMPLES_AVCONV-$(call DEMDEC, AVI, HQ_HQA) += $(FATE_CANOPUS_HQ_HQA)
fate-canopus-hq_hqa: $(FATE_CANOPUS_HQ_HQA)

FATE_CANOPUS_HQX += fate-canopus-hqx422
fate-canopus-hqx422: CMD = framecrc -i $(TARGET_SAMPLES)/canopus/hqx422.avi -pix_fmt yuv422p16be -an

FATE_CANOPUS_HQX += fate-canopus-hqx422a
fate-canopus-hqx422a: CMD = framecrc -i $(TARGET_SAMPLES)/canopus/hqx422a.avi -pix_fmt yuv422p16be -an

FATE_VIDEO-$(call DEMDEC, AVI, HQX) += $(FATE_CANOPUS_HQX)
fate-canopus-hqx: $(FATE_CANOPUS_HQX)

FATE_VIDEO-$(call DEMDEC, MPEGPS, CAVS) += fate-cavs
fate-cavs: CMD = framecrc -i $(TARGET_SAMPLES)/cavs/cavs.mpg -an

FATE_VIDEO-$(call DEMDEC, CDG, CDGRAPHICS) += fate-cdgraphics
fate-cdgraphics: CMD = framecrc -i $(TARGET_SAMPLES)/cdgraphics/BrotherJohn.cdg -pix_fmt rgb24 -t 1

FATE_VIDEO-$(call DEMDEC, AVI, CLJR) += fate-cljr
fate-cljr: CMD = framecrc -i $(TARGET_SAMPLES)/cljr/testcljr-partial.avi

FATE_VIDEO-$(call DEMDEC, AVI, PNG) += fate-corepng
fate-corepng: CMD = framecrc -i $(TARGET_SAMPLES)/png1/corepng-partial.avi

FATE_VIDEO-$(call DEMDEC, AVS, AVS) += fate-creatureshock-avs
fate-creatureshock-avs: CMD = framecrc -i $(TARGET_SAMPLES)/creatureshock-avs/OUTATIME.AVS -pix_fmt rgb24

FATE_CVID-$(CONFIG_MOV_DEMUXER) += fate-cvid-palette
fate-cvid-palette: CMD = framecrc -i $(TARGET_SAMPLES)/cvid/catfight-cvid-pal8-partial.mov -pix_fmt rgb24 -an

FATE_CVID-$(CONFIG_AVI_DEMUXER) += fate-cvid-partial
fate-cvid-partial: CMD = framecrc -i $(TARGET_SAMPLES)/cvid/laracroft-cinepak-partial.avi -an

FATE_CVID-$(CONFIG_AVI_DEMUXER) += fate-cvid-grayscale
fate-cvid-grayscale: CMD = framecrc -i $(TARGET_SAMPLES)/cvid/pcitva15.avi -an

FATE_VIDEO-$(CONFIG_CINEPAK_DECODER) += $(FATE_CVID-yes)
fate-cvid: $(FATE_CVID-yes)

FATE_VIDEO-$(call DEMDEC, C93, C93) += fate-cyberia-c93
fate-cyberia-c93: CMD = framecrc -i $(TARGET_SAMPLES)/cyberia-c93/intro1.c93 -t 3 -pix_fmt rgb24

FATE_VIDEO-$(call DEMDEC, AVI, CYUV) += fate-cyuv
fate-cyuv: CMD = framecrc -i $(TARGET_SAMPLES)/cyuv/cyuv.avi

FATE_VIDEO-$(call DEMDEC, DSICIN, DSICINVIDEO) += fate-delphine-cin-video
fate-delphine-cin-video: CMD = framecrc -i $(TARGET_SAMPLES)/delphine-cin/LOGO-partial.CIN -pix_fmt rgb24 -an

FATE_VIDEO-$(call DEMDEC, ANM, ANM) += fate-deluxepaint-anm
fate-deluxepaint-anm: CMD = framecrc -i $(TARGET_SAMPLES)/deluxepaint-anm/INTRO1.ANM -pix_fmt rgb24

FATE_VIDEO-$(call DEMDEC, DIRAC, DIRAC) += fate-dirac
fate-dirac: CMD = framecrc -i $(TARGET_SAMPLES)/dirac/vts.profile-main.drc

FATE_TRUEMOTION1 += fate-truemotion1-15
fate-truemotion1-15: CMD = framecrc -i $(TARGET_SAMPLES)/duck/phant2-940.duk -pix_fmt rgb24 -an

FATE_TRUEMOTION1 += fate-truemotion1-24
fate-truemotion1-24: CMD = framecrc -i $(TARGET_SAMPLES)/duck/sonic3dblast_intro-partial.avi -pix_fmt rgb24 -an

FATE_VIDEO-$(call DEMDEC, AVI, TRUEMOTION1) += $(FATE_TRUEMOTION1)
fate-truemotion1: $(FATE_TRUEMOTION1)

FATE_VIDEO-$(call DEMDEC, AVI, TRUEMOTION2) += fate-truemotion2
fate-truemotion2: CMD = framecrc -i $(TARGET_SAMPLES)/duck/tm20.avi

FATE_DXA += fate-dxa-feeble
fate-dxa-feeble: CMD = framecrc -i $(TARGET_SAMPLES)/dxa/meetsquid.dxa -t 2 -pix_fmt rgb24 -an

FATE_DXA += fate-dxa-scummvm
fate-dxa-scummvm: CMD = framecrc -i $(TARGET_SAMPLES)/dxa/scummvm.dxa -pix_fmt rgb24

FATE_VIDEO-$(call DEMDEC, DXA, DXA) += $(FATE_DXA)
fate-dxa: $(FATE_DXA)

FATE_VIDEO-$(call DEMDEC, SEGAFILM, CINEPAK) += fate-film-cvid
fate-film-cvid: CMD = framecrc -i $(TARGET_SAMPLES)/film/logo-capcom.cpk -an

FATE_FLIC += fate-flic-af11-palette-change
fate-flic-af11-palette-change: CMD = framecrc -i $(TARGET_SAMPLES)/fli/fli-engines.fli -t 3.31 -pix_fmt rgb24

FATE_FLIC += fate-flic-af12
fate-flic-af12: CMD = framecrc -i $(TARGET_SAMPLES)/fli/jj00c2.fli -pix_fmt rgb24

FATE_FLIC += fate-flic-magiccarpet
fate-flic-magiccarpet: CMD = framecrc -i $(TARGET_SAMPLES)/fli/intel.dat -pix_fmt rgb24

FATE_VIDEO-$(call DEMDEC, FLIC, FLIC) += $(FATE_FLIC)
fate-flic: $(FATE_FLIC)

FATE_VIDEO-$(call DEMDEC, AVI, FRWU) += fate-frwu
fate-frwu: CMD = framecrc -i $(TARGET_SAMPLES)/frwu/frwu.avi

FATE_VIDEO-$(call DEMDEC, IDCIN, IDCIN) += fate-id-cin-video
fate-id-cin-video: CMD = framecrc -i $(TARGET_SAMPLES)/idcin/idlog-2MB.cin -pix_fmt rgb24

FATE_VIDEO-$(call ENCDEC, ROQ PGMYUV, ROQ IMAGE2) += fate-idroq-video-encode
fate-idroq-video-encode: CMD = md5 -f image2 -vcodec pgmyuv -i $(TARGET_SAMPLES)/ffmpeg-synthetic/vsynth1/%02d.pgm -r 30 -sws_flags +bitexact -vf pad=512:512:80:112 -f roq -t 0.2

FATE_IFF-$(CONFIG_IFF_BYTERUN1_DECODER) += fate-iff-byterun1
fate-iff-byterun1: CMD = framecrc -i $(TARGET_SAMPLES)/iff/ASH.LBM -pix_fmt rgb24

FATE_IFF-$(CONFIG_EIGHTSVX_FIB_DECODER) += fate-iff-fibonacci
fate-iff-fibonacci: CMD = md5 -i $(TARGET_SAMPLES)/iff/dasboot-in-compressed -f s16le

FATE_IFF-$(CONFIG_IFF_ILBM_DECODER) += fate-iff-ilbm
fate-iff-ilbm: CMD = framecrc -i $(TARGET_SAMPLES)/iff/lms-matriks.ilbm -pix_fmt rgb24

FATE_VIDEO-$(CONFIG_IFF_DEMUXER)  += $(FATE_IFF-yes)
fate-iff: $(FATE_IFF-yes)

FATE_VIDEO-$(call DEMDEC, IPMOVIE, INTERPLAY_VIDEO) += fate-interplay-mve-8bit
fate-interplay-mve-8bit: CMD = framecrc -i $(TARGET_SAMPLES)/interplay-mve/interplay-logo-2MB.mve -pix_fmt rgb24 -an

FATE_VIDEO-$(call DEMDEC, IPMOVIE, INTERPLAY_VIDEO) += fate-interplay-mve-16bit
fate-interplay-mve-16bit: CMD = framecrc -i $(TARGET_SAMPLES)/interplay-mve/descent3-level5-16bit-partial.mve -pix_fmt rgb24 -an

FATE_VIDEO-$(call DEMDEC, MXF, JPEG2000) += fate-jpeg2000-dcinema
fate-jpeg2000-dcinema: CMD = framecrc -flags +bitexact -vcodec jpeg2000 -i $(TARGET_SAMPLES)/jpeg2000/chiens_dcinema2K.mxf -pix_fmt xyz12le

FATE_VIDEO-$(call DEMDEC, JV, JV) += fate-jv
fate-jv: CMD = framecrc -i $(TARGET_SAMPLES)/jv/intro.jv -an -pix_fmt rgb24

FATE_VIDEO-$(call DEMDEC, AVI, KGV1) += fate-kgv1
fate-kgv1: CMD = framecrc -i $(TARGET_SAMPLES)/kega/kgv1.avi -pix_fmt rgb555le -an

FATE_VIDEO-$(call DEMDEC, AVI, KMVC) += fate-kmvc
fate-kmvc: CMD = framecrc -i $(TARGET_SAMPLES)/KMVC/LOGO1.AVI -an -t 3 -pix_fmt rgb24

FATE_VIDEO-$(call DEMDEC, EA, MDEC) += fate-mdec
fate-mdec: CMD = framecrc -idct simple -i $(TARGET_SAMPLES)/ea-dct/NFS2Esprit-partial.dct -an

FATE_VIDEO-$(call DEMDEC, STR, MDEC) += fate-mdec-v3
fate-mdec-v3: CMD = framecrc -idct simple -i $(TARGET_SAMPLES)/psx-str/abc000_cut.str -an

FATE_VIDEO-$(call DEMDEC, MSNWC_TCP, MIMIC) += fate-mimic
fate-mimic: CMD = framecrc -idct simple -i $(TARGET_SAMPLES)/mimic/mimic2-womanloveffmpeg.cam

FATE_VIDEO-$(call DEMDEC, MOV, MJPEGB) += fate-mjpegb
fate-mjpegb: CMD = framecrc -idct simple -fflags +bitexact -i $(TARGET_SAMPLES)/mjpegb/mjpegb_part.mov -an

FATE_VIDEO-$(call DEMDEC, MVI, MOTIONPIXELS) += fate-motionpixels
fate-motionpixels: CMD = framecrc -i $(TARGET_SAMPLES)/motion-pixels/INTRO-partial.MVI -an -pix_fmt rgb24 -vframes 111

FATE_VIDEO-$(call DEMDEC, MPEGTS, MPEG2VIDEO) += fate-mpeg2-field-enc
fate-mpeg2-field-enc: CMD = framecrc -flags +bitexact -idct simple -i $(TARGET_SAMPLES)/mpeg2/mpeg2_field_encoding.ts -an -vframes 30

FATE_VIDEO-$(call DEMDEC, MV, MVC1) += fate-mv-mvc1
fate-mv-mvc1: CMD = framecrc -i $(TARGET_SAMPLES)/mv/posture.mv -an -frames 25 -pix_fmt rgb555le

FATE_VIDEO-$(call DEMDEC, MV, MVC2) += fate-mv-mvc2
fate-mv-mvc2: CMD = framecrc -i $(TARGET_SAMPLES)/mv/12345.mv -an -frames 30 -pix_fmt bgra

FATE_VIDEO-$(call DEMDEC, MV, SGIRLE) += fate-mv-sgirle
fate-mv-sgirle: CMD = framecrc -i $(TARGET_SAMPLES)/mv/pet-rle.movie -an

FATE_VIDEO-$(call DEMDEC, MXG, MXPEG) += fate-mxpeg
fate-mxpeg: CMD = framecrc -idct simple -flags +bitexact -i $(TARGET_SAMPLES)/mxpeg/m1.mxg -an

# FIXME dropped frames in this test because of coarse timebase
FATE_NUV += fate-nuv-rtjpeg
fate-nuv-rtjpeg: CMD = framecrc -idct simple -i $(TARGET_SAMPLES)/nuv/Today.nuv -an

FATE_NUV += fate-nuv-rtjpeg-fh
fate-nuv-rtjpeg-fh: CMD = framecrc -idct simple -i $(TARGET_SAMPLES)/nuv/rtjpeg_frameheader.nuv -an

FATE_VIDEO-$(call DEMDEC, NUV, NUV) += $(FATE_NUV)
fate-nuv: $(FATE_NUV)

FATE_VIDEO-$(call DEMDEC, PAF, PAF_VIDEO) += fate-paf-video
fate-paf-video: CMD = framecrc -i $(TARGET_SAMPLES)/paf/hod1-partial.paf -pix_fmt rgb24 -an

FATE_VIDEO-$(call DEMDEC, AVI, QPEG) += fate-qpeg
fate-qpeg: CMD = framecrc -i $(TARGET_SAMPLES)/qpeg/Clock.avi -an -pix_fmt rgb24

FATE_VIDEO-$(call DEMDEC, AVI, R210) += fate-r210
fate-r210: CMD = framecrc -i $(TARGET_SAMPLES)/r210/r210.avi -pix_fmt rgb48le

FATE_VIDEO-$(call DEMDEC, RL2, RL2) += fate-rl2
fate-rl2: CMD = framecrc -i $(TARGET_SAMPLES)/rl2/Z4915300.RL2 -pix_fmt rgb24 -an

FATE_VIDEO-$(call DEMDEC, ROQ, ROQ) += fate-roqvideo
fate-roqvideo: CMD = framecrc -i $(TARGET_SAMPLES)/idroq/idlogo.roq -an

FATE_VIDEO-$(call DEMDEC, SMUSH, SANM) += fate-sanm
fate-sanm: CMD = framecrc -i $(TARGET_SAMPLES)/smush/ronin_part.znm -an -pix_fmt rgb24

FATE_VIDEO-$(call DEMDEC, VMD, VMDVIDEO) += fate-sierra-vmd-video
fate-sierra-vmd-video: CMD = framecrc -i $(TARGET_SAMPLES)/vmd/12.vmd -pix_fmt rgb24 -an

FATE_VIDEO-$(call DEMDEC, SMACKER, SMACKER) += fate-smacker-video
fate-smacker-video: CMD = framecrc -i $(TARGET_SAMPLES)/smacker/wetlogo.smk -pix_fmt rgb24 -an

FATE_VIDEO-$(call DEMDEC, MOV, SMC) += fate-smc
fate-smc: CMD = framecrc -i $(TARGET_SAMPLES)/smc/cass_schi.qt -pix_fmt rgb24

FATE_VIDEO-$(call DEMDEC, AVI, SP5X) += fate-sp5x
fate-sp5x: CMD = framecrc -idct simple -i $(TARGET_SAMPLES)/sp5x/sp5x_problem.avi

FATE_VIDEO-$(call DEMDEC, THP, THP) += fate-thp
fate-thp: CMD = framecrc -idct simple -i $(TARGET_SAMPLES)/thp/pikmin2-opening1-partial.thp -an

FATE_VIDEO-$(call DEMDEC, TIERTEXSEQ, TIERTEXSEQVIDEO) += fate-tiertex-seq
fate-tiertex-seq: CMD = framecrc -i $(TARGET_SAMPLES)/tiertex-seq/Gameover.seq -pix_fmt rgb24

FATE_VIDEO-$(call DEMDEC, TMV, TMV) += fate-tmv
fate-tmv: CMD = framecrc -i $(TARGET_SAMPLES)/tmv/pop-partial.tmv -pix_fmt rgb24

FATE_TXD += fate-txd-16bpp
fate-txd-16bpp: CMD = framecrc -i $(TARGET_SAMPLES)/txd/misc.txd -pix_fmt bgra -an

FATE_TXD += fate-txd-pal8
fate-txd-pal8: CMD = framecrc -i $(TARGET_SAMPLES)/txd/outro.txd -pix_fmt rgb24 -an

FATE_VIDEO-$(call DEMDEC, TXD, TXD) += $(FATE_TXD)
fate-txd: $(FATE_TXD)

FATE_VIDEO-$(call DEMDEC, AVI, ULTI) += fate-ulti
fate-ulti: CMD = framecrc -i $(TARGET_SAMPLES)/ulti/hit12w.avi -an

FATE_VIDEO-$(call DEMDEC, AVI, V210) += fate-v210
fate-v210: CMD = framecrc -i $(TARGET_SAMPLES)/v210/v210_720p-partial.avi -pix_fmt yuv422p16be -an

FATE_VIDEO-$(call DEMDEC, MOV, V410) += fate-v410dec
fate-v410dec: CMD = framecrc -i $(TARGET_SAMPLES)/v410/lenav410.mov -pix_fmt yuv444p10le

FATE_VIDEO-$(call ENCDEC, V410 PGMYUV, AVI IMAGE2) += fate-v410enc
fate-v410enc: $(VREF)
fate-v410enc: CMD = md5 -f image2 -vcodec pgmyuv -i $(TARGET_PATH)/tests/vsynth1/%02d.pgm -fflags +bitexact -vcodec v410 -f avi

FATE_VIDEO-$(call DEMDEC, SIFF, VB) += fate-vb
fate-vb: CMD = framecrc -i $(TARGET_SAMPLES)/SIFF/INTRO_B.VB -t 3 -pix_fmt rgb24 -an

FATE_VIDEO-$(call DEMDEC, AVI, VCR1) += fate-vcr1
fate-vcr1: CMD = framecrc -i $(TARGET_SAMPLES)/vcr1/VCR1test.avi -an

FATE_VIDEO-$(call DEMDEC, AVI, MPEG2VIDEO) += fate-vcr2
fate-vcr2: CMD = framecrc -flags +bitexact -idct simple -i $(TARGET_SAMPLES)/vcr2/VCR2test.avi -an

FATE_VIDEO-$(call DEMDEC, AVI, XL) += fate-videoxl
fate-videoxl: CMD = framecrc -i $(TARGET_SAMPLES)/vixl/pig-vixl.avi

FATE_VIDEO-$(call DEMDEC, WSVQA, VQA) += fate-vqa-cc
fate-vqa-cc: CMD = framecrc -i $(TARGET_SAMPLES)/vqa/cc-demo1-partial.vqa -pix_fmt rgb24 -an

FATE_VIDEO-$(call DEMDEC, WC3, XAN_WC3) += fate-wc3movie-xan
fate-wc3movie-xan: CMD = framecrc -i $(TARGET_SAMPLES)/wc3movie/SC_32-part.MVE -pix_fmt rgb24

FATE_VIDEO-$(call DEMDEC, AVI, WNV1) += fate-wnv1
fate-wnv1: CMD = framecrc -i $(TARGET_SAMPLES)/wnv1/wnv1-codec.avi -an

FATE_VIDEO-$(call DEMDEC, YOP, YOP) += fate-yop
fate-yop: CMD = framecrc -i $(TARGET_SAMPLES)/yop/test1.yop -pix_fmt rgb24 -an

FATE_VIDEO-$(call DEMDEC, AVI, XAN_WC4) += fate-xxan-wc4
fate-xxan-wc4: CMD = framecrc -i $(TARGET_SAMPLES)/wc4-xan/wc4trailer-partial.avi -an

FATE_VIDEO-$(call DEMDEC, WAV, SMVJPEG) += fate-smvjpeg
fate-smvjpeg: CMD = framecrc -idct simple -flags +bitexact -i $(TARGET_SAMPLES)/smv/clock.smv -an

FATE_VIDEO += $(FATE_VIDEO-yes)

FATE_SAMPLES_FFMPEG += $(FATE_VIDEO)
fate-video: $(FATE_VIDEO)
