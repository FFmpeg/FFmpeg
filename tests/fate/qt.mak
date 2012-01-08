FATE_QT += fate-8bps
fate-8bps: CMD = framecrc -i $(SAMPLES)/8bps/full9iron-partial.mov -pix_fmt rgb24

FATE_QT += fate-qdm2
fate-qdm2: CMD = pcm -i $(SAMPLES)/qt-surge-suite/surge-2-16-B-QDM2.mov
fate-qdm2: CMP = oneoff
fate-qdm2: REF = $(SAMPLES)/qt-surge-suite/surge-2-16-B-QDM2.pcm
fate-qdm2: FUZZ = 2

FATE_QT += fate-qt-alaw-mono
fate-qt-alaw-mono: CMD = md5 -i $(SAMPLES)/qt-surge-suite/surge-1-16-B-alaw.mov -f s16le

FATE_QT += fate-qt-alaw-stereo
fate-qt-alaw-stereo: CMD = md5 -i $(SAMPLES)/qt-surge-suite/surge-2-16-B-alaw.mov -f s16le

FATE_QT += fate-qt-ima4-mono
fate-qt-ima4-mono: CMD = md5 -i $(SAMPLES)/qt-surge-suite/surge-1-16-B-ima4.mov -f s16le

FATE_QT += fate-qt-ima4-stereo
fate-qt-ima4-stereo: CMD = md5 -i $(SAMPLES)/qt-surge-suite/surge-2-16-B-ima4.mov -f s16le

FATE_QT += fate-qt-mac3-mono
fate-qt-mac3-mono: CMD = md5 -i $(SAMPLES)/qt-surge-suite/surge-1-8-MAC3.mov -f s16le

FATE_QT += fate-qt-mac3-stereo
fate-qt-mac3-stereo: CMD = md5 -i $(SAMPLES)/qt-surge-suite/surge-2-8-MAC3.mov -f s16le

FATE_QT += fate-qt-mac6-mono
fate-qt-mac6-mono: CMD = md5 -i $(SAMPLES)/qt-surge-suite/surge-1-8-MAC6.mov -f s16le

FATE_QT += fate-qt-mac6-stereo
fate-qt-mac6-stereo: CMD = md5 -i $(SAMPLES)/qt-surge-suite/surge-2-8-MAC6.mov -f s16le

FATE_QT += fate-qt-ulaw-mono
fate-qt-ulaw-mono: CMD = md5 -i $(SAMPLES)/qt-surge-suite/surge-1-16-B-ulaw.mov -f s16le

FATE_QT += fate-qt-ulaw-stereo
fate-qt-ulaw-stereo: CMD = md5 -i $(SAMPLES)/qt-surge-suite/surge-2-16-B-ulaw.mov -f s16le

FATE_QT += fate-quickdraw
fate-quickdraw: CMD = framecrc -i $(SAMPLES)/quickdraw/Airplane.mov -pix_fmt rgb24

FATE_QT += fate-rpza
fate-rpza: CMD = framecrc -i $(SAMPLES)/rpza/rpza2.mov -t 2 -pix_fmt rgb24

FATE_QT += fate-svq1
fate-svq1: CMD = framecrc -i $(SAMPLES)/svq1/marymary-shackles.mov -an -t 10

FATE_QT += fate-svq3
fate-svq3: CMD = framecrc -i $(SAMPLES)/svq3/Vertical400kbit.sorenson3.mov -t 6 -an

FATE_TESTS += $(FATE_QT)
fate-qt: $(FATE_QT)
