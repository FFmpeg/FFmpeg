#include "tests.h"

#if ARCH_AARCH64

uint64_t selftest_get_cpu_flags_aarch64(void)
{
    return SELFTEST_CPU_FLAG_AARCH64;
}

DEF_NOOP_FUNC(clobber_x0);
DEF_NOOP_FUNC(clobber_x1);
DEF_NOOP_FUNC(clobber_x2);
DEF_NOOP_FUNC(clobber_x3);
DEF_NOOP_FUNC(clobber_x4);
DEF_NOOP_FUNC(clobber_x5);
DEF_NOOP_FUNC(clobber_x6);
DEF_NOOP_FUNC(clobber_x7);
DEF_NOOP_FUNC(clobber_x8);
DEF_NOOP_FUNC(clobber_x9);
DEF_NOOP_FUNC(clobber_x10);
DEF_NOOP_FUNC(clobber_x11);
DEF_NOOP_FUNC(clobber_x12);
DEF_NOOP_FUNC(clobber_x13);
DEF_NOOP_FUNC(clobber_x14);
DEF_NOOP_FUNC(clobber_x15);
DEF_NOOP_FUNC(clobber_x16);
DEF_NOOP_FUNC(clobber_x17);
// x18 skipped
DEF_NOOP_FUNC(clobber_x19);
DEF_NOOP_FUNC(clobber_x20);
DEF_NOOP_FUNC(clobber_x21);
DEF_NOOP_FUNC(clobber_x22);
DEF_NOOP_FUNC(clobber_x23);
DEF_NOOP_FUNC(clobber_x24);
DEF_NOOP_FUNC(clobber_x25);
DEF_NOOP_FUNC(clobber_x26);
DEF_NOOP_FUNC(clobber_x27);
DEF_NOOP_FUNC(clobber_x28);
DEF_NOOP_FUNC(clobber_x29);

DEF_NOOP_FUNC(clobber_d0);
DEF_NOOP_FUNC(clobber_d1);
DEF_NOOP_FUNC(clobber_d2);
DEF_NOOP_FUNC(clobber_d3);
DEF_NOOP_FUNC(clobber_d4);
DEF_NOOP_FUNC(clobber_d5);
DEF_NOOP_FUNC(clobber_d6);
DEF_NOOP_FUNC(clobber_d7);
DEF_NOOP_FUNC(clobber_d8);
DEF_NOOP_FUNC(clobber_d9);
DEF_NOOP_FUNC(clobber_d10);
DEF_NOOP_FUNC(clobber_d11);
DEF_NOOP_FUNC(clobber_d12);
DEF_NOOP_FUNC(clobber_d13);
DEF_NOOP_FUNC(clobber_d14);
DEF_NOOP_FUNC(clobber_d15);
DEF_NOOP_FUNC(clobber_d16);
DEF_NOOP_FUNC(clobber_d17);
DEF_NOOP_FUNC(clobber_d18);
DEF_NOOP_FUNC(clobber_d19);
DEF_NOOP_FUNC(clobber_d20);
DEF_NOOP_FUNC(clobber_d21);
DEF_NOOP_FUNC(clobber_d22);
DEF_NOOP_FUNC(clobber_d23);
DEF_NOOP_FUNC(clobber_d24);
DEF_NOOP_FUNC(clobber_d25);
DEF_NOOP_FUNC(clobber_d26);
DEF_NOOP_FUNC(clobber_d27);
DEF_NOOP_FUNC(clobber_d28);
DEF_NOOP_FUNC(clobber_d29);
DEF_NOOP_FUNC(clobber_d30);
DEF_NOOP_FUNC(clobber_d31);

DEF_NOOP_FUNC(clobber_v8_upper);
DEF_NOOP_FUNC(clobber_v9_upper);
DEF_NOOP_FUNC(clobber_v10_upper);
DEF_NOOP_FUNC(clobber_v11_upper);
DEF_NOOP_FUNC(clobber_v12_upper);
DEF_NOOP_FUNC(clobber_v13_upper);
DEF_NOOP_FUNC(clobber_v14_upper);
DEF_NOOP_FUNC(clobber_v15_upper);

DEF_NOOP_FUNC(sigill_aarch64);

// A function with 8 parameters in registers and 3 on the stack.
typedef void(many_args_func)(int, int, int, int, int, int, int, int, int, int, int);

#define DEF_MANY_ARGS_FUNC(NAME)                                                         \
    void selftest_##NAME(int, int, int, int, int, int, int, int, int, int, int)
#define DEF_MANY_ARGS_GETTER(FLAG, NAME) DEF_GETTER(FLAG, NAME, many_args_func, NULL)

DEF_MANY_ARGS_FUNC(clobber_stack_args_aarch64);
DEF_MANY_ARGS_FUNC(clobber_stack_aarch64);
DEF_MANY_ARGS_FUNC(check_clobber_upper_x0_aarch64);
DEF_MANY_ARGS_FUNC(check_clobber_upper_x1_aarch64);
DEF_MANY_ARGS_FUNC(check_clobber_upper_x2_aarch64);
DEF_MANY_ARGS_FUNC(check_clobber_upper_x3_aarch64);
DEF_MANY_ARGS_FUNC(check_clobber_upper_x4_aarch64);
DEF_MANY_ARGS_FUNC(check_clobber_upper_x5_aarch64);
DEF_MANY_ARGS_FUNC(check_clobber_upper_x6_aarch64);
DEF_MANY_ARGS_FUNC(check_clobber_upper_x7_aarch64);
DEF_MANY_ARGS_FUNC(check_clobber_upper_stack0_aarch64);
DEF_MANY_ARGS_FUNC(check_clobber_upper_stack1_aarch64);
DEF_MANY_ARGS_FUNC(check_clobber_upper_stack2_aarch64);

// A function with MAX_ARGS (15) arguments
typedef void(max_int_args_func)(int, int, int, int, int, int, int, int, int, int, int,
                                int, int, int, int);
typedef void(max_int64_args_func)(int64_t, int64_t, int64_t, int64_t, int64_t, int64_t,
                                  int64_t, int64_t, int64_t, int64_t, int64_t, int64_t,
                                  int64_t, int64_t, int64_t);

#define DEF_MAX_INT_ARGS_FUNC(NAME)                                                      \
    void selftest_##NAME(int, int, int, int, int, int, int, int, int, int, int, int,     \
                         int, int, int)
#define DEF_MAX_INT_ARGS_GETTER(FLAG, NAME)                                              \
    DEF_GETTER(FLAG, NAME, max_int_args_func, NULL)
#define DEF_MAX_INT64_ARGS_FUNC(NAME)                                                    \
    void selftest_##NAME(int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t,  \
                         int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t,  \
                         int64_t)
#define DEF_MAX_INT64_ARGS_GETTER(FLAG, NAME)                                            \
    DEF_GETTER(FLAG, NAME, max_int64_args_func, NULL)

DEF_MAX_INT_ARGS_FUNC(check_max_int_args_aarch64);
DEF_MAX_INT64_ARGS_FUNC(check_max_int64_args_aarch64);

static noop_func *get_clobber_x(int reg)
{
    if (!(checkasm_get_cpu_flags() & SELFTEST_CPU_FLAG_AARCH64))
        return NULL;

    switch (reg) {
    case 0:  return selftest_clobber_x0;
    case 1:  return selftest_clobber_x1;
    case 2:  return selftest_clobber_x2;
    case 3:  return selftest_clobber_x3;
    case 4:  return selftest_clobber_x4;
    case 5:  return selftest_clobber_x5;
    case 6:  return selftest_clobber_x6;
    case 7:  return selftest_clobber_x7;
    case 8:  return selftest_clobber_x8;
    case 9:  return selftest_clobber_x9;
    case 10: return selftest_clobber_x10;
    case 11: return selftest_clobber_x11;
    case 12: return selftest_clobber_x12;
    case 13: return selftest_clobber_x13;
    case 14: return selftest_clobber_x14;
    case 15: return selftest_clobber_x15;
    case 16: return selftest_clobber_x16;
    case 17: return selftest_clobber_x17;
    // x18 skipped
    case 19: return selftest_clobber_x19;
    case 20: return selftest_clobber_x20;
    case 21: return selftest_clobber_x21;
    case 22: return selftest_clobber_x22;
    case 23: return selftest_clobber_x23;
    case 24: return selftest_clobber_x24;
    case 25: return selftest_clobber_x25;
    case 26: return selftest_clobber_x26;
    case 27: return selftest_clobber_x27;
    case 28: return selftest_clobber_x28;
    case 29: return selftest_clobber_x29;
    default: return NULL;
    }
}

static noop_func *get_clobber_d(int reg)
{
    if (!(checkasm_get_cpu_flags() & SELFTEST_CPU_FLAG_AARCH64))
        return NULL;

    switch (reg) {
    case 0:  return selftest_clobber_d0;
    case 1:  return selftest_clobber_d1;
    case 2:  return selftest_clobber_d2;
    case 3:  return selftest_clobber_d3;
    case 4:  return selftest_clobber_d4;
    case 5:  return selftest_clobber_d5;
    case 6:  return selftest_clobber_d6;
    case 7:  return selftest_clobber_d7;
    case 8:  return selftest_clobber_d8;
    case 9:  return selftest_clobber_d9;
    case 10: return selftest_clobber_d10;
    case 11: return selftest_clobber_d11;
    case 12: return selftest_clobber_d12;
    case 13: return selftest_clobber_d13;
    case 14: return selftest_clobber_d14;
    case 15: return selftest_clobber_d15;
    case 16: return selftest_clobber_d16;
    case 17: return selftest_clobber_d17;
    case 18: return selftest_clobber_d18;
    case 19: return selftest_clobber_d19;
    case 20: return selftest_clobber_d20;
    case 21: return selftest_clobber_d21;
    case 22: return selftest_clobber_d22;
    case 23: return selftest_clobber_d23;
    case 24: return selftest_clobber_d24;
    case 25: return selftest_clobber_d25;
    case 26: return selftest_clobber_d26;
    case 27: return selftest_clobber_d27;
    case 28: return selftest_clobber_d28;
    case 29: return selftest_clobber_d29;
    case 30: return selftest_clobber_d30;
    case 31: return selftest_clobber_d31;
    default: return NULL;
    }
}

static noop_func *get_clobber_v_upper(int reg)
{
    if (!(checkasm_get_cpu_flags() & SELFTEST_CPU_FLAG_AARCH64))
        return NULL;

    switch (reg) {
    case 8:  return selftest_clobber_v8_upper;
    case 9:  return selftest_clobber_v9_upper;
    case 10: return selftest_clobber_v10_upper;
    case 11: return selftest_clobber_v11_upper;
    case 12: return selftest_clobber_v12_upper;
    case 13: return selftest_clobber_v13_upper;
    case 14: return selftest_clobber_v14_upper;
    case 15: return selftest_clobber_v15_upper;
    default: return NULL;
    }
}

static many_args_func *get_check_clobber_upper(int arg)
{
    if (!(checkasm_get_cpu_flags() & SELFTEST_CPU_FLAG_AARCH64))
        return NULL;

    switch (arg) {
    case 0:  return selftest_check_clobber_upper_x0_aarch64;
    case 1:  return selftest_check_clobber_upper_x1_aarch64;
    case 2:  return selftest_check_clobber_upper_x2_aarch64;
    case 3:  return selftest_check_clobber_upper_x3_aarch64;
    case 4:  return selftest_check_clobber_upper_x4_aarch64;
    case 5:  return selftest_check_clobber_upper_x5_aarch64;
    case 6:  return selftest_check_clobber_upper_x6_aarch64;
    case 7:  return selftest_check_clobber_upper_x7_aarch64;
    case 8:  return selftest_check_clobber_upper_stack0_aarch64;
    case 9:  return selftest_check_clobber_upper_stack1_aarch64;
    case 10: return selftest_check_clobber_upper_stack2_aarch64;
    default: return NULL;
    }
}

DEF_NOOP_GETTER(SELFTEST_CPU_FLAG_AARCH64, sigill_aarch64)
DEF_MANY_ARGS_GETTER(SELFTEST_CPU_FLAG_AARCH64, clobber_stack_args_aarch64)
DEF_MANY_ARGS_GETTER(SELFTEST_CPU_FLAG_AARCH64, clobber_stack_aarch64)

DEF_MAX_INT_ARGS_GETTER(SELFTEST_CPU_FLAG_AARCH64, check_max_int_args_aarch64)
DEF_MAX_INT64_ARGS_GETTER(SELFTEST_CPU_FLAG_AARCH64, check_max_int64_args_aarch64)

static void check_clobber_x(int from, int to)
{
    checkasm_declare(void, int);

    for (int reg = from; reg < to; reg++) {
        noop_func *clobber = get_clobber_x(reg);
        if (!clobber)
            break;

        if (checkasm_check_func(clobber, "clobber_x%d", reg)) {
            checkasm_call_new(0);
        }
    }

    checkasm_report("clobber_x");
}

static void check_clobber_d(int from, int to)
{
    checkasm_declare(void, int);

    for (int reg = from; reg < to; reg++) {
        noop_func *clobber = get_clobber_d(reg);
        if (!clobber)
            break;

        if (checkasm_check_func(clobber, "clobber_d%d", reg)) {
            checkasm_call_new(0);
        }
    }

    checkasm_report("clobber_d");
}

static void check_clobber_v_upper(int from, int to)
{
    checkasm_declare(void, int);

    for (int reg = from; reg < to; reg++) {
        noop_func *clobber = get_clobber_v_upper(reg);
        if (!clobber)
            break;

        if (checkasm_check_func(clobber, "clobber_v%d_upper", reg)) {
            checkasm_call_new(0);
        }
    }

    checkasm_report("clobber_v_upper");
}

static void check_clobber_arg_upper(void)
{
    checkasm_declare(void, int, int, int, int, int, int, int, int, int, int, int);

    for (int arg = 0; arg < 11; arg++) {
        many_args_func *check_clobber = get_check_clobber_upper(arg);
        if (!check_clobber)
            break;

        if (checkasm_check_func(check_clobber, "check_clobber_arg_upper_%d", arg)) {
            checkasm_call_new(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11);
        }
    }

    checkasm_report("check_clobber_arg_upper");
}

static void selftest_test_many_args(many_args_func fun, const char *name)
{
    checkasm_declare(void, int, int, int, int, int, int, int, int, int, int, int);

    if (checkasm_check_func(fun, "%s", name)) {
        /* don't call unchecked because that one is called without wrapping,
         * and we try to cloober the stack here. */
        (void) func_ref;
        checkasm_call_new(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11);
    }

    checkasm_report("%s", name);
}

static void selftest_test_max_int_args(max_int_args_func fun, const char *name)
{
    checkasm_declare(void, int, int, int, int, int, int, int, int, int, int, int, int,
                     int, int, int);

    if (checkasm_check_func(fun, "%s", name)) {
        (void) func_ref;
        checkasm_call_new(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    }

    checkasm_report("%s", name);
}

static void selftest_test_max_int64_args(max_int64_args_func fun, const char *name)
{
    checkasm_declare(void, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t,
                     int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t,
                     int64_t);

    if (checkasm_check_func(fun, "%s", name)) {
        (void) func_ref;
        checkasm_call_new(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    }

    checkasm_report("%s", name);
}

void selftest_check_aarch64(void)
{
    check_clobber_x(0, 18);
    // Don't try testing x18 - it has platform specific behaviour.
    check_clobber_d(0, 8);
    check_clobber_d(16, 32);
    check_clobber_v_upper(8, 16);
    selftest_test_many_args(get_clobber_stack_args_aarch64(), "clobber_stack_args");

    selftest_test_max_int_args(get_check_max_int_args_aarch64(), "check_max_int_args");
    selftest_test_max_int64_args(get_check_max_int64_args_aarch64(),
                                 "check_max_int64_args");
    check_clobber_arg_upper();

    if (!checkasm_should_fail(SELFTEST_CPU_FLAG_AARCH64))
        return;
    selftest_test_noop(get_sigill_aarch64(), "sigill");
    selftest_test_many_args(get_clobber_stack_aarch64(), "clobber_stack");
    check_clobber_x(19, 30);
    check_clobber_d(8, 16);
}

#endif
