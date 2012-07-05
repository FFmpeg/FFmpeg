FATE_ASYNCTS += fate-filter-asyncts
fate-filter-asyncts: SRC = $(SAMPLES)/nellymoser/nellymoser-discont.flv
fate-filter-asyncts: CMD = md5 -i $(SRC) -af asyncts -f wav
fate-filter-asyncts: CMP = oneline
fate-filter-asyncts: REF = 5faa5d6ecec8d0c982e80a090d114576

FATE_FILTER += $(FATE_ASYNCTS)
FATE_SAMPLES_AVCONV += $(FATE_ASYNCTS)

fate-filter: $(FATE_FILTER)
