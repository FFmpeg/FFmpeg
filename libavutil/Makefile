NAME = avutil

HEADERS = adler32.h                                                     \
          aes.h                                                         \
          attributes.h                                                  \
          audioconvert.h                                                \
          avassert.h                                                    \
          avstring.h                                                    \
          avutil.h                                                      \
          base64.h                                                      \
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

BUILT_HEADERS = avconfig.h

OBJS = adler32.o                                                        \
       aes.o                                                            \
       audioconvert.o                                                   \
       avstring.o                                                       \
       base64.o                                                         \
       cpu.o                                                            \
       crc.o                                                            \
       des.o                                                            \
       error.o                                                          \
       eval.o                                                           \
       fifo.o                                                           \
       file.o                                                           \
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
       tree.o                                                           \
       utils.o                                                          \

OBJS-$(ARCH_ARM) += arm/cpu.o
OBJS-$(ARCH_PPC) += ppc/cpu.o
OBJS-$(ARCH_X86) += x86/cpu.o

TESTPROGS = adler32 aes avstring base64 cpu crc des eval fifo lfg lls \
            md5 opt parseutils rational sha tree
TESTPROGS-$(HAVE_LZO1X_999_COMPRESS) += lzo

DIRS = arm bfin sh4 x86

ARCH_HEADERS = bswap.h intmath.h intreadwrite.h timer.h

$(SUBDIR)lzo-test$(EXESUF): ELIBS = -llzo2
