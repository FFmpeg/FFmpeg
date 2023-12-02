FATE_QOA-$(call DEMDEC, QOA, QOA) += fate-qoa-152
fate-qoa-152: CMD = framecrc -i $(TARGET_SAMPLES)/qoa/coin_48_1_152.qoa

FATE_QOA-$(call DEMDEC, QOA, QOA) += fate-qoa-278
fate-qoa-278: CMD = framecrc -i $(TARGET_SAMPLES)/qoa/vibra_44_2_278.qoa

FATE_QOA-$(call DEMDEC, QOA, QOA) += fate-qoa-303
fate-qoa-303: CMD = framecrc -i $(TARGET_SAMPLES)/qoa/banjo_48_2_303.qoa

fate-qoa: fate-qoa-152 fate-qoa-278 fate-qoa-303

FATE_SAMPLES_AUDIO += $(FATE_QOA-yes)
