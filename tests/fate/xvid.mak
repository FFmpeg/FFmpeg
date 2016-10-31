fate-xvid-custom-matrix: CMD = framemd5 -flags +bitexact -idct simple  -i $(TARGET_SAMPLES)/mpeg4/xvid_vlc_trac7411.h263
fate-xvid-idct:          CMD = framemd5 -flags +bitexact -cpuflags all -i $(TARGET_SAMPLES)/mpeg4/xvid_vlc_trac7411.h263

FATE_XVID-$(call DEMDEC, M4V, MPEG4) += fate-xvid-custom-matrix fate-xvid-idct

FATE_SAMPLES_AVCONV += $(FATE_XVID-yes)

fate-xvid: $(FATE_XVID-yes)
