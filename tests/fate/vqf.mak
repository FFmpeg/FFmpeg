FATE_SAMPLES_AVCONV += fate-twinvq
fate-twinvq: CMD = pcm -i $(SAMPLES)/vqf/achterba.vqf
fate-twinvq: CMP = oneoff
fate-twinvq: REF = $(SAMPLES)/vqf/achterba.pcm

FATE_SAMPLES_AVCONV += fate-vqf-demux
fate-vqf-demux: CMD = md5 -i $(SAMPLES)/vqf/achterba.vqf -acodec copy -f framecrc
