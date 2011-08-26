FATE_TESTS += fate-adler32
fate-adler32: libavutil/adler32-test$(EXESUF)
fate-adler32: CMD = run libavutil/adler32-test
fate-adler32: REF = /dev/null

FATE_TESTS += fate-aes
fate-aes: libavutil/aes-test$(EXESUF)
fate-aes: CMD = run libavutil/aes-test
fate-aes: REF = /dev/null

FATE_TESTS += fate-base64
fate-base64: libavutil/base64-test$(EXESUF)
fate-base64: CMD = run libavutil/base64-test

FATE_TESTS += fate-crc
fate-crc: libavutil/crc-test$(EXESUF)
fate-crc: CMD = run libavutil/crc-test

FATE_TESTS += fate-des
fate-des: libavutil/des-test$(EXESUF)
fate-des: CMD = run libavutil/des-test
fate-des: REF = /dev/null

FATE_TESTS += fate-eval
fate-eval: libavutil/eval-test$(EXESUF)
fate-eval: CMD = run libavutil/eval-test

FATE_TESTS += fate-fifo
fate-fifo: libavutil/fifo-test$(EXESUF)
fate-fifo: CMD = run libavutil/fifo-test

FATE_TESTS += fate-md5
fate-md5: libavutil/md5-test$(EXESUF)
fate-md5: CMD = run libavutil/md5-test

FATE_TESTS += fate-sha
fate-sha: libavutil/sha-test$(EXESUF)
fate-sha: CMD = run libavutil/sha-test
