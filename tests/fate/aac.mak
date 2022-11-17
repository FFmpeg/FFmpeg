FATE_AAC += fate-aac-al04_44
fate-aac-al04_44: CMD = pcm -i $(TARGET_SAMPLES)/aac/al04_44.mp4
fate-aac-al04_44: REF = $(SAMPLES)/aac/al04_44.s16

FATE_AAC += fate-aac-al04sf_48
fate-aac-al04sf_48: CMD = pcm -i $(TARGET_SAMPLES)/aac/al04sf_48.mp4
fate-aac-al04sf_48: REF = $(SAMPLES)/aac/al04sf_48.s16

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

FATE_AAC += fate-aac-al_sbr_hq_sr_48_2_fsaac48
fate-aac-al_sbr_hq_sr_48_2_fsaac48: CMD = pcm -i $(TARGET_SAMPLES)/aac/al_sbr_sr_48_2_fsaac48.mp4
fate-aac-al_sbr_hq_sr_48_2_fsaac48: REF = $(SAMPLES)/aac/al_sbr_hq_sr_48_2_fsaac48.s16

FATE_AAC += fate-aac-al_sbr_ps_04_ur
fate-aac-al_sbr_ps_04_ur: CMD = pcm -i $(TARGET_SAMPLES)/aac/al_sbr_ps_04_new.mp4
fate-aac-al_sbr_ps_04_ur: REF = $(SAMPLES)/aac/al_sbr_ps_04_ur.s16

FATE_AAC += fate-aac-al_sbr_ps_06_ur
fate-aac-al_sbr_ps_06_ur: CMD = pcm -i $(TARGET_SAMPLES)/aac/al_sbr_ps_06_new.mp4
fate-aac-al_sbr_ps_06_ur: REF = $(SAMPLES)/aac/al_sbr_ps_06_ur.s16

FATE_AAC += fate-aac-ap05_48
fate-aac-ap05_48: CMD = pcm -i $(TARGET_SAMPLES)/aac/ap05_48.mp4
fate-aac-ap05_48: REF = $(SAMPLES)/aac/ap05_48.s16

FATE_AAC += fate-aac-er_ad6000np_44_ep0
fate-aac-er_ad6000np_44_ep0: CMD = pcm -i $(TARGET_SAMPLES)/aac/er_ad6000np_44_ep0.mp4
fate-aac-er_ad6000np_44_ep0: REF = $(SAMPLES)/aac/er_ad6000np_44.s16

FATE_AAC += fate-aac-er_eld1001np_44_ep0
fate-aac-er_eld1001np_44_ep0: CMD = pcm -i $(TARGET_SAMPLES)/aac/er_eld1001np_44_ep0.mp4
fate-aac-er_eld1001np_44_ep0: REF = $(SAMPLES)/aac/er_eld1001np_44.s16

FATE_AAC += fate-aac-er_eld2000np_48_ep0
fate-aac-er_eld2000np_48_ep0: CMD = pcm -i $(TARGET_SAMPLES)/aac/er_eld2000np_48_ep0.mp4
fate-aac-er_eld2000np_48_ep0: REF = $(SAMPLES)/aac/er_eld2000np_48_ep0.s16

FATE_AAC += fate-aac-er_eld2100np_48_ep0
fate-aac-er_eld2100np_48_ep0: CMD = pcm -i $(TARGET_SAMPLES)/aac/er_eld2100np_48_ep0.mp4
fate-aac-er_eld2100np_48_ep0: REF = $(SAMPLES)/aac/er_eld2100np_48.s16

FATE_AAC_FIXED += fate-aac-fixed-al04_44
fate-aac-fixed-al04_44: CMD = pcm -c aac_fixed -i $(TARGET_SAMPLES)/aac/al04_44.mp4
fate-aac-fixed-al04_44: REF = $(SAMPLES)/aac/al04_44.s16

FATE_AAC_FIXED += fate-aac-fixed-al05_44
fate-aac-fixed-al05_44: CMD = pcm -c aac_fixed -i $(TARGET_SAMPLES)/aac/al05_44.mp4
fate-aac-fixed-al05_44: REF = $(SAMPLES)/aac/al05_44.s16

FATE_AAC_FIXED += fate-aac-fixed-al06_44
fate-aac-fixed-al06_44: CMD = pcm -c aac_fixed -i $(TARGET_SAMPLES)/aac/al06_44.mp4
fate-aac-fixed-al06_44: REF = $(SAMPLES)/aac/al06_44_reorder.s16

FATE_AAC_FIXED += fate-aac-fixed-al15_44
fate-aac-fixed-al15_44: CMD = pcm -c aac_fixed -i $(TARGET_SAMPLES)/aac/al15_44.mp4
fate-aac-fixed-al15_44: REF = $(SAMPLES)/aac/al15_44_reorder.s16

FATE_AAC_FIXED += fate-aac-fixed-al17_44
fate-aac-fixed-al17_44: CMD = pcm -c aac_fixed -i $(TARGET_SAMPLES)/aac/al17_44.mp4
fate-aac-fixed-al17_44: REF = $(SAMPLES)/aac/al17_44.s16

FATE_AAC_FIXED += fate-aac-fixed-al18_44
fate-aac-fixed-al18_44: CMD = pcm -c aac_fixed -i $(TARGET_SAMPLES)/aac/al18_44.mp4
fate-aac-fixed-al18_44: REF = $(SAMPLES)/aac/al18_44.s16

FATE_AAC_FIXED += fate-aac-fixed-al_sbr_hq_cm_48_2
fate-aac-fixed-al_sbr_hq_cm_48_2: CMD = pcm -c aac_fixed -i $(TARGET_SAMPLES)/aac/al_sbr_cm_48_2.mp4
fate-aac-fixed-al_sbr_hq_cm_48_2: REF = $(SAMPLES)/aac/al_sbr_hq_cm_48_2.s16

FATE_AAC_FIXED += fate-aac-fixed-al_sbr_hq_cm_48_5.1
fate-aac-fixed-al_sbr_hq_cm_48_5.1: CMD = pcm -c aac_fixed -i $(TARGET_SAMPLES)/aac/al_sbr_cm_48_5.1.mp4
fate-aac-fixed-al_sbr_hq_cm_48_5.1: REF = $(SAMPLES)/aac/al_sbr_hq_cm_48_5.1_reorder.s16

FATE_AAC_FIXED += fate-aac-fixed-al_sbr_hq_sr_48_2_fsaac48
fate-aac-fixed-al_sbr_hq_sr_48_2_fsaac48: CMD = pcm -c aac_fixed -i $(TARGET_SAMPLES)/aac/al_sbr_sr_48_2_fsaac48.mp4
fate-aac-fixed-al_sbr_hq_sr_48_2_fsaac48: REF = $(SAMPLES)/aac/al_sbr_hq_sr_48_2_fsaac48.s16

#FATE_AAC_FIXED += fate-aac-fixed-al_sbr_ps_06_ur
#fate-aac-fixed-al_sbr_ps_06_ur: CMD = pcm -c aac_fixed-i $(TARGET_SAMPLES)/aac/al_sbr_ps_06_new.mp4
#fate-aac-fixed-al_sbr_ps_06_ur: REF = $(SAMPLES)/aac/al_sbr_ps_06_ur.s16

FATE_AAC_FIXED += fate-aac-fixed-ap05_48
fate-aac-fixed-ap05_48: CMD = pcm -c aac_fixed -i $(TARGET_SAMPLES)/aac/ap05_48.mp4
fate-aac-fixed-ap05_48: REF = $(SAMPLES)/aac/ap05_48.s16

FATE_AAC_FIXED += fate-aac-fixed-er_ad6000np_44_ep0
fate-aac-fixed-er_ad6000np_44_ep0: CMD = pcm -c aac_fixed -i $(TARGET_SAMPLES)/aac/er_ad6000np_44_ep0.mp4
fate-aac-fixed-er_ad6000np_44_ep0: REF = $(SAMPLES)/aac/er_ad6000np_44.s16

FATE_AAC_FIXED += fate-aac-fixed-er_eld1001np_44_ep0
fate-aac-fixed-er_eld1001np_44_ep0: CMD = pcm -c aac_fixed -i $(TARGET_SAMPLES)/aac/er_eld1001np_44_ep0.mp4
fate-aac-fixed-er_eld1001np_44_ep0: REF = $(SAMPLES)/aac/er_eld1001np_44.s16

FATE_AAC_FIXED += fate-aac-fixed-er_eld2000np_48_ep0
fate-aac-fixed-er_eld2000np_48_ep0: CMD = pcm -c aac_fixed -i $(TARGET_SAMPLES)/aac/er_eld2000np_48_ep0.mp4
fate-aac-fixed-er_eld2000np_48_ep0: REF = $(SAMPLES)/aac/er_eld2000np_48_ep0.s16

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
fate-aac-aref-encode: CMD = enc_dec_pcm adts wav s16le $(REF) -c:a aac -aac_coder fast -aac_is 0 -aac_pns 0 -aac_ms 0 -aac_tns 0 -b:a 512k -fflags +bitexact -flags +bitexact
fate-aac-aref-encode: CMP = stddev
fate-aac-aref-encode: REF = ./tests/data/asynth-44100-2.wav
fate-aac-aref-encode: CMP_SHIFT = -4096
fate-aac-aref-encode: CMP_TARGET = 596
fate-aac-aref-encode: SIZE_TOLERANCE = 2464
fate-aac-aref-encode: FUZZ = 89

FATE_AAC_ENCODE += fate-aac-ln-encode
fate-aac-ln-encode: CMD = enc_dec_pcm adts wav s16le $(TARGET_SAMPLES)/audio-reference/luckynight_2ch_44kHz_s16.wav -c:a aac -aac_coder fast -aac_is 0 -aac_pns 0 -aac_ms 0 -aac_tns 0 -b:a 512k -fflags +bitexact -flags +bitexact
fate-aac-ln-encode: CMP = stddev
fate-aac-ln-encode: REF = $(SAMPLES)/audio-reference/luckynight_2ch_44kHz_s16.wav
fate-aac-ln-encode: CMP_SHIFT = -4096
fate-aac-ln-encode: CMP_TARGET = 72
fate-aac-ln-encode: SIZE_TOLERANCE = 3560
fate-aac-ln-encode: FUZZ = 30

FATE_AAC_ENCODE += fate-aac-ln-encode-128k
fate-aac-ln-encode-128k: CMD = enc_dec_pcm adts wav s16le $(TARGET_SAMPLES)/audio-reference/luckynight_2ch_44kHz_s16.wav -c:a aac -aac_coder fast -aac_is 0 -aac_pns 0 -aac_ms 0 -aac_tns 0 -b:a 128k -cutoff 22050 -fflags +bitexact -flags +bitexact
fate-aac-ln-encode-128k: CMP = stddev
fate-aac-ln-encode-128k: REF = $(SAMPLES)/audio-reference/luckynight_2ch_44kHz_s16.wav
fate-aac-ln-encode-128k: CMP_SHIFT = -4096
fate-aac-ln-encode-128k: CMP_TARGET = 622
fate-aac-ln-encode-128k: SIZE_TOLERANCE = 3560
fate-aac-ln-encode-128k: FUZZ = 10

FATE_AAC_ENCODE += fate-aac-pns-encode
fate-aac-pns-encode: CMD = enc_dec_pcm adts wav s16le $(TARGET_SAMPLES)/audio-reference/luckynight_2ch_44kHz_s16.wav -c:a aac -aac_coder fast -aac_pns 1 -aac_is 0 -aac_ms 0 -aac_tns 0 -b:a 128k -cutoff 22050  -fflags +bitexact -flags +bitexact
fate-aac-pns-encode: CMP = stddev
fate-aac-pns-encode: REF = $(SAMPLES)/audio-reference/luckynight_2ch_44kHz_s16.wav
fate-aac-pns-encode: CMP_SHIFT = -4096
fate-aac-pns-encode: CMP_TARGET = 655
fate-aac-pns-encode: SIZE_TOLERANCE = 3560
fate-aac-pns-encode: FUZZ = 74

FATE_AAC_ENCODE += fate-aac-tns-encode
fate-aac-tns-encode: CMD = enc_dec_pcm adts wav s16le $(TARGET_SAMPLES)/audio-reference/luckynight_2ch_44kHz_s16.wav -c:a aac -aac_coder fast -aac_tns 1 -aac_is 0 -aac_pns 0 -aac_ms 0 -b:a 128k -cutoff 22050  -fflags +bitexact -flags +bitexact
fate-aac-tns-encode: CMP = stddev
fate-aac-tns-encode: REF = $(SAMPLES)/audio-reference/luckynight_2ch_44kHz_s16.wav
fate-aac-tns-encode: CMP_SHIFT = -4096
fate-aac-tns-encode: CMP_TARGET = 637
fate-aac-tns-encode: FUZZ = 7
fate-aac-tns-encode: SIZE_TOLERANCE = 3560

FATE_AAC_ENCODE += fate-aac-is-encode
fate-aac-is-encode: CMD = enc_dec_pcm adts wav s16le $(TARGET_SAMPLES)/audio-reference/luckynight_2ch_44kHz_s16.wav -c:a aac -aac_coder fast -aac_pns 0 -aac_is 1 -aac_ms 0 -b:a 128k -aac_tns 0 -cutoff 22050 -fflags +bitexact -flags +bitexact
fate-aac-is-encode: CMP = stddev
fate-aac-is-encode: REF = $(SAMPLES)/audio-reference/luckynight_2ch_44kHz_s16.wav
fate-aac-is-encode: CMP_SHIFT = -4096
fate-aac-is-encode: CMP_TARGET = 514
fate-aac-is-encode: SIZE_TOLERANCE = 3560
fate-aac-is-encode: FUZZ = 10

FATE_AAC_ENCODE += fate-aac-ms-encode
fate-aac-ms-encode: CMD = enc_dec_pcm adts wav s16le $(TARGET_SAMPLES)/audio-reference/luckynight_2ch_44kHz_s16.wav -c:a aac -aac_coder fast -aac_pns 0 -aac_is 0 -aac_ms 1 -aac_tns 0 -b:a 128k -cutoff 22050 -fflags +bitexact -flags +bitexact
fate-aac-ms-encode: CMP = stddev
fate-aac-ms-encode: REF = $(SAMPLES)/audio-reference/luckynight_2ch_44kHz_s16.wav
fate-aac-ms-encode: CMP_SHIFT = -4096
fate-aac-ms-encode: CMP_TARGET = 558
fate-aac-ms-encode: SIZE_TOLERANCE = 3560
fate-aac-ms-encode: FUZZ = 15

#Ticket1784
FATE_AAC_ENCODE += fate-aac-yoraw-encode
fate-aac-yoraw-encode: CMD = enc_dec_pcm adts wav s16le $(TARGET_SAMPLES)/audio-reference/yo.raw-short.wav -c:a aac -aac_coder fast -fflags +bitexact -flags +bitexact
fate-aac-yoraw-encode: CMP = stddev
fate-aac-yoraw-encode: REF = $(SAMPLES)/audio-reference/yo.raw-short.wav
fate-aac-yoraw-encode: CMP_SHIFT = -12288
fate-aac-yoraw-encode: CMP_TARGET = 226
fate-aac-yoraw-encode: SIZE_TOLERANCE = 3560
fate-aac-yoraw-encode: FUZZ = 17


FATE_AAC_ENCODE += fate-aac-pred-encode
fate-aac-pred-encode: CMD = enc_dec_pcm adts wav s16le $(TARGET_SAMPLES)/audio-reference/luckynight_2ch_44kHz_s16.wav -profile:a aac_main -c:a aac -aac_coder fast -aac_is 0 -aac_pns 0 -aac_ms 0 -aac_tns 0 -b:a 128k -cutoff 22050 -fflags +bitexact -flags +bitexact
fate-aac-pred-encode: CMP = stddev
fate-aac-pred-encode: REF = $(SAMPLES)/audio-reference/luckynight_2ch_44kHz_s16.wav
fate-aac-pred-encode: CMP_SHIFT = -4096
fate-aac-pred-encode: CMP_TARGET = 662
fate-aac-pred-encode: FUZZ = 12
fate-aac-pred-encode: SIZE_TOLERANCE = 3560

FATE_AAC_LATM += fate-aac-latm_000000001180bc60
fate-aac-latm_000000001180bc60: CMD = pcm -i $(TARGET_SAMPLES)/aac/latm_000000001180bc60.mpg
fate-aac-latm_000000001180bc60: REF = $(SAMPLES)/aac/latm_000000001180bc60.s16

FATE_AAC_LATM += fate-aac-latm_stereo_to_51
fate-aac-latm_stereo_to_51: CMD = pcm -i $(TARGET_SAMPLES)/aac/latm_stereo_to_51.ts -channel_layout 5.1
fate-aac-latm_stereo_to_51: REF = $(SAMPLES)/aac/latm_stereo_to_51_ref.s16

fate-aac-autobsf-adtstoasc: CMD = transcode "aac" $(TARGET_SAMPLES)/audiomatch/tones_afconvert_16000_mono_aac_lc.adts \
                                            matroska "-c:a copy" "-c:a copy"

FATE_AAC-$(call      DEMDEC, AAC,    AAC)      += $(FATE_AAC_CT_RAW)
FATE_AAC-$(call      DEMDEC, MOV,    AAC)      += $(FATE_AAC)
FATE_AAC_LATM-$(call DEMDEC, MPEGTS, AAC_LATM) += $(FATE_AAC_LATM)
FATE_AAC-$(call      DEMDEC, AAC,    AAC_FIXED)+= $(FATE_AAC_FIXED)

FATE_AAC_ALL = $(FATE_AAC-yes) $(FATE_AAC_LATM-yes) $(FATE_AAC_FIXED-yes)

$(FATE_AAC_ALL): CMP  = oneoff
$(FATE_AAC_ALL): FUZZ = 2

FATE_AAC_ENCODE-$(call ENCMUX, AAC, ADTS) += $(FATE_AAC_ENCODE)

FATE_AAC_BSF-$(call ALLYES, AAC_DEMUXER AAC_ADTSTOASC_BSF MATROSKA_MUXER) += fate-aac-autobsf-adtstoasc

FATE_SAMPLES_FFMPEG += $(FATE_AAC_ALL) $(FATE_AAC_ENCODE-yes) $(FATE_AAC_BSF-yes)

fate-aac: $(FATE_AAC_ALL) $(FATE_AAC_ENCODE) $(FATE_AAC_BSF-yes)
fate-aac-latm: $(FATE_AAC_LATM-yes)
