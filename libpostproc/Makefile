NAME = postproc
DESC = FFmpeg postprocessing library
FFLIBS = avutil

HEADERS = postprocess.h        \
          version.h            \
          version_major.h      \

OBJS = postprocess.o           \
       version.o               \

# Windows resource file
SLIBOBJS-$(HAVE_GNU_WINDRES) += postprocres.o
