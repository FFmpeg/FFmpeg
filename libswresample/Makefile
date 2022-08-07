NAME = swresample
DESC = FFmpeg audio resampling library
FFLIBS = avutil

HEADERS = swresample.h                       \
          version.h                          \
          version_major.h                    \

OBJS = audioconvert.o                        \
       dither.o                              \
       options.o                             \
       rematrix.o                            \
       resample.o                            \
       resample_dsp.o                        \
       swresample.o                          \
       swresample_frame.o                    \
       version.o                             \

OBJS-$(CONFIG_LIBSOXR) += soxr_resample.o

# Objects duplicated from other libraries for shared builds
SHLIBOBJS              += log2_tab.o

# Windows resource file
SHLIBOBJS-$(HAVE_GNU_WINDRES) += swresampleres.o

TESTPROGS = swresample
