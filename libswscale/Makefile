NAME = swscale
DESC = FFmpeg image rescaling library

HEADERS = swscale.h                                                     \
          version.h                                                     \
          version_major.h                                               \

OBJS = alphablend.o                                     \
       hscale.o                                         \
       hscale_fast_bilinear.o                           \
       gamma.o                                          \
       input.o                                          \
       options.o                                        \
       output.o                                         \
       rgb2rgb.o                                        \
       slice.o                                          \
       swscale.o                                        \
       swscale_unscaled.o                               \
       utils.o                                          \
       yuv2rgb.o                                        \
       vscale.o                                         \

# Objects duplicated from other libraries for shared builds
SHLIBOBJS                    += log2_tab.o

# Windows resource file
SLIBOBJS-$(HAVE_GNU_WINDRES) += swscaleres.o

TESTPROGS = colorspace                                                  \
            floatimg_cmp                                                \
            pixdesc_query                                               \
            swscale                                                     \
