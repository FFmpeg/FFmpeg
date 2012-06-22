FATE_COVER_ART += fate-cover-art-m4a
fate-cover-art-m4a: CMD = md5 -i $(SAMPLES)/cover_art/Owner-iTunes_9.0.3.15.m4a -an -c:v copy -f rawvideo
fate-cover-art-m4a: REF = 08ba70a3b594ff6345a93965e96a9d3e

$(FATE_COVER_ART): CMP = oneline
FATE_SAMPLES_AVCONV += $(FATE_COVER_ART)
fate-cover-art: $(FATE_COVER_ART)
