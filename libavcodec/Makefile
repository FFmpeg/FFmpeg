#
# libavcodec Makefile
# (c) 2000, 2001, 2002 Fabrice Bellard
#
include ../config.mak

VPATH=$(SRC_PATH)/libavcodec

# NOTE: -I.. is needed to include config.h
CFLAGS= $(OPTFLAGS) -Wall -g -DHAVE_AV_CONFIG_H -I.. -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_GNU_SOURCE
LDFLAGS= -g

OBJS= common.o utils.o mem.o allcodecs.o \
      mpegvideo.o h263.o jrevdct.o jfdctfst.o jfdctint.o\
      mpegaudio.o ac3enc.o mjpeg.o resample.o dsputil.o \
      motion_est.o imgconvert.o imgresample.o msmpeg4.o \
      mpeg12.o h263dec.o svq1.o rv10.o mpegaudiodec.o pcm.o simple_idct.o \
      ratecontrol.o adpcm.o eval.o
ASM_OBJS=

# currently using liba52 for ac3 decoding
ifeq ($(CONFIG_AC3),yes)
OBJS+= a52dec.o

# using builtin liba52 or runtime linked liba52.so.0
ifneq ($(CONFIG_A52BIN),yes)
OBJS+= liba52/bit_allocate.o liba52/bitstream.o liba52/downmix.o \
	liba52/imdct.o  liba52/parse.o
endif
endif

ifeq ($(CONFIG_MP3LAME),yes)
OBJS += mp3lameaudio.o
EXTRALIBS += -lmp3lame
endif

ifeq ($(CONFIG_VORBIS),yes)
OBJS += oggvorbis.o
EXTRALIBS += -lvorbis -lvorbisenc
endif

ifeq ($(TARGET_GPROF),yes)
CFLAGS+=-p
LDFLAGS+=-p
endif

# i386 mmx specific stuff
ifeq ($(TARGET_MMX),yes)
OBJS += i386/fdct_mmx.o i386/cputest.o \
	i386/dsputil_mmx.o i386/mpegvideo_mmx.o \
	i386/idct_mmx.o i386/motion_est_mmx.o \
	i386/simple_idct_mmx.o
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

# alpha specific stuff
ifeq ($(TARGET_ARCH_ALPHA),yes)
OBJS += alpha/dsputil_alpha.o alpha/mpegvideo_alpha.o alpha/motion_est_alpha.o
ASM_OBJS += alpha/dsputil_alpha_asm.o
CFLAGS += -Wa,-mpca56 -finline-limit=8000 -fforce-addr -freduce-all-givs
endif

ifeq ($(TARGET_ARCH_POWERPC),yes)
OBJS += ppc/dsputil_ppc.o
endif

ifeq ($(TARGET_ALTIVEC),yes)
CFLAGS += -faltivec
OBJS += ppc/dsputil_altivec.o
endif

SRCS := $(OBJS:.o=.c) $(ASM_OBJS:.o=.S)
OBJS := $(OBJS) $(ASM_OBJS)

LIB= libavcodec.a
ifeq ($(BUILD_SHARED),yes)
SLIB= libavcodec.so
endif
TESTS= imgresample-test dct-test motion-test

all: $(LIB) $(SLIB)

tests: apiexample cpuid_test $(TESTS)

$(LIB): $(OBJS)
	rm -f $@
	$(AR) rc $@ $(OBJS)
	$(RANLIB) $@

$(SLIB): $(OBJS)
	$(CC) $(SHFLAGS) -o $@ $(OBJS) $(EXTRALIBS)

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
	rm -f *.o *~ .depend $(LIB) $(SLIB) *.so i386/*.o i386/*~ \
	   armv4l/*.o armv4l/*~ \
	   mlib/*.o mlib/*~ \
	   alpha/*.o alpha/*~ \
	   ppc/*.o ppc/*~ \
	   liba52/*.o liba52/*~ \
	   apiexample $(TESTS)

distclean: clean
	rm -f Makefile.bak .depend

# api example program
apiexample: apiexample.c $(LIB)
	$(CC) $(CFLAGS) -o $@ $< $(LIB) $(EXTRALIBS) -lm

# cpuid test
cpuid_test: i386/cputest.c
	$(CC) $(CFLAGS) -D__TEST__ -o $@ $<

# testing progs

imgresample-test: imgresample.c
	$(CC) $(CFLAGS) -DTEST -o $@ $^ -lm

dct-test: dct-test.o jfdctfst.o jfdctint.o i386/fdct_mmx.o\
          fdctref.o jrevdct.o i386/idct_mmx.o simple_idct.o i386/simple_idct_mmx.o
	$(CC) -o $@ $^ -lm

motion-test: motion_test.o $(LIB)
	$(CC) -o $@ $^ -lm

install: all
ifeq ($(BUILD_SHARED),yes)
	install -d $(prefix)/lib
	install -s -m 755 $(SLIB) $(prefix)/lib/libavcodec-$(VERSION).so
	ln -sf libavcodec-$(VERSION).so $(prefix)/lib/libavcodec.so
	ldconfig || true
	mkdir -p $(prefix)/include/ffmpeg
	install -m 644 avcodec.h $(prefix)/include/ffmpeg/avcodec.h
	install -m 644 common.h $(prefix)/include/ffmpeg/common.h
endif

installlib: all
	install -m 644 $(LIB) $(prefix)/lib
	mkdir -p $(prefix)/include/ffmpeg
	install -m 644 $(SRC_PATH)/libavcodec/avcodec.h $(SRC_PATH)/libavcodec/common.h \
                $(prefix)/include/ffmpeg

#
# include dependency files if they exist
#
ifneq ($(wildcard .depend),)
include .depend
endif
