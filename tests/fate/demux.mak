FATE_SAMPLES_AVCONV-$(CONFIG_AAC_DEMUXER) += fate-adts-demux
fate-adts-demux: CMD = crc -i $(SAMPLES)/aac/ct_faac-adts.aac -acodec copy

FATE_SAMPLES_AVCONV-$(CONFIG_AEA_DEMUXER) += fate-aea-demux
fate-aea-demux: CMD = crc -i $(SAMPLES)/aea/chirp.aea -acodec copy

FATE_SAMPLES_AVCONV-$(CONFIG_BINK_DEMUXER) += fate-bink-demux
fate-bink-demux: CMD = crc -i $(SAMPLES)/bink/Snd0a7d9b58.dee -vn -acodec copy

FATE_SAMPLES_AVCONV-$(CONFIG_CAF_DEMUXER) += fate-caf
fate-caf: CMD = crc -i $(SAMPLES)/caf/caf-pcm16.caf -c copy

FATE_SAMPLES_AVCONV-$(CONFIG_CDXL_DEMUXER) += fate-cdxl-demux
fate-cdxl-demux: CMD = framecrc -i $(SAMPLES)/cdxl/mirage.cdxl -vcodec copy -acodec copy

FATE_SAMPLES_AVCONV-$(CONFIG_DAUD_DEMUXER) += fate-d-cinema-demux
fate-d-cinema-demux: CMD = framecrc -i $(SAMPLES)/d-cinema/THX_Science_FLT_1920-partial.302 -acodec copy

FATE_SAMPLES_AVCONV-$(CONFIG_IV8_DEMUXER) += fate-iv8-demux
fate-iv8-demux: CMD = framecrc -i $(SAMPLES)/iv8/zzz-partial.mpg -vcodec copy

FATE_SAMPLES_AVCONV-$(CONFIG_LMLM4_DEMUXER) += fate-lmlm4-demux
fate-lmlm4-demux: CMD = framecrc -i $(SAMPLES)/lmlm4/LMLM4_CIFat30fps.divx -t 3 -acodec copy -vcodec copy

FATE_SAMPLES_AVCONV-$(CONFIG_XA_DEMUXER) += fate-maxis-xa
fate-maxis-xa: CMD = framecrc -i $(SAMPLES)/maxis-xa/SC2KBUG.XA -frames:a 30 -c:a copy

FATE_SAMPLES_AVCONV-$(CONFIG_MTV_DEMUXER) += fate-mtv
fate-mtv: CMD = framecrc -i $(SAMPLES)/mtv/comedian_auto-partial.mtv -c copy

FATE_SAMPLES_AVCONV-$(CONFIG_MXF_DEMUXER) += fate-mxf-demux
fate-mxf-demux: CMD = framecrc -i $(SAMPLES)/mxf/C0023S01.mxf -acodec copy -vcodec copy

FATE_SAMPLES_AVCONV-$(CONFIG_NC_DEMUXER) += fate-nc-demux
fate-nc-demux: CMD = framecrc -i $(SAMPLES)/nc-camera/nc-sample-partial -vcodec copy

FATE_SAMPLES_AVCONV-$(CONFIG_NSV_DEMUXER) += fate-nsv-demux
fate-nsv-demux: CMD = framecrc -i $(SAMPLES)/nsv/witchblade-51kbps.nsv -t 6 -vcodec copy -acodec copy

FATE_SAMPLES_AVCONV-$(CONFIG_OMA_DEMUXER) += fate-oma-demux
fate-oma-demux: CMD = crc -i $(SAMPLES)/oma/01-Untitled-partial.oma -acodec copy

FATE_SAMPLES_AVCONV-$(CONFIG_STR_DEMUXER) += fate-psx-str-demux
fate-psx-str-demux: CMD = framecrc -i $(SAMPLES)/psx-str/descent-partial.str -c copy

FATE_SAMPLES_AVCONV-$(CONFIG_PVA_DEMUXER) += fate-pva-demux
fate-pva-demux: CMD = framecrc -idct simple -i $(SAMPLES)/pva/PVA_test-partial.pva -t 0.6 -acodec copy -vn

FATE_SAMPLES_AVCONV-$(CONFIG_QCP_DEMUXER) += fate-qcp-demux
fate-qcp-demux: CMD = crc -i $(SAMPLES)/qcp/0036580847.QCP -acodec copy

FATE_SAMPLES_AVCONV-$(CONFIG_R3D_DEMUXER) += fate-redcode-demux
fate-redcode-demux: CMD = framecrc -i $(SAMPLES)/r3d/4MB-sample.r3d -vcodec copy -acodec copy

FATE_SAMPLES_AVCONV-$(CONFIG_SIFF_DEMUXER) += fate-siff-demux
fate-siff-demux: CMD = framecrc -i $(SAMPLES)/SIFF/INTRO_B.VB -c copy

FATE_SAMPLES_AVCONV-$(CONFIG_SMJPEG_DEMUXER) += fate-smjpeg-demux
fate-smjpeg-demux: CMD = framecrc -i $(SAMPLES)/smjpeg/scenwin.mjpg -c copy

FATE_SAMPLES_AVCONV-$(CONFIG_WSAUD_DEMUXER) += fate-westwood-aud
fate-westwood-aud: CMD = framecrc -i $(SAMPLES)/westwood-aud/excellent.aud -c copy

FATE_SAMPLES_AVCONV-$(CONFIG_WTV_DEMUXER) += fate-wtv-demux
fate-wtv-demux: CMD = framecrc -i $(SAMPLES)/wtv/law-and-order-partial.wtv -vcodec copy -acodec copy

FATE_SAMPLES_AVCONV-$(CONFIG_XMV_DEMUXER) += fate-xmv-demux
fate-xmv-demux: CMD = framecrc -i $(SAMPLES)/xmv/logos1p.fmv -vcodec copy -acodec copy

FATE_SAMPLES_AVCONV-$(CONFIG_XWMA_DEMUXER) += fate-xwma-demux
fate-xwma-demux: CMD = crc -i $(SAMPLES)/xwma/ergon.xwma -acodec copy
