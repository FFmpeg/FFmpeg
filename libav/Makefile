CFLAGS= -O2 -Wall -g
LDFLAGS= -g

OBJS= common.o mpegvideo.o h263enc.o jrevdct.o jfdctfst.o \
      mpegaudio.o ac3enc.o mjpegenc.o resample.o
LIB= libav.a

all: $(LIB)

$(LIB): $(OBJS)
	ar rcs $@ $(OBJS)

%.o: %.c
	gcc $(CFLAGS) -c -o $@ $< 

clean: 
	rm -f *.o *~ *.a 
