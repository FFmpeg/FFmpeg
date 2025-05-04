NAME = postproc
DESC = FFmpeg postprocessing library
FFLIBS = avutil

HEADERS = postprocess.h        \
          version.h            \
          version_major.h      \

OBJS = postprocess.o           \
       version.o               \

TESTOBJS = tests/test_utils.o        \

# Windows resource file
SHLIBOBJS-$(HAVE_GNU_WINDRES) += postprocres.o

TESTPROGS = blocktest          \
            stripetest         \
            temptest	       \

$(SUBDIR)tests/blocktest$(EXESUF):  $(SUBDIR)tests/test_utils.o
$(SUBDIR)tests/stripetest$(EXESUF): $(SUBDIR)tests/test_utils.o
$(SUBDIR)tests/temptest$(EXESUF):  $(SUBDIR)tests/test_utils.o
