include ../config.mak

CFLAGS= $(OPTFLAGS) -Wall -g -DHAVE_AV_CONFIG_H
LDFLAGS= -g

OBJS= common.o utils.o mpegvideo.o h263.o jrevdct.o jfdctfst.o \
      mpegaudio.o ac3enc.o mjpegenc.o resample.o dsputil.o \
      motion_est.o imgconvert.o imgresample.o msmpeg4.o \
      mpeg12.o h263dec.o rv10.o
ASM_OBJS=

# currently using libac3 for ac3 decoding
ifeq ($(CONFIG_AC3),yes)
OBJS+= ac3dec.o \
       libac3/bit_allocate.o libac3/bitstream.o libac3/downmix.o \
       libac3/imdct.o  libac3/parse.o
endif

# currently using mpglib for mpeg audio decoding
ifeq ($(CONFIG_MPGLIB),yes)
OBJS+= mpegaudiodec.o \
       mpglib/layer1.o mpglib/layer2.o mpglib/layer3.o \
       mpglib/dct64_i386.o mpglib/decode_i386.o  mpglib/tabinit.o
endif

# i386 mmx specific stuff
ifeq ($(TARGET_MMX),yes)
ASM_OBJS += i386/fdct_mmx.o i386/sad_mmx.o
OBJS += i386/fdctdata.o i386/cputest.o \
	i386/dsputil_mmx.o i386/mpegvideo_mmx.o
endif

SRCS = $(OBJS:.o=.c) $(ASM_OBJS:.o=.s)

LIB= libavcodec.a
TESTS= imgresample-test dct-test

all: $(LIB)
tests: apiexample $(TESTS)

$(LIB): $(OBJS) $(ASM_OBJS)
	rm -f $@
	$(AR) rcs $@ $(OBJS) $(ASM_OBJS)

dsputil.o: dsputil.c dsputil.h

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $< 

%.o: %.s
	nasm -f elf -o $@ $<

# depend only used by mplayer now
dep:	depend

depend:
	$(CC) -MM $(CFLAGS) $(SRCS) 1>.depend

clean: 
	rm -f *.o *~ *.a i386/*.o i386/*~ \
           libac3/*.o libac3/*~ \
           mpglib/*.o mpglib/*~ \
           apiexample $(TESTS)

distclean: clean
	rm -f Makefile.bak .depend

# api example program
apiexample: apiexample.c $(LIB)
	$(CC) $(CFLAGS) -o $@ $< $(LIB) -lm

# testing progs

imgresample-test: imgresample.c
	$(CC) $(CFLAGS) -DTEST -o $@ $^ 

dct-test: dct-test.o jfdctfst.o i386/fdct_mmx.o i386/fdctdata.o fdctref.o
	$(CC) -o $@ $^

#
# include dependency files if they exist
#
ifneq ($(wildcard .depend),)
include .depend
endif
