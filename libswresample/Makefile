include $(SUBDIR)../config.mak

NAME = swresample
FFLIBS = avutil

HEADERS = swresample.h                       \
          version.h                          \

OBJS = audioconvert.o                        \
       dither.o                              \
       log2_tab.o                            \
       rematrix.o                            \
       resample.o                            \
       swresample.o                          \

OBJS-$(CONFIG_LIBSOXR) += soxr_resample.o

TESTPROGS = swresample
