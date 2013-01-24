FATE_VORBIS += fate-vorbis-1
fate-vorbis-1: CMD = pcm -i $(SAMPLES)/vorbis/1.0.1-test_small.ogg
fate-vorbis-1: REF = $(SAMPLES)/vorbis/1.0.1-test_small.pcm

FATE_VORBIS += fate-vorbis-2
fate-vorbis-2: CMD = pcm -i $(SAMPLES)/vorbis/1.0-test_small.ogg
fate-vorbis-2: REF = $(SAMPLES)/vorbis/1.0-test_small.pcm

FATE_VORBIS += fate-vorbis-3
fate-vorbis-3: CMD = pcm -i $(SAMPLES)/vorbis/beta3-test_small.ogg
fate-vorbis-3: REF = $(SAMPLES)/vorbis/beta3-test_small.pcm

FATE_VORBIS += fate-vorbis-4
fate-vorbis-4: CMD = pcm -i $(SAMPLES)/vorbis/beta4-test_small.ogg
fate-vorbis-4: REF = $(SAMPLES)/vorbis/beta4-test_small.pcm

FATE_VORBIS += fate-vorbis-5
fate-vorbis-5: CMD = pcm -i $(SAMPLES)/vorbis/chain-test1_small.ogg
fate-vorbis-5: REF = $(SAMPLES)/vorbis/chain-test1_small.pcm

FATE_VORBIS += fate-vorbis-6
fate-vorbis-6: CMD = pcm -i $(SAMPLES)/vorbis/chain-test2_small.ogg
fate-vorbis-6: REF = $(SAMPLES)/vorbis/chain-test2_small.pcm

FATE_VORBIS += fate-vorbis-7
fate-vorbis-7: CMD = pcm -i $(SAMPLES)/vorbis/highrate-test_small.ogg
fate-vorbis-7: REF = $(SAMPLES)/vorbis/highrate-test_small.pcm

FATE_VORBIS += fate-vorbis-8
fate-vorbis-8: CMD = pcm -i $(SAMPLES)/vorbis/lsp-test2_small.ogg
fate-vorbis-8: REF = $(SAMPLES)/vorbis/lsp-test2_small.pcm

FATE_VORBIS += fate-vorbis-9
fate-vorbis-9: CMD = pcm -i $(SAMPLES)/vorbis/lsp-test3_small.ogg
fate-vorbis-9: REF = $(SAMPLES)/vorbis/lsp-test3_small.pcm

FATE_VORBIS += fate-vorbis-10
fate-vorbis-10: CMD = pcm -i $(SAMPLES)/vorbis/lsp-test4_small.ogg
fate-vorbis-10: REF = $(SAMPLES)/vorbis/lsp-test4_small.pcm

FATE_VORBIS += fate-vorbis-11
fate-vorbis-11: CMD = pcm -i $(SAMPLES)/vorbis/lsp-test_small.ogg
fate-vorbis-11: REF = $(SAMPLES)/vorbis/lsp-test_small.pcm

FATE_VORBIS += fate-vorbis-12
fate-vorbis-12: CMD = pcm -i $(SAMPLES)/vorbis/mono_small.ogg
fate-vorbis-12: REF = $(SAMPLES)/vorbis/mono_small.pcm

FATE_VORBIS += fate-vorbis-13
fate-vorbis-13: CMD = pcm -i $(SAMPLES)/vorbis/moog_small.ogg
fate-vorbis-13: REF = $(SAMPLES)/vorbis/moog_small.pcm
fate-vorbis-13: FUZZ = 2

FATE_VORBIS += fate-vorbis-14
fate-vorbis-14: CMD = pcm -i $(SAMPLES)/vorbis/rc1-test_small.ogg
fate-vorbis-14: REF = $(SAMPLES)/vorbis/rc1-test_small.pcm

FATE_VORBIS += fate-vorbis-15
fate-vorbis-15: CMD = pcm -i $(SAMPLES)/vorbis/rc2-test2_small.ogg
fate-vorbis-15: REF = $(SAMPLES)/vorbis/rc2-test2_small.pcm

FATE_VORBIS += fate-vorbis-16
fate-vorbis-16: CMD = pcm -i $(SAMPLES)/vorbis/rc2-test_small.ogg
fate-vorbis-16: REF = $(SAMPLES)/vorbis/rc2-test_small.pcm

FATE_VORBIS += fate-vorbis-17
fate-vorbis-17: CMD = pcm -i $(SAMPLES)/vorbis/rc3-test_small.ogg
fate-vorbis-17: REF = $(SAMPLES)/vorbis/rc3-test_small.pcm

FATE_VORBIS += fate-vorbis-18
fate-vorbis-18: CMD = pcm -i $(SAMPLES)/vorbis/sleepzor_small.ogg
fate-vorbis-18: REF = $(SAMPLES)/vorbis/sleepzor_small.pcm
fate-vorbis-18: FUZZ = 2

FATE_VORBIS += fate-vorbis-19
fate-vorbis-19: CMD = pcm -i $(SAMPLES)/vorbis/test-short2_small.ogg
fate-vorbis-19: REF = $(SAMPLES)/vorbis/test-short2_small.pcm

FATE_VORBIS += fate-vorbis-20
fate-vorbis-20: CMD = pcm -i $(SAMPLES)/vorbis/6.ogg
fate-vorbis-20: REF = $(SAMPLES)/vorbis/6.pcm

FATE_SAMPLES_AVCONV-$(call DEMDEC, OGG, VORBIS) += $(FATE_VORBIS)
fate-vorbis: $(FATE_VORBIS)
$(FATE_VORBIS): CMP = oneoff
