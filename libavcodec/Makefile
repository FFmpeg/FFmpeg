include ../config.mk
CFLAGS= -O2 -Wall -g
LDFLAGS= -g

OBJS= common.o utils.o mpegvideo.o h263.o jrevdct.o jfdctfst.o \
      mpegaudio.o ac3enc.o mjpegenc.o resample.o dsputil.o \
      motion_est.o imgconvert.o imgresample.o msmpeg4.o \
      mpeg12.o h263dec.o rv10.o

# currently using libac3 for ac3 decoding
OBJS+= ac3dec.o \
       libac3/bit_allocate.o libac3/bitstream.o libac3/downmix.o \
       libac3/imdct.o  libac3/parse.o

# currently using mpglib for mpeg audio decoding
OBJS+= mpegaudiodec.o \
       mpglib/layer1.o mpglib/layer2.o mpglib/layer3.o \
       mpglib/dct64_i386.o mpglib/decode_i386.o  mpglib/tabinit.o

# i386 mmx specific stuff
ifdef CONFIG_MMX
OBJS += i386/fdct_mmx.o i386/fdctdata.o i386/sad_mmx.o i386/cputest.o \
	i386/dsputil_mmx.o
endif

LIB= libavcodec.a
TESTS= imgresample-test dct-test

all: $(LIB) apiexample

$(LIB): $(OBJS)
	rm -f $@
	$(AR) rcs $@ $(OBJS)

dsputil.o: dsputil.c dsputil.h

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $< 

%.o: %.s
	nasm -f elf -o $@ $<

clean: 
	rm -f *.o *~ *.a i386/*.o i386/*~ \
           libac3/*.o libac3/*~ \
           mpglib/*.o mpglib/*~ \
           apiexample $(TESTS)

# api example program
apiexample: apiexample.c $(LIB)
	$(CC) $(CFLAGS) -o $@ $< $(LIB) -lm

# testing progs

imgresample-test: imgresample.c
	$(CC) $(CFLAGS) -DTEST -o $@ $^ 

dct-test: dct-test.o jfdctfst.o i386/fdct_mmx.o i386/fdctdata.o fdctref.o
	$(CC) -o $@ $^
