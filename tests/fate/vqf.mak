FATE_VQF += fate-twinvq
fate-twinvq: CMD = pcm -i $(SAMPLES)/vqf/achterba.vqf
fate-twinvq: CMP = oneoff
fate-twinvq: REF = $(SAMPLES)/vqf/achterba.pcm

FATE_VQF += fate-vqf-demux
fate-vqf-demux: CMD = md5 -i $(SAMPLES)/vqf/achterba.vqf -acodec copy -f framecrc

FATE_TESTS += $(FATE_VQF)
fate-vqf: $(FATE_VQF)
