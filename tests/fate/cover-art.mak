FATE_COVER_ART += fate-cover-art-ape
fate-cover-art-ape: CMD = md5 -i $(TARGET_SAMPLES)/cover_art/luckynight_cover.ape -an -c:v copy -f rawvideo
fate-cover-art-ape: REF = 45333c983c45af54449dff10af144317

FATE_COVER_ART += fate-cover-art-flac
fate-cover-art-flac: CMD = md5 -i $(TARGET_SAMPLES)/cover_art/cover_art.flac -an -c:v copy -f rawvideo
fate-cover-art-flac: REF = 0de1fc6200596fa32b8f7300a14c0261

FATE_COVER_ART += fate-cover-art-m4a
fate-cover-art-m4a: CMD = md5 -i $(TARGET_SAMPLES)/cover_art/Owner-iTunes_9.0.3.15.m4a -an -c:v copy -f rawvideo
fate-cover-art-m4a: REF = 08ba70a3b594ff6345a93965e96a9d3e

FATE_COVER_ART += fate-cover-art-ogg
fate-cover-art-ogg: CMD = md5 -i $(TARGET_SAMPLES)/cover_art/ogg_vorbiscomment_cover.opus -an -c:v copy -f rawvideo
fate-cover-art-ogg: REF = b796d33363dbfed1868523b5c751b7b1

FATE_COVER_ART += fate-cover-art-wma
fate-cover-art-wma: CMD = md5 -i $(TARGET_SAMPLES)/cover_art/Californication_cover.wma -an -c:v copy -f rawvideo
fate-cover-art-wma: REF = 0808bd0e1b61542a16e1906812dd924b

FATE_COVER_ART += fate-cover-art-wma-id3
fate-cover-art-wma-id3: CMD = md5 -i $(TARGET_SAMPLES)/cover_art/wma_with_ID3_APIC_trimmed.wma -an -c:v copy -f rawvideo
fate-cover-art-wma-id3: REF = e6a8dd03687d5178bc13fc7d3316696e

FATE_COVER_ART += fate-cover-art-wma-metadatalib
fate-cover-art-wma-metadatalib: CMD = md5 -i $(TARGET_SAMPLES)/cover_art/wma_with_metadata_library_object_tag_trimmed.wma -map 0:v -c:v copy -f rawvideo
fate-cover-art-wma-metadatalib: REF = 32e8bd4fad546f63d881a0256f083aea

FATE_COVER_ART += fate-cover-art-wv
fate-cover-art-wv: CMD = md5 -i $(TARGET_SAMPLES)/cover_art/luckynight_cover.wv -an -c:v copy -f rawvideo
fate-cover-art-wv: REF = 45333c983c45af54449dff10af144317

$(FATE_COVER_ART): CMP = oneline
FATE_SAMPLES_AVCONV += $(FATE_COVER_ART)
fate-cover-art: $(FATE_COVER_ART)
