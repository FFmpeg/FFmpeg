FATE_MP3 += fate-mp3-float-conf-compl
fate-mp3-float-conf-compl: CMD = ffmpeg -auto_conversion_filters -c:a mp3float -i $(TARGET_SAMPLES)/mp3-conformance/compl.bit -f f32le -
fate-mp3-float-conf-compl: REF = $(SAMPLES)/mp3-conformance/compl.f32

FATE_MP3 += fate-mp3-float-conf-he_32khz
fate-mp3-float-conf-he_32khz: CMD = ffmpeg -auto_conversion_filters -c:a mp3float -i $(TARGET_SAMPLES)/mp3-conformance/he_32khz.bit -af atrim=end_sample=171648 -f f32le -
fate-mp3-float-conf-he_32khz: REF = $(SAMPLES)/mp3-conformance/he_32khz.f32

FATE_MP3 += fate-mp3-float-conf-he_44khz
fate-mp3-float-conf-he_44khz: CMD = ffmpeg -auto_conversion_filters -c:a mp3float -i $(TARGET_SAMPLES)/mp3-conformance/he_44khz.bit -af atrim=end_sample=471168 -f f32le -
fate-mp3-float-conf-he_44khz: REF = $(SAMPLES)/mp3-conformance/he_44khz.f32

FATE_MP3 += fate-mp3-float-conf-he_48khz
fate-mp3-float-conf-he_48khz: CMD = ffmpeg -auto_conversion_filters -c:a mp3float -i $(TARGET_SAMPLES)/mp3-conformance/he_48khz.bit -af atrim=end_sample=171648 -f f32le -
fate-mp3-float-conf-he_48khz: REF = $(SAMPLES)/mp3-conformance/he_48khz.f32

FATE_MP3 += fate-mp3-float-conf-hecommon
fate-mp3-float-conf-hecommon: CMD = ffmpeg -auto_conversion_filters -c:a mp3float -i $(TARGET_SAMPLES)/mp3-conformance/hecommon.bit -af atrim=end_sample=33408 -f f32le -
fate-mp3-float-conf-hecommon: REF = $(SAMPLES)/mp3-conformance/hecommon.f32

FATE_MP3 += fate-mp3-float-conf-si
fate-mp3-float-conf-si: CMD = ffmpeg -auto_conversion_filters -c:a mp3float -i $(TARGET_SAMPLES)/mp3-conformance/si.bit -af atrim=end_sample=134784 -f f32le -
fate-mp3-float-conf-si: REF = $(SAMPLES)/mp3-conformance/si.f32

FATE_MP3 += fate-mp3-float-conf-si_block
fate-mp3-float-conf-si_block: CMD = ffmpeg -auto_conversion_filters -c:a mp3float -i $(TARGET_SAMPLES)/mp3-conformance/si_block.bit -af atrim=end_sample=72576 -f f32le -
fate-mp3-float-conf-si_block: REF = $(SAMPLES)/mp3-conformance/si_block.f32

FATE_MP3 += fate-mp3-float-extra_overread
fate-mp3-float-extra_overread: CMD = ffmpeg -auto_conversion_filters -c:a mp3float -i $(TARGET_SAMPLES)/mpegaudio/extra_overread.mp3 -f f32le -
fate-mp3-float-extra_overread: REF = $(SAMPLES)/mpegaudio/extra_overread.f32

$(FATE_MP3): CMP = oneoff
$(FATE_MP3): CMP_UNIT = f32
$(FATE_MP3): FUZZ = 18

fate-mp3-float-extra_overread: FUZZ = 23

FATE_MP3-$(call DEMDEC, MP3, MP3FLOAT) += $(FATE_MP3)

FATE_SAMPLES_AVCONV += $(FATE_MP3-yes)
fate-mp3: $(FATE_MP3-yes)
