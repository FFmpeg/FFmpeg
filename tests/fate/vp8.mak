VP8_SUITE = 001 002 003 004 005 006 007 008 009 010 011 012 013 014 015 016 017

define FATE_VP8_SUITE
FATE_VP8 += fate-vp8-test-vector$(2)-$(1)
fate-vp8-test-vector$(2)-$(1): CMD = framemd5 $(3) -i $(SAMPLES)/vp8-test-vectors-r1/vp80-00-comprehensive-$(1).ivf
fate-vp8-test-vector$(2)-$(1): REF = $(SRC_PATH_BARE)/tests/ref/fate/vp8-test-vector-$(1)
endef

define FATE_VP8_FULL_SUITE
$(foreach N,$(VP8_SUITE),$(eval $(call FATE_VP8_SUITE,$(N),$(2),$(VP8_OPT$(1)))))

FATE_VP8 += fate-vp8-sign-bias$(2)
fate-vp8-sign-bias$(2): CMD = framemd5 $(VP8_OPT$(1)) -i $(SAMPLES)/vp8/sintel-signbias.ivf
fate-vp8-sign-bias$(2): REF = $(SRC_PATH_BARE)/tests/ref/fate/vp8-sign-bias
endef

VP8_OPTS = _ _emu_edge_
VP8_OPT_ =
VP8_OPT_emu_edge_ = -flags emu_edge

$(foreach O,$(VP8_OPTS),$(eval $(call FATE_VP8_FULL_SUITE,$(O),$(subst _,-,$(O:_=)))))
FATE_TESTS += $(FATE_VP8)
fate-vp8: $(FATE_VP8)
