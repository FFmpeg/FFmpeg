FATE_TESTS += fate-4xm-1
fate-4xm-1: CMD = framecrc -i $(SAMPLES)/4xm/version1.4xm -pix_fmt rgb24 -an

FATE_TESTS += fate-4xm-2
fate-4xm-2: CMD = framecrc -i $(SAMPLES)/4xm/version2.4xm -pix_fmt rgb24 -an

FATE_TESTS += fate-aasc
fate-aasc: CMD = framecrc -i $(SAMPLES)/aasc/AASC-1.5MB.AVI -pix_fmt rgb24

FATE_TESTS += fate-alg-mm
fate-alg-mm: CMD = framecrc -i $(SAMPLES)/alg-mm/ibmlogo.mm -an -pix_fmt rgb24

FATE_TESTS += fate-amv
fate-amv: CMD = framecrc -idct simple -i $(SAMPLES)/amv/MTV_high_res_320x240_sample_Penguin_Joke_MTV_from_WMV.amv -t 10

FATE_TESTS += fate-ansi
fate-ansi: CMD = framecrc -chars_per_frame 44100 -i $(SAMPLES)/ansi/TRE-IOM5.ANS -pix_fmt rgb24

FATE_TESTS += fate-armovie-escape124
fate-armovie-escape124: CMD = framecrc -i $(SAMPLES)/rpl/ESCAPE.RPL -pix_fmt rgb24

FATE_TESTS += fate-auravision
fate-auravision: CMD = framecrc -i $(SAMPLES)/auravision/SOUVIDEO.AVI -an

FATE_TESTS += fate-auravision-v2
fate-auravision-v2: CMD = framecrc -i $(SAMPLES)/auravision/salma-hayek-in-ugly-betty-partial-avi -an

FATE_TESTS += fate-bethsoft-vid
fate-bethsoft-vid: CMD = framecrc -i $(SAMPLES)/bethsoft-vid/ANIM0001.VID -vsync 0 -t 5 -pix_fmt rgb24

FATE_TESTS += fate-bfi
fate-bfi: CMD = framecrc -i $(SAMPLES)/bfi/2287.bfi -pix_fmt rgb24

FATE_TESTS += fate-cdgraphics
fate-cdgraphics: CMD = framecrc -i $(SAMPLES)/cdgraphics/BrotherJohn.cdg -pix_fmt rgb24 -t 1

FATE_TESTS += fate-cljr
fate-cljr: CMD = framecrc -i $(SAMPLES)/cljr/testcljr-partial.avi

FATE_TESTS += fate-corepng
fate-corepng: CMD = framecrc -i $(SAMPLES)/png1/corepng-partial.avi

FATE_TESTS += fate-creatureshock-avs
fate-creatureshock-avs: CMD = framecrc -i $(SAMPLES)/creatureshock-avs/OUTATIME.AVS -pix_fmt rgb24

FATE_TESTS += fate-cvid
fate-cvid: CMD = framecrc -i $(SAMPLES)/cvid/laracroft-cinepak-partial.avi -an

FATE_TESTS += fate-cvid-palette
fate-cvid-palette: CMD = framecrc -i $(SAMPLES)/cvid/catfight-cvid-pal8-partial.mov -pix_fmt rgb24 -an

FATE_TESTS += fate-cyberia-c93
fate-cyberia-c93: CMD = framecrc -i $(SAMPLES)/cyberia-c93/intro1.c93 -t 3 -pix_fmt rgb24

FATE_TESTS += fate-cyuv
fate-cyuv: CMD = framecrc -i $(SAMPLES)/cyuv/cyuv.avi

FATE_TESTS += fate-delphine-cin
fate-delphine-cin: CMD = framecrc -i $(SAMPLES)/delphine-cin/LOGO-partial.CIN -pix_fmt rgb24 -vsync 0

FATE_TESTS += fate-deluxepaint-anm
fate-deluxepaint-anm: CMD = framecrc -i $(SAMPLES)/deluxepaint-anm/INTRO1.ANM -pix_fmt rgb24

FATE_TESTS += fate-duck-tm2
fate-duck-tm2: CMD = framecrc -i $(SAMPLES)/duck/tm20.avi

FATE_TESTS += fate-dxa-scummvm
fate-dxa-scummvm: CMD = framecrc -i $(SAMPLES)/dxa/scummvm.dxa -pix_fmt rgb24

FATE_TESTS += fate-feeble-dxa
fate-feeble-dxa: CMD = framecrc -i $(SAMPLES)/dxa/meetsquid.dxa -t 2 -pix_fmt rgb24

FATE_TESTS += fate-flic-af11-palette-change
fate-flic-af11-palette-change: CMD = framecrc -i $(SAMPLES)/fli/fli-engines.fli -t 3.3 -pix_fmt rgb24

FATE_TESTS += fate-flic-af12
fate-flic-af12: CMD = framecrc -i $(SAMPLES)/fli/jj00c2.fli -pix_fmt rgb24

FATE_TESTS += fate-flic-magiccarpet
fate-flic-magiccarpet: CMD = framecrc -i $(SAMPLES)/fli/intel.dat -pix_fmt rgb24

FATE_TESTS += fate-frwu
fate-frwu: CMD = framecrc -i $(SAMPLES)/frwu/frwu.avi

FATE_TESTS += fate-id-cin-video
fate-id-cin-video: CMD = framecrc -i $(SAMPLES)/idcin/idlog-2MB.cin -pix_fmt rgb24

FATE_TESTS-$(CONFIG_AVFILTER) += fate-idroq-video-encode
fate-idroq-video-encode: CMD = md5 -f image2 -vcodec pgmyuv -i $(SAMPLES)/ffmpeg-synthetic/vsynth1/%02d.pgm -sws_flags +bitexact -vf pad=512:512:80:112 -f RoQ -t 0.2

FATE_TESTS += fate-iff-byterun1
fate-iff-byterun1: CMD = framecrc -i $(SAMPLES)/iff/ASH.LBM -pix_fmt rgb24

FATE_TESTS += fate-iff-fibonacci
fate-iff-fibonacci: CMD = md5 -i $(SAMPLES)/iff/dasboot-in-compressed -f s16le

FATE_TESTS += fate-iff-ilbm
fate-iff-ilbm: CMD = framecrc -i $(SAMPLES)/iff/lms-matriks.ilbm -pix_fmt rgb24

FATE_TESTS += fate-kmvc
fate-kmvc: CMD = framecrc -i $(SAMPLES)/KMVC/LOGO1.AVI -an -t 3 -pix_fmt rgb24

FATE_TESTS += fate-mimic
fate-mimic: CMD = framecrc -idct simple -i $(SAMPLES)/mimic/mimic2-womanloveffmpeg.cam -vsync 0

FATE_TESTS += fate-mjpegb
fate-mjpegb: CMD = framecrc -idct simple -flags +bitexact -i $(SAMPLES)/mjpegb/mjpegb_part.mov -an

FATE_TESTS += fate-motionpixels
fate-motionpixels: CMD = framecrc -i $(SAMPLES)/motion-pixels/INTRO-partial.MVI -an -pix_fmt rgb24 -vframes 111

FATE_TESTS += fate-mpeg2-field-enc
fate-mpeg2-field-enc: CMD = framecrc -flags +bitexact -dct fastint -idct simple -i $(SAMPLES)/mpeg2/mpeg2_field_encoding.ts -an

FATE_TESTS += fate-nuv
fate-nuv: CMD = framecrc -idct simple -i $(SAMPLES)/nuv/Today.nuv -vsync 0

FATE_TESTS += fate-qpeg
fate-qpeg: CMD = framecrc -i $(SAMPLES)/qpeg/Clock.avi -an -pix_fmt rgb24

FATE_TESTS += fate-r210
fate-r210: CMD = framecrc -i $(SAMPLES)/r210/r210.avi -pix_fmt rgb48le

FATE_TESTS += fate-rl2
fate-rl2: CMD = framecrc -i $(SAMPLES)/rl2/Z4915300.RL2 -pix_fmt rgb24 -an -vsync 0

FATE_TESTS += fate-smacker
fate-smacker: CMD = framecrc -i $(SAMPLES)/smacker/wetlogo.smk -pix_fmt rgb24

FATE_TESTS += fate-smc
fate-smc: CMD = framecrc -i $(SAMPLES)/smc/cass_schi.qt -vsync 0 -pix_fmt rgb24

FATE_TESTS += fate-sp5x
fate-sp5x: CMD = framecrc -idct simple -i $(SAMPLES)/sp5x/sp5x_problem.avi

FATE_TESTS += fate-sub-srt
fate-sub-srt: CMD = md5 -i $(SAMPLES)/sub/SubRip_capability_tester.srt -f ass

FATE_TESTS += fate-tiertex-seq
fate-tiertex-seq: CMD = framecrc -i $(SAMPLES)/tiertex-seq/Gameover.seq -pix_fmt rgb24

FATE_TESTS += fate-tmv
fate-tmv: CMD = framecrc -i $(SAMPLES)/tmv/pop-partial.tmv -pix_fmt rgb24

FATE_TESTS += fate-truemotion1-15
fate-truemotion1-15: CMD = framecrc -i $(SAMPLES)/duck/phant2-940.duk -pix_fmt rgb24

FATE_TESTS += fate-truemotion1-24
fate-truemotion1-24: CMD = framecrc -i $(SAMPLES)/duck/sonic3dblast_intro-partial.avi -pix_fmt rgb24

FATE_TESTS += fate-txd-16bpp
fate-txd-16bpp: CMD = framecrc -i $(SAMPLES)/txd/misc.txd -pix_fmt bgra -an

FATE_TESTS += fate-txd-pal8
fate-txd-pal8: CMD = framecrc -i $(SAMPLES)/txd/outro.txd -pix_fmt rgb24 -an

FATE_TESTS += fate-ulti
fate-ulti: CMD = framecrc -i $(SAMPLES)/ulti/hit12w.avi -an

FATE_TESTS += fate-v210
fate-v210: CMD = framecrc -i $(SAMPLES)/v210/v210_720p-partial.avi -pix_fmt yuv422p16be -an

FATE_TESTS += fate-v410dec
fate-v410dec: CMD = framecrc -i $(SAMPLES)/v410/lenav410.mov -pix_fmt yuv444p10le

FATE_TESTS += fate-v410enc
fate-v410enc: tests/vsynth1/00.pgm
fate-v410enc: CMD = md5 -f image2 -vcodec pgmyuv -i $(TARGET_PATH)/tests/vsynth1/%02d.pgm -flags +bitexact -vcodec v410 -f avi

FATE_TESTS += fate-vcr1
fate-vcr1: CMD = framecrc -i $(SAMPLES)/vcr1/VCR1test.avi -an

FATE_TESTS += fate-video-xl
fate-video-xl: CMD = framecrc -i $(SAMPLES)/vixl/pig-vixl.avi

FATE_TESTS += fate-vqa-cc
fate-vqa-cc: CMD = framecrc -i $(SAMPLES)/vqa/cc-demo1-partial.vqa -pix_fmt rgb24

FATE_TESTS += fate-wc3movie-xan
fate-wc3movie-xan: CMD = framecrc -i $(SAMPLES)/wc3movie/SC_32-part.MVE -pix_fmt rgb24

FATE_TESTS += fate-wnv1
fate-wnv1: CMD = framecrc -i $(SAMPLES)/wnv1/wnv1-codec.avi -an

FATE_TESTS += fate-yop
fate-yop: CMD = framecrc -i $(SAMPLES)/yop/test1.yop -pix_fmt rgb24 -an

FATE_TESTS += fate-xxan-wc4
fate-xxan-wc4: CMD = framecrc -i $(SAMPLES)/wc4-xan/wc4_2.avi -an -vframes 10
