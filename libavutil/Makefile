include $(SUBDIR)../config.mak

NAME = avutil

HEADERS = adler32.h                                                     \
          aes.h                                                         \
          attributes.h                                                  \
          audio_fifo.h                                                  \
          audioconvert.h                                                \
          avassert.h                                                    \
          avstring.h                                                    \
          avutil.h                                                      \
          base64.h                                                      \
          blowfish.h                                                    \
          bprint.h                                                      \
          bswap.h                                                       \
          common.h                                                      \
          cpu.h                                                         \
          crc.h                                                         \
          error.h                                                       \
          eval.h                                                        \
          fifo.h                                                        \
          file.h                                                        \
          imgutils.h                                                    \
          intfloat.h                                                    \
          intfloat_readwrite.h                                          \
          intreadwrite.h                                                \
          lfg.h                                                         \
          log.h                                                         \
          lzo.h                                                         \
          mathematics.h                                                 \
          md5.h                                                         \
          mem.h                                                         \
          dict.h                                                        \
          opt.h                                                         \
          parseutils.h                                                  \
          pixdesc.h                                                     \
          pixfmt.h                                                      \
          random_seed.h                                                 \
          rational.h                                                    \
          samplefmt.h                                                   \
          sha.h                                                         \
          time.h                                                        \
          timecode.h                                                    \
          timestamp.h                                                   \
          version.h                                                     \
          xtea.h                                                        \

ARCH_HEADERS = bswap.h                                                  \
               intmath.h                                                \
               intreadwrite.h                                           \
               timer.h                                                  \

BUILT_HEADERS = avconfig.h

OBJS = adler32.o                                                        \
       aes.o                                                            \
       audio_fifo.o                                                     \
       audioconvert.o                                                   \
       avstring.o                                                       \
       base64.o                                                         \
       blowfish.o                                                       \
       bprint.o                                                         \
       cpu.o                                                            \
       crc.o                                                            \
       des.o                                                            \
       error.o                                                          \
       eval.o                                                           \
       fifo.o                                                           \
       file.o                                                           \
       float_dsp.o                                                      \
       imgutils.o                                                       \
       intfloat_readwrite.o                                             \
       inverse.o                                                        \
       lfg.o                                                            \
       lls.o                                                            \
       log.o                                                            \
       lzo.o                                                            \
       mathematics.o                                                    \
       md5.o                                                            \
       mem.o                                                            \
       dict.o                                                           \
       opt.o                                                            \
       parseutils.o                                                     \
       pixdesc.o                                                        \
       random_seed.o                                                    \
       rational.o                                                       \
       rc4.o                                                            \
       samplefmt.o                                                      \
       sha.o                                                            \
       time.o                                                           \
       timecode.o                                                       \
       tree.o                                                           \
       utils.o                                                          \
       xtea.o                                                           \

OBJS-$(HAVE_BROKEN_SNPRINTF) += ../compat/msvcrt/snprintf.o

OBJS-$(HAVE_MSVCRT) += ../compat/strtod.o

TESTPROGS = adler32                                                     \
            aes                                                         \
            avstring                                                    \
            base64                                                      \
            blowfish                                                    \
            bprint                                                      \
            cpu                                                         \
            crc                                                         \
            des                                                         \
            error                                                       \
            eval                                                        \
            file                                                        \
            fifo                                                        \
            lfg                                                         \
            lls                                                         \
            md5                                                         \
            opt                                                         \
            pca                                                         \
            parseutils                                                  \
            random_seed                                                 \
            rational                                                    \
            sha                                                         \
            tree                                                        \
            xtea                                                        \

TESTPROGS-$(HAVE_LZO1X_999_COMPRESS) += lzo

TOOLS = ffeval

$(SUBDIR)lzo-test$(EXESUF): ELIBS = -llzo2
