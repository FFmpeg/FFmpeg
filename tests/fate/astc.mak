# ASTC (Adaptive Scalable Texture Compression) codec + container tests.
#
# Input is the self-generated "testsrc" lavfi source. astcenc compression is
# fully deterministic, so encode -> container -> decode can be locked down with
# framecrc/md5 with no external sample files required.
#
# The codec is single-image, but the lavfi sources are *infinite*, so every
# test caps the encode to a single frame with -frames:v 1 (otherwise the encode
# step would never reach EOF and the test would hang).
#
# testsrc is native rgb24, so no pixel-format conversion is needed under
# FATE's -noauto_conversion_filters. For the HDR path the graph explicitly
# requests format=gbrapf32: swscale cannot convert *to* packed half/float
# (rgbaf16/rgbaf32) but it CAN produce planar float (gbrapf32), which the
# encoder also accepts as HDR input and auto-promotes to HDR_RGB_LDR_A.

# LDR round-trip through the raw .astc container.
FATE_ASTC-$(call TRANSCODE, LIBASTCENC, ASTC, LAVFI_INDEV WRAPPED_AVFRAME_DECODER TESTSRC_FILTER) += fate-astc-ldr
fate-astc-ldr: CMD = transcode lavfi "testsrc=size=128x128" astc \
    "-c:v libastcenc -block_size 8x8 -frames:v 1" "" "" "" "" ""

# LDR round-trip through the KTX 1.0 container (HDR output is not implemented
# by the KTX muxer, see the negative test below). The linear GL enum only fixes
# the colour space, so the decoder samples a linear texture with HDR precision
# by default; -dec_profile ldr pins this test to the 8-bit LDR path.
FATE_ASTC-$(call TRANSCODE, LIBASTCENC, KTX, LAVFI_INDEV WRAPPED_AVFRAME_DECODER TESTSRC_FILTER) += fate-astc-ktx
fate-astc-ktx: CMD = transcode lavfi "testsrc=size=128x128" ktx \
    "-c:v libastcenc -block_size 8x8 -profile:v ldr -frames:v 1 -srgb 0" "" "" "" "-dec_profile ldr" ""

# Check the KTX enum and single-level header fields directly rather than only
# through the FFmpeg KTX demuxer, which could mirror the muxer's mistake.
FATE_ASTC-$(call ENCMUX, LIBASTCENC, KTX, LAVFI_INDEV WRAPPED_AVFRAME_DECODER TESTSRC_FILTER FILE_PROTOCOL) += fate-astc-ktx-header
fate-astc-ktx-header: CMD = \
    cleanfiles="$${cleanfiles} $${outfile}.linear.ktx $${outfile}.srgb.ktx"; \
    linear="$$(target_path "$${outfile}.linear.ktx")"; \
    srgb="$$(target_path "$${outfile}.srgb.ktx")"; \
    read_le32() { \
        od -An -tx1 -j "$$2" -N 4 "$$1" | tr -d "[:space:]"; \
    }; \
    ffmpeg -f lavfi -i "testsrc=size=16x16" -frames:v 1 -c:v libastcenc -block_size 8x8 -profile:v ldr -srgb 0 \
        -f ktx -y "$$linear" && \
    ffmpeg -f lavfi -i "testsrc=size=16x16" -frames:v 1 -c:v libastcenc -block_size 8x8 -profile:v ldr-srgb -srgb 1 \
        -f ktx -y "$$srgb" && \
    printf "linear glInternalFormat: " && read_le32 "$${outfile}.linear.ktx" 28 && printf "\n" && \
    printf "linear faces: " && read_le32 "$${outfile}.linear.ktx" 52 && printf "\n" && \
    printf "linear mipmap levels: " && read_le32 "$${outfile}.linear.ktx" 56 && printf "\n" && \
    printf "srgb glInternalFormat: " && read_le32 "$${outfile}.srgb.ktx" 28 && printf "\n" && \
    printf "srgb faces: " && read_le32 "$${outfile}.srgb.ktx" 52 && printf "\n" && \
    printf "srgb mipmap levels: " && read_le32 "$${outfile}.srgb.ktx" 56 && printf "\n"

# The KTX linear GL enum fixes the colour space but not the endpoint format, so a
# linear texture may hold HDR endpoints and must be sampled with HDR precision by
# default: inferring LDR from the enum makes astcenc return its magenta
# profile-mismatch colour for the RGB=2.5 payload used here. Decoding without
# -dec_profile has to match an explicit -dec_profile hdr-ldr-a decode (which
# yields 2.5, 2.5, 2.5, 1.0) and differ from an explicit LDR decode.
#
# The KTX file is produced by remuxing the .astc stream: a raw .astc carries no
# profile, so the KTX muxer writes the linear format the test needs.
FATE_ASTC-$(call ENCMUX, LIBASTCENC, KTX, LAVFI_INDEV WRAPPED_AVFRAME_DECODER \
                                            TESTSRC2_FILTER FORMAT_FILTER SCALE_FILTER GEQ_FILTER \
                                            FILE_PROTOCOL RAWVIDEO_MUXER RAWVIDEO_ENCODER \
                                            LIBASTCENC_DECODER \
                                            ASTC_MUXER ASTC_DEMUXER KTX_DEMUXER) += fate-astc-ktx-linear-hdr
fate-astc-ktx-linear-hdr: CMD = \
    cleanfiles="$${cleanfiles} $${outfile}.astc $${outfile}.ktx $${outfile}.raw $${outfile}.hdr.raw $${outfile}.ldr.raw"; \
    enc="$$(target_path "$${outfile}.astc")"; \
    ktx="$$(target_path "$${outfile}.ktx")"; \
    ffmpeg -f lavfi -i "testsrc2=size=64x64,format=gbrapf32,geq=r=2.5:g=2.5:b=2.5:a=1" \
        -frames:v 1 -c:v libastcenc -block_size 8x8 -profile:v hdr-ldr-a \
        -f astc -y "$$enc" && \
    ffmpeg -i "$$enc" -c copy -srgb linear -f ktx -y "$$ktx" && \
    ffmpeg -i "$$ktx" -frames:v 1 -f rawvideo -y "$$(target_path "$${outfile}.raw")" && \
    ffmpeg -dec_profile hdr-ldr-a -i "$$ktx" -frames:v 1 -f rawvideo \
        -y "$$(target_path "$${outfile}.hdr.raw")" && \
    ffmpeg -dec_profile ldr -i "$$ktx" -frames:v 1 -f rawvideo \
        -y "$$(target_path "$${outfile}.ldr.raw")" && \
    printf "linear KTX default decode vs explicit hdr-ldr-a: " && \
    { cmp -s "$${outfile}.raw" "$${outfile}.hdr.raw" && echo "identical" || echo "DIFFERENT"; } && \
    printf "linear KTX default decode vs explicit ldr: " && \
    { cmp -s "$${outfile}.raw" "$${outfile}.ldr.raw" && echo "identical" || echo "different"; }

# HDR round-trip: float/half input auto-promotes the profile to
# HDR_RGB_LDR_A; the decoder receives the profile through -dec_profile and
# emits rgbaf16 (no 8-bit clamp). Verifies the HDR encode/decode chain end to end.
#
# Note: swscale cannot convert *to* packed half/float (rgbaf16/rgbaf32),
# but it CAN produce planar float (gbrapf32le), which the encoder also accepts
# as a valid HDR input. testsrc -> gbrapf32 exercises the float path.
#
# The decoded rgbaf16 frames are converted to the explicitly little-endian
# gbrapf32le before hashing, so the bytewise FATE output does not depend on
# the endianness of the host that runs the test. FATE disables automatic
# filter insertion, so the scaler is named explicitly. That conversion clamps
# to [0, 1] while this source's HDR decode overshoots above it, so the hash
# locks the bitstream, the output precision and the in-range values; the
# region above 1.0 is locked by fate-astc-hdr-native below.
FATE_ASTC-$(call TRANSCODE, LIBASTCENC, ASTC, LAVFI_INDEV WRAPPED_AVFRAME_DECODER TESTSRC_FILTER FORMAT_FILTER SCALE_FILTER) += fate-astc-hdr
fate-astc-hdr: CMD = transcode lavfi "testsrc=size=128x128,format=gbrapf32" astc \
    "-c:v libastcenc -block_size 8x8 -frames:v 1" "-vf scale,format=gbrapf32le" "" "" "-dec_profile hdr-ldr-a" ""

# The value hash above cannot see the HDR region: it hashes through a
# conversion that clamps to [0, 1], while this source decodes to values up to
# 4.0. Hash the decoder's native rgbaf16 output instead, on little-endian
# hosts, where native order is already defined. A big-endian host would hash
# the same samples in the other byte order, so the test is limited to the
# little-endian case rather than becoming endian dependent.
ifneq ($(HAVE_BIGENDIAN),yes)
FATE_ASTC-$(call TRANSCODE, LIBASTCENC, ASTC, LAVFI_INDEV WRAPPED_AVFRAME_DECODER TESTSRC_FILTER FORMAT_FILTER SCALE_FILTER) += fate-astc-hdr-native
fate-astc-hdr-native: CMD = transcode lavfi "testsrc=size=128x128,format=gbrapf32" astc \
    "-c:v libastcenc -block_size 8x8 -frames:v 1" "" "" "" "-dec_profile hdr-ldr-a" ""
endif

# HDR profile mismatch: ASTC bitstreams do not carry a profile field, so if
# the decoder is asked to decode HDR-encoded endpoints with the (default)
# LDR profile, astcenc's endpoint unpacking intentionally returns a fixed
# magenta error color (0xFF, 0x00, 0xFF) rather than garbage or a decode
# failure -- this is astcenc's own defined behavior for a profile mismatch,
# confirmed against the astcenc reference CLI decoding the same bitstream
# with the wrong profile flag. Locking down the magenta output as a
# regression test ensures our decoder keeps propagating this signal instead
# of silently misinterpreting HDR data as LDR.
#
# The source pixel value (2.5) is genuinely out of the LDR [0,1] range so
# the encoder is forced to use HDR endpoint formats; decoding those with
# the LDR profile is what triggers the magenta error path.
FATE_ASTC-$(call TRANSCODE, LIBASTCENC, ASTC, LAVFI_INDEV WRAPPED_AVFRAME_DECODER TESTSRC2_FILTER FORMAT_FILTER SCALE_FILTER GEQ_FILTER) += fate-astc-hdr-profile-mismatch
fate-astc-hdr-profile-mismatch: CMD = transcode lavfi \
    "testsrc2=size=64x64,format=gbrapf32,geq=r=2.5:g=2.5:b=2.5:a=1" astc \
    "-c:v libastcenc -block_size 8x8 -profile:v hdr-ldr-a -frames:v 1" "" "" "" "" ""

# 3D block round-trip: a 3D block size (4x4x4) compresses a 2D image using
# three-dimensional partition patterns.  The block_z value is carried in the
# .astc header and mirrored by the decoder.
FATE_ASTC-$(call TRANSCODE, LIBASTCENC, ASTC, LAVFI_INDEV WRAPPED_AVFRAME_DECODER TESTSRC_FILTER) += fate-astc-3d
fate-astc-3d:  CMD = transcode lavfi "testsrc=size=64x64" astc \
    "-c:v libastcenc -block_size 4x4x4 -frames:v 1" "" "" "" "" ""

# Negative test: writing HDR to KTX is not implemented, so feeding HDR
# (float/half) input to the .ktx muxer must be rejected with AVERROR(EINVAL)
# and the specific "Writing HDR ASTC to KTX 1.0 is not supported" message.
# The command asserts the non-zero exit status, and CMP = grep asserts the
# diagnostic.
FATE_ASTC-$(call ENCMUX, LIBASTCENC, KTX, LAVFI_INDEV WRAPPED_AVFRAME_DECODER TESTSRC_FILTER FORMAT_FILTER SCALE_FILTER FILE_PROTOCOL) += fate-astc-ktx-hdr-reject
fate-astc-ktx-hdr-reject: CMP = grep
fate-astc-ktx-hdr-reject: REF = Writing HDR ASTC to KTX 1.0 is not supported
fate-astc-ktx-hdr-reject: CMD = \
    cleanfiles="$${cleanfiles} $${outfile}.ktx"; \
    ffmpeg -y -f lavfi -i "testsrc=size=128x128,format=gbrapf32" \
    -threads 1 -c:v libastcenc -block_size 8x8 -frames:v 1 \
    -f ktx "$$(target_path "$${outfile}.ktx")" ; \
    test $$? -ne 0

FATE_ASTC += $(FATE_ASTC-yes)
fate-astc: $(FATE_ASTC-yes)

FATE_FFMPEG += $(FATE_ASTC)
