FATE_WMAPRO += fate-wmapro-2ch
fate-wmapro-2ch: CMD = pcm -i $(SAMPLES)/wmapro/Beethovens_9th-1_small.wma
fate-wmapro-2ch: CMP = oneoff
fate-wmapro-2ch: REF = $(SAMPLES)/wmapro/Beethovens_9th-1_small.pcm

FATE_WMAPRO += fate-wmapro-5.1
fate-wmapro-5.1: CMD = pcm -i $(SAMPLES)/wmapro/latin_192_mulitchannel_cut.wma
fate-wmapro-5.1: CMP = oneoff
fate-wmapro-5.1: REF = $(SAMPLES)/wmapro/latin_192_mulitchannel_cut.pcm

FATE_WMAPRO += fate-wmapro-ism
fate-wmapro-ism: CMD = pcm -i $(SAMPLES)/isom/vc1-wmapro.ism -vn
fate-wmapro-ism: CMP = oneoff
fate-wmapro-ism: REF = $(SAMPLES)/isom/vc1-wmapro.pcm

FATE_TESTS += $(FATE_WMAPRO)
fate-wmapro: $(FATE_WMAPRO)

FATE_WMAVOICE += fate-wmavoice-7k
fate-wmavoice-7k: CMD = pcm -i $(SAMPLES)/wmavoice/streaming_CBR-7K.wma
fate-wmavoice-7k: CMP = stddev
fate-wmavoice-7k: REF = $(SAMPLES)/wmavoice/streaming_CBR-7K.pcm
fate-wmavoice-7k: FUZZ = 3

FATE_WMAVOICE += fate-wmavoice-11k
fate-wmavoice-11k: CMD = pcm -i $(SAMPLES)/wmavoice/streaming_CBR-11K.wma
fate-wmavoice-11k: CMP = stddev
fate-wmavoice-11k: REF = $(SAMPLES)/wmavoice/streaming_CBR-11K.pcm
fate-wmavoice-11k: FUZZ = 3

FATE_WMAVOICE += fate-wmavoice-19k
fate-wmavoice-19k: CMD = pcm -i $(SAMPLES)/wmavoice/streaming_CBR-19K.wma
fate-wmavoice-19k: CMP = stddev
fate-wmavoice-19k: REF = $(SAMPLES)/wmavoice/streaming_CBR-19K.pcm
fate-wmavoice-19k: FUZZ = 3

FATE_TESTS += $(FATE_WMAVOICE)
fate-wmavoice: $(FATE_WMAVOICE)
