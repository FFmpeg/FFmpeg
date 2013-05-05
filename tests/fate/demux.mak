FATE_SAMPLES_DEMUX-$(call DEMDEC, AVI, FRAPS) += fate-avio-direct
fate-avio-direct: CMD = framecrc -avioflags direct -i $(SAMPLES)/fraps/fraps-v5-bouncing-balls-partial.avi -avioflags direct

FATE_SAMPLES_DEMUX-$(CONFIG_AAC_DEMUXER) += fate-adts-demux
fate-adts-demux: CMD = crc -i $(SAMPLES)/aac/ct_faac-adts.aac -acodec copy

FATE_SAMPLES_DEMUX-$(CONFIG_AEA_DEMUXER) += fate-aea-demux
fate-aea-demux: CMD = crc -i $(SAMPLES)/aea/chirp.aea -acodec copy

FATE_SAMPLES_DEMUX-$(CONFIG_AST_DEMUXER) += fate-ast
fate-ast: CMD = crc -i $(SAMPLES)/ast/demo11_02_partial.ast -c copy

FATE_SAMPLES_DEMUX-$(CONFIG_BINK_DEMUXER) += fate-bink-demux
fate-bink-demux: CMD = crc -i $(SAMPLES)/bink/Snd0a7d9b58.dee -vn -acodec copy

FATE_SAMPLES_DEMUX-$(CONFIG_BRSTM_DEMUXER) += fate-brstm
fate-brstm: CMD = crc -i $(SAMPLES)/brstm/lozswd_partial.brstm -acodec copy

FATE_SAMPLES_DEMUX-$(CONFIG_CAF_DEMUXER) += fate-caf
fate-caf: CMD = crc -i $(SAMPLES)/caf/caf-pcm16.caf -c copy

FATE_SAMPLES_DEMUX-$(CONFIG_CDXL_DEMUXER) += fate-cdxl-demux
fate-cdxl-demux: CMD = framecrc -i $(SAMPLES)/cdxl/mirage.cdxl -vcodec copy -acodec copy

FATE_SAMPLES_DEMUX-$(CONFIG_DAUD_DEMUXER) += fate-d-cinema-demux
fate-d-cinema-demux: CMD = framecrc -i $(SAMPLES)/d-cinema/THX_Science_FLT_1920-partial.302 -acodec copy

FATE_SAMPLES_DEMUX-$(CONFIG_GIF_DEMUXER) += fate-gif-demux
fate-gif-demux: CMD = framecrc -i $(SAMPLES)/gif/Newtons_cradle_animation_book_2.gif -vcodec copy

FATE_SAMPLES_DEMUX-$(CONFIG_IV8_DEMUXER) += fate-iv8-demux
fate-iv8-demux: CMD = framecrc -i $(SAMPLES)/iv8/zzz-partial.mpg -vcodec copy

FATE_SAMPLES_DEMUX-$(CONFIG_JV_DEMUXER) += fate-jv-demux
fate-jv-demux: CMD = framecrc -i $(SAMPLES)/jv/intro.jv -vcodec copy -acodec copy

FATE_SAMPLES_DEMUX-$(CONFIG_LMLM4_DEMUXER) += fate-lmlm4-demux
fate-lmlm4-demux: CMD = framecrc -i $(SAMPLES)/lmlm4/LMLM4_CIFat30fps.divx -t 3 -acodec copy -vcodec copy

FATE_SAMPLES_DEMUX-$(CONFIG_XA_DEMUXER) += fate-maxis-xa
fate-maxis-xa: CMD = framecrc -i $(SAMPLES)/maxis-xa/SC2KBUG.XA -frames:a 30 -c:a copy

FATE_SAMPLES_DEMUX-$(CONFIG_MATROSKA_DEMUXER) += fate-mkv
fate-mkv: CMD = framecrc -i $(SAMPLES)/mkv/test7_cut.mkv -c copy

FATE_SAMPLES_DEMUX-$(CONFIG_MTV_DEMUXER) += fate-mtv
fate-mtv: CMD = framecrc -i $(SAMPLES)/mtv/comedian_auto-partial.mtv -c copy

FATE_SAMPLES_DEMUX-$(CONFIG_MXF_DEMUXER) += fate-mxf-demux
fate-mxf-demux: CMD = framecrc -i $(SAMPLES)/mxf/C0023S01.mxf -acodec copy -vcodec copy

FATE_SAMPLES_DEMUX-$(CONFIG_NC_DEMUXER) += fate-nc-demux
fate-nc-demux: CMD = framecrc -i $(SAMPLES)/nc-camera/nc-sample-partial -vcodec copy

FATE_SAMPLES_DEMUX-$(CONFIG_NISTSPHERE_DEMUXER) += fate-nistsphere-demux
fate-nistsphere-demux: CMD = crc -i $(SAMPLES)/nistsphere/nist-ulaw.nist -acodec copy

FATE_SAMPLES_DEMUX-$(CONFIG_NSV_DEMUXER) += fate-nsv-demux
fate-nsv-demux: CMD = framecrc -i $(SAMPLES)/nsv/witchblade-51kbps.nsv -t 6 -vcodec copy -acodec copy

FATE_SAMPLES_DEMUX-$(CONFIG_OMA_DEMUXER) += fate-oma-demux
fate-oma-demux: CMD = crc -i $(SAMPLES)/oma/01-Untitled-partial.oma -acodec copy

FATE_SAMPLES_DEMUX-$(CONFIG_PAF_DEMUXER) += fate-paf-demux
fate-paf-demux: CMD = framecrc -i $(SAMPLES)/paf/hod1-partial.paf -vcodec copy -acodec copy

FATE_SAMPLES_DEMUX-$(CONFIG_PMP_DEMUXER) += fate-pmp-demux
fate-pmp-demux: CMD = framecrc -i $(SAMPLES)/pmp/demo.pmp -vn -c:a copy

FATE_SAMPLES_DEMUX-$(CONFIG_STR_DEMUXER) += fate-psx-str-demux
fate-psx-str-demux: CMD = framecrc -i $(SAMPLES)/psx-str/descent-partial.str -c copy

FATE_SAMPLES_DEMUX-$(CONFIG_PVA_DEMUXER) += fate-pva-demux
fate-pva-demux: CMD = framecrc -idct simple -i $(SAMPLES)/pva/PVA_test-partial.pva -t 0.6 -acodec copy

FATE_SAMPLES_DEMUX-$(CONFIG_QCP_DEMUXER) += fate-qcp-demux
fate-qcp-demux: CMD = crc -i $(SAMPLES)/qcp/0036580847.QCP -acodec copy

FATE_SAMPLES_DEMUX-$(CONFIG_R3D_DEMUXER) += fate-redcode-demux
fate-redcode-demux: CMD = framecrc -i $(SAMPLES)/r3d/4MB-sample.r3d -vcodec copy -acodec copy

FATE_SAMPLES_DEMUX-$(CONFIG_SIFF_DEMUXER) += fate-siff-demux
fate-siff-demux: CMD = framecrc -i $(SAMPLES)/SIFF/INTRO_B.VB -c copy

FATE_SAMPLES_DEMUX-$(CONFIG_SMJPEG_DEMUXER) += fate-smjpeg-demux
fate-smjpeg-demux: CMD = framecrc -i $(SAMPLES)/smjpeg/scenwin.mjpg -c copy

FATE_SAMPLES_DEMUX-$(CONFIG_WSAUD_DEMUXER) += fate-westwood-aud
fate-westwood-aud: CMD = framecrc -i $(SAMPLES)/westwood-aud/excellent.aud -c copy

FATE_SAMPLES_DEMUX-$(CONFIG_WTV_DEMUXER) += fate-wtv-demux
fate-wtv-demux: CMD = framecrc -i $(SAMPLES)/wtv/law-and-order-partial.wtv -vcodec copy -acodec copy

FATE_SAMPLES_DEMUX-$(CONFIG_XMV_DEMUXER) += fate-xmv-demux
fate-xmv-demux: CMD = framecrc -i $(SAMPLES)/xmv/logos1p.fmv -vcodec copy -acodec copy

FATE_SAMPLES_DEMUX-$(CONFIG_XWMA_DEMUXER) += fate-xwma-demux
fate-xwma-demux: CMD = crc -i $(SAMPLES)/xwma/ergon.xwma -acodec copy

FATE_SAMPLES_DEMUX += $(FATE_SAMPLES_DEMUX-yes)
FATE_SAMPLES_FFMPEG += $(FATE_SAMPLES_DEMUX)
fate-demux: $(FATE_SAMPLES_DEMUX)
