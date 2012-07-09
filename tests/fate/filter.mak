FATE_AMIX += fate-filter-amix-simple
fate-filter-amix-simple: CMD = avconv -filter_complex amix -i $(SRC) -ss 3 -i $(SRC1) -f f32le -
fate-filter-amix-simple: REF = $(SAMPLES)/filter/amix_simple.pcm

FATE_AMIX += fate-filter-amix-first
fate-filter-amix-first: CMD = avconv -filter_complex amix=duration=first -ss 4 -i $(SRC) -i $(SRC1) -f f32le -
fate-filter-amix-first: REF = $(SAMPLES)/filter/amix_first.pcm

FATE_AMIX += fate-filter-amix-transition
fate-filter-amix-transition: tests/data/asynth-44100-2-3.wav
fate-filter-amix-transition: SRC2 = $(TARGET_PATH)/tests/data/asynth-44100-2-3.wav
fate-filter-amix-transition: CMD = avconv -filter_complex amix=inputs=3:dropout_transition=0.5 -i $(SRC) -ss 2 -i $(SRC1) -ss 4 -i $(SRC2) -f f32le -
fate-filter-amix-transition: REF = $(SAMPLES)/filter/amix_transition.pcm

$(FATE_AMIX): tests/data/asynth-44100-2.wav tests/data/asynth-44100-2-2.wav
$(FATE_AMIX): SRC  = $(TARGET_PATH)/tests/data/asynth-44100-2.wav
$(FATE_AMIX): SRC1 = $(TARGET_PATH)/tests/data/asynth-44100-2-2.wav
$(FATE_AMIX): CMP  = oneoff

FATE_FILTER += $(FATE_AMIX)
FATE_SAMPLES_AVCONV += $(FATE_AMIX)

FATE_ASYNCTS += fate-filter-asyncts
fate-filter-asyncts: SRC = $(SAMPLES)/nellymoser/nellymoser-discont.flv
fate-filter-asyncts: CMD = pcm -i $(SRC) -af asyncts
fate-filter-asyncts: CMP = oneoff
fate-filter-asyncts: REF = $(SAMPLES)/nellymoser/nellymoser-discont.pcm

FATE_FILTER += $(FATE_ASYNCTS)
FATE_SAMPLES_AVCONV += $(FATE_ASYNCTS)

fate-filter-delogo: CMD = framecrc -i $(SAMPLES)/real/rv30.rm -vf delogo=show=0:x=290:y=25:w=26:h=16 -an

FATE_FILTER += fate-filter-delogo
FATE_SAMPLES_AVCONV += fate-filter-delogo

fate-filter: $(FATE_FILTER)
