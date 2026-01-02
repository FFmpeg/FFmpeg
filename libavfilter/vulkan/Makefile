clean::
	$(RM) $(CLEANSUFFIXES:%=libavfilter/vulkan/%)

OBJS-$(CONFIG_BWDIF_VULKAN_FILTER) += vulkan/bwdif.comp.spv.o
OBJS-$(CONFIG_SCALE_VULKAN_FILTER) += vulkan/debayer.o

VULKAN = $(subst $(SRC_PATH)/,,$(wildcard $(SRC_PATH)/libavfilter/vulkan/*.comp))
.SECONDARY: $(VULKAN:.comp=.c)
libavfilter/vulkan/%.c: TAG = VULKAN
libavfilter/vulkan/%.c: $(SRC_PATH)/libavfilter/vulkan/%.comp
	$(M)$(SRC_PATH)/tools/source2c $< $@
