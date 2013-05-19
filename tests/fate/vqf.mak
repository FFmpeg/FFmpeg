FATE_SAMPLES_AVCONV-$(call DEMDEC, VQF, TWINVQ) += fate-twinvq
fate-twinvq: CMD = pcm -i $(TARGET_SAMPLES)/vqf/achterba.vqf
fate-twinvq: CMP = oneoff
fate-twinvq: REF = $(SAMPLES)/vqf/achterba.pcm

FATE_SAMPLES_AVCONV-$(CONFIG_VQF_DEMUXER) += fate-vqf-demux
fate-vqf-demux: CMD = md5 -i $(TARGET_SAMPLES)/vqf/achterba.vqf -acodec copy -f framecrc
