FATE_INDEO2-$(call FRAMECRC, AVI, INDEO2) += fate-indeo2-delta
fate-indeo2-delta: CMD = framecrc -i $(TARGET_SAMPLES)/rt21/ISKATE.AVI -an

FATE_INDEO2-$(call FRAMECRC, AVI, INDEO2) += fate-indeo2-intra
fate-indeo2-intra: CMD = framecrc -i $(TARGET_SAMPLES)/rt21/VPAR0026.AVI

fate-indeo2: $(FATE_INDEO2-yes)

FATE_INDEO3-$(call FRAMECRC, MOV, INDEO3) += fate-indeo3-1
fate-indeo3-1: CMD = framecrc -i $(TARGET_SAMPLES)/iv32/cubes.mov

FATE_INDEO3-$(call FRAMECRC, AVI, INDEO3) += fate-indeo3-2
fate-indeo3-2: CMD = framecrc -i $(TARGET_SAMPLES)/iv32/OPENINGH.avi

fate-indeo3: $(FATE_INDEO3-yes)

FATE_INDEO-$(call FRAMECRC, AVI, INDEO4) += fate-indeo4
fate-indeo4: CMD = framecrc -i $(TARGET_SAMPLES)/iv41/indeo41-partial.avi -an

FATE_INDEO-$(call FRAMECRC, AVI, INDEO5) += fate-indeo5
fate-indeo5: CMD = framecrc -i $(TARGET_SAMPLES)/iv50/Educ_Movie_DeadlyForce.avi -an

FATE_SAMPLES_AVCONV += $(FATE_INDEO2-yes) $(FATE_INDEO3-yes) $(FATE_INDEO-yes)
fate-indeo: $(FATE_INDEO2) $(FATE_INDEO3-yes) $(FATE_INDEO-yes)
