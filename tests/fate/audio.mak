FATE_BINKAUDIO += fate-binkaudio-dct
fate-binkaudio-dct: CMD = pcm -i $(SAMPLES)/bink/binkaudio_dct.bik
fate-binkaudio-dct: CMP = oneoff
fate-binkaudio-dct: REF = $(SAMPLES)/bink/binkaudio_dct.pcm
fate-binkaudio-dct: FUZZ = 2

FATE_BINKAUDIO += fate-binkaudio-rdft
fate-binkaudio-rdft: CMD = pcm -i $(SAMPLES)/bink/binkaudio_rdft.bik
fate-binkaudio-rdft: CMP = oneoff
fate-binkaudio-rdft: REF = $(SAMPLES)/bink/binkaudio_rdft.pcm
fate-binkaudio-rdft: FUZZ = 2

FATE_AVCONV += $(FATE_BINKAUDIO)
fate-binkaudio: $(FATE_BINKAUDIO)

FATE_AVCONV += fate-bmv-audio
fate-bmv-audio: CMD = framecrc -i $(SAMPLES)/bmv/SURFING-partial.BMV -vn

FATE_AVCONV += fate-delphine-cin-audio
fate-delphine-cin-audio: CMD = framecrc -i $(SAMPLES)/delphine-cin/LOGO-partial.CIN -vn

FATE_AVCONV += fate-dts
fate-dts: CMD = pcm -i $(SAMPLES)/dts/dts.ts
fate-dts: CMP = oneoff
fate-dts: REF = $(SAMPLES)/dts/dts.pcm

FATE_AVCONV += fate-imc
fate-imc: CMD = pcm -i $(SAMPLES)/imc/imc.avi
fate-imc: CMP = oneoff
fate-imc: REF = $(SAMPLES)/imc/imc.pcm

FATE_AVCONV += fate-nellymoser
fate-nellymoser: CMD = pcm -i $(SAMPLES)/nellymoser/nellymoser.flv
fate-nellymoser: CMP = oneoff
fate-nellymoser: REF = $(SAMPLES)/nellymoser/nellymoser.pcm

FATE_AVCONV += fate-smacker-audio
fate-smacker-audio: CMD = framecrc -i $(SAMPLES)/smacker/wetlogo.smk -vn

FATE_AVCONV += fate-ws_snd
fate-ws_snd: CMD = md5 -i $(SAMPLES)/vqa/ws_snd.vqa -f s16le
