FATE_PRORES = fate-prores-422                                           \
              fate-prores-422_hq                                        \
              fate-prores-422_lt                                        \
              fate-prores-422_proxy                                     \
              fate-prores-alpha                                         \
              fate-prores-alpha_skip                                    \
              fate-prores-transparency                                  \
              fate-prores-transparency_skip                             \
              fate-prores-gray                                          \

FATE_SAMPLES_AVCONV-$(call DEMDEC, MOV, PRORES) += $(FATE_PRORES)
fate-prores: $(FATE_PRORES)

fate-prores-422:       CMD = framecrc -flags +bitexact -i $(TARGET_SAMPLES)/prores/Sequence_1-Apple_ProRes_422.mov -pix_fmt yuv422p10le
fate-prores-422_hq:    CMD = framecrc -flags +bitexact -i $(TARGET_SAMPLES)/prores/Sequence_1-Apple_ProRes_422_HQ.mov -pix_fmt yuv422p10le
fate-prores-422_lt:    CMD = framecrc -flags +bitexact -i $(TARGET_SAMPLES)/prores/Sequence_1-Apple_ProRes_422_LT.mov -pix_fmt yuv422p10le
fate-prores-422_proxy: CMD = framecrc -flags +bitexact -i $(TARGET_SAMPLES)/prores/Sequence_1-Apple_ProRes_422_Proxy.mov -pix_fmt yuv422p10le
fate-prores-alpha:     CMD = framecrc -flags +bitexact -i $(TARGET_SAMPLES)/prores/Sequence_1-Apple_ProRes_with_Alpha.mov -pix_fmt yuva444p10le
fate-prores-alpha_skip: CMD = framecrc -flags +bitexact -skip_alpha 1 -i $(TARGET_SAMPLES)/prores/Sequence_1-Apple_ProRes_with_Alpha.mov -pix_fmt yuv444p10le
fate-prores-transparency: CMD = framecrc -flags +bitexact -i $(TARGET_SAMPLES)/prores/prores4444_with_transparency.mov -pix_fmt yuva444p10le
fate-prores-transparency_skip: CMD = framecrc -flags +bitexact -skip_alpha 1 -i $(TARGET_SAMPLES)/prores/prores4444_with_transparency.mov -pix_fmt yuv444p10le
fate-prores-gray:      CMD = framecrc -flags +bitexact -acodec aac_fixed -i $(TARGET_SAMPLES)/prores/gray.mov -pix_fmt yuv422p10le
