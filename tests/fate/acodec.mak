fate-acodec-%: CODEC = $(@:fate-acodec-%=%)
fate-acodec-%: SRC = tests/data/asynth-44100-2.wav
fate-acodec-%: CMD = enc_dec wav $(SRC) $(FMT) "-b 128k -c $(CODEC)" wav "-c pcm_s16le" -keep
fate-acodec-%: CMP_UNIT = 2
fate-acodec-%: REF = $(SRC_PATH)/tests/ref/acodec/$(@:fate-acodec-%=%)

FATE_ACODEC_PCM-$(call ENCDEC, PCM_ALAW,  WAV) += alaw
FATE_ACODEC_PCM-$(call ENCDEC, PCM_MULAW, WAV) += mulaw
FATE_ACODEC_PCM-$(call ENCDEC, PCM_S8,    MOV) += s8
FATE_ACODEC_PCM-$(call ENCDEC, PCM_U8,    WAV) += u8
FATE_ACODEC_PCM-$(call ENCDEC, PCM_S16BE, MOV) += s16be
FATE_ACODEC_PCM-$(call ENCDEC, PCM_S16LE, WAV) += s16le
FATE_ACODEC_PCM-$(call ENCDEC, PCM_S24BE, MOV) += s24be
FATE_ACODEC_PCM-$(call ENCDEC, PCM_S24LE, WAV) += s24le
FATE_ACODEC_PCM-$(call ENCDEC, PCM_S32BE, MOV) += s32be
FATE_ACODEC_PCM-$(call ENCDEC, PCM_S32LE, WAV) += s32le
FATE_ACODEC_PCM-$(call ENCDEC, PCM_F32BE, AU)  += f32be
FATE_ACODEC_PCM-$(call ENCDEC, PCM_F32LE, WAV) += f32le
FATE_ACODEC_PCM-$(call ENCDEC, PCM_F64BE, AU)  += f64be
FATE_ACODEC_PCM-$(call ENCDEC, PCM_F64LE, WAV) += f64le

FATE_ACODEC_PCM := $(FATE_ACODEC_PCM-yes:%=fate-acodec-pcm-%)
FATE_ACODEC += $(FATE_ACODEC_PCM)
fate-acodec-pcm: $(FATE_ACODEC_PCM)

fate-acodec-pcm-%: FMT = wav
fate-acodec-pcm-%: CODEC = pcm_$(@:fate-acodec-pcm-%=%)

fate-acodec-pcm-s8:   FMT = mov
fate-acodec-pcm-s%be: FMT = mov
fate-acodec-pcm-f%be: FMT = au

FATE_ACODEC_ADPCM-$(call ENCDEC, ADPCM_ADX,     ADX)  += adx
FATE_ACODEC_ADPCM-$(call ENCDEC, ADPCM_IMA_QT,  AIFF) += ima_qt
FATE_ACODEC_ADPCM-$(call ENCDEC, ADPCM_IMA_WAV, WAV)  += ima_wav
FATE_ACODEC_ADPCM-$(call ENCDEC, ADPCM_MS,      WAV)  += ms
FATE_ACODEC_ADPCM-$(call ENCDEC, ADPCM_SWF,     FLV)  += swf
FATE_ACODEC_ADPCM-$(call ENCDEC, ADPCM_YAMAHA,  WAV)  += yamaha

FATE_ACODEC_ADPCM := $(FATE_ACODEC_ADPCM-yes:%=fate-acodec-adpcm-%)
FATE_ACODEC += $(FATE_ACODEC_ADPCM)
fate-acodec-adpcm: $(FATE_ACODEC_ADPCM)

fate-acodec-adpcm-%: CODEC = adpcm_$(@:fate-acodec-adpcm-%=%)

fate-acodec-adpcm-adx:     FMT = adx
fate-acodec-adpcm-ima_qt:  FMT = aiff
fate-acodec-adpcm-ima_wav: FMT = wav
fate-acodec-adpcm-ms:      FMT = wav
fate-acodec-adpcm-swf:     FMT = flv
fate-acodec-adpcm-yamaha:  FMT = wav

FATE_ACODEC-$(call ENCDEC, MP2, MP2 MP3) += fate-acodec-mp2
fate-acodec-mp2: FMT = mp2
fate-acodec-mp2: CMP_SHIFT = -1924

FATE_ACODEC-$(call ENCDEC, ALAC, MOV) += fate-acodec-alac
fate-acodec-alac: FMT = mov
fate-acodec-alac: CODEC = alac -compression_level 1

FATE_ACODEC-$(call ENCDEC, FLAC, FLAC) += fate-acodec-flac
fate-acodec-flac: FMT = flac
fate-acodec-flac: CODEC = flac -compression_level 2

FATE_ACODEC += $(FATE_ACODEC-yes)

$(FATE_ACODEC): tests/data/asynth-44100-2.wav

FATE_AVCONV += $(FATE_ACODEC)
fate-acodec: $(FATE_ACODEC)
