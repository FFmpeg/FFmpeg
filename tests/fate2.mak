FATE_TESTS += fate-twinvq
fate-twinvq: CMD = pcm -i $(SAMPLES)/vqf/achterba.vqf
fate-twinvq: CMP = oneoff
fate-twinvq: REF = $(SAMPLES)/vqf/achterba.pcm

FATE_TESTS += fate-sipr-16k
fate-sipr-16k: CMD = pcm -i $(SAMPLES)/sipr/sipr_16k.rm
fate-sipr-16k: CMP = oneoff
fate-sipr-16k: REF = $(SAMPLES)/sipr/sipr_16k.pcm

FATE_TESTS += fate-sipr-8k5
fate-sipr-8k5: CMD = pcm -i $(SAMPLES)/sipr/sipr_8k5.rm
fate-sipr-8k5: CMP = oneoff
fate-sipr-8k5: REF = $(SAMPLES)/sipr/sipr_8k5.pcm

FATE_TESTS += fate-sipr-6k5
fate-sipr-6k5: CMD = pcm -i $(SAMPLES)/sipr/sipr_6k5.rm
fate-sipr-6k5: CMP = oneoff
fate-sipr-6k5: REF = $(SAMPLES)/sipr/sipr_6k5.pcm

FATE_TESTS += fate-sipr-5k0
fate-sipr-5k0: CMD = pcm -i $(SAMPLES)/sipr/sipr_5k0.rm
fate-sipr-5k0: CMP = oneoff
fate-sipr-5k0: REF = $(SAMPLES)/sipr/sipr_5k0.pcm

FATE_TESTS += fate-ra-288
fate-ra-288: CMD = pcm -i $(SAMPLES)/real/ra_288.rm
fate-ra-288: CMP = oneoff
fate-ra-288: REF = $(SAMPLES)/real/ra_288.pcm
fate-ra-288: FUZZ = 2

FATE_TESTS += fate-ra-cook
fate-ra-cook: CMD = pcm -i $(SAMPLES)/real/ra_cook.rm
fate-ra-cook: CMP = oneoff
fate-ra-cook: REF = $(SAMPLES)/real/ra_cook.pcm

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

FATE_TESTS += fate-pictor
fate-pictor: CMD = framecrc -i $(SAMPLES)/pictor/MFISH.PIC -pix_fmt rgb24 -an

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

FATE_TESTS += fate-ac3-2.0
fate-ac3-2.0: CMD = pcm -i $(SAMPLES)/ac3/monsters_inc_2.0_192_small.ac3
fate-ac3-2.0: CMP = oneoff
fate-ac3-2.0: REF = $(SAMPLES)/ac3/monsters_inc_2.0_192_small.pcm

FATE_TESTS += fate-ac3-5.1
fate-ac3-5.1: CMD = pcm -i $(SAMPLES)/ac3/monsters_inc_5.1_448_small.ac3
fate-ac3-5.1: CMP = oneoff
fate-ac3-5.1: REF = $(SAMPLES)/ac3/monsters_inc_5.1_448_small.pcm

FATE_TESTS += fate-eac3-1
fate-eac3-1: CMD = pcm -i $(SAMPLES)/eac3/csi_miami_5.1_256_spx_small.eac3
fate-eac3-1: CMP = oneoff
fate-eac3-1: REF = $(SAMPLES)/eac3/csi_miami_5.1_256_spx_small.pcm

FATE_TESTS += fate-eac3-2
fate-eac3-2: CMD = pcm -i $(SAMPLES)/eac3/csi_miami_stereo_128_spx_small.eac3
fate-eac3-2: CMP = oneoff
fate-eac3-2: REF = $(SAMPLES)/eac3/csi_miami_stereo_128_spx_small.pcm

FATE_TESTS += fate-eac3-3
fate-eac3-3: CMD = pcm -i $(SAMPLES)/eac3/matrix2_commentary1_stereo_192_small.eac3
fate-eac3-3: CMP = oneoff
fate-eac3-3: REF = $(SAMPLES)/eac3/matrix2_commentary1_stereo_192_small.pcm

FATE_TESTS += fate-eac3-4
fate-eac3-4: CMD = pcm -i $(SAMPLES)/eac3/serenity_english_5.1_1536_small.eac3
fate-eac3-4: CMP = oneoff
fate-eac3-4: REF = $(SAMPLES)/eac3/serenity_english_5.1_1536_small.pcm

FATE_TESTS += fate-atrac1
fate-atrac1: CMD = pcm -i $(SAMPLES)/atrac1/test_tones_small.aea
fate-atrac1: CMP = oneoff
fate-atrac1: REF = $(SAMPLES)/atrac1/test_tones_small.pcm

FATE_TESTS += fate-atrac3-1
fate-atrac3-1: CMD = pcm -i $(SAMPLES)/atrac3/mc_sich_at3_066_small.wav
fate-atrac3-1: CMP = oneoff
fate-atrac3-1: REF = $(SAMPLES)/atrac3/mc_sich_at3_066_small.pcm

FATE_TESTS += fate-atrac3-2
fate-atrac3-2: CMD = pcm -i $(SAMPLES)/atrac3/mc_sich_at3_105_small.wav
fate-atrac3-2: CMP = oneoff
fate-atrac3-2: REF = $(SAMPLES)/atrac3/mc_sich_at3_105_small.pcm

FATE_TESTS += fate-atrac3-3
fate-atrac3-3: CMD = pcm -i $(SAMPLES)/atrac3/mc_sich_at3_132_small.wav
fate-atrac3-3: CMP = oneoff
fate-atrac3-3: REF = $(SAMPLES)/atrac3/mc_sich_at3_132_small.pcm

FATE_TESTS += fate-gsm
fate-gsm: CMD = framecrc -i $(SAMPLES)/gsm/sample-gsm-8000.mov -t 10

FATE_TESTS += fate-gsm-ms
fate-gsm-ms: CMD = framecrc -i $(SAMPLES)/gsm/ciao.wav

FATE_TESTS += fate-g722dec-1
fate-g722dec-1: CMD = framecrc -i $(SAMPLES)/g722/conf-adminmenu-162.g722

FATE_TESTS += fate-g722enc
fate-g722enc: tests/data/asynth-16000-1.sw
fate-g722enc: CMD = md5 -ar 16000 -ac 1 -f s16le -i $(TARGET_PATH)/tests/data/asynth-16000-1.sw -acodec g722 -ac 1 -f g722

FATE_TESTS += fate-mapchan-6ch-extract-2
fate-mapchan-6ch-extract-2: tests/data/mapchan-6ch.sw
fate-mapchan-6ch-extract-2: CMD = avconv -ar 22050 -ac 6 -f s16le -i $(TARGET_PATH)/tests/data/mapchan-6ch.sw -map_channel 0.0.0 -f wav md5: -map_channel 0.0.1 -f wav md5:

FATE_TESTS += fate-mapchan-6ch-extract-2-downmix-mono
fate-mapchan-6ch-extract-2-downmix-mono: tests/data/mapchan-6ch.sw
fate-mapchan-6ch-extract-2-downmix-mono: CMD = md5 -ar 22050 -ac 6 -f s16le -i $(TARGET_PATH)/tests/data/mapchan-6ch.sw -map_channel 0.0.1 -map_channel 0.0.0 -ac 1 -f wav

FATE_TESTS += fate-mapchan-silent-mono
fate-mapchan-silent-mono: tests/data/mapchan-mono.sw
fate-mapchan-silent-mono: CMD = md5 -ar 22050 -ac 1 -f s16le -i $(TARGET_PATH)/tests/data/mapchan-mono.sw -map_channel -1 -map_channel 0.0.0 -f wav

FATE_TESTS += fate-msmpeg4v1
fate-msmpeg4v1: CMD = framecrc -flags +bitexact -dct fastint -idct simple -i $(SAMPLES)/msmpeg4v1/mpg4.avi -an

FATE_TESTS += fate-wmavoice-7k
fate-wmavoice-7k: CMD = pcm -i $(SAMPLES)/wmavoice/streaming_CBR-7K.wma
fate-wmavoice-7k: CMP = stddev
fate-wmavoice-7k: REF = $(SAMPLES)/wmavoice/streaming_CBR-7K.pcm
fate-wmavoice-7k: FUZZ = 3

FATE_TESTS += fate-wmavoice-11k
fate-wmavoice-11k: CMD = pcm -i $(SAMPLES)/wmavoice/streaming_CBR-11K.wma
fate-wmavoice-11k: CMP = stddev
fate-wmavoice-11k: REF = $(SAMPLES)/wmavoice/streaming_CBR-11K.pcm
fate-wmavoice-11k: FUZZ = 3

FATE_TESTS += fate-wmavoice-19k
fate-wmavoice-19k: CMD = pcm -i $(SAMPLES)/wmavoice/streaming_CBR-19K.wma
fate-wmavoice-19k: CMP = stddev
fate-wmavoice-19k: REF = $(SAMPLES)/wmavoice/streaming_CBR-19K.pcm
fate-wmavoice-19k: FUZZ = 3

FATE_TESTS += fate-wmapro-5.1
fate-wmapro-5.1: CMD = pcm -i $(SAMPLES)/wmapro/latin_192_mulitchannel_cut.wma
fate-wmapro-5.1: CMP = oneoff
fate-wmapro-5.1: REF = $(SAMPLES)/wmapro/latin_192_mulitchannel_cut.pcm

FATE_TESTS += fate-wmapro-2ch
fate-wmapro-2ch: CMD = pcm -i $(SAMPLES)/wmapro/Beethovens_9th-1_small.wma
fate-wmapro-2ch: CMP = oneoff
fate-wmapro-2ch: REF = $(SAMPLES)/wmapro/Beethovens_9th-1_small.pcm

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

FATE_TESTS += fate-fax-g3
fate-fax-g3: CMD = framecrc -i $(SAMPLES)/CCITT_fax/G31D.TIF

FATE_TESTS += fate-fax-g3s
fate-fax-g3s: CMD = framecrc -i $(SAMPLES)/CCITT_fax/G31DS.TIF

FATE_TESTS += fate-ws_snd
fate-ws_snd: CMD = md5  -i $(SAMPLES)/vqa/ws_snd.vqa -f s16le

FATE_TESTS += fate-dxa-scummvm
fate-dxa-scummvm: CMD = framecrc -i $(SAMPLES)/dxa/scummvm.dxa -pix_fmt rgb24

FATE_TESTS += fate-mjpegb
fate-mjpegb: CMD = framecrc -idct simple -flags +bitexact -i $(SAMPLES)/mjpegb/mjpegb_part.mov -an

FATE_TESTS += fate-rv30
fate-rv30: CMD = framecrc -flags +bitexact -dct fastint -idct simple -i $(SAMPLES)/real/rv30.rm -an

FATE_TESTS += fate-musepack7
fate-musepack7: CMD = pcm -i $(SAMPLES)/musepack/inside-mp7.mpc
fate-musepack7: CMP = oneoff
fate-musepack7: REF = $(SAMPLES)/musepack/inside-mp7.pcm
fate-musepack7: FUZZ = 1

FATE_TESTS += fate-iirfilter
fate-iirfilter: libavcodec/iirfilter-test$(EXESUF)
fate-iirfilter: CMD = run libavcodec/iirfilter-test
