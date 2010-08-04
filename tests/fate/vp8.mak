VP8_SUITE = 001 002 003 004 005 006 007 008 009 010 011 012 013 014 015 016 017

define FATE_VP8_SUITE
FATE_VP8 += fate-vp8-test-vector-$(1)
fate-vp8-test-vector-$(1): CMD = framemd5 -i $(SAMPLES)/vp8-test-vectors-r1/vp80-00-comprehensive-$(1).ivf
endef

$(foreach N,$(VP8_SUITE),$(eval $(call FATE_VP8_SUITE,$(N))))

FATE_VP8 += fate-vp8-sign-bias
fate-vp8-sign-bias: CMD = framemd5 -i $(SAMPLES)/vp8/sintel-signbias.ivf

FATE_TESTS += $(FATE_VP8)
fate-vp8: $(FATE_VP8)
