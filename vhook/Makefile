include ../config.mak

VPATH=$(SRC_PATH)/vhook

CFLAGS=-fPIC $(SHCFLAGS) -I.. -I$(SRC_PATH) -I$(SRC_PATH)/libavformat -I$(SRC_PATH)/libavcodec -DHAVE_AV_CONFIG_H

ifeq ($(CONFIG_DARWIN),yes)
SHFLAGS+=-flat_namespace -undefined suppress
endif
HOOKS=null.so fish.so ppm.so

ifeq ($(HAVE_IMLIB2),yes)
    HOOKS += imlib2.so
endif    

ifeq ($(HAVE_FREETYPE2),yes)
    HOOKS += drawtext.so
    CFLAGS += `freetype-config --cflags`
endif    

all: $(HOOKS) 

SRCS := $(HOOKS:.so=.c)

depend: $(SRCS)
	$(CC) -MM $(CFLAGS) $^ 1>.depend

install:
	install -d "$(prefix)/lib/vhook"
	install -m 755 $(HOOKS) "$(prefix)/lib/vhook"

imlib2.so: imlib2.o
	$(CC) -g -o $@ $(SHFLAGS) $< -lImlib2

drawtext.so: drawtext.o
	$(CC) -g -o $@ $(SHFLAGS) $< `freetype-config --libs`

%.so: %.o
	$(CC) -g -o $@ $(SHFLAGS) $<

clean:
	rm -f *.o *.d .depend *.so *~

ifneq ($(wildcard .depend),)
include .depend
endif
