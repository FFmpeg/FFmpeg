include ../config.mak

VPATH=$(SRC_PATH)/vhook

CFLAGS=-fPIC $(OPTFLAGS) -g -Wall -I.. -I$(SRC_PATH) -I$(SRC_PATH)/libavformat -I$(SRC_PATH)/libavcodec -DHAVE_AV_CONFIG_H

ifeq ($(CONFIG_DARWIN),yes)
SHFLAGS+=-bundle -flat_namespace -undefined suppress
endif
HOOKS=null.so fish.so

ifeq ($(HAVE_IMLIB2),yes)
    HOOKS += imlib2.so
endif    

all: $(HOOKS) 

SRCS := $(OBJS:.o=.c)

.depend: $(SRCS)
	$(CC) -MM $(CFLAGS) $^ 1>.depend

install:
	install -s -m 755 $(HOOKS) $(INSTDIR)

imlib2.so: imlib2.o
	$(CC) -g -o $@ $(SHFLAGS) $< -lImlib2

%.so: %.o
	$(CC) -g -o $@ $(SHFLAGS) $<

clean:
	rm -f *.o *.d .depend *.so *~

ifneq ($(wildcard .depend),)
include .depend
endif
