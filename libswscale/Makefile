NAME = swscale
DESC = FFmpeg image rescaling library

HEADERS = swscale.h                                                     \
          version.h                                                     \
          version_major.h                                               \

OBJS = alphablend.o                                     \
       cms.o                                            \
       csputils.o                                       \
       hscale.o                                         \
       hscale_fast_bilinear.o                           \
       filters.o                                        \
       format.o                                         \
       gamma.o                                          \
       graph.o                                          \
       input.o                                          \
       lut3d.o                                          \
       options.o                                        \
       output.o                                         \
       rgb2rgb.o                                        \
       slice.o                                          \
       swscale.o                                        \
       swscale_unscaled.o                               \
       utils.o                                          \
       version.o                                        \
       yuv2rgb.o                                        \
       vscale.o                                         \

OBJS-$(CONFIG_UNSTABLE) +=                              \
       ops.o                                            \
       ops_backend.o                                    \
       ops_chain.o                                      \
       ops_dispatch.o                                   \
       ops_memcpy.o                                     \
       ops_optimizer.o                                  \

ifeq ($(CONFIG_UNSTABLE),yes)
include $(SRC_PATH)/libswscale/vulkan/Makefile
endif

# Objects duplicated from other libraries for shared builds
SHLIBOBJS                    += log2_tab.o half2float.o

# Windows resource file
SHLIBOBJS-$(HAVE_GNU_WINDRES) += swscaleres.o

TESTPROGS = colorspace                                                  \
            floatimg_cmp                                                \
            pixdesc_query                                               \
            swscale                                                     \
            sws_ops                                                     \
            sws_ops_aarch64                                             \

sws_ops_entries_aarch64: TAG = GEN
sws_ops_entries_aarch64: $(SUBDIR)tests/sws_ops_aarch64$(EXESUF)
	$(M)$< > $(SRC_PATH)/libswscale/aarch64/ops_entries.c.tmp
	$(CP) $(SRC_PATH)/libswscale/aarch64/ops_entries.c.tmp $(SRC_PATH)/libswscale/aarch64/ops_entries.c
	$(RM) $(SRC_PATH)/libswscale/aarch64/ops_entries.c.tmp
