NAME = avresample
DESC = Libav audio resampling library

HEADERS = avresample.h                                                  \
          version.h                                                     \

OBJS = audio_convert.o                                                  \
       audio_data.o                                                     \
       audio_mix.o                                                      \
       audio_mix_matrix.o                                               \
       dither.o                                                         \
       options.o                                                        \
       resample.o                                                       \
       utils.o                                                          \

# Windows resource file
SLIBOBJS-$(HAVE_GNU_WINDRES) += avresampleres.o

TESTPROGS = avresample
