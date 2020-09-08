FATE_G722-$(call DEMDEC, G722, ADPCM_G722) += fate-g722dec-1
fate-g722dec-1: CMD = framecrc -i $(TARGET_SAMPLES)/g722/conf-adminmenu-162.g722

FATE_G722-$(call ENCMUX, ADPCM_G722, WAV) += fate-g722-encode
fate-g722-encode: tests/data/asynth-16000-1.wav
fate-g722-encode: SRC = tests/data/asynth-16000-1.wav
fate-g722-encode: CMD = enc_dec_pcm wav framemd5 s16le $(SRC) -c:a g722

FATE_VOICE-yes += $(FATE_G722-yes)
fate-g722: $(FATE_G722-yes)

FATE_G723_1 += fate-g723_1-dec-1
fate-g723_1-dec-1: CMD = framecrc -postfilter 0 -i $(TARGET_SAMPLES)/g723_1/ineqd53.tco -af aresample

FATE_G723_1 += fate-g723_1-dec-2
fate-g723_1-dec-2: CMD = framecrc -postfilter 0 -i $(TARGET_SAMPLES)/g723_1/overd53.tco -af aresample

FATE_G723_1 += fate-g723_1-dec-3
fate-g723_1-dec-3: CMD = framecrc -postfilter 1 -i $(TARGET_SAMPLES)/g723_1/overd63p.tco -af aresample

FATE_G723_1 += fate-g723_1-dec-4
fate-g723_1-dec-4: CMD = framecrc -postfilter 0 -i $(TARGET_SAMPLES)/g723_1/pathd53.tco -af aresample

FATE_G723_1 += fate-g723_1-dec-5
fate-g723_1-dec-5: CMD = framecrc -postfilter 1 -i $(TARGET_SAMPLES)/g723_1/pathd63p.tco -af aresample

FATE_G723_1 += fate-g723_1-dec-6
fate-g723_1-dec-6: CMD = framecrc -postfilter 1 -i $(TARGET_SAMPLES)/g723_1/tamed63p.tco -af aresample

FATE_G723_1 += fate-g723_1-dec-7
fate-g723_1-dec-7: CMD = framecrc -postfilter 1 -i $(TARGET_SAMPLES)/g723_1/dtx63b.tco -af aresample

FATE_G723_1 += fate-g723_1-dec-8
fate-g723_1-dec-8: CMD = framecrc -postfilter 1 -i $(TARGET_SAMPLES)/g723_1/dtx63e.tco -af aresample

FATE_VOICE-$(call DEMDEC, G723_1, G723_1) += $(FATE_G723_1)
fate-g723_1: $(FATE_G723_1)

FATE_G726 += fate-g726-encode-2bit
fate-g726-encode-2bit: CMD = enc_dec_pcm wav framemd5 s16le $(SRC) -c:a g726 -b:a 16k

FATE_G726 += fate-g726-encode-3bit
fate-g726-encode-3bit: CMD = enc_dec_pcm wav framemd5 s16le $(SRC) -c:a g726 -b:a 24k

FATE_G726 += fate-g726-encode-4bit
fate-g726-encode-4bit: CMD = enc_dec_pcm wav framemd5 s16le $(SRC) -c:a g726 -b:a 32k

FATE_G726 += fate-g726-encode-5bit
fate-g726-encode-5bit: CMD = enc_dec_pcm wav framemd5 s16le $(SRC) -c:a g726 -b:a 40k

$(FATE_G726): tests/data/asynth-8000-1.wav
$(FATE_G726): SRC = tests/data/asynth-8000-1.wav

FATE_VOICE-$(call ENCMUX, ADPCM_G726, WAV) += $(FATE_G726)
fate-g726: $(FATE_G726)

FATE_GSM-$(call DEMDEC, WAV, GSM) += fate-gsm-ms
fate-gsm-ms: CMD = framecrc -i $(TARGET_SAMPLES)/gsm/ciao.wav

FATE_GSM-$(call DEMDEC, MOV, GSM) += fate-gsm-toast
fate-gsm-toast: CMD = framecrc -i $(TARGET_SAMPLES)/gsm/sample-gsm-8000.mov -t 10

FATE_VOICE-yes += $(FATE_GSM-yes)
fate-gsm: $(FATE_GSM-yes)

FATE_VOICE-$(call DEMDEC, QCP, QCELP) += fate-qcelp
fate-qcelp: CMD = pcm -i $(TARGET_SAMPLES)/qcp/0036580847.QCP
fate-qcelp: CMP = oneoff
fate-qcelp: REF = $(SAMPLES)/qcp/0036580847.pcm

FATE_VOICE-$(call DEMDEC, WAV, TRUESPEECH) += fate-truespeech
fate-truespeech: CMD = pcm -i $(TARGET_SAMPLES)/truespeech/a6.wav
fate-truespeech: CMP = oneoff
fate-truespeech: REF = $(SAMPLES)/truespeech/a6.pcm

FATE_SAMPLES_FFMPEG += $(FATE_VOICE-yes)
fate-voice: $(FATE_VOICE-yes)
