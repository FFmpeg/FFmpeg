include ../config.mak

CFLAGS= $(OPTFLAGS) -Wall -g -DHAVE_AV_CONFIG_H
LDFLAGS= -g

OBJS= common.o utils.o mpegvideo.o h263.o jrevdct.o jfdctfst.o \
      mpegaudio.o ac3enc.o mjpeg.o resample.o dsputil.o \
      motion_est.o imgconvert.o imgresample.o msmpeg4.o \
      mpeg12.o h263dec.o rv10.o mpegaudiodec.o pcm.o
ASM_OBJS=

# currently using libac3 for ac3 decoding
ifeq ($(CONFIG_AC3),yes)
OBJS+= ac3dec.o \
       libac3/bit_allocate.o libac3/bitstream.o libac3/downmix.o \
       libac3/imdct.o  libac3/parse.o
endif

# i386 mmx specific stuff
ifeq ($(TARGET_MMX),yes)
OBJS += i386/fdct_mmx.o i386/cputest.o \
	i386/dsputil_mmx.o i386/mpegvideo_mmx.o \
        i386/idct_mmx.o i386/motion_est_mmx.o
endif

# armv4l specific stuff
ifeq ($(TARGET_ARCH_ARMV4L),yes)
ASM_OBJS += armv4l/jrevdct_arm.o
OBJS += armv4l/dsputil_arm.o
endif

# sun mediaLib specific stuff
# currently only works when libavcodec is used in mplayer
ifeq ($(HAVE_MLIB),yes)
OBJS += mlib/dsputil_mlib.o
CFLAGS += $(MLIB_INC)
endif

SRCS = $(OBJS:.o=.c) $(ASM_OBJS:.o=.s)

LIB= libavcodec.a
ifeq ($(BUILD_SHARED),yes)
SLIB= libffmpeg-$(VERSION).so
endif
TESTS= imgresample-test dct-test motion-test

all: $(LIB) $(SLIB)
tests: apiexample cpuid_test $(TESTS)

$(LIB): $(OBJS) $(ASM_OBJS)
	rm -f $@
	$(AR) rcs $@ $(OBJS) $(ASM_OBJS)

$(SLIB): $(OBJS) $(ASM_OBJS)
	rm -f $@
	$(CC) -shared -o $@ $(OBJS) $(ASM_OBJS)
	ln -sf $@ libffmpeg.so
dsputil.o: dsputil.c dsputil.h

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $< 

%.o: %.S
	$(CC) $(CFLAGS) -c -o $@ $<

# depend only used by mplayer now
dep:	depend

depend:
	$(CC) -MM $(CFLAGS) $(SRCS) 1>.depend

clean: 
	rm -f *.o *~ $(LIB) $(SLIB) *.so i386/*.o i386/*~ \
	   armv4l/*.o armv4l/*~ \
	   mlib/*.o mlib/*~ \
           libac3/*.o libac3/*~ \
           apiexample $(TESTS)

distclean: clean
	rm -f Makefile.bak .depend

# api example program
apiexample: apiexample.c $(LIB)
	$(CC) $(CFLAGS) -o $@ $< $(LIB) -lm

# cpuid test
cpuid_test: i386/cputest.c
	$(CC) $(CFLAGS) -D__TEST__ -o $@ $<

# testing progs

imgresample-test: imgresample.c
	$(CC) $(CFLAGS) -DTEST -o $@ $^ 

dct-test: dct-test.o jfdctfst.o i386/fdct_mmx.o \
          fdctref.o jrevdct.o i386/idct_mmx.o
	$(CC) -o $@ $^

motion-test: motion_test.o $(LIB)
	$(CC) -o $@ $^

install: all
#	install -m 644 $(LIB) $(prefix)/lib
ifeq ($(BUILD_SHARED),yes)
	install -s -m 755 $(SLIB) $(prefix)/lib
	ln -sf $(prefix)/lib/$(SLIB) $(prefix)/lib/libffmpeg.so
	ldconfig
	mkdir -p $(prefix)/include/libffmpeg
	install -m 644 avcodec.h $(prefix)/include/libffmpeg/avcodec.h
	install -m 644 common.h $(prefix)/include/libffmpeg/common.h
endif
#
# include dependency files if they exist
#
ifneq ($(wildcard .depend),)
include .depend
endif
