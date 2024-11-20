GEN_CLEANSUFFIXES = *.o *.c *.d

clean::
	$(RM) $(GEN_CLEANSUFFIXES:%=libavcodec/vulkan/%)

OBJS-$(CONFIG_FFV1_VULKAN_ENCODER)  +=  vulkan/common.o \
					vulkan/rangecoder.o vulkan/ffv1_vlc.o \
					vulkan/ffv1_common.o vulkan/ffv1_reset.o \
					vulkan/ffv1_enc_rct.o vulkan/ffv1_enc_setup.o \
					vulkan/ffv1_rct_search.o vulkan/ffv1_enc.o

OBJS-$(CONFIG_FFV1_VULKAN_HWACCEL)  +=  vulkan/common.o \
					vulkan/rangecoder.o vulkan/ffv1_vlc.o \
					vulkan/ffv1_common.o vulkan/ffv1_reset.o \
					vulkan/ffv1_dec_setup.o vulkan/ffv1_dec.o

VULKAN = $(subst $(SRC_PATH)/,,$(wildcard $(SRC_PATH)/libavcodec/vulkan/*.comp))
.SECONDARY: $(VULKAN:.comp=.c)
libavcodec/vulkan/%.c: TAG = VULKAN
libavcodec/vulkan/%.c: $(SRC_PATH)/libavcodec/vulkan/%.comp
	$(M)$(SRC_PATH)/tools/source2c $< $@
