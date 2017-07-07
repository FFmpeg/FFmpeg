FATE_INDEO2 += fate-indeo2-delta
fate-indeo2-delta: CMD = framecrc -i $(TARGET_SAMPLES)/rt21/ISKATE.AVI -an

FATE_INDEO2 += fate-indeo2-intra
fate-indeo2-intra: CMD = framecrc -i $(TARGET_SAMPLES)/rt21/VPAR0026.AVI

FATE_SAMPLES_AVCONV-$(call DEMDEC, AVI, INDEO2) += $(FATE_INDEO2)
fate-indeo2: $(FATE_INDEO2)

FATE_INDEO3-$(CONFIG_MOV_DEMUXER) += fate-indeo3-1
fate-indeo3-1: CMD = framecrc -i $(TARGET_SAMPLES)/iv32/cubes.mov

FATE_INDEO3-$(CONFIG_AVI_DEMUXER) += fate-indeo3-2
fate-indeo3-2: CMD = framecrc -i $(TARGET_SAMPLES)/iv32/OPENINGH.avi

FATE_SAMPLES_AVCONV-$(CONFIG_INDEO3_DECODER) += $(FATE_INDEO3-yes)
fate-indeo3: $(FATE_INDEO3-yes)

FATE_INDEO-$(call DEMDEC, AVI, INDEO4) += fate-indeo4
fate-indeo4: CMD = framecrc -i $(TARGET_SAMPLES)/iv41/indeo41-partial.avi -an

FATE_INDEO-$(call DEMDEC, AVI, INDEO5) += fate-indeo5
fate-indeo5: CMD = framecrc -i $(TARGET_SAMPLES)/iv50/Educ_Movie_DeadlyForce.avi -an

FATE_SAMPLES_AVCONV += $(FATE_INDEO-yes)
fate-indeo: $(FATE_INDEO2) $(FATE_INDEO3-yes) $(FATE_INDEO-yes)
