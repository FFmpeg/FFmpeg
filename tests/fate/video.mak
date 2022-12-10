FATE_4XM += fate-4xm-1
fate-4xm-1: CMD = framecrc -i $(TARGET_SAMPLES)/4xm/version1.4xm -pix_fmt rgb24 -an -vf scale

FATE_4XM += fate-4xm-2
fate-4xm-2: CMD = framecrc -i $(TARGET_SAMPLES)/4xm/version2.4xm -pix_fmt rgb24 -an -vf scale

FATE_4XM-$(call FRAMECRC, FOURXM, FOURXM, SCALE_FILTER) += $(FATE_4XM)
FATE_VIDEO += $(FATE_4XM-yes)
fate-4xm: $(FATE_4XM-yes)

FATE_VIDEO-$(call FRAMECRC, AVI, ZERO12V, SCALE_FILTER) += fate-012v
fate-012v: CMD = framecrc -i $(TARGET_SAMPLES)/012v/sample.avi -pix_fmt yuv422p16le -vf scale

FATE_VIDEO-$(call FRAMECRC, AVI, AASC, SCALE_FILTER) += fate-aasc
fate-aasc: CMD = framecrc -i $(TARGET_SAMPLES)/aasc/AASC-1.5MB.AVI -pix_fmt rgb24 -vf scale

FATE_VIDEO-$(call FRAMECRC, MOV, AIC) += fate-aic
fate-aic: CMD = framecrc -idct simple -i $(TARGET_SAMPLES)/aic/small_apple_intermediate_codec.mov -an -frames:v 15

FATE_VIDEO-$(call FRAMECRC, MOV, AIC) += fate-aic-oddsize
fate-aic-oddsize: CMD = framecrc -idct simple -i $(TARGET_SAMPLES)/aic/aic_odd_dimensions.mov

FATE_VIDEO-$(call FRAMECRC, MM, MMVIDEO, SCALE_FILTER) += fate-alg-mm
fate-alg-mm: CMD = framecrc -i $(TARGET_SAMPLES)/alg-mm/ibmlogo.mm -an -pix_fmt rgb24 -vf scale

FATE_VIDEO-$(call FRAMECRC, AVI, AMV) += fate-amv
fate-amv: CMD = framecrc -idct simple -i $(TARGET_SAMPLES)/amv/MTV_high_res_320x240_sample_Penguin_Joke_MTV_from_WMV.amv -t 10 -an

FATE_VIDEO-$(call FRAMECRC, TTY, ANSI, SCALE_FILTER) += fate-ansi
fate-ansi: CMD = framecrc -chars_per_frame 44100 -i $(TARGET_SAMPLES)/ansi/TRE-IOM5.ANS -pix_fmt rgb24 -vf scale

FATE_VIDEO-$(call FRAMECRC, TTY, ANSI, SCALE_FILTER) += fate-ansi256
fate-ansi256: CMD = framecrc -chars_per_frame 44100 -i $(TARGET_SAMPLES)/ansi/ansi256.ans -pix_fmt rgb24 -vf scale

FATE_VIDEO-$(call FRAMECRC, RPL, ESCAPE124, ARESAMPLE_FILTER SCALE_FILTER) += fate-armovie-escape124
fate-armovie-escape124: CMD = framecrc -i $(TARGET_SAMPLES)/rpl/ESCAPE.RPL -pix_fmt rgb24 -vf scale -af aresample

FATE_VIDEO-$(call FRAMECRC, RPL, ESCAPE130) += fate-armovie-escape130
fate-armovie-escape130: CMD = framecrc -i $(TARGET_SAMPLES)/rpl/landing.rpl -an

FATE_VIDEO-$(call FRAMECRC, AVI, AURA) += fate-auravision-v1
fate-auravision-v1: CMD = framecrc -i $(TARGET_SAMPLES)/auravision/SOUVIDEO.AVI -an

FATE_VIDEO-$(call FRAMECRC, AVI, AURA2) += fate-auravision-v2
fate-auravision-v2: CMD = framecrc -i $(TARGET_SAMPLES)/auravision/salma-hayek-in-ugly-betty-partial-avi -an

FATE_VIDEO-$(call FRAMECRC, AVI, AVRN) += fate-avid-interlaced
fate-avid-interlaced: CMD = framecrc -i $(TARGET_SAMPLES)/avid/avid_ntsc_interlaced.avi

FATE_VIDEO-$(call FRAMECRC, MOV, MJPEG) += fate-avid-meridian
fate-avid-meridian: CMD = framecrc -i $(TARGET_SAMPLES)/avid/avidmeridianntsc.mov

FATE_VIDEO-$(call FRAMECRC, BETHSOFTVID, BETHSOFTVID, ARESAMPLE_FILTER SCALE_FILTER) += fate-bethsoft-vid
fate-bethsoft-vid: CMD = framecrc -i $(TARGET_SAMPLES)/bethsoft-vid/ANIM0001.VID -t 5 -pix_fmt rgb24 -vf scale -af aresample

FATE_VIDEO-$(call FRAMECRC, BFI, BFI, ARESAMPLE_FILTER SCALE_FILTER) += fate-bfi
fate-bfi: CMD = framecrc -i $(TARGET_SAMPLES)/bfi/2287.bfi -pix_fmt rgb24 -vf scale -af aresample

FATE_BINK_VIDEO += fate-bink-video-b
fate-bink-video-b: CMD = framecrc -i $(TARGET_SAMPLES)/bink/RISE.BIK -frames 30

FATE_BINK_VIDEO += fate-bink-video-f
fate-bink-video-f: CMD = framecrc -i $(TARGET_SAMPLES)/bink/hol2br.bik

FATE_BINK_VIDEO += fate-bink-video-i
fate-bink-video-i: CMD = framecrc -i $(TARGET_SAMPLES)/bink/RazOnBull.bik -an

FATE_VIDEO-$(call FRAMECRC, BINK, BINK) += $(FATE_BINK_VIDEO)

FATE_VIDEO-$(call FRAMECRC, BMV, BMV_VIDEO, SCALE_FILTER) += fate-bmv-video
fate-bmv-video: CMD = framecrc -i $(TARGET_SAMPLES)/bmv/SURFING-partial.BMV -pix_fmt rgb24 -an -vf scale

FATE_VIDEO-$(call FRAMECRC, MPEGPS, CAVS) += fate-cavs
fate-cavs: CMD = framecrc -i $(TARGET_SAMPLES)/cavs/cavs.mpg -an

FATE_VIDEO-$(call FRAMECRC, CDG, CDGRAPHICS, SCALE_FILTER) += fate-cdgraphics
fate-cdgraphics: CMD = framecrc -i $(TARGET_SAMPLES)/cdgraphics/BrotherJohn.cdg -pix_fmt rgba -t 1 -vf scale

FATE_CFHD-$(call FRAMECRC, AVI, CFHD, SCALE_FILTER) += fate-cfhd-1
fate-cfhd-1: CMD = framecrc -i $(TARGET_SAMPLES)/cfhd/cfhd_422.avi -pix_fmt yuv422p10le -vf scale

FATE_CFHD-$(call FRAMECRC, AVI, CFHD, SCALE_FILTER) += fate-cfhd-2
fate-cfhd-2: CMD = framecrc -i $(TARGET_SAMPLES)/cfhd/cfhd_444.avi -pix_fmt gbrp12le -vf scale

FATE_CFHD-$(call FRAMECRC, MOV, CFHD, SCALE_FILTER) += fate-cfhd-3
fate-cfhd-3: CMD = framecrc -i $(TARGET_SAMPLES)/cfhd/cfhd_odd.mov -pix_fmt yuv422p10le -vf scale

FATE_VIDEO += $(FATE_CFHD-yes)
fate-cfhd: $(FATE_CFHD-yes)

FATE_VIDEO-$(call FRAMECRC, AVI, CLJR) += fate-cljr
fate-cljr: CMD = framecrc -i $(TARGET_SAMPLES)/cljr/testcljr-partial.avi

FATE_VIDEO-$(call FRAMECRC, AVI, PNG, ARESAMPLE_FILTER) += fate-corepng
fate-corepng: CMD = framecrc -i $(TARGET_SAMPLES)/png1/corepng-partial.avi -af aresample

FATE_VIDEO-$(call FRAMECRC, AVI, PNG) += fate-rgbapng-4816
fate-rgbapng-4816: CMD = framecrc -i $(TARGET_SAMPLES)/png1/55c99e750a5fd6_50314226.png

FATE_VIDEO-$(call FRAMECRC, AVS, AVS, ARESAMPLE_FILTER SCALE_FILTER) += fate-creatureshock-avs
fate-creatureshock-avs: CMD = framecrc -i $(TARGET_SAMPLES)/creatureshock-avs/OUTATIME.AVS -pix_fmt rgb24 -vf scale -af aresample

FATE_CVID-$(call FRAMECRC, MOV, CINEPAK, SCALE_FILTER) += fate-cvid-palette
fate-cvid-palette: CMD = framecrc -i $(TARGET_SAMPLES)/cvid/catfight-cvid-pal8-partial.mov -pix_fmt rgb24 -an -vf scale

FATE_CVID-$(call FRAMECRC, AVI, CINEPAK) += fate-cvid-partial
fate-cvid-partial: CMD = framecrc -i $(TARGET_SAMPLES)/cvid/laracroft-cinepak-partial.avi -an

FATE_CVID-$(call FRAMECRC, AVI, CINEPAK) += fate-cvid-grayscale
fate-cvid-grayscale: CMD = framecrc -i $(TARGET_SAMPLES)/cvid/pcitva15.avi -an

FATE_VIDEO += $(FATE_CVID-yes)
fate-cvid: $(FATE_CVID-yes)

FATE_VIDEO-$(call FRAMECRC, C93, C93, SCALE_FILTER ARESAMPLE_FILTER) += fate-cyberia-c93
fate-cyberia-c93: CMD = framecrc -i $(TARGET_SAMPLES)/cyberia-c93/intro1.c93 -t 3 -pix_fmt rgb24 -vf scale -af aresample

FATE_VIDEO-$(call FRAMECRC, AVI, CYUV) += fate-cyuv
fate-cyuv: CMD = framecrc -i $(TARGET_SAMPLES)/cyuv/cyuv.avi

FATE_VIDEO-$(call FRAMECRC, DSICIN, DSICINVIDEO, SCALE_FILTER) += fate-delphine-cin-video
fate-delphine-cin-video: CMD = framecrc -i $(TARGET_SAMPLES)/delphine-cin/LOGO-partial.CIN -pix_fmt rgb24 -an -vf scale

FATE_VIDEO-$(call FRAMECRC, ANM, ANM, SCALE_FILTER) += fate-deluxepaint-anm
fate-deluxepaint-anm: CMD = framecrc -i $(TARGET_SAMPLES)/deluxepaint-anm/INTRO1.ANM -pix_fmt rgb24 -vf scale

FATE_VIDEO-$(call FRAMECRC, DIRAC, DIRAC) += fate-dirac
fate-dirac: CMD = framecrc -i $(TARGET_SAMPLES)/dirac/vts.profile-main.drc

FATE_VIDEO-$(call FRAMECRC, DIRAC, DIRAC) += fate-dirac-low-delay
fate-dirac-low-delay: CMD = framecrc -i $(TARGET_SAMPLES)/dirac/vts.profile-vc2-low-delay.drc

FATE_DXA += fate-dxa-feeble
fate-dxa-feeble: CMD = framecrc -i $(TARGET_SAMPLES)/dxa/meetsquid.dxa -t 2 -pix_fmt rgb24 -an -vf scale

FATE_DXA += fate-dxa-scummvm
fate-dxa-scummvm: CMD = framecrc -i $(TARGET_SAMPLES)/dxa/scummvm.dxa -pix_fmt rgb24 -vf scale

FATE_DXA-$(call FRAMECRC, DXA, DXA, SCALE_FILTER) += $(FATE_DXA)
FATE_VIDEO += $(FATE_DXA-yes)
fate-dxa: $(FATE_DXA-yes)

FATE_DXV += fate-dxv-dxt1
fate-dxv-dxt1: CMD = framecrc -i $(TARGET_SAMPLES)/dxv/dxv-na.mov

FATE_DXV += fate-dxv-dxt5
fate-dxv-dxt5: CMD = framecrc -i $(TARGET_SAMPLES)/dxv/dxv-wa.mov

FATE_DXV += fate-dxv3-dxt1
fate-dxv3-dxt1: CMD = framecrc -i $(TARGET_SAMPLES)/dxv/dxv3-nqna.mov

FATE_DXV += fate-dxv3-dxt5
fate-dxv3-dxt5: CMD = framecrc -i $(TARGET_SAMPLES)/dxv/dxv3-nqwa.mov

FATE_VIDEO-$(call FRAMECRC, MOV, DXV) += $(FATE_DXV)
fate-dxv: $(FATE_DXV)

FATE_VIDEO-$(call FRAMECRC, SEGAFILM, CINEPAK) += fate-film-cvid
fate-film-cvid: CMD = framecrc -i $(TARGET_SAMPLES)/film/logo-capcom.cpk -an

FATE_FLIC += fate-flic-af11-palette-change
fate-flic-af11-palette-change: CMD = framecrc -i $(TARGET_SAMPLES)/fli/fli-engines.fli -t 3.31 -pix_fmt rgb24 -vf scale

FATE_FLIC += fate-flic-af12
fate-flic-af12: CMD = framecrc -i $(TARGET_SAMPLES)/fli/jj00c2.fli -pix_fmt rgb24 -vf scale

FATE_FLIC += fate-flic-magiccarpet
fate-flic-magiccarpet: CMD = framecrc -i $(TARGET_SAMPLES)/fli/intel.dat -pix_fmt rgb24 -vf scale

FATE_FLIC-$(call FRAMECRC, FLIC, FLIC, SCALE_FILTER) += $(FATE_FLIC)
FATE_VIDEO += $(FATE_FLIC-yes)
fate-flic: $(FATE_FLIC-yes)

FATE_VIDEO-$(call FRAMECRC, AVI, FRWU) += fate-frwu
fate-frwu: CMD = framecrc -i $(TARGET_SAMPLES)/frwu/frwu.avi

FATE_VIDEO-$(call FRAMECRC, IDCIN, IDCIN, SCALE_FILTER) += fate-id-cin-video
fate-id-cin-video: CMD = framecrc -i $(TARGET_SAMPLES)/idcin/idlog-2MB.cin -pix_fmt rgb24 -vf scale

FATE_VIDEO-$(call ENCDEC, ROQ PGMYUV, ROQ IMAGE2, SCALE_FILTER) += fate-idroq-video-encode
fate-idroq-video-encode: CMD = md5 -auto_conversion_filters -f image2 -c:v pgmyuv -i $(TARGET_SAMPLES)/ffmpeg-synthetic/vsynth1/%02d.pgm -r 30 -sws_flags +bitexact -vf pad=512:512:80:112 -f roq -t 0.2

FATE_IFF-$(call FRAMECRC, IFF, IFF_ILBM, SCALE_FILTER) += fate-iff-byterun1
fate-iff-byterun1: CMD = framecrc -i $(TARGET_SAMPLES)/iff/ASH.LBM -pix_fmt rgb24 -vf scale

FATE_IFF-$(call ENCDEC, PCM_S16LE EIGHTSVX_FIB, PCM_S16LE IFF, ARESAMPLE_FILTER) += fate-iff-fibonacci
fate-iff-fibonacci: CMD = md5 -i $(TARGET_SAMPLES)/iff/dasboot-in-compressed -f s16le -af aresample

FATE_IFF-$(call FRAMECRC, IFF, IFF_ILBM, SCALE_FILTER) += fate-iff-ilbm
fate-iff-ilbm: CMD = framecrc -i $(TARGET_SAMPLES)/iff/lms-matriks.ilbm -pix_fmt rgb24 -vf scale

FATE_VIDEO-$(CONFIG_IFF_DEMUXER)  += $(FATE_IFF-yes)
fate-iff: $(FATE_IFF-yes)

FATE_VIDEO-$(call FRAMECRC, IPMOVIE, INTERPLAY_VIDEO, SCALE_FILTER) += fate-interplay-mve-8bit
fate-interplay-mve-8bit: CMD = framecrc -i $(TARGET_SAMPLES)/interplay-mve/interplay-logo-2MB.mve -pix_fmt rgb24 -an -vf scale

FATE_VIDEO-$(call FRAMECRC, IPMOVIE, INTERPLAY_VIDEO, SCALE_FILTER) += fate-interplay-mve-16bit
fate-interplay-mve-16bit: CMD = framecrc -i $(TARGET_SAMPLES)/interplay-mve/descent3-level5-16bit-partial.mve -pix_fmt rgb24 -an -vf scale

FATE_VIDEO-$(call FRAMECRC, MXF, JPEG2000, SCALE_FILTER) += fate-jpeg2000-dcinema
fate-jpeg2000-dcinema: CMD = framecrc -flags +bitexact -c:v jpeg2000 -i $(TARGET_SAMPLES)/jpeg2000/chiens_dcinema2K.mxf -pix_fmt xyz12le -vf scale

FATE_VIDEO-$(call FRAMECRC, JV, JV, SCALE_FILTER) += fate-jv
fate-jv: CMD = framecrc -i $(TARGET_SAMPLES)/jv/intro.jv -an -pix_fmt rgb24 -vf scale

FATE_VIDEO-$(call FRAMECRC, AVI, KGV1, SCALE_FILTER) += fate-kgv1
fate-kgv1: CMD = framecrc -i $(TARGET_SAMPLES)/kega/kgv1.avi -pix_fmt rgb555le -an -vf scale

FATE_VIDEO-$(call FRAMECRC, AVI, KMVC, SCALE_FILTER) += fate-kmvc
fate-kmvc: CMD = framecrc -i $(TARGET_SAMPLES)/KMVC/LOGO1.AVI -an -t 3 -pix_fmt rgb24 -vf scale

FATE_VIDEO-$(call FRAMECRC, AVI, LSCR) += fate-lscr
fate-lscr: CMD = framecrc -i $(TARGET_SAMPLES)/lscr/lscr_compr9_short.avi

FATE_MAGICYUV += fate-magicyuv-y4444i \
                 fate-magicyuv-y400i  \
                 fate-magicyuv-y420   \
                 fate-magicyuv-y422i  \
                 fate-magicyuv-y444   \
                 fate-magicyuv-rgba   \
                 fate-magicyuv-rgb

FATE_VIDEO-$(call FRAMECRC, AVI, MAGICYUV) += $(FATE_MAGICYUV)
fate-magicyuv: $(FATE_MAGICYUV)

fate-magicyuv-rgb:    CMD = framecrc -i $(TARGET_SAMPLES)/magy/magy_rgb_median.avi
fate-magicyuv-rgba:   CMD = framecrc -i $(TARGET_SAMPLES)/magy/magy_rgba_gradient.avi
fate-magicyuv-y400i:  CMD = framecrc -i $(TARGET_SAMPLES)/magy/magy_yuv400_gradient_interlaced.avi
fate-magicyuv-y420:   CMD = framecrc -i $(TARGET_SAMPLES)/magy/magy_yuv420_median.avi
fate-magicyuv-y422i:  CMD = framecrc -i $(TARGET_SAMPLES)/magy/magy_yuv422_median_interlaced.avi
fate-magicyuv-y4444i: CMD = framecrc -i $(TARGET_SAMPLES)/magy/magy_yuv4444_left_interlaced.avi
fate-magicyuv-y444:   CMD = framecrc -i $(TARGET_SAMPLES)/magy/magy_yuv444_left.avi

FATE_VIDEO-$(call FRAMECRC, EA, MDEC) += fate-mdec
fate-mdec: CMD = framecrc -idct simple -i $(TARGET_SAMPLES)/ea-dct/NFS2Esprit-partial.dct -an

FATE_VIDEO-$(call FRAMECRC, STR, MDEC) += fate-mdec-v3
fate-mdec-v3: CMD = framecrc -idct simple -i $(TARGET_SAMPLES)/psx-str/abc000_cut.str -an

FATE_VIDEO-$(call FRAMECRC, MSNWC_TCP, MIMIC) += fate-mimic
fate-mimic: CMD = framecrc -idct simple -i $(TARGET_SAMPLES)/mimic/mimic2-womanloveffmpeg.cam

FATE_VIDEO-$(call FRAMECRC, MOV, MJPEGB) += fate-mjpegb
fate-mjpegb: CMD = framecrc -idct simple -fflags +bitexact -i $(TARGET_SAMPLES)/mjpegb/mjpegb_part.mov -an

FATE_VIDEO-$(call FRAMECRC, AVI, MJPEG) += fate-mjpeg-ticket3229
fate-mjpeg-ticket3229: CMD = framecrc -idct simple -fflags +bitexact -i $(TARGET_SAMPLES)/mjpeg/mjpeg_field_order.avi -an

FATE_VIDEO-$(call FRAMECRC, MVI, MOTIONPIXELS, SCALE_FILTER) += fate-motionpixels
fate-motionpixels: CMD = framecrc -i $(TARGET_SAMPLES)/motion-pixels/INTRO-partial.MVI -an -pix_fmt rgb24 -frames:v 111 -vf scale

FATE_VIDEO-$(call FRAMECRC, MPEGTS, MPEG2VIDEO) += fate-mpeg2-field-enc fate-mpeg2-ticket186
fate-mpeg2-field-enc: CMD = framecrc -flags +bitexact -idct simple -i $(TARGET_SAMPLES)/mpeg2/mpeg2_field_encoding.ts -an -frames:v 30
fate-mpeg2-ticket186: CMD = framecrc -flags +bitexact -idct simple -i $(TARGET_SAMPLES)/mpeg2/t.mpg -an

FATE_VIDEO-$(call FRAMECRC, MPEGVIDEO, MPEG2VIDEO) += fate-mpeg2-ticket6677
fate-mpeg2-ticket6677: CMD = framecrc -flags +bitexact -idct simple -i $(TARGET_SAMPLES)/mpeg2/sony-ct3.bs

FATE_VIDEO-$(call FRAMECRC, MV, MVC1, SCALE_FILTER) += fate-mv-mvc1
fate-mv-mvc1: CMD = framecrc -i $(TARGET_SAMPLES)/mv/posture.mv -an -frames 25 -pix_fmt rgb555le -vf scale

FATE_VIDEO-$(call FRAMECRC, MV, MVC2, SCALE_FILTER) += fate-mv-mvc2
fate-mv-mvc2: CMD = framecrc -i $(TARGET_SAMPLES)/mv/12345.mv -an -frames 30 -pix_fmt bgra -vf scale

FATE_VIDEO-$(call FRAMECRC, MV, SGIRLE) += fate-mv-sgirle
fate-mv-sgirle: CMD = framecrc -i $(TARGET_SAMPLES)/mv/pet-rle.movie -an

FATE_VIDEO-$(call FRAMECRC, MXG, MXPEG) += fate-mxpeg
fate-mxpeg: CMD = framecrc -idct simple -flags +bitexact -i $(TARGET_SAMPLES)/mxpeg/m1.mxg -an

FATE_NUV += fate-nuv-rtjpeg
fate-nuv-rtjpeg: CMD = framecrc -idct simple -i $(TARGET_SAMPLES)/nuv/Today.nuv -an -enc_time_base -1

FATE_NUV += fate-nuv-rtjpeg-fh
fate-nuv-rtjpeg-fh: CMD = framecrc -idct simple -i $(TARGET_SAMPLES)/nuv/rtjpeg_frameheader.nuv -an

FATE_VIDEO-$(call DEMDEC, NUV, NUV) += $(FATE_NUV)
fate-nuv: $(FATE_NUV)

FATE_VIDEO-$(call FRAMECRC, PAF, PAF_VIDEO, SCALE_FILTER) += fate-paf-video
fate-paf-video: CMD = framecrc -i $(TARGET_SAMPLES)/paf/hod1-partial.paf -pix_fmt rgb24 -an -vf scale

FATE_VIDEO-$(call FRAMECRC, AVI, QPEG, SCALE_FILTER) += fate-qpeg
fate-qpeg: CMD = framecrc -i $(TARGET_SAMPLES)/qpeg/Clock.avi -an -pix_fmt rgb24 -vf scale

FATE_VIDEO-$(call FRAMECRC, AVI, R210, SCALE_FILTER) += fate-r210
fate-r210: CMD = framecrc -i $(TARGET_SAMPLES)/r210/r210.avi -pix_fmt rgb48le -vf scale

FATE_VIDEO-$(call FRAMECRC, RL2, RL2, SCALE_FILTER) += fate-rl2
fate-rl2: CMD = framecrc -i $(TARGET_SAMPLES)/rl2/Z4915300.RL2 -pix_fmt rgb24 -an -vf scale

FATE_VIDEO-$(call FRAMECRC, ROQ, ROQ) += fate-roqvideo
fate-roqvideo: CMD = framecrc -i $(TARGET_SAMPLES)/idroq/idlogo.roq -an

FATE_VIDEO-$(call FRAMECRC, SMUSH, SANM, SCALE_FILTER) += fate-sanm
fate-sanm: CMD = framecrc -i $(TARGET_SAMPLES)/smush/ronin_part.znm -an -pix_fmt rgb24 -vf scale

FATE_VIDEO-$(call FRAMECRC, VMD, VMDVIDEO, SCALE_FILTER) += fate-sierra-vmd-video
fate-sierra-vmd-video: CMD = framecrc -i $(TARGET_SAMPLES)/vmd/12.vmd -pix_fmt rgb24 -an -vf scale

FATE_VIDEO-$(call FRAMECRC, SMACKER, SMACKER, SCALE_FILTER) += fate-smacker-video
fate-smacker-video: CMD = framecrc -i $(TARGET_SAMPLES)/smacker/wetlogo.smk -pix_fmt rgb24 -an -vf scale

FATE_VIDEO-$(call FRAMECRC, MOV, SMC, SCALE_FILTER) += fate-smc
fate-smc: CMD = framecrc -i $(TARGET_SAMPLES)/smc/cass_schi.qt -pix_fmt rgb24 -vf scale

FATE_VIDEO-$(call FRAMECRC, AVI, SP5X, ARESAMPLE_FILTER) += fate-sp5x
fate-sp5x: CMD = framecrc -idct simple -i $(TARGET_SAMPLES)/sp5x/sp5x_problem.avi -af aresample

FATE_VIDEO-$(call FRAMECRC, THP, THP) += fate-thp
fate-thp: CMD = framecrc -idct simple -i $(TARGET_SAMPLES)/thp/pikmin2-opening1-partial.thp -an

FATE_VIDEO-$(call FRAMECRC, TIERTEXSEQ, TIERTEXSEQVIDEO, SCALE_FILTER) += fate-tiertex-seq
fate-tiertex-seq: CMD = framecrc -i $(TARGET_SAMPLES)/tiertex-seq/Gameover.seq -pix_fmt rgb24 -vf scale

FATE_VIDEO-$(call FRAMECRC, TMV, TMV, ARESAMPLE_FILTER SCALE_FILTER) += fate-tmv
fate-tmv: CMD = framecrc -i $(TARGET_SAMPLES)/tmv/pop-partial.tmv -pix_fmt rgb24 -vf scale -af aresample

FATE_TXD += fate-txd-16bpp
fate-txd-16bpp: CMD = framecrc -i $(TARGET_SAMPLES)/txd/misc.txd -an

FATE_TXD += fate-txd-odd
fate-txd-odd: CMD = framecrc -i $(TARGET_SAMPLES)/txd/odd.txd -an

FATE_TXD-$(call FRAMECRC, TXD, TXD, SCALE_FILTER) += fate-txd-pal8
fate-txd-pal8: CMD = framecrc -i $(TARGET_SAMPLES)/txd/outro.txd -pix_fmt rgb24 -an -vf scale

FATE_TXD-$(call FRAMECRC, TXD, TXD) += $(FATE_TXD)
FATE_VIDEO += $(FATE_TXD-yes)
fate-txd: $(FATE_TXD-yes)

FATE_VIDEO-$(call FRAMECRC, AVI, ULTI) += fate-ulti
fate-ulti: CMD = framecrc -i $(TARGET_SAMPLES)/ulti/hit12w.avi -an

FATE_VIDEO-$(call FRAMECRC, AVI, V210, SCALE_FILTER) += fate-v210
fate-v210: CMD = framecrc -i $(TARGET_SAMPLES)/v210/v210_720p-partial.avi -pix_fmt yuv422p16be -an -vf scale

FATE_VIDEO-$(call FRAMECRC, MOV, V410, SCALE_FILTER) += fate-v410dec
fate-v410dec: CMD = framecrc -i $(TARGET_SAMPLES)/v410/lenav410.mov -pix_fmt yuv444p10le -vf scale

FATE_VIDEO-$(call ENCDEC, V410 PGMYUV, AVI IMAGE2, SCALE_FILTER) += fate-v410enc
fate-v410enc: $(VREF)
fate-v410enc: CMD = md5 -f image2 -c:v pgmyuv -i $(TARGET_PATH)/tests/vsynth1/%02d.pgm -fflags +bitexact -c:v v410 -f avi -vf scale

FATE_VIDEO-$(call FRAMECRC, SIFF, VB, SCALE_FILTER) += fate-vb
fate-vb: CMD = framecrc -i $(TARGET_SAMPLES)/SIFF/INTRO_B.VB -t 3 -pix_fmt rgb24 -an -vf scale

FATE_VIDEO-$(call FRAMECRC, AVI, VCR1) += fate-vcr1
fate-vcr1: CMD = framecrc -i $(TARGET_SAMPLES)/vcr1/VCR1test.avi -an

FATE_VIDEO-$(call FRAMECRC, AVI, MPEG2VIDEO) += fate-vcr2
fate-vcr2: CMD = framecrc -flags +bitexact -idct simple -i $(TARGET_SAMPLES)/vcr2/VCR2test.avi -an

FATE_VIDEO-$(call FRAMECRC, AVI, XL) += fate-videoxl
fate-videoxl: CMD = framecrc -i $(TARGET_SAMPLES)/vixl/pig-vixl.avi

FATE_VIDEO-$(call FRAMECRC, WSVQA, VQA, SCALE_FILTER) += fate-vqa-cc
fate-vqa-cc: CMD = framecrc -i $(TARGET_SAMPLES)/vqa/cc-demo1-partial.vqa -pix_fmt rgb24 -an -vf scale

FATE_VIDEO-$(call FRAMECRC, WC3, XAN_WC3, SCALE_FILTER) += fate-wc3movie-xan
fate-wc3movie-xan: CMD = framecrc -i $(TARGET_SAMPLES)/wc3movie/SC_32-part.MVE -pix_fmt rgb24 -vf scale

FATE_VIDEO-$(call FRAMECRC, AVI, WNV1) += fate-wnv1
fate-wnv1: CMD = framecrc -i $(TARGET_SAMPLES)/wnv1/wnv1-codec.avi -an

FATE_VIDEO-$(call FRAMECRC, YOP, YOP, SCALE_FILTER) += fate-yop
fate-yop: CMD = framecrc -i $(TARGET_SAMPLES)/yop/test1.yop -pix_fmt rgb24 -an -vf scale

FATE_VIDEO-$(call FRAMECRC, AVI, XAN_WC4) += fate-xxan-wc4
fate-xxan-wc4: CMD = framecrc -i $(TARGET_SAMPLES)/wc4-xan/wc4trailer-partial.avi -an

FATE_VIDEO-$(call FRAMECRC, WAV, SMVJPEG) += fate-smvjpeg
fate-smvjpeg: CMD = framecrc -idct simple -flags +bitexact -i $(TARGET_SAMPLES)/smv/clock.smv -an

FATE_VIDEO-$(call FRAMECRC, AVI, VQC) += fate-vqc
fate-vqc: CMD = framecrc -i $(TARGET_SAMPLES)/vqc/samp1.avi

FATE_VIDEO += $(FATE_VIDEO-yes)

FATE_SAMPLES_FFMPEG += $(FATE_VIDEO)
fate-video: $(FATE_VIDEO)
