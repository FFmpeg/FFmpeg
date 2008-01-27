include ../config.mak

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

NAME=avutil
LIBVERSION=$(LAVUVERSION)
LIBMAJOR=$(LAVUMAJOR)

include ../common.mak

TESTS = $(addsuffix -test$(EXESUF), adler32 aes crc des lls md5 sha1 softfloat tree)

tests: $(TESTS)

%-test$(EXESUF): %.c $(LIBNAME)
	$(CC) $(CFLAGS) $(LDFLAGS) -DTEST -o $@ $^ $(EXTRALIBS)

lzo-test$(EXESUF): lzo.c $(LIBNAME)
	$(CC) $(CFLAGS) $(LDFLAGS) -DTEST -o $@ $^ $(EXTRALIBS) -llzo2

clean::
	rm -f $(TESTS) lzo-test$(EXESUF)

.PHONY: tests
