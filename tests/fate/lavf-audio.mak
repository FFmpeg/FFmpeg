FATE_LAVF_AUDIO-$(call ENCDEC,  PCM_S16BE,    AIFF)             += aiff
FATE_LAVF_AUDIO-$(call ENCDEC,  PCM_ALAW,     PCM_ALAW)         += al
FATE_LAVF_AUDIO-$(call ENCDEC,  PCM_S16BE,    AU)               += au
FATE_LAVF_AUDIO-$(call ENCDEC,  ADPCM_YAMAHA, MMF)              += mmf
FATE_LAVF_AUDIO-$(call ENCDEC,  FLAC,         OGG)              += ogg
FATE_LAVF_AUDIO-$(call ENCDEC,  PCM_U8,       RSO)              += rso
FATE_LAVF_AUDIO-$(call ENCDEC,  PCM_S16LE,    SOX)              += sox
FATE_LAVF_AUDIO-$(call ENCDEC,  PCM_MULAW,    PCM_MULAW)        += ul
FATE_LAVF_AUDIO-$(call ENCDEC,  PCM_U8,       VOC)              += voc
FATE_LAVF_AUDIO-$(call ENCDEC,  PCM_S16LE,    VOC)              += s16.voc
FATE_LAVF_AUDIO-$(call ENCDEC,  PCM_S16LE,    WAV)              += wav

FATE_LAVF_AUDIO = $(FATE_LAVF_AUDIO-yes:%=fate-lavf-%)

$(FATE_LAVF_AUDIO): CMD = lavf_audio
$(FATE_LAVF_AUDIO): REF = $(SRC_PATH)/tests/ref/lavf/$(@:fate-lavf-%=%)
$(FATE_LAVF_AUDIO): $(AREF)

fate-lavf-al fate-lavf-ul: CMD = lavf_audio "" "" "-ar 44100"
fate-lavf-ogg: CMD = lavf_audio "" "-c:a flac"
fate-lavf-s16.voc: CMD = lavf_audio "-ac 2" "-c:a pcm_s16le"

FATE_AVCONV += $(FATE_LAVF_AUDIO)
fate-lavf-audio fate-lavf: $(FATE_LAVF_AUDIO)
