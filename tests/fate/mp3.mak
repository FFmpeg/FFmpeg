FATE_MP3 += fate-mp3-float-conf-compl
fate-mp3-float-conf-compl: CMD = pcm -acodec mp3float -i $(SAMPLES)/mp3-conformance/compl.bit
fate-mp3-float-conf-compl: REF = $(SAMPLES)/mp3-conformance/compl.pcm

FATE_MP3 += fate-mp3-float-conf-he_32khz
fate-mp3-float-conf-he_32khz: CMD = pcm -acodec mp3float -i $(SAMPLES)/mp3-conformance/he_32khz.bit -fs 343296
fate-mp3-float-conf-he_32khz: REF = $(SAMPLES)/mp3-conformance/he_32khz.pcm

FATE_MP3 += fate-mp3-float-conf-he_44khz
fate-mp3-float-conf-he_44khz: CMD = pcm -acodec mp3float -i $(SAMPLES)/mp3-conformance/he_44khz.bit -fs 942336
fate-mp3-float-conf-he_44khz: REF = $(SAMPLES)/mp3-conformance/he_44khz.pcm

FATE_MP3 += fate-mp3-float-conf-he_48khz
fate-mp3-float-conf-he_48khz: CMD = pcm -acodec mp3float -i $(SAMPLES)/mp3-conformance/he_48khz.bit -fs 343296
fate-mp3-float-conf-he_48khz: REF = $(SAMPLES)/mp3-conformance/he_48khz.pcm

FATE_MP3 += fate-mp3-float-conf-hecommon
fate-mp3-float-conf-hecommon: CMD = pcm -acodec mp3float -i $(SAMPLES)/mp3-conformance/hecommon.bit -fs 133632
fate-mp3-float-conf-hecommon: REF = $(SAMPLES)/mp3-conformance/hecommon.pcm

FATE_MP3 += fate-mp3-float-conf-si
fate-mp3-float-conf-si: CMD = pcm -acodec mp3float -i $(SAMPLES)/mp3-conformance/si.bit -fs 269568
fate-mp3-float-conf-si: REF = $(SAMPLES)/mp3-conformance/si.pcm

FATE_MP3 += fate-mp3-float-conf-si_block
fate-mp3-float-conf-si_block: CMD = pcm -acodec mp3float -i $(SAMPLES)/mp3-conformance/si_block.bit -fs 145152
fate-mp3-float-conf-si_block: REF = $(SAMPLES)/mp3-conformance/si_block.pcm

FATE_MP3 += fate-mp3-float-extra_overread
fate-mp3-float-extra_overread: CMD = pcm -c:a mp3float -i $(SAMPLES)/mpegaudio/extra_overread.mp3
fate-mp3-float-extra_overread: REF = $(SAMPLES)/mpegaudio/extra_overread.pcm

$(FATE_MP3): CMP = stddev
$(FATE_MP3): FUZZ = 0.07

ifdef HAVE_NEON
fate-mp3-float-conf-hecommon: FUZZ = 0.70
endif

FATE_MP3-$(call DEMDEC, MP3, MP3FLOAT) += $(FATE_MP3)

FATE_SAMPLES_AVCONV += $(FATE_MP3-yes)
fate-mp3: $(FATE_MP3-yes)
