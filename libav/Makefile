include ../config.mak
CFLAGS= $(OPTFLAGS) -Wall -g -I../libavcodec -DHAVE_AV_CONFIG_H

OBJS= rm.o mpeg.o asf.o avienc.o jpeg.o swf.o wav.o raw.o \
	 avidec.o ffm.o \
	 avio.o aviobuf.o utils.o \
	 file.o img.o

ifeq ($(CONFIG_GRAB),yes)
OBJS+= grab.o audio.o 
endif

ifneq ($(CONFIG_WIN32),yes)
OBJS+= udp.o http.o
endif

LIB= libav.a

all: $(LIB)

$(LIB): $(OBJS)
	rm -f $@
	$(AR) rcs $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $< 

clean: 
	rm -f *.o *~ *.a 

