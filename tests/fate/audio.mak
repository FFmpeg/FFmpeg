FATE_BINKAUDIO-$(call DEMDEC, BINK, BINKAUDIO_DCT) += fate-binkaudio-dct
fate-binkaudio-dct: CMD = pcm -i $(TARGET_SAMPLES)/bink/binkaudio_dct.bik
fate-binkaudio-dct: REF = $(SAMPLES)/bink/binkaudio_dct.pcm
fate-binkaudio-dct: FUZZ = 2

FATE_BINKAUDIO-$(call DEMDEC, BINK, BINKAUDIO_RDFT) += fate-binkaudio-rdft
fate-binkaudio-rdft: CMD = pcm -i $(TARGET_SAMPLES)/bink/binkaudio_rdft.bik
fate-binkaudio-rdft: REF = $(SAMPLES)/bink/binkaudio_rdft.pcm
fate-binkaudio-rdft: FUZZ = 2

$(FATE_BINKAUDIO-yes): CMP = oneoff

FATE_SAMPLES_AVCONV += $(FATE_BINKAUDIO-yes)
fate-binkaudio: $(FATE_BINKAUDIO-yes)

FATE_SAMPLES_AVCONV-$(call DEMDEC, BMV, BMV_AUDIO) += fate-bmv-audio
fate-bmv-audio: CMD = framecrc -i $(TARGET_SAMPLES)/bmv/SURFING-partial.BMV -vn

FATE_DCA-$(CONFIG_MPEGTS_DEMUXER) += fate-dca-core
fate-dca-core: CMD = pcm -i $(TARGET_SAMPLES)/dts/dts.ts
fate-dca-core: CMP = oneoff
fate-dca-core: REF = $(SAMPLES)/dts/dts.pcm

FATE_DCA-$(CONFIG_DTS_DEMUXER) += fate-dca-xll
fate-dca-xll: CMD = pcm -disable_xll 0 -i $(TARGET_SAMPLES)/dts/master_audio_7.1_24bit.dts
fate-dca-xll: CMP = oneoff
fate-dca-xll: REF = $(SAMPLES)/dts/master_audio_7.1_24bit_2.pcm

FATE_SAMPLES_AVCONV-$(CONFIG_DCA_DECODER) += $(FATE_DCA-yes)
fate-dca: $(FATE_DCA-yes)

FATE_SAMPLES_AVCONV-$(call DEMDEC, DSICIN, DSICINAUDIO) += fate-delphine-cin-audio
fate-delphine-cin-audio: CMD = framecrc -i $(TARGET_SAMPLES)/delphine-cin/LOGO-partial.CIN -vn

FATE_SAMPLES_AVCONV-$(call DEMDEC, DSS, DSS_SP) += fate-dss-lp fate-dss-sp
fate-dss-lp: CMD = framecrc -i $(TARGET_SAMPLES)/dss/lp.dss -frames 30
fate-dss-sp: CMD = framecrc -i $(TARGET_SAMPLES)/dss/sp.dss -frames 30

FATE_SAMPLES_AVCONV-$(call DEMDEC, AVI, IMC) += fate-imc
fate-imc: CMD = pcm -i $(TARGET_SAMPLES)/imc/imc.avi
fate-imc: CMP = oneoff
fate-imc: REF = $(SAMPLES)/imc/imc.pcm

FATE_SAMPLES_AVCONV-$(call DEMDEC, FLV, NELLYMOSER) += fate-nellymoser
fate-nellymoser: CMD = pcm -i $(TARGET_SAMPLES)/nellymoser/nellymoser.flv
fate-nellymoser: CMP = oneoff
fate-nellymoser: REF = $(SAMPLES)/nellymoser/nellymoser.pcm

FATE_SAMPLES_AVCONV-$(call DEMDEC, AVI, ON2AVC) += fate-on2avc
fate-on2avc: CMD = framecrc -i $(TARGET_SAMPLES)/vp7/potter-40.vp7 -frames 30 -vn

FATE_SAMPLES_AVCONV-$(call DEMDEC, PAF, PAF_AUDIO) += fate-paf-audio
fate-paf-audio: CMD = framecrc -i $(TARGET_SAMPLES)/paf/hod1-partial.paf -vn

FATE_SAMPLES_AVCONV-$(call DEMDEC, VMD, VMDAUDIO) += fate-sierra-vmd-audio
fate-sierra-vmd-audio: CMD = framecrc -i $(TARGET_SAMPLES)/vmd/12.vmd -vn

FATE_SAMPLES_AVCONV-$(call DEMDEC, SMACKER, SMACKAUD) += fate-smacker-audio
fate-smacker-audio: CMD = framecrc -i $(TARGET_SAMPLES)/smacker/wetlogo.smk -vn

FATE_SAMPLES_AVCONV-$(call DEMDEC, WSVQA, WS_SND1) += fate-ws_snd
fate-ws_snd: CMD = md5 -i $(TARGET_SAMPLES)/vqa/ws_snd.vqa -f s16le
