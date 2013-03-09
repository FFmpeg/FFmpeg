FATE_BINKAUDIO-$(call DEMDEC, BINK, BINKAUDIO_DCT) += fate-binkaudio-dct
fate-binkaudio-dct: CMD = pcm -i $(SAMPLES)/bink/binkaudio_dct.bik
fate-binkaudio-dct: REF = $(SAMPLES)/bink/binkaudio_dct.pcm
fate-binkaudio-dct: FUZZ = 2

FATE_BINKAUDIO-$(call DEMDEC, BINK, BINKAUDIO_RDFT) += fate-binkaudio-rdft
fate-binkaudio-rdft: CMD = pcm -i $(SAMPLES)/bink/binkaudio_rdft.bik
fate-binkaudio-rdft: REF = $(SAMPLES)/bink/binkaudio_rdft.pcm
fate-binkaudio-rdft: FUZZ = 2

$(FATE_BINKAUDIO-yes): CMP = oneoff

FATE_SAMPLES_AUDIO += $(FATE_BINKAUDIO-yes)
fate-binkaudio: $(FATE_BINKAUDIO-yes)

FATE_SAMPLES_AUDIO-$(call DEMDEC, BMV, BMV_AUDIO) += fate-bmv-audio
fate-bmv-audio: CMD = framecrc -i $(SAMPLES)/bmv/SURFING-partial.BMV -vn

FATE_SAMPLES_AUDIO-$(call DEMDEC, DSICIN, DSICINAUDIO) += fate-delphine-cin-audio
fate-delphine-cin-audio: CMD = framecrc -i $(SAMPLES)/delphine-cin/LOGO-partial.CIN -vn

FATE_SAMPLES_AUDIO-$(call DEMDEC, MPEGTS, DCA) += fate-dts
fate-dts: CMD = pcm -i $(SAMPLES)/dts/dts.ts
fate-dts: CMP = oneoff
fate-dts: REF = $(SAMPLES)/dts/dts.pcm

FATE_SAMPLES_AUDIO-$(call DEMDEC, AVI, IMC) += fate-imc
fate-imc: CMD = pcm -i $(SAMPLES)/imc/imc.avi
fate-imc: CMP = oneoff
fate-imc: REF = $(SAMPLES)/imc/imc.pcm

FATE_SAMPLES_AUDIO-$(call DEMDEC, FLV, NELLYMOSER) += fate-nellymoser
fate-nellymoser: CMD = pcm -i $(SAMPLES)/nellymoser/nellymoser.flv
fate-nellymoser: CMP = oneoff
fate-nellymoser: REF = $(SAMPLES)/nellymoser/nellymoser.pcm

FATE_SAMPLES_AUDIO-$(call ENCMUX, NELLYMOSER, FLV) += fate-nellymoser-aref-encode
fate-nellymoser-aref-encode: $(AREF) ./tests/data/asynth-16000-1.wav
fate-nellymoser-aref-encode: CMD = enc_dec_pcm flv wav s16le $(REF) -c:a nellymoser
fate-nellymoser-aref-encode: CMP = stddev
fate-nellymoser-aref-encode: REF = ./tests/data/asynth-16000-1.wav
fate-nellymoser-aref-encode: CMP_SHIFT = -244
fate-nellymoser-aref-encode: CMP_TARGET = 9612
fate-nellymoser-aref-encode: SIZE_TOLERANCE = 268

FATE_SAMPLES_AUDIO-$(call DEMDEC, PAF, PAF_AUDIO) += fate-paf-audio
fate-paf-audio: CMD = framecrc -i $(SAMPLES)/paf/hod1-partial.paf -vn

FATE_SAMPLES_AUDIO-$(call DEMDEC, VMD, VMDAUDIO) += fate-sierra-vmd-audio
fate-sierra-vmd-audio: CMD = framecrc -i $(SAMPLES)/vmd/12.vmd -vn

FATE_SAMPLES_AUDIO-$(call DEMDEC, SMACKER, SMACKAUD) += fate-smacker-audio
fate-smacker-audio: CMD = framecrc -i $(SAMPLES)/smacker/wetlogo.smk -vn

FATE_SAMPLES_AUDIO-$(call DEMDEC, SMUSH, VIMA) += fate-vima
fate-vima: CMD = framecrc -i $(SAMPLES)/smush/ronin_part.znm -vn

FATE_SAMPLES_AUDIO-$(call DEMDEC, WSVQA, WS_SND1) += fate-ws_snd
fate-ws_snd: CMD = md5 -i $(SAMPLES)/vqa/ws_snd.vqa -f s16le

FATE_SAMPLES_AUDIO += $(FATE_SAMPLES_AUDIO-yes)

FATE_SAMPLES_FFMPEG += $(FATE_SAMPLES_AUDIO)
fate-audio: $(FATE_SAMPLES_AUDIO)
