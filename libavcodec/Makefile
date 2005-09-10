#
# libavcodec Makefile
# (c) 2000-2005 Fabrice Bellard
#
include ../config.mak

VPATH=$(SRC_PATH)/libavcodec

# NOTE: -I.. is needed to include config.h
CFLAGS=$(OPTFLAGS) -DHAVE_AV_CONFIG_H -I.. -I$(SRC_PATH)/libavutil -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_GNU_SOURCE $(AMR_CFLAGS)

OBJS= bitstream.o utils.o mem.o allcodecs.o \
      mpegvideo.o jrevdct.o jfdctfst.o jfdctint.o\
      mpegaudio.o ac3enc.o mjpeg.o resample.o resample2.o dsputil.o \
      motion_est.o imgconvert.o imgresample.o \
      mpeg12.o mpegaudiodec.o pcm.o simple_idct.o \
      ratecontrol.o adpcm.o eval.o error_resilience.o \
      fft.o mdct.o raw.o golomb.o cabac.o\
      dpcm.o adx.o faandct.o parser.o g726.o \
      vp3dsp.o h264idct.o rangecoder.o pnm.o h263.o msmpeg4.o h263dec.o dvdsub.o dvbsub.o dvbsubdec.o\
      opt.o

ifeq ($(CONFIG_AASC_DECODER),yes)
    OBJS+= aasc.o
endif
ifeq ($(CONFIG_ALAC_DECODER),yes)
    OBJS+= alac.o
endif
ifneq ($(CONFIG_ASV1_DECODER)$(CONFIG_ASV1_ENCODER)$(CONFIG_ASV2_DECODER)$(CONFIG_ASV2_ENCODER),)
    OBJS+= asv1.o
endif
ifeq ($(CONFIG_CINEPAK_DECODER),yes)
    OBJS+= cinepak.o
endif
ifneq ($(CONFIG_CLJR_DECODER)$(CONFIG_CLJR_ENCODER),)
    OBJS+= cljr.o
endif
ifeq ($(CONFIG_CYUV_DECODER),yes)
    OBJS+= cyuv.o
endif
ifneq ($(CONFIG_DVVIDEO_DECODER)$(CONFIG_DVVIDEO_ENCODER),)
    OBJS+= dv.o
endif
ifeq ($(CONFIG_EIGHTBPS_DECODER),yes)
    OBJS+= 8bps.o
endif
ifneq ($(CONFIG_FFV1_DECODER)$(CONFIG_FFV1_ENCODER),)
    OBJS+= ffv1.o
endif
ifeq ($(CONFIG_FLAC_DECODER),yes)
    OBJS+= flac.o
endif
ifeq ($(CONFIG_FLIC_DECODER),yes)
    OBJS+= flicvideo.o
endif
ifeq ($(CONFIG_FOURXM_DECODER),yes)
    OBJS+= 4xm.o
endif
ifeq ($(CONFIG_FRAPS_DECODER),yes)
    OBJS+= fraps.o
endif
ifneq ($(CONFIG_H261_DECODER)$(CONFIG_H261_ENCODER),)
    OBJS+= h261.o
endif
ifneq ($(CONFIG_H264_DECODER)$(CONFIG_SVQ3_DECODER),)
    OBJS+= h264.o
endif
ifneq ($(CONFIG_HUFFYUV_DECODER)$(CONFIG_HUFFYUV_ENCODER)$(CONFIG_FFVHUFF_DECODER)$(CONFIG_FFVHUFF_ENCODER),)
    OBJS+= huffyuv.o
endif
ifeq ($(CONFIG_IDCIN_DECODER),yes)
    OBJS+= idcinvideo.o
endif
ifeq ($(CONFIG_INDEO2_DECODER),yes)
    OBJS+= indeo2.o
endif
ifeq ($(CONFIG_INDEO3_DECODER),yes)
    OBJS+= indeo3.o
endif
ifeq ($(CONFIG_INTERPLAY_VIDEO_DECODER),yes)
    OBJS+= interplayvideo.o
endif
ifneq ($(CONFIG_MSZH_DECODER)$(CONFIG_ZLIB_DECODER)$(CONFIG_ZLIB_ENCODER),)
    OBJS+= lcl.o
endif
ifeq ($(CONFIG_LOCO_DECODER),yes)
    OBJS+= loco.o
endif
ifneq ($(CONFIG_MACE3_DECODER)$(CONFIG_MACE6_DECODER),)
    OBJS+= mace.o
endif
ifeq ($(CONFIG_MSRLE_DECODER),yes)
    OBJS+= msrle.o
endif
ifeq ($(CONFIG_MSVIDEO1_DECODER),yes)
    OBJS+= msvideo1.o
endif
ifneq ($(CONFIG_PNG_DECODER)$(CONFIG_PNG_ENCODER),)
    OBJS+= png.o
endif
ifeq ($(CONFIG_QDRAW_DECODER),yes)
    OBJS+= qdrw.o
endif
ifeq ($(CONFIG_QPEG_DECODER),yes)
    OBJS+= qpeg.o
endif
ifeq ($(CONFIG_QTRLE_DECODER),yes)
    OBJS+= qtrle.o
endif
ifeq ($(CONFIG_RA_144_DECODER),yes)
    OBJS+= ra144.o
endif
ifeq ($(CONFIG_RA_288_DECODER),yes)
    OBJS+= ra288.o
endif
ifeq ($(CONFIG_ROQ_DECODER),yes)
    OBJS+= roqvideo.o
endif
ifeq ($(CONFIG_RPZA_DECODER),yes)
    OBJS+= rpza.o
endif
ifneq ($(CONFIG_RV10_DECODER)$(CONFIG_RV20_DECODER)$(CONFIG_RV10_ENCODER)$(CONFIG_RV20_ENCODER),)
    OBJS+= rv10.o
endif
ifeq ($(CONFIG_SHORTEN_DECODER),yes)
    OBJS+= shorten.o
endif
ifeq ($(CONFIG_SMC_DECODER),yes)
    OBJS+= smc.o
endif
ifneq ($(CONFIG_SNOW_DECODER)$(CONFIG_SNOW_ENCODER),)
    OBJS+= snow.o
endif
ifneq ($(CONFIG_SONIC_DECODER)$(CONFIG_SONIC_ENCODER)$(CONFIG_SONIC_LS_ENCODER),)
    OBJS+= sonic.o
endif
ifneq ($(CONFIG_SVQ1_DECODER)$(CONFIG_SVQ1_ENCODER),)
    OBJS+= svq1.o
endif
ifeq ($(CONFIG_TRUEMOTION1_DECODER),yes)
    OBJS+= truemotion1.o
endif
ifeq ($(CONFIG_TSCC_DECODER),yes)
    OBJS+= tscc.o
endif
ifeq ($(CONFIG_ULTI_DECODER),yes)
    OBJS+= ulti.o
endif
ifneq ($(CONFIG_VC9_DECODER)$(CONFIG_WMV3_DECODER),)
    OBJS+= vc9.o
endif
ifneq ($(CONFIG_VCR1_DECODER)$(CONFIG_VCR1_ENCODER),)
    OBJS+= vcr1.o
endif
ifneq ($(CONFIG_VMDVIDEO_DECODER)$(CONFIG_VMDAUDIO_DECODER),)
    OBJS+= vmdav.o
endif
ifeq ($(CONFIG_VORBIS_DECODER),yes)
    OBJS+= vorbis.o
endif
ifneq ($(CONFIG_VP3_DECODER)$(CONFIG_THEORA_DECODER),)
    OBJS+= vp3.o
endif
ifeq ($(CONFIG_VQA_DECODER),yes)
    OBJS+= vqavideo.o
endif
ifneq ($(CONFIG_WMAV1_DECODER)$(CONFIG_WMAV2_DECODER),)
    OBJS+= wmadec.o
endif
ifeq ($(CONFIG_WNV1_DECODER),yes)
    OBJS+= wnv1.o
endif
ifeq ($(CONFIG_WS_SND1_DECODER),yes)
    OBJS+= ws-snd1.o
endif
ifneq ($(CONFIG_XAN_WC3_DECODER)$(CONFIG_XAN_WC4_DECODER),)
    OBJS+= xan.o
endif
ifeq ($(CONFIG_XL_DECODER),yes)
    OBJS+= xl.o
endif
      
      
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

EXTRALIBS += -L../libavutil -lavutil$(BUILDSUF)

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

ifeq ($(CONFIG_X264),yes)
OBJS+= x264.o
EXTRALIBS += -lx264
endif

ifeq ($(CONFIG_PP),yes)
ifeq ($(SHARED_PP),yes)
EXTRALIBS += -Llibpostproc -lpostproc$(BUILDSUF)
else
# LIBS += libpostproc/libpostproc.a ... should be fixed
OBJS += libpostproc/postprocess.o
endif
endif

ifeq ($(CONFIG_MP3LAME),yes)
OBJS += mp3lameaudio.o
EXTRALIBS += -lmp3lame
endif

ifeq ($(CONFIG_LIBOGG),yes)
ifeq ($(CONFIG_LIBVORBIS),yes)
OBJS += oggvorbis.o
EXTRALIBS += -lvorbisenc -lvorbis
endif
ifeq ($(CONFIG_LIBTHEORA), yes)
OBJS += oggtheora.o
EXTRALIBS += -ltheora
endif
EXTRALIBS += -logg
endif

ifeq ($(CONFIG_LIBGSM),yes)
OBJS += libgsm.o
EXTRALIBS += -lgsm
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
ifeq ($(CONFIG_GPL),yes)
OBJS += i386/idct_mmx_xvid.o
endif
ifdef TARGET_BUILTIN_VECTOR
i386/fft_sse.o: CFLAGS+= -msse
depend: CFLAGS+= -msse
endif
endif

# armv4l specific stuff
ifeq ($(TARGET_ARCH_ARMV4L),yes)
ASM_OBJS += armv4l/jrevdct_arm.o armv4l/simple_idct_arm.o armv4l/dsputil_arm_s.o
OBJS += armv4l/dsputil_arm.o armv4l/mpegvideo_arm.o
ifeq ($(TARGET_IWMMXT),yes)
OBJS += armv4l/dsputil_iwmmxt.o armv4l/mpegvideo_iwmmxt.o
endif
endif

# sun mediaLib specific stuff
# currently only works when libavcodec is used in mplayer
ifeq ($(HAVE_MLIB),yes)
OBJS += mlib/dsputil_mlib.o
CFLAGS += $(MLIB_INC)
endif

# Intel IPP specific stuff
# currently only works when libavcodec is used in mplayer
ifeq ($(HAVE_IPP),yes)
CFLAGS += $(IPP_INC)
endif

# alpha specific stuff
ifeq ($(TARGET_ARCH_ALPHA),yes)
OBJS += alpha/dsputil_alpha.o alpha/mpegvideo_alpha.o \
	alpha/simple_idct_alpha.o alpha/motion_est_alpha.o
ASM_OBJS += alpha/dsputil_alpha_asm.o alpha/motion_est_mvi_asm.o
CFLAGS += -fforce-addr
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
LIBAVUTIL= $(SRC_PATH)/libavutil/$(LIBPREF)avutil$(LIBSUF)
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
	$(MAKE) -C libpostproc
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
	$(CC) $(CFLAGS) $(LIBOBJFLAGS) -c -o $@ $< 

%.o: %.S
	$(CC) $(CFLAGS) $(LIBOBJFLAGS) -c -o $@ $<

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
	$(CC) $(CFLAGS) -o $@ $< $(LIB) $(LIBAVUTIL) $(EXTRALIBS) -lm

# cpuid test
cpuid_test: i386/cputest.c
	$(CC) $(CFLAGS) -D__TEST__ -o $@ $<

# testing progs

imgresample-test: imgresample.c
	$(CC) $(CFLAGS) -DTEST -o $@ $^ -lm

dct-test: dct-test.o fdctref.o $(LIB)
	$(CC) -o $@ $^ -lm $(LIBAVUTIL)

motion-test: motion_test.o $(LIB)
	$(CC) -o $@ $^ -lm

fft-test: fft-test.o $(LIB)
	$(CC) -o $@ $^ $(LIBAVUTIL) -lm

ifeq ($(BUILD_SHARED),yes)
install: all install-headers
ifeq ($(CONFIG_WIN32),yes)
	install $(INSTALLSTRIP) -m 755 $(SLIB) "$(prefix)"
else
	install -d $(libdir)
	install $(INSTALLSTRIP) -m 755 $(SLIB) $(libdir)/libavcodec-$(VERSION).so
	ln -sf libavcodec-$(VERSION).so $(libdir)/libavcodec.so
	$(LDCONFIG) || true
endif
ifeq ($(CONFIG_PP),yes)
	$(MAKE) -C libpostproc $@
endif
else
install:
endif

installlib: all install-headers
	install -m 644 $(LIB) "$(libdir)"

install-headers:
	mkdir -p "$(prefix)/include/ffmpeg"
	install -m 644 $(SRC_PATH)/libavcodec/avcodec.h \
                "$(prefix)/include/ffmpeg"
	install -d $(libdir)/pkgconfig
	install -m 644 ../libavcodec.pc $(libdir)/pkgconfig

#
# include dependency files if they exist
#
ifneq ($(wildcard .depend),)
include .depend
endif
