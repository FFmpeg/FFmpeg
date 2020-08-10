DNNTESTPROGS += dnn-layer-pad
DNNTESTPROGS += dnn-layer-conv2d
DNNTESTPROGS += dnn-layer-depth2space
DNNTESTPROGS += dnn-layer-mathbinary
DNNTESTPROGS += dnn-layer-maximum
DNNTESTPROGS += dnn-layer-mathunary
DNNTESTPROGS += dnn-layer-avgpool

DNNTESTOBJS  := $(DNNTESTOBJS:%=$(DNNTESTSDIR)%) $(DNNTESTPROGS:%=$(DNNTESTSDIR)/%-test.o)
DNNTESTPROGS := $(DNNTESTPROGS:%=$(DNNTESTSDIR)/%-test$(EXESUF))
-include $(wildcard $(DNNTESTOBJS:.o=.d))

$(DNNTESTPROGS): %$(EXESUF): %.o $(FF_STATIC_DEP_LIBS)
	$(LD) $(LDFLAGS) $(LDEXEFLAGS) $(LD_O) $(filter %.o,$^) $(FF_STATIC_DEP_LIBS) $(EXTRALIBS-avcodec) $(EXTRALIBS-avfilter) $(EXTRALIBS-avformat) $(EXTRALIBS-avutil) $(EXTRALIBS-swresample) $(EXTRALIBS)

testclean::
	$(RM) $(addprefix $(DNNTESTSDIR)/,$(CLEANSUFFIXES) *-test$(EXESUF))
