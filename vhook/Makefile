include ../config.mak

VPATH=$(SRC_PATH)/vhook

CFLAGS=-fPIC $(OPTFLAGS) -g -Wall -I$(SRC_PATH) -I$(SRC_PATH)/libavformat -I$(SRC_PATH)/libavcodec -DHAVE_AV_CONFIG_H

ifeq ($(CONFIG_DARWIN),yes)
SHFLAGS+=-bundle -flat_namespace -undefined suppress
endif
HOOKS=null.so fish.so

ifeq ($(HAVE_IMLIB2),yes)
    HOOKS += imlib2.so
endif    

all: $(HOOKS) hooks.html

install:
	install -s -m 755 $(HOOKS) $(INSTDIR)

imlib2.so: imlib2.o
	$(CC) -g -o $@ $(SHFLAGS) $< -lImlib2
	rm $<

%.so: %.o
	$(CC) -g -o $@ $(SHFLAGS) $<

%.html: %.texi
	texi2html -monolithic -number $<

clean:
	rm -f *.o *.so *~
