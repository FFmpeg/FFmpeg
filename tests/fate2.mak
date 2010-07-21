FATE2_TESTS += fate-twinvq
fate-twinvq: CMD = pcm -i $(SAMPLES)/vqf/achterba.vqf
fate-twinvq: CMP = oneoff
fate-twinvq: REF = $(SAMPLES)/vqf/achterba.pcm

FATE2_TESTS += fate-sipr-16k
fate-sipr-16k: CMD = pcm -i $(SAMPLES)/sipr/sipr_16k.rm
fate-sipr-16k: CMP = oneoff
fate-sipr-16k: REF = $(SAMPLES)/sipr/sipr_16k.pcm

FATE2_TESTS += fate-sipr-8k5
fate-sipr-8k5: CMD = pcm -i $(SAMPLES)/sipr/sipr_8k5.rm
fate-sipr-8k5: CMP = oneoff
fate-sipr-8k5: REF = $(SAMPLES)/sipr/sipr_8k5.pcm

FATE2_TESTS += fate-sipr-6k5
fate-sipr-6k5: CMD = pcm -i $(SAMPLES)/sipr/sipr_6k5.rm
fate-sipr-6k5: CMP = oneoff
fate-sipr-6k5: REF = $(SAMPLES)/sipr/sipr_6k5.pcm

FATE2_TESTS += fate-sipr-5k0
fate-sipr-5k0: CMD = pcm -i $(SAMPLES)/sipr/sipr_5k0.rm
fate-sipr-5k0: CMP = oneoff
fate-sipr-5k0: REF = $(SAMPLES)/sipr/sipr_5k0.pcm

FATE2_TESTS += fate-aac-al04_44
fate-aac-al04_44: CMD = pcm -i $(SAMPLES)/aac/al04_44.mp4
fate-aac-al04_44: CMP = oneoff
fate-aac-al04_44: REF = $(SAMPLES)/aac/al04_44.s16
fate-aac-al04_44: FUZZ = 2

FATE2_TESTS += fate-aac-al07_96
fate-aac-al07_96: CMD = pcm -i $(SAMPLES)/aac/al07_96.mp4
fate-aac-al07_96: CMP = oneoff
fate-aac-al07_96: REF = $(SAMPLES)/aac/al07_96.s16
fate-aac-al07_96: FUZZ = 2

FATE2_TESTS += fate-aac-am00_88
fate-aac-am00_88: CMD = pcm -i $(SAMPLES)/aac/am00_88.mp4
fate-aac-am00_88: CMP = oneoff
fate-aac-am00_88: REF = $(SAMPLES)/aac/am00_88.s16
fate-aac-am00_88: FUZZ = 2

FATE2_TESTS += fate-aac-al_sbr_hq_cm_48_2
fate-aac-al_sbr_hq_cm_48_2: CMD = pcm -i $(SAMPLES)/aac/al_sbr_cm_48_2.mp4
fate-aac-al_sbr_hq_cm_48_2: CMP = oneoff
fate-aac-al_sbr_hq_cm_48_2: REF = $(SAMPLES)/aac/al_sbr_hq_cm_48_2.s16
fate-aac-al_sbr_hq_cm_48_2: FUZZ = 2

FATE2_TESTS += fate-aac-al_sbr_ps_06_ur
fate-aac-al_sbr_ps_06_ur: CMD = pcm -i $(SAMPLES)/aac/al_sbr_ps_06_new.mp4
fate-aac-al_sbr_ps_06_ur: CMP = oneoff
fate-aac-al_sbr_ps_06_ur: REF = $(SAMPLES)/aac/al_sbr_ps_06_ur.s16
fate-aac-al_sbr_ps_06_ur: FUZZ = 2

FATE2_TESTS += fate-ra-288
fate-ra-288: CMD = pcm -i $(SAMPLES)/real/ra_288.rm
fate-ra-288: CMP = oneoff
fate-ra-288: REF = $(SAMPLES)/real/ra_288.pcm

FATE2_TESTS += fate-ra-cook
fate-ra-cook: CMD = pcm -i $(SAMPLES)/real/ra_cook.rm
fate-ra-cook: CMP = oneoff
fate-ra-cook: REF = $(SAMPLES)/real/ra_cook.pcm

FATE2_TESTS += fate-mpeg2-field-enc
fate-mpeg2-field-enc: CMD = framecrc -flags +bitexact -dct fastint -idct simple -i $(SAMPLES)/mpeg2/mpeg2_field_encoding.ts -an

FATE2_TESTS += fate-qcelp
fate-qcelp: CMD = pcm -i $(SAMPLES)/qcp/0036580847.QCP
fate-qcelp: CMP = oneoff
fate-qcelp: REF = $(SAMPLES)/qcp/0036580847.pcm

FATE2_TESTS += fate-qdm2
fate-qdm2: CMD = pcm -i $(SAMPLES)/qt-surge-suite/surge-2-16-B-QDM2.mov
fate-qdm2: CMP = oneoff
fate-qdm2: REF = $(SAMPLES)/qt-surge-suite/surge-2-16-B-QDM2.pcm
fate-qdm2: FUZZ = 2

FATE2_TESTS += fate-imc
fate-imc: CMD = pcm -i $(SAMPLES)/imc/imc.avi
fate-imc: CMP = oneoff
fate-imc: REF = $(SAMPLES)/imc/imc.pcm

FATE2_TESTS += fate-yop
fate-yop: CMD = framecrc -i $(SAMPLES)/yop/test1.yop -pix_fmt rgb24 -an

FATE2_TESTS += fate-pictor
fate-pictor: CMD = framecrc -i $(SAMPLES)/pictor/MFISH.PIC -pix_fmt rgb24 -an

FATE2_TESTS += fate-dts
fate-dts: CMD = pcm -i $(SAMPLES)/dts/dts.ts
fate-dts: CMP = oneoff
fate-dts: REF = $(SAMPLES)/dts/dts.pcm

FATE2_TESTS += fate-nellymoser
fate-nellymoser: CMD = pcm -i $(SAMPLES)/nellymoser/nellymoser.flv
fate-nellymoser: CMP = oneoff
fate-nellymoser: REF = $(SAMPLES)/nellymoser/nellymoser.pcm

FATE2_TESTS += fate-truespeech
fate-truespeech: CMD = pcm -i $(SAMPLES)/truespeech/a6.wav
fate-truespeech: CMP = oneoff
fate-truespeech: REF = $(SAMPLES)/truespeech/a6.pcm

FATE2_TESTS += fate-ac3-2.0
fate-ac3-2.0: CMD = pcm -i $(SAMPLES)/ac3/monsters_inc_2.0_192_small.ac3
fate-ac3-2.0: CMP = oneoff
fate-ac3-2.0: REF = $(SAMPLES)/ac3/monsters_inc_2.0_192_small.pcm

FATE2_TESTS += fate-ac3-5.1
fate-ac3-5.1: CMD = pcm -i $(SAMPLES)/ac3/monsters_inc_5.1_448_small.ac3
fate-ac3-5.1: CMP = oneoff
fate-ac3-5.1: REF = $(SAMPLES)/ac3/monsters_inc_5.1_448_small.pcm

FATE2_TESTS += fate-eac3-1
fate-eac3-1: CMD = pcm -i $(SAMPLES)/eac3/csi_miami_5.1_256_spx_small.eac3
fate-eac3-1: CMP = oneoff
fate-eac3-1: REF = $(SAMPLES)/eac3/csi_miami_5.1_256_spx_small.pcm

FATE2_TESTS += fate-eac3-2
fate-eac3-2: CMD = pcm -i $(SAMPLES)/eac3/csi_miami_stereo_128_spx_small.eac3
fate-eac3-2: CMP = oneoff
fate-eac3-2: REF = $(SAMPLES)/eac3/csi_miami_stereo_128_spx_small.pcm

FATE2_TESTS += fate-eac3-3
fate-eac3-3: CMD = pcm -i $(SAMPLES)/eac3/matrix2_commentary1_stereo_192_small.eac3
fate-eac3-3: CMP = oneoff
fate-eac3-3: REF = $(SAMPLES)/eac3/matrix2_commentary1_stereo_192_small.pcm

FATE2_TESTS += fate-eac3-4
fate-eac3-4: CMD = pcm -i $(SAMPLES)/eac3/serenity_english_5.1_1536_small.eac3
fate-eac3-4: CMP = oneoff
fate-eac3-4: REF = $(SAMPLES)/eac3/serenity_english_5.1_1536_small.pcm

FATE2_TESTS += fate-atrac1
fate-atrac1: CMD = pcm -i $(SAMPLES)/atrac1/test_tones_small.aea
fate-atrac1: CMP = oneoff
fate-atrac1: REF = $(SAMPLES)/atrac1/test_tones_small.pcm

FATE2_TESTS += fate-atrac3-1
fate-atrac3-1: CMD = pcm -i $(SAMPLES)/atrac3/mc_sich_at3_066_small.wav
fate-atrac3-1: CMP = oneoff
fate-atrac3-1: REF = $(SAMPLES)/atrac3/mc_sich_at3_066_small.pcm

FATE2_TESTS += fate-atrac3-2
fate-atrac3-2: CMD = pcm -i $(SAMPLES)/atrac3/mc_sich_at3_105_small.wav
fate-atrac3-2: CMP = oneoff
fate-atrac3-2: REF = $(SAMPLES)/atrac3/mc_sich_at3_105_small.pcm

FATE2_TESTS += fate-atrac3-3
fate-atrac3-3: CMD = pcm -i $(SAMPLES)/atrac3/mc_sich_at3_132_small.wav
fate-atrac3-3: CMP = oneoff
fate-atrac3-3: REF = $(SAMPLES)/atrac3/mc_sich_at3_132_small.pcm

FATE2_TESTS += fate-gsm
fate-gsm: CMD = framecrc -i $(SAMPLES)/gsm/ciao.wav

FATE2_TESTS += fate-vorbis-1
fate-vorbis-1: CMD = pcm -i $(SAMPLES)/vorbis/1.0.1-test_small.ogg
fate-vorbis-1: CMP = oneoff
fate-vorbis-1: REF = $(SAMPLES)/vorbis/1.0.1-test_small.pcm

FATE2_TESTS += fate-vorbis-2
fate-vorbis-2: CMD = pcm -i $(SAMPLES)/vorbis/1.0-test_small.ogg
fate-vorbis-2: CMP = oneoff
fate-vorbis-2: REF = $(SAMPLES)/vorbis/1.0-test_small.pcm

FATE2_TESTS += fate-vorbis-3
fate-vorbis-3: CMD = pcm -i $(SAMPLES)/vorbis/beta3-test_small.ogg
fate-vorbis-3: CMP = oneoff
fate-vorbis-3: REF = $(SAMPLES)/vorbis/beta3-test_small.pcm

FATE2_TESTS += fate-vorbis-4
fate-vorbis-4: CMD = pcm -i $(SAMPLES)/vorbis/beta4-test_small.ogg
fate-vorbis-4: CMP = oneoff
fate-vorbis-4: REF = $(SAMPLES)/vorbis/beta4-test_small.pcm

FATE2_TESTS += fate-vorbis-5
fate-vorbis-5: CMD = pcm -i $(SAMPLES)/vorbis/chain-test1_small.ogg
fate-vorbis-5: CMP = oneoff
fate-vorbis-5: REF = $(SAMPLES)/vorbis/chain-test1_small.pcm

FATE2_TESTS += fate-vorbis-6
fate-vorbis-6: CMD = pcm -i $(SAMPLES)/vorbis/chain-test2_small.ogg
fate-vorbis-6: CMP = oneoff
fate-vorbis-6: REF = $(SAMPLES)/vorbis/chain-test2_small.pcm

FATE2_TESTS += fate-vorbis-7
fate-vorbis-7: CMD = pcm -i $(SAMPLES)/vorbis/highrate-test_small.ogg
fate-vorbis-7: CMP = oneoff
fate-vorbis-7: REF = $(SAMPLES)/vorbis/highrate-test_small.pcm

FATE2_TESTS += fate-vorbis-8
fate-vorbis-8: CMD = pcm -i $(SAMPLES)/vorbis/lsp-test2_small.ogg
fate-vorbis-8: CMP = oneoff
fate-vorbis-8: REF = $(SAMPLES)/vorbis/lsp-test2_small.pcm

FATE2_TESTS += fate-vorbis-9
fate-vorbis-9: CMD = pcm -i $(SAMPLES)/vorbis/lsp-test3_small.ogg
fate-vorbis-9: CMP = oneoff
fate-vorbis-9: REF = $(SAMPLES)/vorbis/lsp-test3_small.pcm

FATE2_TESTS += fate-vorbis-10
fate-vorbis-10: CMD = pcm -i $(SAMPLES)/vorbis/lsp-test4_small.ogg
fate-vorbis-10: CMP = oneoff
fate-vorbis-10: REF = $(SAMPLES)/vorbis/lsp-test4_small.pcm

FATE2_TESTS += fate-vorbis-11
fate-vorbis-11: CMD = pcm -i $(SAMPLES)/vorbis/lsp-test_small.ogg
fate-vorbis-11: CMP = oneoff
fate-vorbis-11: REF = $(SAMPLES)/vorbis/lsp-test_small.pcm

FATE2_TESTS += fate-vorbis-12
fate-vorbis-12: CMD = pcm -i $(SAMPLES)/vorbis/mono_small.ogg
fate-vorbis-12: CMP = oneoff
fate-vorbis-12: REF = $(SAMPLES)/vorbis/mono_small.pcm

FATE2_TESTS += fate-vorbis-13
fate-vorbis-13: CMD = pcm -i $(SAMPLES)/vorbis/moog_small.ogg
fate-vorbis-13: CMP = oneoff
fate-vorbis-13: REF = $(SAMPLES)/vorbis/moog_small.pcm

FATE2_TESTS += fate-vorbis-14
fate-vorbis-14: CMD = pcm -i $(SAMPLES)/vorbis/rc1-test_small.ogg
fate-vorbis-14: CMP = oneoff
fate-vorbis-14: REF = $(SAMPLES)/vorbis/rc1-test_small.pcm

FATE2_TESTS += fate-vorbis-15
fate-vorbis-15: CMD = pcm -i $(SAMPLES)/vorbis/rc2-test2_small.ogg
fate-vorbis-15: CMP = oneoff
fate-vorbis-15: REF = $(SAMPLES)/vorbis/rc2-test2_small.pcm

FATE2_TESTS += fate-vorbis-16
fate-vorbis-16: CMD = pcm -i $(SAMPLES)/vorbis/rc2-test_small.ogg
fate-vorbis-16: CMP = oneoff
fate-vorbis-16: REF = $(SAMPLES)/vorbis/rc2-test_small.pcm

FATE2_TESTS += fate-vorbis-17
fate-vorbis-17: CMD = pcm -i $(SAMPLES)/vorbis/rc3-test_small.ogg
fate-vorbis-17: CMP = oneoff
fate-vorbis-17: REF = $(SAMPLES)/vorbis/rc3-test_small.pcm

FATE2_TESTS += fate-vorbis-18
fate-vorbis-18: CMD = pcm -i $(SAMPLES)/vorbis/sleepzor_small.ogg
fate-vorbis-18: CMP = oneoff
fate-vorbis-18: REF = $(SAMPLES)/vorbis/sleepzor_small.pcm

FATE2_TESTS += fate-vorbis-19
fate-vorbis-19: CMD = pcm -i $(SAMPLES)/vorbis/test-short2_small.ogg
fate-vorbis-19: CMP = oneoff
fate-vorbis-19: REF = $(SAMPLES)/vorbis/test-short2_small.pcm

FATE2_TESTS += fate-msmpeg4v1
fate-msmpeg4v1: CMD = framecrc -flags +bitexact -dct fastint -idct simple -i $(SAMPLES)/msmpeg4v1/mpg4.avi -an

FATE2_TESTS += fate-wmavoice-7k
fate-wmavoice-7k: CMD = pcm -i $(SAMPLES)/wmavoice/streaming_CBR-7K.wma
fate-wmavoice-7k: CMP = stddev
fate-wmavoice-7k: REF = $(SAMPLES)/wmavoice/streaming_CBR-7K.pcm
fate-wmavoice-7k: FUZZ = 3

FATE2_TESTS += fate-wmavoice-11k
fate-wmavoice-11k: CMD = pcm -i $(SAMPLES)/wmavoice/streaming_CBR-11K.wma
fate-wmavoice-11k: CMP = stddev
fate-wmavoice-11k: REF = $(SAMPLES)/wmavoice/streaming_CBR-11K.pcm
fate-wmavoice-11k: FUZZ = 3

FATE2_TESTS += fate-wmavoice-19k
fate-wmavoice-19k: CMD = pcm -i $(SAMPLES)/wmavoice/streaming_CBR-19K.wma
fate-wmavoice-19k: CMP = stddev
fate-wmavoice-19k: REF = $(SAMPLES)/wmavoice/streaming_CBR-19K.pcm
fate-wmavoice-19k: FUZZ = 3

FATE2_TESTS += fate-wmapro-5.1
fate-wmapro-5.1: CMD = pcm -i $(SAMPLES)/wmapro/latin_192_mulitchannel_cut.wma
fate-wmapro-5.1: CMP = oneoff
fate-wmapro-5.1: REF = $(SAMPLES)/wmapro/latin_192_mulitchannel_cut.pcm

FATE2_TESTS += fate-wmapro-2ch
fate-wmapro-2ch: CMD = pcm -i $(SAMPLES)/wmapro/Beethovens_9th-1_small.wma
fate-wmapro-2ch: CMP = oneoff
fate-wmapro-2ch: REF = $(SAMPLES)/wmapro/Beethovens_9th-1_small.pcm
