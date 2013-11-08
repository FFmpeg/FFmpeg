FATE_AAC += fate-aac-al04_44
fate-aac-al04_44: CMD = pcm -i $(TARGET_SAMPLES)/aac/al04_44.mp4
fate-aac-al04_44: REF = $(SAMPLES)/aac/al04_44.s16

FATE_AAC += fate-aac-al05_44
fate-aac-al05_44: CMD = pcm -i $(TARGET_SAMPLES)/aac/al05_44.mp4
fate-aac-al05_44: REF = $(SAMPLES)/aac/al05_44.s16

FATE_AAC += fate-aac-al06_44
fate-aac-al06_44: CMD = pcm -i $(TARGET_SAMPLES)/aac/al06_44.mp4
fate-aac-al06_44: REF = $(SAMPLES)/aac/al06_44_reorder.s16

FATE_AAC += fate-aac-al07_96
fate-aac-al07_96: CMD = pcm -i $(TARGET_SAMPLES)/aac/al07_96.mp4
fate-aac-al07_96: REF = $(SAMPLES)/aac/al07_96_reorder.s16

FATE_AAC += fate-aac-al15_44
fate-aac-al15_44: CMD = pcm -i $(TARGET_SAMPLES)/aac/al15_44.mp4
fate-aac-al15_44: REF = $(SAMPLES)/aac/al15_44_reorder.s16

FATE_AAC += fate-aac-al17_44
fate-aac-al17_44: CMD = pcm -i $(TARGET_SAMPLES)/aac/al17_44.mp4
fate-aac-al17_44: REF = $(SAMPLES)/aac/al17_44.s16

FATE_AAC += fate-aac-al18_44
fate-aac-al18_44: CMD = pcm -i $(TARGET_SAMPLES)/aac/al18_44.mp4
fate-aac-al18_44: REF = $(SAMPLES)/aac/al18_44.s16

FATE_AAC += fate-aac-am00_88
fate-aac-am00_88: CMD = pcm -i $(TARGET_SAMPLES)/aac/am00_88.mp4
fate-aac-am00_88: REF = $(SAMPLES)/aac/am00_88.s16

FATE_AAC += fate-aac-am05_44
fate-aac-am05_44: CMD = pcm -i $(TARGET_SAMPLES)/aac/am05_44.mp4
fate-aac-am05_44: REF = $(SAMPLES)/aac/am05_44_reorder.s16

FATE_AAC += fate-aac-al_sbr_hq_cm_48_2
fate-aac-al_sbr_hq_cm_48_2: CMD = pcm -i $(TARGET_SAMPLES)/aac/al_sbr_cm_48_2.mp4
fate-aac-al_sbr_hq_cm_48_2: REF = $(SAMPLES)/aac/al_sbr_hq_cm_48_2.s16

FATE_AAC += fate-aac-al_sbr_hq_cm_48_5.1
fate-aac-al_sbr_hq_cm_48_5.1: CMD = pcm -i $(TARGET_SAMPLES)/aac/al_sbr_cm_48_5.1.mp4
fate-aac-al_sbr_hq_cm_48_5.1: REF = $(SAMPLES)/aac/al_sbr_hq_cm_48_5.1_reorder.s16

FATE_AAC += fate-aac-al_sbr_ps_06_ur
fate-aac-al_sbr_ps_06_ur: CMD = pcm -i $(TARGET_SAMPLES)/aac/al_sbr_ps_06_new.mp4
fate-aac-al_sbr_ps_06_ur: REF = $(SAMPLES)/aac/al_sbr_ps_06_ur.s16

FATE_AAC += fate-aac-ap05_48
fate-aac-ap05_48: CMD = pcm -i $(TARGET_SAMPLES)/aac/ap05_48.mp4
fate-aac-ap05_48: REF = $(SAMPLES)/aac/ap05_48.s16

FATE_AAC += fate-aac-er_ad6000np_44_ep0
fate-aac-er_ad6000np_44_ep0: CMD = pcm -i $(TARGET_SAMPLES)/aac/er_ad6000np_44_ep0.mp4
fate-aac-er_ad6000np_44_ep0: REF = $(SAMPLES)/aac/er_ad6000np_44_ep0.s16

FATE_AAC += fate-aac-er_eld2000np_48_ep0
fate-aac-er_eld2000np_48_ep0: CMD = pcm -i $(TARGET_SAMPLES)/aac/er_eld2000np_48_ep0.mp4
fate-aac-er_eld2000np_48_ep0: REF = $(SAMPLES)/aac/er_eld2000np_48_ep0.s16


fate-aac-ct%: CMD = pcm -i $(TARGET_SAMPLES)/aac/CT_DecoderCheck/$(@:fate-aac-ct-%=%)
fate-aac-ct%: REF = $(SAMPLES)/aac/CT_DecoderCheck/aacPlusv2.wav

FATE_AAC_CT_RAW = fate-aac-ct-sbr_i-ps_i.aac

FATE_AAC_CT = sbr_bc-ps_i.3gp  \
              sbr_bic-ps_i.3gp \
              sbr_bc-ps_bc.mp4 \
              sbr_bc-ps_i.mp4  \
              sbr_i-ps_bic.mp4 \
              sbr_i-ps_i.mp4

FATE_AAC += $(FATE_AAC_CT:%=fate-aac-ct-%)

FATE_AAC_ENCODE += fate-aac-aref-encode
fate-aac-aref-encode: ./tests/data/asynth-44100-2.wav
fate-aac-aref-encode: CMD = enc_dec_pcm adts wav s16le $(REF) -strict -2 -c:a aac -b:a 512k
fate-aac-aref-encode: CMP = stddev
fate-aac-aref-encode: REF = ./tests/data/asynth-44100-2.wav
fate-aac-aref-encode: CMP_SHIFT = -4096
fate-aac-aref-encode: CMP_TARGET = 1862
fate-aac-aref-encode: SIZE_TOLERANCE = 2464

FATE_AAC_ENCODE += fate-aac-ln-encode
fate-aac-ln-encode: CMD = enc_dec_pcm adts wav s16le $(REF) -strict -2 -c:a aac -b:a 512k
fate-aac-ln-encode: CMP = stddev
fate-aac-ln-encode: REF = $(SAMPLES)/audio-reference/luckynight_2ch_44kHz_s16.wav
fate-aac-ln-encode: CMP_SHIFT = -4096
fate-aac-ln-encode: CMP_TARGET = 65
fate-aac-ln-encode: SIZE_TOLERANCE = 3560

FATE_AAC_LATM += fate-aac-latm_000000001180bc60
fate-aac-latm_000000001180bc60: CMD = pcm -i $(TARGET_SAMPLES)/aac/latm_000000001180bc60.mpg
fate-aac-latm_000000001180bc60: REF = $(SAMPLES)/aac/latm_000000001180bc60.s16

FATE_AAC_LATM += fate-aac-latm_stereo_to_51
fate-aac-latm_stereo_to_51: CMD = pcm -i $(TARGET_SAMPLES)/aac/latm_stereo_to_51.ts -channel_layout 5.1
fate-aac-latm_stereo_to_51: REF = $(SAMPLES)/aac/latm_stereo_to_51_ref.s16

FATE_AAC-$(call      DEMDEC, AAC,    AAC)      += $(FATE_AAC_CT_RAW)
FATE_AAC-$(call      DEMDEC, MOV,    AAC)      += $(FATE_AAC)
FATE_AAC_LATM-$(call DEMDEC, MPEGTS, AAC_LATM) += $(FATE_AAC_LATM)

FATE_AAC_ALL = $(FATE_AAC-yes) $(FATE_AAC_LATM-yes)

$(FATE_AAC_ALL): CMP  = oneoff
$(FATE_AAC_ALL): FUZZ = 2

FATE_AAC_ENCODE-$(call ENCMUX, AAC, ADTS) += $(FATE_AAC_ENCODE)

FATE_SAMPLES_FFMPEG += $(FATE_AAC_ALL) $(FATE_AAC_ENCODE-yes)

fate-aac: $(FATE_AAC_ALL) $(FATE_AAC_ENCODE)
fate-aac-latm: $(FATE_AAC_LATM-yes)
