include ../config.mak

VPATH=$(SRC_PATH)/libav
PWD=$(shell pwd)

CFLAGS= $(OPTFLAGS) -Wall -g -I.. -I$(SRC_PATH) -I$(SRC_PATH)/libavcodec -DHAVE_AV_CONFIG_H

OBJS= utils.o 

# mux and demuxes
OBJS+=mpeg.o mpegts.o ffm.o crc.o img.o raw.o rm.o asf.o \
      avienc.o avidec.o wav.o swf.o au.o gif.o mov.o jpeg.o
# file I/O
OBJS+= avio.o aviobuf.o file.o 

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

