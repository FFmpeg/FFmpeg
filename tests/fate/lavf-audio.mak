FATE_LAVF_AUDIO-$(call ENCDEC,  PCM_S16BE,    AIFF)             += aiff
FATE_LAVF_AUDIO-$(call ENCDEC,  PCM_ALAW,     PCM_ALAW)         += al
FATE_LAVF_AUDIO-$(call ENCDEC,  PCM_S16BE,    AU)               += au
FATE_LAVF_AUDIO-$(call ENCDEC,  PCM_S16BE,    CAF)              += caf
FATE_LAVF_AUDIO-$(call ENCDEC,  ADPCM_YAMAHA, MMF)              += mmf
FATE_LAVF_AUDIO-$(call ENCDEC,  FLAC,         OGG)              += ogg
FATE_LAVF_AUDIO-$(call ENCDEC,  PCM_MULAW,    PCM_MULAW)        += ul
FATE_LAVF_AUDIO-$(call ENCDEC,  PCM_S16LE,    IRCAM)            += ircam
FATE_LAVF_AUDIO-$(call ENCDEC,  PCM_S16LE,    VOC)              += s16.voc
FATE_LAVF_AUDIO-$(call ENCDEC,  PCM_S16LE,    WAV)              += wav
FATE_LAVF_AUDIO-$(call ENCDEC,  PCM_S16LE,    WAV)              += peak.wav
FATE_LAVF_AUDIO-$(call ENCDEC,  PCM_S16LE,    WAV)              += peak_only.wav
FATE_LAVF_AUDIO-$(call ENCDEC,  PCM_S16LE,    W64)              += w64
FATE_LAVF_AUDIO-$(call ENCDEC,  TTA,          TTA)              += tta
FATE_LAVF_AUDIO-$(call ENCMUX,  TTA,          MATROSKA_AUDIO)   += mka
FATE_LAVF_AUDIO_RESAMPLE-$(call ENCDEC,  PCM_S16BE_PLANAR, AST) += ast
FATE_LAVF_AUDIO_RESAMPLE-$(call ENCDEC,  PCM_U8,           RSO) += rso
FATE_LAVF_AUDIO_RESAMPLE-$(call ENCDEC,  PCM_S16LE,        SOX) += sox
FATE_LAVF_AUDIO_RESAMPLE-$(call ENCDEC,  PCM_U8,           VOC) += voc
FATE_LAVF_AUDIO_RESAMPLE-$(call ENCDEC,  WAVPACK,          WV)  += wv

FATE_LAVF_AUDIO-$(CONFIG_ARESAMPLE_FILTER) += $(FATE_LAVF_AUDIO_RESAMPLE-yes)
FATE_LAVF_AUDIO = $(FATE_LAVF_AUDIO-yes:%=fate-lavf-%)
FATE_LAVF_AUDIO := $(if $(call ENCDEC, PCM_S16LE, CRC PCM_S16LE), $(FATE_LAVF_AUDIO))

$(FATE_LAVF_AUDIO): CMD = lavf_audio
$(FATE_LAVF_AUDIO): REF = $(SRC_PATH)/tests/ref/lavf/$(@:fate-lavf-%=%)
$(FATE_LAVF_AUDIO): $(AREF)

fate-lavf-al fate-lavf-ul: CMD = lavf_audio "" "" "-ar 44100"
fate-lavf-ogg: CMD = lavf_audio "" "-c:a flac"
fate-lavf-s16.voc: CMD = lavf_audio "-ac 2" "-c:a pcm_s16le"
fate-lavf-ast: CMD = lavf_audio "-ac 2" "-loopstart 1 -loopend 10"
fate-lavf-mka: CMD = lavf_audio "" "-c:a tta"
fate-lavf-voc: CMD = lavf_audio "" "-c:a pcm_u8"
fate-lavf-peak.wav: CMD = lavf_audio "" "-write_peak on"
fate-lavf-peak_only.wav: CMD = lavf_audio "" "-write_peak only" "" disable_crc

FATE_AVCONV += $(FATE_LAVF_AUDIO)
fate-lavf-audio fate-lavf: $(FATE_LAVF_AUDIO)

FATE_WAV_FFPROBE-$(CONFIG_WAV_DEMUXER) += fate-wav-chapters
fate-wav-chapters: CMD = probechapters $(TARGET_SAMPLES)/wav/200828-005.wav

FATE_SAMPLES_FFPROBE += $(FATE_WAV_FFPROBE-yes)
