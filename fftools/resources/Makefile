clean::
	$(RM) $(CLEANSUFFIXES:%=fftools/resources/%)

vpath %.html $(SRC_PATH)
vpath %.css  $(SRC_PATH)

OBJS-resman +=                     \
    fftools/resources/resman.o     \


RESOBJS +=                         \
    fftools/resources/graph.html.o \
    fftools/resources/graph.css.o  \


$(RESOBJS): CCDEP       =
$(RESOBJS): CC_DEPFLAGS =

.SECONDARY: $(RESOBJS:.o=.gz) $(RESOBJS:.o=.c) $(RESOBJS:%.css.o=%.css.min) $(RESOBJS:%.css.o=%.css.min.gz) $(RESOBJS:%.html.o=%.html.gz) $(RESOBJS:.o=)
