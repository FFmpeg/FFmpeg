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
      mpegvideo.o jrevdct.o jfdctfst.o jfdctint.o\
      mpegaudio.o ac3enc.o mjpeg.o resample.o dsputil.o \
      motion_est.o imgconvert.o imgresample.o \
      mpeg12.o mpegaudiodec.o pcm.o simple_idct.o \
      ratecontrol.o adpcm.o eval.o dv.o error_resilience.o \
      fft.o mdct.o mace.o huffyuv.o cyuv.o opts.o raw.o h264.o golomb.o \
      vp3.o

ifeq ($(AMR_NB),yes)
OBJS+= amr.o
endif

ASM_OBJS=

# codecs which are patented in some non free countries like the us
ifeq ($(CONFIG_RISKY),yes)
OBJS+= h263.o msmpeg4.o h263dec.o svq1.o rv10.o wmadec.o indeo3.o
endif

# currently using liba52 for ac3 decoding
ifeq ($(CONFIG_AC3),yes)
OBJS+= a52dec.o

# using builtin liba52 or runtime linked liba52.so.0
ifneq ($(CONFIG_A52BIN),yes)
OBJS+= liba52/bit_allocate.o liba52/bitstream.o liba52/downmix.o \
	liba52/imdct.o  liba52/parse.o liba52/crc.o liba52/resample.o
endif
endif

ifeq ($(CONFIG_FAAD),yes)
OBJS+= faad.o
ifeq ($(CONFIG_FAADBIN),yes)
# no libs needed
else
EXTRALIBS += -lfaad
endif
endif

ifeq ($(CONFIG_PP),yes)
ifeq ($(SHARED_PP),yes)
EXTRALIBS += -lpostproc
else
# LIBS += libpostproc/libpostproc.a ... should be fixed
OBJS += libpostproc/postprocess.o
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
	i386/simple_idct_mmx.o i386/fft_sse.o
ifdef TARGET_BUILTIN_VECTOR
i386/fft_sse.o: CFLAGS+= -msse
endif
endif

# armv4l specific stuff
ifeq ($(TARGET_ARCH_ARMV4L),yes)
ASM_OBJS += armv4l/jrevdct_arm.o
OBJS += armv4l/dsputil_arm.o armv4l/mpegvideo_arm.o
endif

# sun mediaLib specific stuff
# currently only works when libavcodec is used in mplayer
ifeq ($(HAVE_MLIB),yes)
OBJS += mlib/dsputil_mlib.o
CFLAGS += $(MLIB_INC)
endif

# alpha specific stuff
ifeq ($(TARGET_ARCH_ALPHA),yes)
OBJS += alpha/dsputil_alpha.o alpha/mpegvideo_alpha.o \
	alpha/simple_idct_alpha.o alpha/motion_est_alpha.o
ASM_OBJS += alpha/dsputil_alpha_asm.o alpha/motion_est_mvi_asm.o
CFLAGS += -fforce-addr -freduce-all-givs
endif

ifeq ($(TARGET_ARCH_POWERPC),yes)
OBJS += ppc/dsputil_ppc.o ppc/mpegvideo_ppc.o
endif

ifeq ($(TARGET_MMI),yes)
OBJS += ps2/dsputil_mmi.o ps2/idct_mmi.o ps2/mpegvideo_mmi.o
endif

ifeq ($(TARGET_ALTIVEC),yes)
ifeq ($(TARGET_OS),Darwin)
CFLAGS += -faltivec
else
CFLAGS += -maltivec -mabi=altivec
endif
OBJS += ppc/dsputil_altivec.o ppc/mpegvideo_altivec.o ppc/idct_altivec.o \
        ppc/fft_altivec.o ppc/gmc_altivec.o
endif

SRCS := $(OBJS:.o=.c) $(ASM_OBJS:.o=.S)
OBJS := $(OBJS) $(ASM_OBJS)

LIB= $(LIBPREF)avcodec$(LIBSUF)
ifeq ($(BUILD_SHARED),yes)
SLIB= $(SLIBPREF)avcodec$(SLIBSUF)
endif
TESTS= imgresample-test dct-test motion-test fft-test

all: $(LIB) $(SLIB)

tests: apiexample cpuid_test $(TESTS)

$(LIB): $(OBJS)
	rm -f $@
	$(AR) rc $@ $(OBJS)
	$(RANLIB) $@

$(SLIB): $(OBJS)
	$(CC) $(SHFLAGS) -o $@ $(OBJS) $(EXTRALIBS)

dsputil.o: dsputil.c dsputil.h

libpostproc/libpostproc.a:
	$(MAKE) -C libpostproc

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $< 

%.o: %.S
	$(CC) $(CFLAGS) -c -o $@ $<

# motion_est_alpha uses the MVI extension, which is not available with
# -mcpu=ev4 (default) or ev5/ev56. Thus, force -mcpu=pca56 in those
# cases.
ifeq ($(TARGET_ARCH_ALPHA),yes)
alpha/motion_est_alpha.o: alpha/motion_est_alpha.c
	cpu=`echo "$(CFLAGS)" | sed -n 's,.*-mcpu=\([a-zA-Z0-9]*\).*,\1,p'`; \
	case x"$$cpu" in x|xev[45]*) newcpu=pca56;; *) newcpu=$$cpu;; esac; \
	echo $(CC) $(CFLAGS) -mcpu=$$newcpu -c -o $@ $<;\
	$(CC) $(CFLAGS) -mcpu=$$newcpu -c -o $@ $<
endif

depend: $(SRCS)
	$(CC) -MM $(CFLAGS) $^ 1>.depend

dep:	depend

clean: 
	rm -f *.o *.d *~ .depend $(LIB) $(SLIB) *.so i386/*.o i386/*~ \
	   armv4l/*.o armv4l/*~ \
	   mlib/*.o mlib/*~ \
	   alpha/*.o alpha/*~ \
	   ppc/*.o ppc/*~ \
	   ps2/*.o ps2/*~ \
	   liba52/*.o liba52/*~ \
	   apiexample $(TESTS)
	$(MAKE) -C libpostproc clean

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

dct-test: dct-test.o fdctref.o $(LIB)
	$(CC) -o $@ $^ -lm

motion-test: motion_test.o $(LIB)
	$(CC) -o $@ $^ -lm

fft-test: fft-test.o $(LIB)
	$(CC) -o $@ $^ -lm

install: all
ifeq ($(BUILD_SHARED),yes)
	install -d $(prefix)/lib
	install -s -m 755 $(SLIB) $(prefix)/lib/libavcodec-$(VERSION).so
	ln -sf libavcodec-$(VERSION).so $(prefix)/lib/libavcodec.so
	ldconfig || true
	mkdir -p $(prefix)/include/ffmpeg
	install -m 644 $(VPATH)/avcodec.h $(prefix)/include/ffmpeg/avcodec.h
	install -m 644 $(VPATH)/common.h $(prefix)/include/ffmpeg/common.h
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
