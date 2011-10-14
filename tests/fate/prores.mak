FATE_PRORES = fate-prores-422                                           \
              fate-prores-422_hq                                        \
              fate-prores-422_lt                                        \
              fate-prores-422_proxy                                     \
              fate-prores-alpha                                         \

FATE_TESTS += $(FATE_PRORES)
fate-prores: $(FATE_PRORES)

fate-prores-422:       CMD = framecrc -flags +bitexact -vsync 0 -i $(SAMPLES)/prores/Sequence_1-Apple_ProRes_422.mov
fate-prores-422_hq:    CMD = framecrc -flags +bitexact -vsync 0 -i $(SAMPLES)/prores/Sequence_1-Apple_ProRes_422_HQ.mov
fate-prores-422_lt:    CMD = framecrc -flags +bitexact -vsync 0 -i $(SAMPLES)/prores/Sequence_1-Apple_ProRes_422_LT.mov
fate-prores-422_proxy: CMD = framecrc -flags +bitexact -vsync 0 -i $(SAMPLES)/prores/Sequence_1-Apple_ProRes_422_Proxy.mov
fate-prores-alpha:     CMD = framecrc -flags +bitexact -vsync 0 -i $(SAMPLES)/prores/Sequence_1-Apple_ProRes_with_Alpha.mov

