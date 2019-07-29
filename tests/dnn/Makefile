DNNTESTPROGS += dnn-layer-pad

DNNTESTOBJS  := $(DNNTESTOBJS:%=$(DNNTESTSDIR)%) $(DNNTESTPROGS:%=$(DNNTESTSDIR)/%-test.o)
DNNTESTPROGS := $(DNNTESTPROGS:%=$(DNNTESTSDIR)/%-test$(EXESUF))
-include $(wildcard $(DNNTESTOBJS:.o=.d))

$(DNNTESTPROGS): %$(EXESUF): %.o $(FF_DEP_LIBS)
	$(LD) $(LDFLAGS) $(LDEXEFLAGS) $(LD_O) $(filter %.o,$^) $(FF_EXTRALIBS) $(ELIBS)

testclean::
	$(RM) $(addprefix $(DNNTESTSDIR)/,$(CLEANSUFFIXES) *-test$(EXESUF))
