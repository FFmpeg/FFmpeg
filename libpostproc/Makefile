
include ../config.mak

# Overload incdir, postproc include files go in a different directory.
incdir=$(prefix)/include/postproc

NAME=postproc
ifeq ($(BUILD_SHARED),yes)
LIBVERSION=$(SPPVERSION)
LIBMAJOR=$(SPPMAJOR)
endif

STATIC_OBJS=postprocess.o
SHARED_OBJS=postprocess_pic.o

HEADERS = postprocess.h

CFLAGS  = $(OPTFLAGS) $(MLIB_INC) -I. -I.. -I$(SRC_PATH)/libavcodec $(EXTRA_INC)
# -I/usr/X11R6/include/

include $(SRC_PATH)/common.mak

ifeq ($(BUILD_SHARED),yes)
postprocess_pic.o: postprocess.c
	$(CC) -c $(CFLAGS) -fomit-frame-pointer -fPIC -DPIC -I.. -I../.. -o $@ $<
endif

