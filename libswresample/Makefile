NAME = swresample
DESC = FFmpeg audio resampling library
FFLIBS = avutil

HEADERS = swresample.h                       \
          version.h                          \

OBJS = audioconvert.o                        \
       dither.o                              \
       options.o                             \
       rematrix.o                            \
       resample.o                            \
       resample_dsp.o                        \
       swresample.o                          \
       swresample_frame.o                    \

OBJS-$(CONFIG_LIBSOXR) += soxr_resample.o
OBJS-$(CONFIG_SHARED)  += log2_tab.o

# Windows resource file
SLIBOBJS-$(HAVE_GNU_WINDRES) += swresampleres.o

TESTPROGS = swresample
