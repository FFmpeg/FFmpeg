# A simple makefile for testing building the files in a foreign build
# system. This is not intended as the user facing way of building the library.

SRC_PATH = $(word 1, $(dir $(MAKEFILE_LIST)))
vpath %.c $(SRC_PATH)
vpath %.S $(SRC_PATH)
vpath %.asm $(SRC_PATH)

CPPFLAGS = -I$(SRC_PATH) -I$(SRC_PATH)include -I$(SRC_PATH)src -I$(SRC_PATH)tests
CFLAGS = -std=gnu11 -Wundef $(EXTRA_CFLAGS)
LIBS = -lm
EXE = checkasm-selftest
OBJS = \
	src/arm/checkasm_32.o \
	src/arm/checkasm_64.o \
	src/arm/cpu.o \
	src/loongarch/checkasm.o \
	src/riscv/callcheck.o \
	src/riscv/cpu.o \
	src/x86/cpu.o \
	src/perf/arm.o \
	src/perf/linux.o \
	src/perf/macos_kperf.o \
	src/checkasm.o \
	src/cpu.o \
	src/function.o \
	src/perf.o \
	src/signal.o \
	src/stackguard.o \
	src/stats.o \
	src/utils.o \
	tests/selftest.o \
	tests/generic.o \
	tests/arm/32/tests.o \
	tests/arm/32/tests_asm.o \
	tests/arm/64/tests.o \
	tests/arm/64/tests_asm.o \
	tests/riscv/tests.o \
	tests/riscv/tests_asm.o \
	tests/x86/tests.o
NASM_OBJS = \
	src/x86/checkasm.o \
	tests/x86/tests_asm.o

NASM_FMT ?=
ifdef NASM_FMT
OBJS += $(NASM_OBJS)
endif

$(EXE): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

%.o: %.asm
	nasm -f $(NASM_FMT) $(CPPFLAGS) -o $@ $<

OBJDIRS = $(sort $(dir $(OBJS)))

$(OBJDIRS):
	mkdir -p $@

$(OBJS): | $(OBJDIRS)

clean:
	$(RM) $(EXE) $(OBJS) $(NASM_OBJS)
