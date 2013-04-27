FATE_INDEO-$(call DEMDEC, AVI, INDEO2) += fate-indeo2
fate-indeo2: CMD = framecrc -i $(SAMPLES)/rt21/VPAR0026.AVI

FATE_INDEO-$(call DEMDEC, MOV, INDEO3) += fate-indeo3
fate-indeo3: CMD = framecrc -i $(SAMPLES)/iv32/cubes.mov

FATE_INDEO-$(call DEMDEC, AVI, INDEO3) += fate-indeo3-2
fate-indeo3-2: CMD = framecrc -i $(SAMPLES)/iv32/OPENINGH.avi

FATE_INDEO-$(call DEMDEC, AVI, INDEO4) += fate-indeo4
fate-indeo4: CMD = framecrc -i $(SAMPLES)/iv41/indeo41-partial.avi -an

FATE_INDEO-$(call DEMDEC, AVI, INDEO5) += fate-indeo5
fate-indeo5: CMD = framecrc -i $(SAMPLES)/iv50/Educ_Movie_DeadlyForce.avi -an

FATE_SAMPLES_AVCONV += $(FATE_INDEO-yes)
fate-indeo: $(FATE_INDEO-yes)
