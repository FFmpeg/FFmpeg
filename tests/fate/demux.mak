FATE_DEMUX += fate-adts-demux
fate-adts-demux: CMD = crc -i $(SAMPLES)/aac/ct_faac-adts.aac -acodec copy

FATE_DEMUX += fate-aea-demux
fate-aea-demux: CMD = crc -i $(SAMPLES)/aea/chirp.aea -acodec copy

FATE_DEMUX += fate-bink-demux
fate-bink-demux: CMD = crc -i $(SAMPLES)/bink/Snd0a7d9b58.dee -vn -acodec copy

FATE_DEMUX += fate-bmv
fate-bmv: CMD = framecrc -i $(SAMPLES)/bmv/SURFING-partial.BMV -pix_fmt rgb24

FATE_DEMUX += fate-caf
fate-caf: CMD = crc -i $(SAMPLES)/caf/caf-pcm16.caf

FATE_DEMUX += fate-cryo-apc
fate-cryo-apc: CMD = md5 -i $(SAMPLES)/cryo-apc/cine007.APC -f s16le

FATE_DEMUX += fate-d-cinema-demux
fate-d-cinema-demux: CMD = framecrc -i $(SAMPLES)/d-cinema/THX_Science_FLT_1920-partial.302 -acodec copy -pix_fmt rgb24

FATE_DEMUX += fate-funcom-iss
fate-funcom-iss: CMD = md5 -i $(SAMPLES)/funcom-iss/0004010100.iss -f s16le

FATE_DEMUX += fate-interplay-mve-16bit
fate-interplay-mve-16bit: CMD = framecrc -i $(SAMPLES)/interplay-mve/descent3-level5-16bit-partial.mve -pix_fmt rgb24

FATE_DEMUX += fate-interplay-mve-8bit
fate-interplay-mve-8bit: CMD = framecrc -i $(SAMPLES)/interplay-mve/interplay-logo-2MB.mve -pix_fmt rgb24

FATE_DEMUX += fate-iv8-demux
fate-iv8-demux: CMD = framecrc -i $(SAMPLES)/iv8/zzz-partial.mpg -vsync 0 -vcodec copy

FATE_DEMUX += fate-lmlm4-demux
fate-lmlm4-demux: CMD = framecrc -i $(SAMPLES)/lmlm4/LMLM4_CIFat30fps.divx -t 3 -acodec copy -vcodec copy

FATE_DEMUX += fate-maxis-xa
fate-maxis-xa: CMD = md5 -i $(SAMPLES)/maxis-xa/SC2KBUG.XA -f s16le

FATE_DEMUX += fate-mtv
fate-mtv: CMD = framecrc -i $(SAMPLES)/mtv/comedian_auto-partial.mtv -acodec copy -pix_fmt rgb24

FATE_DEMUX += fate-mxf-demux
fate-mxf-demux: CMD = framecrc -i $(SAMPLES)/mxf/C0023S01.mxf -acodec copy -vcodec copy

FATE_DEMUX += fate-nc-demux
fate-nc-demux: CMD = framecrc -i $(SAMPLES)/nc-camera/nc-sample-partial -vcodec copy

FATE_DEMUX += fate-nsv-demux
fate-nsv-demux: CMD = framecrc -i $(SAMPLES)/nsv/witchblade-51kbps.nsv -t 6 -vcodec copy -acodec copy

FATE_DEMUX += fate-oma-demux
fate-oma-demux: CMD = crc -i $(SAMPLES)/oma/01-Untitled-partial.oma -acodec copy

FATE_DEMUX += fate-psx-str
fate-psx-str: CMD = framecrc -i $(SAMPLES)/psx-str/descent-partial.str

FATE_DEMUX += fate-psx-str-v3-mdec
fate-psx-str-v3-mdec: CMD = framecrc -i $(SAMPLES)/psx-str/abc000_cut.str -an

FATE_DEMUX += fate-pva-demux
fate-pva-demux: CMD = framecrc -idct simple -i $(SAMPLES)/pva/PVA_test-partial.pva -t 0.6 -acodec copy

FATE_DEMUX += fate-qcp-demux
fate-qcp-demux: CMD = crc -i $(SAMPLES)/qcp/0036580847.QCP -acodec copy

FATE_DEMUX += fate-redcode-demux
fate-redcode-demux: CMD = framecrc -i $(SAMPLES)/r3d/4MB-sample.r3d -vcodec copy -acodec copy

FATE_DEMUX += fate-sierra-vmd
fate-sierra-vmd: CMD = framecrc -i $(SAMPLES)/vmd/12.vmd -vsync 0 -pix_fmt rgb24

FATE_DEMUX += fate-siff
fate-siff: CMD = framecrc -i $(SAMPLES)/SIFF/INTRO_B.VB -t 3 -pix_fmt rgb24

FATE_DEMUX += fate-smjpeg
fate-smjpeg: CMD = framecrc -i $(SAMPLES)/smjpeg/scenwin.mjpg -vcodec copy

FATE_DEMUX += fate-westwood-aud
fate-westwood-aud: CMD = md5 -i $(SAMPLES)/westwood-aud/excellent.aud -f s16le

FATE_DEMUX += fate-wtv-demux
fate-wtv-demux: CMD = framecrc -i $(SAMPLES)/wtv/law-and-order-partial.wtv -vcodec copy -acodec copy

FATE_DEMUX += fate-xmv-demux
fate-xmv-demux: CMD = framecrc -i $(SAMPLES)/xmv/logos1p.fmv -vcodec copy -acodec copy

FATE_DEMUX += fate-xwma-demux
fate-xwma-demux: CMD = crc -i $(SAMPLES)/xwma/ergon.xwma -acodec copy

FATE_TESTS += $(FATE_DEMUX)
fate-demux: $(FATE_DEMUX)
