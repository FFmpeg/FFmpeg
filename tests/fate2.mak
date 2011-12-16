FATE_TESTS += fate-twinvq
fate-twinvq: CMD = pcm -i $(SAMPLES)/vqf/achterba.vqf
fate-twinvq: CMP = oneoff
fate-twinvq: REF = $(SAMPLES)/vqf/achterba.pcm

FATE_TESTS += fate-mpeg2-field-enc
fate-mpeg2-field-enc: CMD = framecrc -flags +bitexact -dct fastint -idct simple -i $(SAMPLES)/mpeg2/mpeg2_field_encoding.ts -an

FATE_TESTS += fate-qcelp
fate-qcelp: CMD = pcm -i $(SAMPLES)/qcp/0036580847.QCP
fate-qcelp: CMP = oneoff
fate-qcelp: REF = $(SAMPLES)/qcp/0036580847.pcm

FATE_TESTS += fate-qdm2
fate-qdm2: CMD = pcm -i $(SAMPLES)/qt-surge-suite/surge-2-16-B-QDM2.mov
fate-qdm2: CMP = oneoff
fate-qdm2: REF = $(SAMPLES)/qt-surge-suite/surge-2-16-B-QDM2.pcm
fate-qdm2: FUZZ = 2

FATE_TESTS += fate-imc
fate-imc: CMD = pcm -i $(SAMPLES)/imc/imc.avi
fate-imc: CMP = oneoff
fate-imc: REF = $(SAMPLES)/imc/imc.pcm

FATE_TESTS += fate-yop
fate-yop: CMD = framecrc -i $(SAMPLES)/yop/test1.yop -pix_fmt rgb24 -an

FATE_TESTS += fate-dts
fate-dts: CMD = pcm -i $(SAMPLES)/dts/dts.ts
fate-dts: CMP = oneoff
fate-dts: REF = $(SAMPLES)/dts/dts.pcm

FATE_TESTS += fate-nellymoser
fate-nellymoser: CMD = pcm -i $(SAMPLES)/nellymoser/nellymoser.flv
fate-nellymoser: CMP = oneoff
fate-nellymoser: REF = $(SAMPLES)/nellymoser/nellymoser.pcm

FATE_TESTS += fate-truespeech
fate-truespeech: CMD = pcm -i $(SAMPLES)/truespeech/a6.wav
fate-truespeech: CMP = oneoff
fate-truespeech: REF = $(SAMPLES)/truespeech/a6.pcm

FATE_TESTS += fate-gsm
fate-gsm: CMD = framecrc -i $(SAMPLES)/gsm/sample-gsm-8000.mov -t 10

FATE_TESTS += fate-gsm-ms
fate-gsm-ms: CMD = framecrc -i $(SAMPLES)/gsm/ciao.wav

FATE_TESTS += fate-g722dec-1
fate-g722dec-1: CMD = framecrc -i $(SAMPLES)/g722/conf-adminmenu-162.g722

FATE_TESTS += fate-g722enc
fate-g722enc: tests/data/asynth-16000-1.sw
fate-g722enc: CMD = md5 -ar 16000 -ac 1 -f s16le -i $(TARGET_PATH)/tests/data/asynth-16000-1.sw -acodec g722 -ac 1 -f g722

FATE_TESTS += fate-msmpeg4v1
fate-msmpeg4v1: CMD = framecrc -flags +bitexact -dct fastint -idct simple -i $(SAMPLES)/msmpeg4v1/mpg4.avi -an

FATE_TESTS += fate-ansi
fate-ansi: CMD = framecrc -chars_per_frame 44100 -i $(SAMPLES)/ansi/TRE-IOM5.ANS -pix_fmt rgb24

FATE_TESTS += fate-wmv8-drm
# discard last packet to avoid fails due to overread of VC-1 decoder
fate-wmv8-drm: CMD = framecrc -cryptokey 137381538c84c068111902a59c5cf6c340247c39 -i $(SAMPLES)/wmv8/wmv_drm.wmv -an -vframes 162

FATE_TESTS += fate-wmv8-drm-nodec
fate-wmv8-drm-nodec: CMD = framecrc -cryptokey 137381538c84c068111902a59c5cf6c340247c39 -i $(SAMPLES)/wmv8/wmv_drm.wmv -acodec copy -vcodec copy

FATE_TESTS += fate-binkaudio-dct
fate-binkaudio-dct: CMD = pcm -i $(SAMPLES)/bink/binkaudio_dct.bik
fate-binkaudio-dct: CMP = oneoff
fate-binkaudio-dct: REF = $(SAMPLES)/bink/binkaudio_dct.pcm
fate-binkaudio-dct: FUZZ = 2

FATE_TESTS += fate-binkaudio-rdft
fate-binkaudio-rdft: CMD = pcm -i $(SAMPLES)/bink/binkaudio_rdft.bik
fate-binkaudio-rdft: CMP = oneoff
fate-binkaudio-rdft: REF = $(SAMPLES)/bink/binkaudio_rdft.pcm
fate-binkaudio-rdft: FUZZ = 2

FATE_TESTS += fate-txd-pal8
fate-txd-pal8: CMD = framecrc -i $(SAMPLES)/txd/outro.txd -pix_fmt rgb24 -an

FATE_TESTS += fate-txd-16bpp
fate-txd-16bpp: CMD = framecrc -i $(SAMPLES)/txd/misc.txd -pix_fmt bgra -an

FATE_TESTS += fate-vp3
fate-vp3: CMD = framecrc -i $(SAMPLES)/vp3/vp31.avi

FATE_TESTS += fate-ws_snd
fate-ws_snd: CMD = md5  -i $(SAMPLES)/vqa/ws_snd.vqa -f s16le

FATE_TESTS += fate-dxa-scummvm
fate-dxa-scummvm: CMD = framecrc -i $(SAMPLES)/dxa/scummvm.dxa -pix_fmt rgb24

FATE_TESTS += fate-mjpegb
fate-mjpegb: CMD = framecrc -idct simple -flags +bitexact -i $(SAMPLES)/mjpegb/mjpegb_part.mov -an

FATE_TESTS += fate-musepack7
fate-musepack7: CMD = pcm -i $(SAMPLES)/musepack/inside-mp7.mpc
fate-musepack7: CMP = oneoff
fate-musepack7: REF = $(SAMPLES)/musepack/inside-mp7.pcm
fate-musepack7: FUZZ = 1

FATE_TESTS += fate-iirfilter
fate-iirfilter: libavcodec/iirfilter-test$(EXESUF)
fate-iirfilter: CMD = run libavcodec/iirfilter-test

FATE_TESTS += fate-v410dec
fate-v410dec: CMD = framecrc -i $(SAMPLES)/v410/lenav410.mov -pix_fmt yuv444p10le

FATE_TESTS += fate-v410enc
fate-v410enc: tests/vsynth1/00.pgm
fate-v410enc: CMD = md5 -f image2 -vcodec pgmyuv -i $(TARGET_PATH)/tests/vsynth1/%02d.pgm -flags +bitexact -vcodec v410 -f avi

FATE_TESTS += fate-r210
fate-r210: CMD = framecrc -i $(SAMPLES)/r210/r210.avi -pix_fmt rgb48le
