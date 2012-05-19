FATE_4XM += fate-4xm-1
fate-4xm-1: CMD = framecrc -i $(SAMPLES)/4xm/version1.4xm -pix_fmt rgb24 -an

FATE_4XM += fate-4xm-2
fate-4xm-2: CMD = framecrc -i $(SAMPLES)/4xm/version2.4xm -pix_fmt rgb24 -an

FATE_SAMPLES_AVCONV += $(FATE_4XM)
fate-4xm: $(FATE_4XM)

FATE_SAMPLES_AVCONV += fate-aasc
fate-aasc: CMD = framecrc -i $(SAMPLES)/aasc/AASC-1.5MB.AVI -pix_fmt rgb24

FATE_SAMPLES_AVCONV += fate-alg-mm
fate-alg-mm: CMD = framecrc -i $(SAMPLES)/alg-mm/ibmlogo.mm -an -pix_fmt rgb24

FATE_SAMPLES_AVCONV += fate-amv
fate-amv: CMD = framecrc -idct simple -i $(SAMPLES)/amv/MTV_high_res_320x240_sample_Penguin_Joke_MTV_from_WMV.amv -t 10 -an

FATE_SAMPLES_AVCONV += fate-ansi
fate-ansi: CMD = framecrc -chars_per_frame 44100 -i $(SAMPLES)/ansi/TRE-IOM5.ANS -pix_fmt rgb24

FATE_SAMPLES_AVCONV += fate-armovie-escape124
fate-armovie-escape124: CMD = framecrc -i $(SAMPLES)/rpl/ESCAPE.RPL -pix_fmt rgb24

FATE_SAMPLES_AVCONV += fate-auravision-v1
fate-auravision-v1: CMD = framecrc -i $(SAMPLES)/auravision/SOUVIDEO.AVI -an

FATE_SAMPLES_AVCONV += fate-auravision-v2
fate-auravision-v2: CMD = framecrc -i $(SAMPLES)/auravision/salma-hayek-in-ugly-betty-partial-avi -an

FATE_SAMPLES_AVCONV += fate-bethsoft-vid
fate-bethsoft-vid: CMD = framecrc -i $(SAMPLES)/bethsoft-vid/ANIM0001.VID -t 5 -pix_fmt rgb24

FATE_SAMPLES_AVCONV += fate-bfi
fate-bfi: CMD = framecrc -i $(SAMPLES)/bfi/2287.bfi -pix_fmt rgb24

FATE_SAMPLES_AVCONV += fate-bink-video
fate-bink-video: CMD = framecrc -i $(SAMPLES)/bink/hol2br.bik

FATE_SAMPLES_AVCONV += fate-bmv-video
fate-bmv-video: CMD = framecrc -i $(SAMPLES)/bmv/SURFING-partial.BMV -pix_fmt rgb24 -an

FATE_SAMPLES_AVCONV += fate-cdgraphics
fate-cdgraphics: CMD = framecrc -i $(SAMPLES)/cdgraphics/BrotherJohn.cdg -pix_fmt rgb24 -t 1

FATE_SAMPLES_AVCONV += fate-cljr
fate-cljr: CMD = framecrc -i $(SAMPLES)/cljr/testcljr-partial.avi

FATE_SAMPLES_AVCONV += fate-corepng
fate-corepng: CMD = framecrc -i $(SAMPLES)/png1/corepng-partial.avi

FATE_SAMPLES_AVCONV += fate-creatureshock-avs
fate-creatureshock-avs: CMD = framecrc -i $(SAMPLES)/creatureshock-avs/OUTATIME.AVS -pix_fmt rgb24

FATE_CVID += fate-cvid-partial
fate-cvid-partial: CMD = framecrc -i $(SAMPLES)/cvid/laracroft-cinepak-partial.avi -an

FATE_CVID += fate-cvid-palette
fate-cvid-palette: CMD = framecrc -i $(SAMPLES)/cvid/catfight-cvid-pal8-partial.mov -pix_fmt rgb24 -an

FATE_CVID += fate-cvid-grayscale
fate-cvid-grayscale: CMD = framecrc -i $(SAMPLES)/cvid/pcitva15.avi -an

FATE_SAMPLES_AVCONV += $(FATE_CVID)
fate-cvid: $(FATE_CVID)

FATE_SAMPLES_AVCONV += fate-cyberia-c93
fate-cyberia-c93: CMD = framecrc -i $(SAMPLES)/cyberia-c93/intro1.c93 -t 3 -pix_fmt rgb24

FATE_SAMPLES_AVCONV += fate-cyuv
fate-cyuv: CMD = framecrc -i $(SAMPLES)/cyuv/cyuv.avi

FATE_SAMPLES_AVCONV += fate-delphine-cin-video
fate-delphine-cin-video: CMD = framecrc -i $(SAMPLES)/delphine-cin/LOGO-partial.CIN -pix_fmt rgb24 -an

FATE_SAMPLES_AVCONV += fate-deluxepaint-anm
fate-deluxepaint-anm: CMD = framecrc -i $(SAMPLES)/deluxepaint-anm/INTRO1.ANM -pix_fmt rgb24

FATE_TRUEMOTION1 += fate-truemotion1-15
fate-truemotion1-15: CMD = framecrc -i $(SAMPLES)/duck/phant2-940.duk -pix_fmt rgb24 -an

FATE_TRUEMOTION1 += fate-truemotion1-24
fate-truemotion1-24: CMD = framecrc -i $(SAMPLES)/duck/sonic3dblast_intro-partial.avi -pix_fmt rgb24 -an

FATE_SAMPLES_AVCONV += $(FATE_TRUEMOTION1)
fate-truemotion1: $(FATE_TRUEMOTION1)

FATE_SAMPLES_AVCONV += fate-truemotion2
fate-truemotion2: CMD = framecrc -i $(SAMPLES)/duck/tm20.avi

FATE_DXA += fate-dxa-feeble
fate-dxa-feeble: CMD = framecrc -i $(SAMPLES)/dxa/meetsquid.dxa -t 2 -pix_fmt rgb24 -an

FATE_DXA += fate-dxa-scummvm
fate-dxa-scummvm: CMD = framecrc -i $(SAMPLES)/dxa/scummvm.dxa -pix_fmt rgb24

FATE_SAMPLES_AVCONV += $(FATE_DXA)
fate-dxa: $(FATE_DXA)

FATE_SAMPLES_AVCONV += fate-film-cvid
fate-film-cvid: CMD = framecrc -i $(SAMPLES)/film/logo-capcom.cpk -an

FATE_FLIC += fate-flic-af11-palette-change
fate-flic-af11-palette-change: CMD = framecrc -i $(SAMPLES)/fli/fli-engines.fli -t 3.3 -pix_fmt rgb24

FATE_FLIC += fate-flic-af12
fate-flic-af12: CMD = framecrc -i $(SAMPLES)/fli/jj00c2.fli -pix_fmt rgb24

FATE_FLIC += fate-flic-magiccarpet
fate-flic-magiccarpet: CMD = framecrc -i $(SAMPLES)/fli/intel.dat -pix_fmt rgb24

FATE_SAMPLES_AVCONV += $(FATE_FLIC)
fate-flic: $(FATE_FLIC)

FATE_SAMPLES_AVCONV += fate-frwu
fate-frwu: CMD = framecrc -i $(SAMPLES)/frwu/frwu.avi

FATE_SAMPLES_AVCONV += fate-id-cin-video
fate-id-cin-video: CMD = framecrc -i $(SAMPLES)/idcin/idlog-2MB.cin -pix_fmt rgb24

FATE_SAMPLES_AVCONV += fate-idroq-video-encode
fate-idroq-video-encode: CMD = md5 -f image2 -vcodec pgmyuv -i $(SAMPLES)/ffmpeg-synthetic/vsynth1/%02d.pgm -sws_flags +bitexact -vf pad=512:512:80:112 -f roq -t 0.2

FATE_IFF += fate-iff-byterun1
fate-iff-byterun1: CMD = framecrc -i $(SAMPLES)/iff/ASH.LBM -pix_fmt rgb24

FATE_IFF += fate-iff-fibonacci
fate-iff-fibonacci: CMD = md5 -i $(SAMPLES)/iff/dasboot-in-compressed -f s16le

FATE_IFF += fate-iff-ilbm
fate-iff-ilbm: CMD = framecrc -i $(SAMPLES)/iff/lms-matriks.ilbm -pix_fmt rgb24

FATE_SAMPLES_AVCONV += $(FATE_IFF)
fate-iff: $(FATE_IFF)

FATE_SAMPLES_AVCONV += fate-interplay-mve-8bit
fate-interplay-mve-8bit: CMD = framecrc -i $(SAMPLES)/interplay-mve/interplay-logo-2MB.mve -pix_fmt rgb24 -an

FATE_SAMPLES_AVCONV += fate-interplay-mve-16bit
fate-interplay-mve-16bit: CMD = framecrc -i $(SAMPLES)/interplay-mve/descent3-level5-16bit-partial.mve -pix_fmt rgb24 -an

FATE_SAMPLES_AVCONV += fate-kgv1
fate-kgv1: CMD = framecrc -i $(SAMPLES)/kega/kgv1.avi -pix_fmt rgb555le -an

FATE_SAMPLES_AVCONV += fate-kmvc
fate-kmvc: CMD = framecrc -i $(SAMPLES)/KMVC/LOGO1.AVI -an -t 3 -pix_fmt rgb24

FATE_SAMPLES_AVCONV += fate-mdec
fate-mdec: CMD = framecrc -idct simple -i $(SAMPLES)/ea-dct/NFS2Esprit-partial.dct -an

FATE_SAMPLES_AVCONV += fate-mdec-v3
fate-mdec-v3: CMD = framecrc -idct simple -i $(SAMPLES)/psx-str/abc000_cut.str -an

FATE_SAMPLES_AVCONV += fate-mimic
fate-mimic: CMD = framecrc -idct simple -i $(SAMPLES)/mimic/mimic2-womanloveffmpeg.cam

FATE_SAMPLES_AVCONV += fate-mjpegb
fate-mjpegb: CMD = framecrc -idct simple -flags +bitexact -i $(SAMPLES)/mjpegb/mjpegb_part.mov -an

FATE_SAMPLES_AVCONV += fate-motionpixels
fate-motionpixels: CMD = framecrc -i $(SAMPLES)/motion-pixels/INTRO-partial.MVI -an -pix_fmt rgb24 -vframes 111

FATE_SAMPLES_AVCONV += fate-mpeg2-field-enc
fate-mpeg2-field-enc: CMD = framecrc -flags +bitexact -dct fastint -idct simple -i $(SAMPLES)/mpeg2/mpeg2_field_encoding.ts -an

# FIXME dropped frames in this test because of coarse timebase
FATE_SAMPLES_AVCONV += fate-nuv
fate-nuv: CMD = framecrc -idct simple -i $(SAMPLES)/nuv/Today.nuv -an

FATE_SAMPLES_AVCONV += fate-qpeg
fate-qpeg: CMD = framecrc -i $(SAMPLES)/qpeg/Clock.avi -an -pix_fmt rgb24

FATE_SAMPLES_AVCONV += fate-r210
fate-r210: CMD = framecrc -i $(SAMPLES)/r210/r210.avi -pix_fmt rgb48le

FATE_SAMPLES_AVCONV += fate-rl2
fate-rl2: CMD = framecrc -i $(SAMPLES)/rl2/Z4915300.RL2 -pix_fmt rgb24 -an

FATE_SAMPLES_AVCONV += fate-roqvideo
fate-roqvideo: CMD = framecrc -i $(SAMPLES)/idroq/idlogo.roq -an

FATE_SAMPLES_AVCONV += fate-sierra-vmd-video
fate-sierra-vmd-video: CMD = framecrc -i $(SAMPLES)/vmd/12.vmd -pix_fmt rgb24 -an

FATE_SAMPLES_AVCONV += fate-smacker-video
fate-smacker-video: CMD = framecrc -i $(SAMPLES)/smacker/wetlogo.smk -pix_fmt rgb24 -an

FATE_SAMPLES_AVCONV += fate-smc
fate-smc: CMD = framecrc -i $(SAMPLES)/smc/cass_schi.qt -pix_fmt rgb24

FATE_SAMPLES_AVCONV += fate-sp5x
fate-sp5x: CMD = framecrc -idct simple -i $(SAMPLES)/sp5x/sp5x_problem.avi

FATE_SAMPLES_AVCONV += fate-sub-srt
fate-sub-srt: CMD = md5 -i $(SAMPLES)/sub/SubRip_capability_tester.srt -f ass

FATE_SAMPLES_AVCONV += fate-thp
fate-thp: CMD = framecrc -idct simple -i $(SAMPLES)/thp/pikmin2-opening1-partial.thp -an

FATE_SAMPLES_AVCONV += fate-tiertex-seq
fate-tiertex-seq: CMD = framecrc -i $(SAMPLES)/tiertex-seq/Gameover.seq -pix_fmt rgb24

FATE_SAMPLES_AVCONV += fate-tmv
fate-tmv: CMD = framecrc -i $(SAMPLES)/tmv/pop-partial.tmv -pix_fmt rgb24

FATE_TXD += fate-txd-16bpp
fate-txd-16bpp: CMD = framecrc -i $(SAMPLES)/txd/misc.txd -pix_fmt bgra -an

FATE_TXD += fate-txd-pal8
fate-txd-pal8: CMD = framecrc -i $(SAMPLES)/txd/outro.txd -pix_fmt rgb24 -an

FATE_SAMPLES_AVCONV += $(FATE_TXD)
fate-txd: $(FATE_TXD)

FATE_SAMPLES_AVCONV += fate-ulti
fate-ulti: CMD = framecrc -i $(SAMPLES)/ulti/hit12w.avi -an

FATE_SAMPLES_AVCONV += fate-v210
fate-v210: CMD = framecrc -i $(SAMPLES)/v210/v210_720p-partial.avi -pix_fmt yuv422p16be -an

FATE_SAMPLES_AVCONV += fate-v410dec
fate-v410dec: CMD = framecrc -i $(SAMPLES)/v410/lenav410.mov -pix_fmt yuv444p10le

FATE_SAMPLES_AVCONV += fate-v410enc
fate-v410enc: tests/vsynth1/00.pgm
fate-v410enc: CMD = md5 -f image2 -vcodec pgmyuv -i $(TARGET_PATH)/tests/vsynth1/%02d.pgm -flags +bitexact -vcodec v410 -f avi

FATE_SAMPLES_AVCONV += fate-vcr1
fate-vcr1: CMD = framecrc -i $(SAMPLES)/vcr1/VCR1test.avi -an

FATE_SAMPLES_AVCONV += fate-videoxl
fate-videoxl: CMD = framecrc -i $(SAMPLES)/vixl/pig-vixl.avi

FATE_SAMPLES_AVCONV += fate-vqa-cc
fate-vqa-cc: CMD = framecrc -i $(SAMPLES)/vqa/cc-demo1-partial.vqa -pix_fmt rgb24 -an

FATE_SAMPLES_AVCONV += fate-wc3movie-xan
fate-wc3movie-xan: CMD = framecrc -i $(SAMPLES)/wc3movie/SC_32-part.MVE -pix_fmt rgb24

FATE_SAMPLES_AVCONV += fate-wnv1
fate-wnv1: CMD = framecrc -i $(SAMPLES)/wnv1/wnv1-codec.avi -an

FATE_SAMPLES_AVCONV += fate-yop
fate-yop: CMD = framecrc -i $(SAMPLES)/yop/test1.yop -pix_fmt rgb24 -an

FATE_SAMPLES_AVCONV += fate-xxan-wc4
fate-xxan-wc4: CMD = framecrc -i $(SAMPLES)/wc4-xan/wc4trailer-partial.avi -an
