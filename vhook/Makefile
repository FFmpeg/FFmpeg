include ../config.mak

CFLAGS=-fPIC $(OPTFLAGS) -g -Wall -I.. -I../libav -I../libavcodec -DHAVE_AV_CONFIG_H

HOOKS=null.so fish.so

ifeq ($(HAVE_IMLIB2),yes)
    HOOKS += imlib2.so
endif    

all: $(HOOKS) hooks.html

install:
	install -s -m 755 $(HOOKS) $(INSTDIR)

imlib2.so: imlib2.o
	$(CC) -g -o $@ -shared $< -lImlib2
	rm $<

%.so: %.o
	$(CC) -g -o $@ -shared $<

%.html: %.texi
	texi2html -monolithic -number $<

clean:
	rm -f *.o *.so *~
