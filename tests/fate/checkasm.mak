fate-checkasm: tests/checkasm/checkasm$(EXESUF)
fate-checkasm: CMD = run tests/checkasm/checkasm
fate-checkasm: REF = /dev/null

FATE += fate-checkasm
