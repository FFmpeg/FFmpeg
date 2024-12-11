FATE_SAMPLES_DEMUX-$(call FRAMECRC, AVI, FRAPS) += fate-avio-direct
fate-avio-direct: CMD = framecrc -avioflags direct -i $(TARGET_SAMPLES)/fraps/fraps-v5-bouncing-balls-partial.avi -avioflags direct

FATE_SAMPLES_DEMUX-$(call FRAMECRC, AAC, AAC, AAC_PARSER CRC_MUXER) += fate-adts-demux fate-adts-id3v1-demux fate-adts-id3v2-demux fate-adts-id3v2-two-tags-demux
fate-adts-demux: CMD = crc -i $(TARGET_SAMPLES)/aac/ct_faac-adts.aac -c:a copy
fate-adts-id3v1-demux: CMD = framecrc -f aac -i $(TARGET_SAMPLES)/aac/id3v1.aac -c:a copy
fate-adts-id3v2-demux: CMD = framecrc -f aac -i $(TARGET_SAMPLES)/aac/id3v2.aac -c:a copy
fate-adts-id3v2-two-tags-demux: CMD = framecrc -i $(TARGET_SAMPLES)/aac/id3v2_two_tags.aac -c:a copy

FATE_SAMPLES_DEMUX-$(call FRAMECRC, AA,, SIPR_PARSER) += fate-aa-demux
fate-aa-demux: CMD = framecrc -i $(TARGET_SAMPLES)/aa/bush.aa -c:a copy

FATE_SAMPLES_DEMUX-$(call CRC, AEA) += fate-aea-demux
fate-aea-demux: CMD = crc -i $(TARGET_SAMPLES)/aea/chirp.aea -c:a copy

FATE_SAMPLES_DEMUX-$(call FRAMECRC, AV1, AV1, EXTRACT_EXTRADATA_BSF) += fate-av1-annexb-demux
fate-av1-annexb-demux: CMD = framecrc -c:v av1 -i $(TARGET_SAMPLES)/av1/annexb.obu -c:v copy

FATE_SAMPLES_DEMUX-$(call CRC, AST) += fate-ast
fate-ast: CMD = crc -i $(TARGET_SAMPLES)/ast/demo11_02_partial.ast -c copy

FATE_SAMPLES_DEMUX-$(call CRC, BINK) += fate-bink-demux
fate-bink-demux: CMD = crc -i $(TARGET_SAMPLES)/bink/Snd0a7d9b58.dee -vn -c:a copy

FATE_SAMPLES_DEMUX-$(call CRC, BFSTM) += fate-bfstm fate-bcstm
fate-bfstm: CMD = crc -i $(TARGET_SAMPLES)/bfstm/spl-forest-day.bfstm -c:a copy
fate-bcstm: CMD = crc -i $(TARGET_SAMPLES)/bfstm/loz-mm-mikau.bcstm -c:a copy

FATE_SAMPLES_DEMUX-$(call CRC, BRSTM) += fate-brstm
fate-brstm: CMD = crc -i $(TARGET_SAMPLES)/brstm/lozswd_partial.brstm -c:a copy

FATE_FFPROBE_DEMUX-$(call DEMDEC, CAVSVIDEO, CAVS, CAVSVIDEO_PARSER EXTRACT_EXTRADATA_BSF) += fate-cavs-demux
fate-cavs-demux: CMD = ffprobe_demux $(TARGET_SAMPLES)/cavs/bunny.mp4

FATE_SAMPLES_DEMUX-$(call FRAMECRC, CDXL) += fate-cdxl-demux
fate-cdxl-demux: CMD = framecrc -i $(TARGET_SAMPLES)/cdxl/mirage.cdxl -c:v copy -c:a copy

FATE_SAMPLES_DEMUX-$(call CRC, CINE) += fate-cine-demux
fate-cine-demux: CMD = crc -i $(TARGET_SAMPLES)/cine/bayer_gbrg8.cine -c copy

FATE_SAMPLES_DEMUX-$(call FRAMECRC, DAUD) += fate-d-cinema-demux
fate-d-cinema-demux: CMD = framecrc -i $(TARGET_SAMPLES)/d-cinema/THX_Science_FLT_1920-partial.302 -c:a copy

FATE_SAMPLES_DEMUX-$(call FRAMECRC, EA, VP6) += fate-d-eavp6-demux
fate-d-eavp6-demux: CMD = framecrc -i $(TARGET_SAMPLES)/ea-vp6/SmallRing.vp6 -map 0 -c:v copy

FATE_SAMPLES_DEMUX-$(call FRAMECRC, FITS GIF, FITS GIF, FITS_MUXER FITS_ENCODER GIF_PARSER SCALE_FILTER) += fate-fits-demux
fate-fits-demux: tests/data/fits-multi.fits
fate-fits-demux: CMD = framecrc -i $(TARGET_PATH)/tests/data/fits-multi.fits -c:v copy

FATE_FFPROBE_DEMUX-$(call DEMDEC, FLV, H264 AAC, H264_PARSER) += fate-flv-demux
fate-flv-demux: CMD = ffprobe_demux $(TARGET_SAMPLES)/flv/Enigma_Principles_of_Lust-part.flv

FATE_SAMPLES_DEMUX-$(call FRAMECRC, GIF,, GIF_PARSER) += fate-gif-demux
fate-gif-demux: CMD = framecrc -i $(TARGET_SAMPLES)/gif/Newtons_cradle_animation_book_2.gif -c:v copy

FATE_SAMPLES_DEMUX-$(call FRAMECRC, IFF) += fate-iff-demux
fate-iff-demux: CMD = framecrc -i $(TARGET_SAMPLES)/iff-anim/Hammer2.sndanim -c:v copy -c:a copy

FATE_SAMPLES_DEMUX-$(call FRAMECRC, IV8,, MPEG4VIDEO_PARSER EXTRACT_EXTRADATA_BSF) += fate-iv8-demux
fate-iv8-demux: CMD = framecrc -i $(TARGET_SAMPLES)/iv8/zzz-partial.mpg -c:v copy

FATE_SAMPLES_DEMUX-$(call FRAMECRC, JV) += fate-jv-demux
fate-jv-demux: CMD = framecrc -i $(TARGET_SAMPLES)/jv/intro.jv -c:v copy -c:a copy

FATE_SAMPLES_DEMUX-$(call FRAMECRC, LMLM4,, MPEG4VIDEO_PARSER MPEGAUDIO_PARSER EXTRACT_EXTRADATA_BSF) += fate-lmlm4-demux
fate-lmlm4-demux: CMD = framecrc -i $(TARGET_SAMPLES)/lmlm4/LMLM4_CIFat30fps.divx -t 3 -c:a copy -c:v copy

FATE_SAMPLES_DEMUX-$(call FRAMECRC, XA) += fate-maxis-xa
fate-maxis-xa: CMD = framecrc -i $(TARGET_SAMPLES)/maxis-xa/SC2KBUG.XA -frames:a 30 -c:a copy

FATE_SAMPLES_DEMUX-$(call FRAMECRC, MATROSKA, H264) += fate-mkv
fate-mkv: CMD = framecrc -i $(TARGET_SAMPLES)/mkv/test7_cut.mkv -c copy

#No dts errors or duplicate DTS should be in this
FATE_SAMPLES_DEMUX-$(call FRAMECRC, MATROSKA,, H264_PARSER) += fate-mkv-1242
fate-mkv-1242: CMD = framecrc -i $(TARGET_SAMPLES)/mkv/1242-small.mkv -c copy -frames:v 11

FATE_SAMPLES_DEMUX-$(call CRC, MLV) += fate-mlv-demux
fate-mlv-demux: CMD = crc -i $(TARGET_SAMPLES)/mlv/M19-0333-cut.MLV -c copy

FATE_SAMPLES_DEMUX-$(call FRAMECRC, MOV) += fate-mov-mp3-demux
fate-mov-mp3-demux: CMD = framecrc -i $(TARGET_SAMPLES)/mpegaudio/packed_maindata.mp3.mp4 -c copy

FATE_FFPROBE_DEMUX-$(call PARSERDEMDEC, OPUS, MPEGTS, OPUS) += fate-ts-opus-demux
fate-ts-opus-demux: CMD = ffprobe_demux $(TARGET_SAMPLES)/opus/test-8-7.1.opus-small.ts

FATE_FFPROBE_DEMUX-$(call PARSERDEMDEC, H264, MPEGTS, H264, EXTRACT_EXTRADATA_BSF) += fate-ts-small-demux
fate-ts-small-demux: CMD = ffprobe_demux $(TARGET_SAMPLES)/mpegts/h264small.ts

FATE_SAMPLES_DEMUX-$(call FRAMECRC, MTV,, MPEGAUDIO_PARSER) += fate-mtv
fate-mtv: CMD = framecrc -i $(TARGET_SAMPLES)/mtv/comedian_auto-partial.mtv -c copy

FATE_SAMPLES_DEMUX-$(call FRAMECRC, MXF,, MPEG4VIDEO_PARSER) += fate-mxf-demux
fate-mxf-demux: CMD = framecrc -i $(TARGET_SAMPLES)/mxf/C0023S01.mxf -c:a copy -c:v copy

FATE_SAMPLES_DEMUX-$(call FRAMECRC, NC,, MPEG4VIDEO_PARSER EXTRACT_EXTRADATA_BSF) += fate-nc-demux
fate-nc-demux: CMD = framecrc -i $(TARGET_SAMPLES)/nc-camera/nc-sample-partial -c:v copy

FATE_SAMPLES_DEMUX-$(call CRC, NISTSPHERE) += fate-nistsphere-demux
fate-nistsphere-demux: CMD = crc -i $(TARGET_SAMPLES)/nistsphere/nist-ulaw.nist -c:a copy

FATE_SAMPLES_DEMUX-$(call FRAMECRC, NSV,, VP3_PARSER MPEGAUDIO_PARSER) += fate-nsv-demux
fate-nsv-demux: CMD = framecrc -i $(TARGET_SAMPLES)/nsv/witchblade-51kbps.nsv -t 6 -c:v copy -c:a copy

FATE_FFPROBE_DEMUX-$(call DEMDEC, OGG, OPUS) += fate-oggopus-demux
fate-oggopus-demux: CMD = ffprobe_demux $(TARGET_SAMPLES)/ogg/intro-partial.opus

FATE_SAMPLES_DEMUX-$(call FRAMECRC, OGG, THEORA) += fate-oggtheora-demux
fate-oggtheora-demux: CMD = framecrc -i $(TARGET_SAMPLES)/ogg/empty_theora_packets.ogv -c:v copy

FATE_SAMPLES_DEMUX-$(call FRAMECRC, OGG,, VP8_PARSER) += fate-oggvp8-demux
fate-oggvp8-demux: CMD = framecrc -i $(TARGET_SAMPLES)/ogg/videotest.ogv -c:v copy

FATE_SAMPLES_DEMUX-$(call FRAMECRC, PAF) += fate-paf-demux
fate-paf-demux: CMD = framecrc -i $(TARGET_SAMPLES)/paf/hod1-partial.paf -c:v copy -c:a copy

FATE_SAMPLES_DEMUX-$(call FRAMECRC, PMP) += fate-pmp-demux
fate-pmp-demux: CMD = framecrc -i $(TARGET_SAMPLES)/pmp/demo.pmp -vn -c:a copy

FATE_SAMPLES_DEMUX-$(call CRC, RSD) += fate-rsd-demux
fate-rsd-demux: CMD = crc -i $(TARGET_SAMPLES)/rsd/hum01_partial.rsd -c:a copy

FATE_SAMPLES_DEMUX-$(call CRC, REDSPARK) += fate-redspark-demux
fate-redspark-demux: CMD = crc -i $(TARGET_SAMPLES)/redspark/jingle04_partial.rsd -c:a copy

FATE_SAMPLES_DEMUX-$(call FRAMECRC, STR) += fate-psx-str-demux
fate-psx-str-demux: CMD = framecrc -i $(TARGET_SAMPLES)/psx-str/descent-partial.str -c copy

FATE_SAMPLES_DEMUX-$(call FRAMECRC, PVA, MPEG2VIDEO, MPEGAUDIO_PARSER) += fate-pva-demux
fate-pva-demux: CMD = framecrc -idct simple -i $(TARGET_SAMPLES)/pva/PVA_test-partial.pva -t 0.6 -c:a copy

FATE_SAMPLES_DEMUX-$(call CRC, QCP) += fate-qcp-demux
fate-qcp-demux: CMD = crc -i $(TARGET_SAMPLES)/qcp/0036580847.QCP -c:a copy

FATE_SAMPLES_DEMUX-$(call FRAMECRC, R3D, JPEG2000 PCM_S32BE) += fate-redcode-demux
fate-redcode-demux: CMD = framecrc -i $(TARGET_SAMPLES)/r3d/4MB-sample.r3d -c:v copy -c:a copy

FATE_SAMPLES_DEMUX-$(call FRAMECRC, S337M,, DOLBY_E_PARSER) += fate-s337m-demux
fate-s337m-demux: CMD = framecrc -i $(TARGET_SAMPLES)/dolby_e/16-11 -c copy -ss 2 -t 1

FATE_SAMPLES_DEMUX-$(call FRAMECRC, SIFF) += fate-siff-demux
fate-siff-demux: CMD = framecrc -i $(TARGET_SAMPLES)/SIFF/INTRO_B.VB -c copy

FATE_SAMPLES_DEMUX-$(call FRAMECRC, SMJPEG, MJPEG ADPCM_IMA_SMJPEG) += fate-smjpeg-demux
fate-smjpeg-demux: CMD = framecrc -i $(TARGET_SAMPLES)/smjpeg/scenwin.mjpg -c copy

FATE_SAMPLES_DEMUX-$(call FRAMECRC, WAV SPDIF,, AC3_PARSER) += fate-wav-ac3
fate-wav-ac3: CMD = framecrc -i $(TARGET_SAMPLES)/ac3/diatonis_invisible_order_anfos_ac3-small.wav -c copy

FATE_SAMPLES_DEMUX-$(call FRAMECRC, WSAUD) += fate-westwood-aud
fate-westwood-aud: CMD = framecrc -i $(TARGET_SAMPLES)/westwood-aud/excellent.aud -c copy

FATE_SAMPLES_DEMUX-$(call FRAMECRC, WTV, MJPEG, MPEGVIDEO_PARSER MPEGAUDIO_PARSER EXTRACT_EXTRADATA_BSF) += fate-wtv-demux
fate-wtv-demux: CMD = framecrc -i $(TARGET_SAMPLES)/wtv/law-and-order-partial.wtv -c:v copy -c:a copy

FATE_SAMPLES_DEMUX-$(call FRAMECRC, XMV) += fate-xmv-demux
fate-xmv-demux: CMD = framecrc -i $(TARGET_SAMPLES)/xmv/logos1p.fmv -c:v copy -c:a copy

FATE_SAMPLES_DEMUX-$(call CRC, XWMA) += fate-xwma-demux
fate-xwma-demux: CMD = crc -i $(TARGET_SAMPLES)/xwma/ergon.xwma -c:a copy

FATE_FFPROBE_DEMUX-$(call PARSERDEMDEC, MPEGVIDEO, MPEGTS, MPEG2VIDEO AC3, EXTRACT_EXTRADATA_BSF) += fate-ts-demux
fate-ts-demux: CMD = ffprobe_demux $(TARGET_SAMPLES)/ac3/mp3ac325-4864-small.ts

FATE_FFPROBE_DEMUX-$(CONFIG_MPEGTS_DEMUXER) += fate-ts-timed-id3-demux
fate-ts-timed-id3-demux: CMD = ffprobe_demux $(TARGET_SAMPLES)/mpegts/id3.ts

FATE_SAMPLES_DEMUX += $(FATE_SAMPLES_DEMUX-yes)
FATE_SAMPLES_FFMPEG += $(FATE_SAMPLES_DEMUX)
FATE_FFPROBE_DEMUX   += $(FATE_FFPROBE_DEMUX-yes)
FATE_SAMPLES_FFPROBE += $(FATE_FFPROBE_DEMUX)
fate-demux: $(FATE_SAMPLES_DEMUX) $(FATE_FFPROBE_DEMUX)
