FATE_CHECKASM = fate-checkasm-aacpsdsp                                  \
                fate-checkasm-af_afir                                   \
                fate-checkasm-alacdsp                                   \
                fate-checkasm-audiodsp                                  \
                fate-checkasm-blockdsp                                  \
                fate-checkasm-bswapdsp                                  \
                fate-checkasm-exrdsp                                    \
                fate-checkasm-fixed_dsp                                 \
                fate-checkasm-flacdsp                                   \
                fate-checkasm-float_dsp                                 \
                fate-checkasm-fmtconvert                                \
                fate-checkasm-g722dsp                                   \
                fate-checkasm-h264dsp                                   \
                fate-checkasm-h264pred                                  \
                fate-checkasm-h264qpel                                  \
                fate-checkasm-hevc_add_res                              \
                fate-checkasm-hevc_idct                                 \
                fate-checkasm-hevc_sao                                  \
                fate-checkasm-jpeg2000dsp                               \
                fate-checkasm-llviddsp                                  \
                fate-checkasm-llviddspenc                               \
                fate-checkasm-opusdsp                                   \
                fate-checkasm-pixblockdsp                               \
                fate-checkasm-sbrdsp                                    \
                fate-checkasm-synth_filter                              \
                fate-checkasm-sw_rgb                                    \
                fate-checkasm-v210dec                                   \
                fate-checkasm-v210enc                                   \
                fate-checkasm-vf_blend                                  \
                fate-checkasm-vf_colorspace                             \
                fate-checkasm-vf_eq                                     \
                fate-checkasm-vf_gblur                                  \
                fate-checkasm-vf_hflip                                  \
                fate-checkasm-vf_threshold                              \
                fate-checkasm-videodsp                                  \
                fate-checkasm-vp8dsp                                    \
                fate-checkasm-vp9dsp                                    \

$(FATE_CHECKASM): tests/checkasm/checkasm$(EXESUF)
$(FATE_CHECKASM): CMD = run tests/checkasm/checkasm$(EXESUF) --test=$(@:fate-checkasm-%=%)
$(FATE_CHECKASM): CMP = null

FATE += $(FATE_CHECKASM)
fate-checkasm: $(FATE_CHECKASM)
