GEN_CLEANSUFFIXES = *.o *.c *.d

clean::
	$(RM) $(GEN_CLEANSUFFIXES:%=libavcodec/vulkan/%)

OBJS-$(CONFIG_FFV1_VULKAN_ENCODER)  +=  vulkan/common.o \
					vulkan/rangecoder.o vulkan/ffv1_vlc.o \
					vulkan/ffv1_common.o vulkan/ffv1_reset.o \
					vulkan/ffv1_enc_setup.o vulkan/ffv1_enc.o \
					vulkan/ffv1_rct_search.o

OBJS-$(CONFIG_FFV1_VULKAN_HWACCEL)  +=  vulkan/common.o \
					vulkan/rangecoder.o vulkan/ffv1_vlc.o \
					vulkan/ffv1_common.o vulkan/ffv1_reset.o \
					vulkan/ffv1_dec_setup.o vulkan/ffv1_dec.o

OBJS-$(CONFIG_PRORES_RAW_VULKAN_HWACCEL) += vulkan/common.o \
                                            vulkan/prores_raw_decode.o \
                                            vulkan/prores_raw_idct.o

OBJS-$(CONFIG_PRORES_VULKAN_HWACCEL) += vulkan/common.o \
                                        vulkan/prores_vld.o \
                                        vulkan/prores_idct.o

OBJS-$(CONFIG_DPX_VULKAN_HWACCEL) += vulkan/common.o \
                                     vulkan/dpx_unpack.o \
                                     vulkan/dpx_copy.o

VULKAN = $(subst $(SRC_PATH)/,,$(wildcard $(SRC_PATH)/libavcodec/vulkan/*.comp))
.SECONDARY: $(VULKAN:.comp=.c)
libavcodec/vulkan/%.c: TAG = VULKAN
libavcodec/vulkan/%.c: $(SRC_PATH)/libavcodec/vulkan/%.comp
	$(M)$(SRC_PATH)/tools/source2c $< $@
