include ../config.mak
CFLAGS= $(OPTFLAGS) -Wall -g -I../libavcodec -DHAVE_AV_CONFIG_H

OBJS= rm.o mpeg.o asf.o avienc.o jpegenc.o swf.o wav.o raw.o \
	 avidec.o ffm.o \
	 avio.o aviobuf.o utils.o \
	 udp.o http.o file.o grab.o audio.o img.o

LIB= libav.a

all: $(LIB)

$(LIB): $(OBJS)
	rm -f $@
	$(AR) rcs $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $< 

clean: 
	rm -f *.o *~ *.a 

