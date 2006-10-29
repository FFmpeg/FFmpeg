
include ../config.mak

# Overload incdir, postproc include files go in a different directory.
incdir=$(prefix)/include/postproc

EXTRALIBS := -L$(BUILD_ROOT)/libavutil -lavutil$(BUILDSUF) $(EXTRALIBS)

NAME=postproc
ifeq ($(BUILD_SHARED),yes)
LIBVERSION=$(SPPVERSION)
LIBMAJOR=$(SPPMAJOR)
endif

STATIC_OBJS=postprocess.o
SHARED_OBJS=postprocess_pic.o

HEADERS = postprocess.h

include $(SRC_PATH)/common.mak

depend dep: postprocess.c

postprocess_pic.o: postprocess.c
	$(CC) -c $(CFLAGS) -fomit-frame-pointer -fPIC -DPIC -o $@ $<

