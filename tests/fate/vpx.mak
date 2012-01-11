FATE_TESTS += fate-ea-vp60
fate-ea-vp60: CMD = framecrc -i $(SAMPLES)/ea-vp6/g36.vp6

FATE_TESTS += fate-ea-vp61
fate-ea-vp61: CMD = framecrc -i $(SAMPLES)/ea-vp6/MovieSkirmishGondor.vp6 -t 4

FATE_VP3 += fate-vp31
fate-vp31: CMD = framecrc -i $(SAMPLES)/vp3/vp31.avi

FATE_VP3 += fate-vp3-coeff-level64
fate-vp3-coeff-level64: CMD = framecrc -i $(SAMPLES)/vp3/coeff_level64.mkv

FATE_TESTS += $(FATE_VP3)
fate-vp3: $(FATE_VP3)

FATE_TESTS += fate-vp5
fate-vp5: CMD = framecrc -i $(SAMPLES)/vp5/potter512-400-partial.avi -an

FATE_TESTS += fate-vp6a
fate-vp6a: CMD = framecrc -i $(SAMPLES)/flash-vp6/300x180-Scr-f8-056alpha.flv

FATE_TESTS += fate-vp6f
fate-vp6f: CMD = framecrc -i $(SAMPLES)/flash-vp6/clip1024.flv

VP8_SUITE = 001 002 003 004 005 006 007 008 009 010 011 012 013 014 015 016 017

define FATE_VP8_SUITE
FATE_VP8 += fate-vp8-test-vector$(2)-$(1)
fate-vp8-test-vector$(2)-$(1): CMD = framemd5 $(3) -i $(SAMPLES)/vp8-test-vectors-r1/vp80-00-comprehensive-$(1).ivf
fate-vp8-test-vector$(2)-$(1): REF = $(SRC_PATH)/tests/ref/fate/vp8-test-vector-$(1)
endef

define FATE_VP8_FULL
$(foreach N,$(VP8_SUITE),$(eval $(call FATE_VP8_SUITE,$(N),$(1),$(2))))

FATE_VP8 += fate-vp8-sign-bias$(1)
fate-vp8-sign-bias$(1): CMD = framemd5 $(2) -i $(SAMPLES)/vp8/sintel-signbias.ivf -vsync 0
fate-vp8-sign-bias$(1): REF = $(SRC_PATH)/tests/ref/fate/vp8-sign-bias
endef

$(eval $(call FATE_VP8_FULL))
$(eval $(call FATE_VP8_FULL,-emu-edge,-flags emu_edge))
FATE_TESTS += $(FATE_VP8)
fate-vp8: $(FATE_VP8)
