FATE_LIBAVUTIL += fate-adler32
fate-adler32: libavutil/tests/adler32$(EXESUF)
fate-adler32: CMD = run libavutil/tests/adler32
fate-adler32: REF = /dev/null

FATE_LIBAVUTIL += fate-aes
fate-aes: libavutil/tests/aes$(EXESUF)
fate-aes: CMD = run libavutil/tests/aes
fate-aes: REF = /dev/null

FATE_LIBAVUTIL += fate-atomic
fate-atomic: libavutil/tests/atomic$(EXESUF)
fate-atomic: CMD = run libavutil/tests/atomic
fate-atomic: REF = /dev/null

FATE_LIBAVUTIL += fate-avstring
fate-avstring: libavutil/tests/avstring$(EXESUF)
fate-avstring: CMD = run libavutil/tests/avstring

FATE_LIBAVUTIL += fate-base64
fate-base64: libavutil/tests/base64$(EXESUF)
fate-base64: CMD = run libavutil/tests/base64

FATE_LIBAVUTIL += fate-blowfish
fate-blowfish: libavutil/tests/blowfish$(EXESUF)
fate-blowfish: CMD = run libavutil/tests/blowfish

FATE_LIBAVUTIL += fate-cpu
fate-cpu: libavutil/tests/cpu$(EXESUF)
fate-cpu: CMD = run libavutil/tests/cpu $(CPUFLAGS:%=-c%) $(THREADS:%=-t%)
fate-cpu: REF = /dev/null

FATE_LIBAVUTIL += fate-crc
fate-crc: libavutil/tests/crc$(EXESUF)
fate-crc: CMD = run libavutil/tests/crc

FATE_LIBAVUTIL += fate-des
fate-des: libavutil/tests/des$(EXESUF)
fate-des: CMD = run libavutil/tests/des
fate-des: REF = /dev/null

FATE_LIBAVUTIL += fate-eval
fate-eval: libavutil/tests/eval$(EXESUF)
fate-eval: CMD = run libavutil/tests/eval

FATE_LIBAVUTIL += fate-fifo
fate-fifo: libavutil/tests/fifo$(EXESUF)
fate-fifo: CMD = run libavutil/tests/fifo

FATE_LIBAVUTIL += fate-float-dsp
fate-float-dsp: libavutil/tests/float_dsp$(EXESUF)
fate-float-dsp: CMD = run libavutil/tests/float_dsp
fate-float-dsp: CMP = null
fate-float-dsp: REF = /dev/null

FATE_LIBAVUTIL += fate-hmac
fate-hmac: libavutil/tests/hmac$(EXESUF)
fate-hmac: CMD = run libavutil/tests/hmac

FATE_LIBAVUTIL += fate-md5
fate-md5: libavutil/tests/md5$(EXESUF)
fate-md5: CMD = run libavutil/tests/md5

FATE_LIBAVUTIL += fate-parseutils
fate-parseutils: libavutil/tests/parseutils$(EXESUF)
fate-parseutils: CMD = run libavutil/tests/parseutils

FATE_LIBAVUTIL += fate-sha
fate-sha: libavutil/tests/sha$(EXESUF)
fate-sha: CMD = run libavutil/tests/sha

FATE_LIBAVUTIL += fate-tree
fate-tree: libavutil/tests/tree$(EXESUF)
fate-tree: CMD = run libavutil/tests/tree
fate-tree: REF = /dev/null

FATE_LIBAVUTIL += fate-xtea
fate-xtea: libavutil/tests/xtea$(EXESUF)
fate-xtea: CMD = run libavutil/tests/xtea

FATE-$(CONFIG_AVUTIL) += $(FATE_LIBAVUTIL)
fate-libavutil: $(FATE_LIBAVUTIL)
