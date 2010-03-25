include $(SUBDIR)../config.mak

NAME = avutil

HEADERS = adler32.h                                                     \
          attributes.h                                                  \
          avstring.h                                                    \
          avutil.h                                                      \
          base64.h                                                      \
          common.h                                                      \
          crc.h                                                         \
          error.h                                                       \
          fifo.h                                                        \
          intfloat_readwrite.h                                          \
          log.h                                                         \
          lzo.h                                                         \
          mathematics.h                                                 \
          md5.h                                                         \
          mem.h                                                         \
          pixdesc.h                                                     \
          pixfmt.h                                                      \
          rational.h                                                    \
          sha1.h                                                        \

BUILT_HEADERS = avconfig.h

OBJS = adler32.o                                                        \
       aes.o                                                            \
       avstring.o                                                       \
       base64.o                                                         \
       crc.o                                                            \
       des.o                                                            \
       error.o                                                          \
       fifo.o                                                           \
       intfloat_readwrite.o                                             \
       lfg.o                                                            \
       lls.o                                                            \
       log.o                                                            \
       lzo.o                                                            \
       mathematics.o                                                    \
       md5.o                                                            \
       mem.o                                                            \
       pixdesc.o                                                        \
       random_seed.o                                                    \
       rational.o                                                       \
       rc4.o                                                            \
       sha.o                                                            \
       tree.o                                                           \
       utils.o                                                          \

TESTPROGS = adler32 aes base64 crc des lls md5 pca sha softfloat tree
TESTPROGS-$(HAVE_LZO1X_999_COMPRESS) += lzo

DIRS = arm bfin sh4 x86

ARCH_HEADERS = bswap.h intmath.h intreadwrite.h timer.h

include $(SUBDIR)../subdir.mak

$(SUBDIR)lzo-test$(EXESUF): ELIBS = -llzo2
