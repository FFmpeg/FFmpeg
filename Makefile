CFLAGS= -O2 -Wall -g -I./libav
LDFLAGS= -g -L./libav

PROG= ffmpeg ffserver

all: lib $(PROG)

lib:
	make -C libav all

ffmpeg: rmenc.o mpegmux.o asfenc.o jpegenc.o swfenc.o udp.o formats.o grab.o ffmpeg.o libav/libav.a
	gcc $(LDFLAGS) -o $@ $^ -lav -lm

ffserver: rmenc.o mpegmux.o asfenc.o jpegenc.o swfenc.o formats.o grab.o ffserver.o libav/libav.a
	gcc $(LDFLAGS) -o $@ $^ -lav -lpthread -lm

%.o: %.c
	gcc $(CFLAGS) -c -o $@ $< 

clean: 
	make -C libav clean
	rm -f *.o *~ gmon.out TAGS $(PROG) 

etags:
	etags *.[ch] libav/*.[ch]

tar:
	(cd .. ; tar zcvf ffmpeg-0.3.tgz ffmpeg --exclude CVS --exclude TAGS )
