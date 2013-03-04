include $(SUBDIR)../config.mak

NAME = swresample
FFLIBS = avutil

HEADERS = swresample.h                       \
          version.h                          \

OBJS = audioconvert.o                        \
       dither.o                              \
       rematrix.o                            \
       resample.o                            \
       swresample.o                          \

OBJS-$(CONFIG_LIBSOXR) += soxr_resample.o
OBJS-$(CONFIG_SHARED)  += log2_tab.o

TESTPROGS = swresample
