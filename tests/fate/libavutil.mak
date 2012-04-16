FATE_LIBAVUTIL += fate-adler32
fate-adler32: libavutil/adler32-test$(EXESUF)
fate-adler32: CMD = run libavutil/adler32-test
fate-adler32: REF = /dev/null

FATE_LIBAVUTIL += fate-aes
fate-aes: libavutil/aes-test$(EXESUF)
fate-aes: CMD = run libavutil/aes-test
fate-aes: REF = /dev/null

FATE_LIBAVUTIL += fate-base64
fate-base64: libavutil/base64-test$(EXESUF)
fate-base64: CMD = run libavutil/base64-test

FATE_LIBAVUTIL += fate-crc
fate-crc: libavutil/crc-test$(EXESUF)
fate-crc: CMD = run libavutil/crc-test

FATE_LIBAVUTIL += fate-des
fate-des: libavutil/des-test$(EXESUF)
fate-des: CMD = run libavutil/des-test
fate-des: REF = /dev/null

FATE_LIBAVUTIL += fate-eval
fate-eval: libavutil/eval-test$(EXESUF)
fate-eval: CMD = run libavutil/eval-test

FATE_LIBAVUTIL += fate-fifo
fate-fifo: libavutil/fifo-test$(EXESUF)
fate-fifo: CMD = run libavutil/fifo-test

FATE_LIBAVUTIL += fate-md5
fate-md5: libavutil/md5-test$(EXESUF)
fate-md5: CMD = run libavutil/md5-test

FATE_LIBAVUTIL += fate-parseutils
fate-parseutils: libavutil/parseutils-test$(EXESUF)
fate-parseutils: CMD = run libavutil/parseutils-test

FATE_LIBAVUTIL += fate-random_seed
fate-random_seed: libavutil/random_seed-test$(EXESUF)
fate-random_seed: CMD = run libavutil/random_seed-test

FATE_LIBAVUTIL += fate-sha
fate-sha: libavutil/sha-test$(EXESUF)
fate-sha: CMD = run libavutil/sha-test

fate-libavutil: $(FATE_LIBAVUTIL)
