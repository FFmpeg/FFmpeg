include $(SUBDIR)../config.mak

NAME = postproc
FFLIBS = avutil

HEADERS = postprocess.h        \
          version.h            \

OBJS = postprocess.o

# Windows resource file
SLIBOBJS-$(HAVE_GNU_WINDRES) += postprocres.o
