FATE_ASYNCTS += fate-filter-asyncts
fate-filter-asyncts: SRC = $(SAMPLES)/nellymoser/nellymoser-discont.flv
fate-filter-asyncts: CMD = pcm -i $(SRC) -af aresample=min_comp=0.001:min_hard_comp=0.1
fate-filter-asyncts: CMP = oneoff
fate-filter-asyncts: REF = $(SAMPLES)/nellymoser/nellymoser-discont.pcm

FATE_FILTER += $(FATE_ASYNCTS)
FATE_SAMPLES_AVCONV += $(FATE_ASYNCTS)

fate-filter: $(FATE_FILTER)
