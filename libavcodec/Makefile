#
# libavcodec Makefile
# (c) 2000-2003 Fabrice Bellard
#
include ../config.mak

VPATH=$(SRC_PATH)/libavcodec

# NOTE: -I.. is needed to include config.h
CFLAGS=$(OPTFLAGS) -DHAVE_AV_CONFIG_H -I.. -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_GNU_SOURCE

OBJS= bitstream.o utils.o mem.o allcodecs.o \
      mpegvideo.o jrevdct.o jfdctfst.o jfdctint.o\
      mpegaudio.o ac3enc.o mjpeg.o resample.o resample2.o dsputil.o \
      motion_est.o imgconvert.o imgresample.o \
      mpeg12.o mpegaudiodec.o pcm.o simple_idct.o \
      ratecontrol.o adpcm.o eval.o dv.o error_resilience.o \
      fft.o mdct.o mace.o huffyuv.o cyuv.o opts.o raw.o h264.o golomb.o \
      vp3.o asv1.o 4xm.o cabac.o ffv1.o ra144.o ra288.o vcr1.o cljr.o \
      roqvideo.o dpcm.o interplayvideo.o xan.o rpza.o cinepak.o msrle.o \
      msvideo1.o vqavideo.o idcinvideo.o adx.o rational.o faandct.o 8bps.o \
      smc.o parser.o flicvideo.o truemotion1.o vmdav.o lcl.o qtrle.o g726.o \
      flac.o vp3dsp.o integer.o snow.o tscc.o sonic.o ulti.o h264idct.o \
      qdrw.o xl.o rangecoder.o png.o pnm.o qpeg.o

AMROBJS=
ifeq ($(AMR_NB),yes)
ifeq ($(AMR_NB_FIXED),yes)
AMROBJS= amr.o
AMREXTRALIBS+= amr/*.o
AMRLIBS=amrlibs
CLEANAMR=cleanamr
else
AMROBJS= amr.o
OBJS+= amr_float/sp_dec.o amr_float/sp_enc.o amr_float/interf_dec.o amr_float/interf_enc.o
CLEANAMR=cleanamrfloat
endif
endif

ifeq ($(HAVE_PTHREADS),yes)
OBJS+= pthread.o
endif

ifeq ($(HAVE_W32THREADS),yes)
OBJS+= w32thread.o
endif

ifeq ($(HAVE_BEOSTHREADS),yes)
OBJS+= beosthread.o
endif

ifeq ($(AMR_WB),yes)
AMROBJS= amr.o
OBJS+= amrwb_float/dec_acelp.o amrwb_float/dec_dtx.o amrwb_float/dec_gain.o \
		amrwb_float/dec_if.o amrwb_float/dec_lpc.o amrwb_float/dec_main.o \
		amrwb_float/dec_rom.o amrwb_float/dec_util.o amrwb_float/enc_acelp.o \
		amrwb_float/enc_dtx.o amrwb_float/enc_gain.o amrwb_float/enc_if.o \
		amrwb_float/enc_lpc.o amrwb_float/enc_main.o amrwb_float/enc_rom.o \
		amrwb_float/enc_util.o amrwb_float/if_rom.o
endif
OBJS+= $(AMROBJS)
CLEANAMRWB=cleanamrwbfloat
ASM_OBJS=

# codecs which are patented in some non free countries like the us
ifeq ($(CONFIG_RISKY),yes)
OBJS+= h263.o h261.o msmpeg4.o h263dec.o svq1.o rv10.o wmadec.o indeo3.o
endif

ifeq ($(HAVE_XVMC_ACCEL),yes)
OBJS+= xvmcvideo.o
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

# currently using libdts for dts decoding
ifeq ($(CONFIG_DTS),yes)
OBJS+= dtsdec.o
CFLAGS += $(DTS_INC)
EXTRALIBS += -ldts
endif

ifeq ($(CONFIG_FAAD),yes)
OBJS+= faad.o
ifeq ($(CONFIG_FAADBIN),yes)
# no libs needed
else
EXTRALIBS += -lfaad
endif
endif

ifeq ($(CONFIG_FAAC),yes)
OBJS+= faac.o
EXTRALIBS += -lfaac
endif

ifeq ($(CONFIG_XVID),yes)
OBJS+= xvidff.o
EXTRALIBS += -lxvidcore
endif

ifeq ($(CONFIG_PP),yes)
ifeq ($(SHARED_PP),yes)
EXTRALIBS += -L$(VPATH)/libpostproc -lpostproc
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
	i386/simple_idct_mmx.o i386/fft_sse.o i386/vp3dsp_mmx.o \
	i386/vp3dsp_sse2.o
ifdef TARGET_BUILTIN_VECTOR
i386/fft_sse.o: CFLAGS+= -msse
depend: CFLAGS+= -msse
endif
endif

# armv4l specific stuff
ifeq ($(TARGET_ARCH_ARMV4L),yes)
ASM_OBJS += armv4l/jrevdct_arm.o armv4l/simple_idct_arm.o
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
OBJS += ppc/dsputil_altivec.o ppc/mpegvideo_altivec.o ppc/idct_altivec.o \
        ppc/fft_altivec.o ppc/gmc_altivec.o ppc/fdct_altivec.o \
        ppc/dsputil_h264_altivec.o
endif

ifeq ($(TARGET_ARCH_SH4),yes)
OBJS+= sh4/idct_sh4.o sh4/dsputil_sh4.o sh4/dsputil_align.o
endif

ifeq ($(TARGET_ARCH_SPARC),yes)
OBJS+=sparc/dsputil_vis.o
sparc/%.o: sparc/%.c
	$(CC) -mcpu=ultrasparc -mtune=ultrasparc $(CFLAGS) -c -o $@ $< 
endif
ifeq ($(TARGET_ARCH_SPARC64),yes)
CFLAGS+= -mcpu=ultrasparc -mtune=ultrasparc
endif

SRCS := $(OBJS:.o=.c) $(ASM_OBJS:.o=.S)
OBJS := $(OBJS) $(ASM_OBJS)

LIB= $(LIBPREF)avcodec$(LIBSUF)
ifeq ($(BUILD_SHARED),yes)
SLIB= $(SLIBPREF)avcodec$(SLIBSUF)
endif
TESTS= imgresample-test dct-test motion-test fft-test

all: $(LIB) $(SLIB)

amrlibs:
	$(MAKE) -C amr spclib fipoplib

tests: apiexample cpuid_test $(TESTS)

$(LIB): $(OBJS) $(AMRLIBS)
	rm -f $@
	$(AR) rc $@ $(OBJS) $(AMREXTRALIBS)
	$(RANLIB) $@

$(SLIB): $(OBJS)
ifeq ($(CONFIG_PP),yes)
	$(MAKE) -C $(VPATH)/libpostproc
endif
ifeq ($(CONFIG_WIN32),yes)
	$(CC) $(SHFLAGS) -Wl,--output-def,$(@:.dll=.def) -o $@ $(OBJS) $(EXTRALIBS) $(AMREXTRALIBS)
	-lib /machine:i386 /def:$(@:.dll=.def)
else
	$(CC) $(SHFLAGS) -o $@ $(OBJS) $(EXTRALIBS) $(AMREXTRALIBS) $(LDFLAGS)
endif

dsputil.o: dsputil.c dsputil.h

libpostproc/libpostproc.a:
	$(MAKE) -C libpostproc

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $< 

%.o: %.S
	$(CC) $(CFLAGS) -c -o $@ $<

depend: $(SRCS)
	$(CC) -MM $(CFLAGS) $^ 1>.depend

dep:	depend

clean: $(CLEANAMR)
	rm -f *.o *.d *~ .depend $(LIB) $(SLIB) *.so i386/*.o i386/*~ \
	   armv4l/*.o armv4l/*~ \
	   mlib/*.o mlib/*~ \
	   alpha/*.o alpha/*~ \
	   ppc/*.o ppc/*~ \
	   ps2/*.o ps2/*~ \
	   sh4/*.o sh4/*~ \
	   sparc/*.o sparc/*~ \
	   liba52/*.o liba52/*~ \
	   apiexample $(TESTS)
	$(MAKE) -C libpostproc clean

distclean: clean
	rm -f Makefile.bak .depend

cleanamr:
	$(MAKE) -C amr clean

cleanamrfloat:
	rm -f amr_float/*.o

cleanamrwbfloat:
	$(MAKE) -C amrwb_float -f makefile.gcc clean

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

ifeq ($(BUILD_SHARED),yes)
install: all install-headers
ifeq ($(CONFIG_WIN32),yes)
	install $(INSTALLSTRIP) -m 755 $(SLIB) "$(prefix)"
else
	install -d $(prefix)/lib
	install $(INSTALLSTRIP) -m 755 $(SLIB) $(prefix)/lib/libavcodec-$(VERSION).so
	ln -sf libavcodec-$(VERSION).so $(prefix)/lib/libavcodec.so
	ldconfig || true
endif
ifeq ($(CONFIG_PP),yes)
	$(MAKE) -C $(VPATH)/libpostproc $@
endif
else
install:
endif

installlib: all install-headers
	install -m 644 $(LIB) "$(prefix)/lib"

install-headers:
	mkdir -p "$(prefix)/include/ffmpeg"
	install -m 644 $(SRC_PATH)/libavcodec/avcodec.h \
	               $(SRC_PATH)/libavcodec/common.h \
	               $(SRC_PATH)/libavcodec/rational.h \
                "$(prefix)/include/ffmpeg"

#
# include dependency files if they exist
#
ifneq ($(wildcard .depend),)
include .depend
endif
