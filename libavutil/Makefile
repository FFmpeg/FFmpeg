include $(SUBDIR)../config.mak

NAME = avutil

OBJS = adler32.o \
       aes.o \
       base64.o \
       crc.o \
       des.o \
       fifo.o \
       intfloat_readwrite.o \
       lls.o \
       log.o \
       lzo.o \
       mathematics.o \
       md5.o \
       mem.o \
       random.o \
       rational.o \
       rc4.o \
       sha1.o \
       string.o \
       tree.o \
       utils.o \

HEADERS = adler32.h \
          avstring.h \
          avutil.h \
          base64.h \
          common.h \
          crc.h \
          fifo.h \
          intfloat_readwrite.h \
          log.h \
          lzo.h \
          mathematics.h \
          md5.h \
          mem.h \
          random.h \
          rational.h \
          sha1.h

TESTS = $(addsuffix -test$(EXESUF), adler32 aes crc des lls md5 random sha1 softfloat tree)

include $(SUBDIR)../subdir.mak

$(SUBDIR)lzo-test$(EXESUF): EXTRALIBS += -llzo2

CLEANFILES = lzo-test$(EXESUF)
