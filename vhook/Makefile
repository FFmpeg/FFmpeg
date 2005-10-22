include ../config.mak

VPATH=$(SRC_PATH)/vhook

CFLAGS=-fPIC $(SHCFLAGS) -I.. -I$(SRC_PATH) -I$(SRC_PATH)/libavformat -I$(SRC_PATH)/libavcodec -I$(SRC_PATH)/libavutil -DHAVE_AV_CONFIG_H

ifeq ($(CONFIG_DARWIN),yes)
	SHFLAGS += -flat_namespace -undefined suppress
endif
HOOKS=null$(SLIBSUF) fish$(SLIBSUF) ppm$(SLIBSUF) watermark$(SLIBSUF)

ifeq ($(HAVE_IMLIB2),yes)
    HOOKS += imlib2$(SLIBSUF)
endif    

ifeq ($(HAVE_FREETYPE2),yes)
    HOOKS += drawtext$(SLIBSUF)
    CFLAGS += `freetype-config --cflags`
endif    

all: $(HOOKS) 

SRCS := $(HOOKS:$(SLIBSUF)=.c)

depend: $(SRCS)
	$(CC) -MM $(CFLAGS) $^ 1>.depend

install:
	install -d "$(libdir)/vhook"
	install -m 755 $(HOOKS) "$(libdir)/vhook"

imlib2$(SLIBSUF): imlib2.o
	$(CC) $(LDFLAGS) -g -o $@ $(SHFLAGS) $< -lImlib2

drawtext$(SLIBSUF): drawtext.o
	$(CC) $(LDFLAGS) -g -o $@ $(SHFLAGS) $< `freetype-config --libs`

%$(SLIBSUF): %.o
ifeq ($(CONFIG_DARWIN),yes)
	$(CC) $(LDFLAGS) $(SHFLAGS) -Wl,-install_name,$(libdir)/vhook/$@ -g -o $@ $<
else
	$(CC) $(LDFLAGS) -g -o $@ $(SHFLAGS) $<
endif

clean:
	rm -f *.o *.d .depend *$(SLIBSUF) *~

ifneq ($(wildcard .depend),)
include .depend
endif
