FATE_DFA += fate-dfa1
fate-dfa1: CMD = framecrc -i $(TARGET_SAMPLES)/chronomaster-dfa/0000.dfa -pix_fmt rgb24

FATE_DFA += fate-dfa2
fate-dfa2: CMD = framecrc -i $(TARGET_SAMPLES)/chronomaster-dfa/0001.dfa -pix_fmt rgb24

FATE_DFA += fate-dfa3
fate-dfa3: CMD = framecrc -i $(TARGET_SAMPLES)/chronomaster-dfa/0002.dfa -pix_fmt rgb24

FATE_DFA += fate-dfa4
fate-dfa4: CMD = framecrc -i $(TARGET_SAMPLES)/chronomaster-dfa/0003.dfa -pix_fmt rgb24

FATE_DFA += fate-dfa5
fate-dfa5: CMD = framecrc -i $(TARGET_SAMPLES)/chronomaster-dfa/0004.dfa -pix_fmt rgb24

FATE_DFA += fate-dfa6
fate-dfa6: CMD = framecrc -i $(TARGET_SAMPLES)/chronomaster-dfa/0005.dfa -pix_fmt rgb24

FATE_DFA += fate-dfa7
fate-dfa7: CMD = framecrc -i $(TARGET_SAMPLES)/chronomaster-dfa/0006.dfa -pix_fmt rgb24

FATE_DFA += fate-dfa8
fate-dfa8: CMD = framecrc -i $(TARGET_SAMPLES)/chronomaster-dfa/0007.dfa -pix_fmt rgb24

FATE_DFA += fate-dfa9
fate-dfa9: CMD = framecrc -i $(TARGET_SAMPLES)/chronomaster-dfa/0008.dfa -pix_fmt rgb24

FATE_DFA += fate-dfa10
fate-dfa10: CMD = framecrc -i $(TARGET_SAMPLES)/chronomaster-dfa/0009.dfa -pix_fmt rgb24

FATE_DFA += fate-dfa11
fate-dfa11: CMD = framecrc -i $(TARGET_SAMPLES)/chronomaster-dfa/0010.dfa -pix_fmt rgb24

FATE_DFA-$(call DEMDEC, DFA, DFA) += $(FATE_DFA)

FATE_SAMPLES_AVCONV += $(FATE_DFA-yes)
fate-dfa: $(FATE_DFA-yes)
