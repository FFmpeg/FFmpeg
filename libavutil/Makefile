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
          mathematics.h                                                 \
          md5.h                                                         \
          mem.h                                                         \
          dict.h                                                        \
          old_pix_fmts.h                                                \
          opt.h                                                         \
          parseutils.h                                                  \
          pixdesc.h                                                     \
          pixfmt.h                                                      \
          random_seed.h                                                 \
          rational.h                                                    \
          samplefmt.h                                                   \
          sha.h                                                         \
          time.h                                                        \
          version.h                                                     \
          xtea.h                                                        \

HEADERS-$(CONFIG_LZO)                   += lzo.h

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
       lfg.o                                                            \
       lls.o                                                            \
       log.o                                                            \
       log2.o                                                           \
       log2_tab.o                                                       \
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
       tree.o                                                           \
       utils.o                                                          \
       xtea.o                                                           \

OBJS-$(CONFIG_LZO)                      += lzo.o

OBJS += $(COMPAT_OBJS:%=../compat/%)

SKIPHEADERS          = old_pix_fmts.h

TESTPROGS = adler32                                                     \
            aes                                                         \
            avstring                                                    \
            base64                                                      \
            blowfish                                                    \
            cpu                                                         \
            crc                                                         \
            des                                                         \
            eval                                                        \
            fifo                                                        \
            lfg                                                         \
            lls                                                         \
            md5                                                         \
            opt                                                         \
            parseutils                                                  \
            sha                                                         \
            tree                                                        \
            xtea                                                        \
