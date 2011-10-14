FATE_PRORES = fate-prores-422                                           \
              fate-prores-422_hq                                        \
              fate-prores-422_lt                                        \
              fate-prores-422_proxy                                     \
              fate-prores-alpha                                         \

FATE_TESTS += $(FATE_PRORES)
fate-prores: $(FATE_PRORES)

fate-prores-422:       CMD = framecrc -vsync 0 -i $(SAMPLES)/prores/Sequence_1-Apple_ProRes_422.mov -pix_fmt yuv422p10le
fate-prores-422_hq:    CMD = framecrc -vsync 0 -i $(SAMPLES)/prores/Sequence_1-Apple_ProRes_422_HQ.mov -pix_fmt yuv422p10le
fate-prores-422_lt:    CMD = framecrc -vsync 0 -i $(SAMPLES)/prores/Sequence_1-Apple_ProRes_422_LT.mov -pix_fmt yuv422p10le
fate-prores-422_proxy: CMD = framecrc -vsync 0 -i $(SAMPLES)/prores/Sequence_1-Apple_ProRes_422_Proxy.mov -pix_fmt yuv422p10le
fate-prores-alpha:     CMD = framecrc -vsync 0 -i $(SAMPLES)/prores/Sequence_1-Apple_ProRes_with_Alpha.mov -pix_fmt yuv444p10le

