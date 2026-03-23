clean::
	$(RM) $(CLEANSUFFIXES:%=libavcodec/vulkan/%)

OBJS-$(CONFIG_FFV1_VULKAN_ENCODER) += vulkan/ffv1_enc_setup.comp.spv.o \
                                      vulkan/ffv1_enc_reset.comp.spv.o \
                                      vulkan/ffv1_enc_reset_golomb.comp.spv.o \
                                      vulkan/ffv1_enc.comp.spv.o \
                                      vulkan/ffv1_enc_golomb.comp.spv.o \
                                      vulkan/ffv1_enc_rgb.comp.spv.o \
                                      vulkan/ffv1_enc_rgb_golomb.comp.spv.o \
                                      vulkan/ffv1_enc_rct_search.comp.spv.o \
                                      vulkan/ffv1_enc_remap.comp.spv.o \
                                      vulkan/ffv1_enc_rgb_float.comp.spv.o

OBJS-$(CONFIG_FFV1_VULKAN_HWACCEL) += vulkan/ffv1_dec_setup.comp.spv.o \
                                      vulkan/ffv1_dec_reset.comp.spv.o \
                                      vulkan/ffv1_dec_reset_golomb.comp.spv.o \
                                      vulkan/ffv1_dec.comp.spv.o \
                                      vulkan/ffv1_dec_golomb.comp.spv.o \
                                      vulkan/ffv1_dec_rgb.comp.spv.o \
                                      vulkan/ffv1_dec_rgb_golomb.comp.spv.o \
                                      vulkan/ffv1_dec_rgb_float.comp.spv.o \
                                      vulkan/ffv1_dec_rgb_float_golomb.comp.spv.o

OBJS-$(CONFIG_PRORES_KS_VULKAN_ENCODER) += vulkan/prores_ks_alpha_data.comp.spv.o \
                                           vulkan/prores_ks_slice_data.comp.spv.o \
                                           vulkan/prores_ks_estimate_slice.comp.spv.o \
                                           vulkan/prores_ks_encode_slice.comp.spv.o \
                                           vulkan/prores_ks_trellis_node.comp.spv.o

OBJS-$(CONFIG_PRORES_RAW_VULKAN_HWACCEL) += vulkan/prores_raw_decode.comp.spv.o \
                                            vulkan/prores_raw_idct.comp.spv.o

OBJS-$(CONFIG_PRORES_VULKAN_HWACCEL) += vulkan/prores_vld.comp.spv.o \
                                        vulkan/prores_idct.comp.spv.o

OBJS-$(CONFIG_DPX_VULKAN_HWACCEL) += vulkan/dpx_unpack.comp.spv.o \
                                     vulkan/dpx_copy.comp.spv.o
