fate-acodec-%: CODEC = $(@:fate-acodec-%=%)
fate-acodec-%: SRC = tests/data/asynth-44100-2.wav
fate-acodec-%: CMD = enc_dec wav $(SRC) $(FMT) "-b:a 128k -c $(CODEC) $(ENCOPTS)" wav "-c pcm_s16le $(DECOPTS)" -keep
fate-acodec-%: CMP_UNIT = 2
fate-acodec-%: REF = $(SRC_PATH)/tests/ref/acodec/$(@:fate-acodec-%=%)

FATE_ACODEC_PCM-$(call ENCDEC, PCM_ALAW,  WAV) += alaw
FATE_ACODEC_PCM-$(call ENCDEC, PCM_MULAW, WAV) += mulaw
FATE_ACODEC_PCM-$(call ENCDEC, PCM_S8,    MOV) += s8
FATE_ACODEC_PCM-$(call ENCDEC, PCM_U8,    WAV) += u8
FATE_ACODEC_PCM-$(call ENCDEC, PCM_S16BE, MOV) += s16be
FATE_ACODEC_PCM-$(call ENCDEC, PCM_S16LE, WAV) += s16le
FATE_ACODEC_PCM-$(call ENCDEC, PCM_U16BE, NUT) += u16be
FATE_ACODEC_PCM-$(call ENCDEC, PCM_U16LE, NUT) += u16le
FATE_ACODEC_PCM-$(call ENCDEC, PCM_S24BE, MOV) += s24be
FATE_ACODEC_PCM-$(call ENCDEC, PCM_S24LE, WAV) += s24le
FATE_ACODEC_PCM-$(call ENCDEC, PCM_U24BE, NUT) += u24be
FATE_ACODEC_PCM-$(call ENCDEC, PCM_U24LE, NUT) += u24le
FATE_ACODEC_PCM-$(call ENCDEC, PCM_S32BE, MOV) += s32be
FATE_ACODEC_PCM-$(call ENCDEC, PCM_S32LE, WAV) += s32le
FATE_ACODEC_PCM-$(call ENCDEC, PCM_U32BE, NUT) += u32be
FATE_ACODEC_PCM-$(call ENCDEC, PCM_U32LE, NUT) += u32le
FATE_ACODEC_PCM-$(call ENCDEC, PCM_F32BE, AU)  += f32be
FATE_ACODEC_PCM-$(call ENCDEC, PCM_F32LE, WAV) += f32le
FATE_ACODEC_PCM-$(call ENCDEC, PCM_F64BE, AU)  += f64be
FATE_ACODEC_PCM-$(call ENCDEC, PCM_F64LE, WAV) += f64le
FATE_ACODEC_PCM-$(call ENCDEC, PCM_S8_PLANAR, NUT) += s8_planar
FATE_ACODEC_PCM-$(call ENCDEC, PCM_S16BE_PLANAR, NUT) += s16be_planar
FATE_ACODEC_PCM-$(call ENCDEC, PCM_S16LE_PLANAR, NUT) += s16le_planar
FATE_ACODEC_PCM-$(call ENCDEC, PCM_S24LE_PLANAR, NUT) += s24le_planar
FATE_ACODEC_PCM-$(call ENCDEC, PCM_S32LE_PLANAR, NUT) += s32le_planar

FATE_ACODEC_PCM := $(FATE_ACODEC_PCM-yes:%=fate-acodec-pcm-%)
FATE_ACODEC += $(FATE_ACODEC_PCM)
fate-acodec-pcm: $(FATE_ACODEC_PCM)

fate-acodec-pcm-%: FMT = wav
fate-acodec-pcm-%_planar: FMT = nut
fate-acodec-pcm-%: CODEC = pcm_$(@:fate-acodec-pcm-%=%)

fate-acodec-pcm-s8:   FMT = mov
fate-acodec-pcm-s%be: FMT = mov
fate-acodec-pcm-u%be: FMT = nut
fate-acodec-pcm-u%le: FMT = nut
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

FATE_ACODEC_ADPCM_TRELLIS-$(call ENCDEC, ADPCM_ADX,     ADX)  += adx
FATE_ACODEC_ADPCM_TRELLIS-$(call ENCDEC, ADPCM_IMA_QT,  AIFF) += ima_qt
FATE_ACODEC_ADPCM_TRELLIS-$(call ENCDEC, ADPCM_IMA_WAV, WAV)  += ima_wav
FATE_ACODEC_ADPCM_TRELLIS-$(call ENCDEC, ADPCM_MS,      WAV)  += ms
FATE_ACODEC_ADPCM_TRELLIS-$(call ENCDEC, ADPCM_SWF,     FLV)  += swf
FATE_ACODEC_ADPCM_TRELLIS-$(call ENCDEC, ADPCM_YAMAHA,  WAV)  += yamaha

FATE_ACODEC_ADPCM_TRELLIS := $(FATE_ACODEC_ADPCM_TRELLIS-yes:%=fate-acodec-adpcm-%-trellis)
FATE_ACODEC += $(FATE_ACODEC_ADPCM_TRELLIS)
fate-acodec-adpcm-trellis: $(FATE_ACODEC_ADPCM_TRELLIS)

fate-acodec-adpcm-%-trellis: CODEC = adpcm_$(@:fate-acodec-adpcm-%-trellis=%)
fate-acodec-adpcm-%-trellis: ENCOPTS = -trellis 5

fate-acodec-adpcm-adx-trellis:     FMT = adx
fate-acodec-adpcm-ima_qt-trellis:  FMT = aiff
fate-acodec-adpcm-ima_wav-trellis: FMT = wav
fate-acodec-adpcm-ms-trellis:      FMT = wav
fate-acodec-adpcm-swf-trellis:     FMT = flv
fate-acodec-adpcm-yamaha-trellis:  FMT = wav

FATE_ACODEC-$(call ENCDEC, MP2, MP2 MP3) += fate-acodec-mp2
fate-acodec-mp2: FMT = mp2
fate-acodec-mp2: CMP_SHIFT = -1924
fate-acodec-mp2: ENCOPTS = -b:a 128k

FATE_ACODEC-$(call ENCDEC, MP2FIXED MP2 , MP2 MP3) += fate-acodec-mp2fixed
fate-acodec-mp2fixed: FMT = mp2
fate-acodec-mp2fixed: CMP_SHIFT = -1924
fate-acodec-mp2fixed: ENCOPTS = -b:a 384k

FATE_ACODEC-$(call ENCDEC, ALAC, MOV) += fate-acodec-alac
fate-acodec-alac: FMT = mov
fate-acodec-alac: CODEC = alac -compression_level 1

FATE_ACODEC-$(call ENCDEC, DCA, DTS) += fate-acodec-dca
fate-acodec-dca: tests/data/asynth-44100-2.wav
fate-acodec-dca: SRC = tests/data/asynth-44100-2.wav
fate-acodec-dca: CMD = md5 -i $(TARGET_PATH)/$(SRC) -c:a dca -strict -2 -f dts -flags +bitexact
fate-acodec-dca: CMP = oneline
fate-acodec-dca: REF = 2aa580ac67820fce4f581b96ebb34acc

FATE_ACODEC-$(call ENCDEC, DCA, WAV) += fate-acodec-dca2
fate-acodec-dca2: CMD = enc_dec_pcm dts wav s16le $(SRC) -c:a dca -strict -2 -flags +bitexact
fate-acodec-dca2: REF = $(SRC)
fate-acodec-dca2: CMP = stddev
fate-acodec-dca2: CMP_SHIFT = -2048
fate-acodec-dca2: CMP_TARGET = 535
fate-acodec-dca2: SIZE_TOLERANCE = 1632

FATE_ACODEC-$(call ENCDEC, FLAC, FLAC) += fate-acodec-flac fate-acodec-flac-exact-rice
fate-acodec-flac: FMT = flac
fate-acodec-flac: CODEC = flac -compression_level 2

fate-acodec-flac-exact-rice: FMT = flac
fate-acodec-flac-exact-rice: CODEC = flac -compression_level 2 -exact_rice_parameters 1

FATE_ACODEC-$(call ENCDEC, G723_1, G723_1) += fate-acodec-g723_1
fate-acodec-g723_1: tests/data/asynth-8000-1.wav
fate-acodec-g723_1: SRC = tests/data/asynth-8000-1.wav
fate-acodec-g723_1: FMT = g723_1
fate-acodec-g723_1: CODEC = g723_1
fate-acodec-g723_1: ENCOPTS = -b:a 6.3k
fate-acodec-g723_1: CMP_SHIFT = 8

FATE_ACODEC-$(call ENCDEC, RA_144, WAV) += fate-acodec-ra144
fate-acodec-ra144: tests/data/asynth-8000-1.wav
fate-acodec-ra144: SRC = tests/data/asynth-8000-1.wav
fate-acodec-ra144: CMD = enc_dec_pcm rm wav s16le $(SRC) -c:a real_144
fate-acodec-ra144: REF = $(SRC)
fate-acodec-ra144: CMP = stddev
fate-acodec-ra144: CMP_TARGET = 4777
fate-acodec-ra144: CMP_SHIFT = -320

FATE_ACODEC-$(call ENCDEC, ROQ_DPCM, ROQ) += fate-acodec-roqaudio
fate-acodec-roqaudio: FMT = roq
fate-acodec-roqaudio: CODEC = roq_dpcm
fate-acodec-roqaudio: ENCOPTS = -ar 22050
fate-acodec-roqaudio: DECOPTS = -ar 44100

FATE_ACODEC-$(call ENCDEC, S302M, MPEGTS) += fate-acodec-s302m
fate-acodec-s302m: FMT = mpegts
fate-acodec-s302m: CODEC = s302m
fate-acodec-s302m: ENCOPTS = -ar 48000 -strict -2
fate-acodec-s302m: DECOPTS = -ar 44100

FATE_ACODEC-$(call ENCDEC, WAVPACK, WV) += fate-acodec-wavpack
fate-acodec-wavpack: FMT = wv
fate-acodec-wavpack: CODEC = wavpack -compression_level 1

FATE_ACODEC-$(call ENCDEC, TTA, TTA) += fate-acodec-tta
fate-acodec-tta: FMT = tta

FATE_ACODEC += $(FATE_ACODEC-yes)

$(FATE_ACODEC): tests/data/asynth-44100-2.wav

FATE_AVCONV += $(FATE_ACODEC)
fate-acodec: $(FATE_ACODEC)
