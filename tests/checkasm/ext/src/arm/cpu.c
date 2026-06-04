/*
 * Copyright © 2025, Martin Storsjo
 * Copyright © 2018, VideoLAN and dav1d authors
 * Copyright © 2018, Two Orioles, LLC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdint.h>

#include "checkasm_config.h"
#include "cpu.h"
#include "internal.h"

#ifdef _WIN32
  #include <windows.h>
#endif
#include <ctype.h>
#include <string.h>

#if ARCH_AARCH64

  #ifdef _WIN32
    #include <windows.h>
  #elif defined(__APPLE__)
    #include <sys/sysctl.h>

static int have_feature(const char *feature)
{
    int    supported = 0;
    size_t size      = sizeof(supported);
    if (sysctlbyname(feature, &supported, &size, NULL, 0) != 0) {
        return 0;
    }
    return supported;
}
  #elif HAVE_GETAUXVAL || HAVE_ELF_AUX_INFO

    #include <sys/auxv.h>

    #define HWCAP_AARCH64_SVE  (1 << 22)
    #define HWCAP2_AARCH64_SME (1 << 23)

  #endif

int checkasm_has_sve(void)
{
  #ifdef _WIN32
    #ifdef PF_ARM_SVE_INSTRUCTIONS_AVAILABLE
    return IsProcessorFeaturePresent(PF_ARM_SVE_INSTRUCTIONS_AVAILABLE);
    #else
    return 0;
    #endif
  #elif defined(__APPLE__)
    /* No sysctlbyname feature defined for SVE yet */
    return 0;
  #elif HAVE_GETAUXVAL || HAVE_ELF_AUX_INFO
    return checkasm_getauxval(AT_HWCAP) & HWCAP_AARCH64_SVE;
  #else
    return 0;
  #endif
}

int checkasm_has_sme(void)
{
  #ifdef _WIN32
    #ifdef PF_ARM_SME_INSTRUCTIONS_AVAILABLE
    return IsProcessorFeaturePresent(PF_ARM_SME_INSTRUCTIONS_AVAILABLE);
    #else
    return 0;
    #endif
  #elif defined(__APPLE__)
    return have_feature("hw.optional.arm.FEAT_SME");
  #elif HAVE_GETAUXVAL || HAVE_ELF_AUX_INFO
    return checkasm_getauxval(AT_HWCAP2) & HWCAP2_AARCH64_SME;
  #else
    return 0;
  #endif
}

#elif ARCH_ARM

  #ifdef _WIN32
    #include <windows.h>
  #elif HAVE_GETAUXVAL || HAVE_ELF_AUX_INFO
    #include <sys/auxv.h>

    #ifndef HWCAP_ARM_VFP
      #define HWCAP_ARM_VFP (1 << 6)
    #endif
    #ifndef HWCAP_ARM_NEON
      #define HWCAP_ARM_NEON (1 << 12)
    #endif
    #ifndef HWCAP_ARM_VFPD32
      #define HWCAP_ARM_VFPD32 (1 << 19)
    #endif

  #endif

static checkasm_checked_call_func checked_call_ptr;

checkasm_checked_call_func checkasm_checked_call_ptr(void)
{
    return checked_call_ptr;
}

COLD void checkasm_init_arm(void)
{
    if (checkasm_has_vfp())
        checked_call_ptr = checkasm_checked_call_vfp;
    else
        checked_call_ptr = checkasm_checked_call_novfp;
}

int checkasm_has_vfpd32(void)
{
  #ifdef _WIN32
    #ifdef PF_ARM_VFP_32_REGISTERS_AVAILABLE
    return IsProcessorFeaturePresent(PF_ARM_VFP_32_REGISTERS_AVAILABLE);
    #else
    return 0;
    #endif
  #elif (defined(__APPLE__) && __ARM_ARCH >= 7)
    return 1;
  #elif HAVE_GETAUXVAL || HAVE_ELF_AUX_INFO
    /* HWCAP_ARM_VFPD32 was introduced in Linux 3.7, alternatively check for NEON as a fallback */
    return checkasm_getauxval(AT_HWCAP) & (HWCAP_ARM_NEON | HWCAP_ARM_VFPD32);
  #else
    return 0;
  #endif
}

int checkasm_has_vfp(void)
{
  #if defined(__APPLE__) || defined(_WIN32)
    return 1;
  #elif HAVE_GETAUXVAL || HAVE_ELF_AUX_INFO
    return checkasm_getauxval(AT_HWCAP) & HWCAP_ARM_VFP;
  #else
    return 0;
  #endif
}

#endif // ARCH_ARM

#if (ARCH_ARM || ARCH_AARCH64) && (defined(__linux__) || defined(_WIN32))
struct arm_core {
    unsigned    id;
    const char *name;
};

struct arm_implementer {
    unsigned               id;
    const struct arm_core *parts;
    const char            *name;
};

  #include "cores.h"

struct arm_core_id {
    unsigned implementer;
    unsigned part;
};

COLD static void find_implementer_part(const struct arm_core_id *core,
                                       const char **implementer, const char **part)
{
    for (int i = 0; arm_implementers[i].name; i++) {
        if (arm_implementers[i].id == core->implementer) {
            *implementer                 = arm_implementers[i].name;
            const struct arm_core *cores = arm_implementers[i].parts;
            for (int j = 0; cores[j].name; j++) {
                if (cores[j].id == core->part) {
                    *part = cores[j].name;
                    return;
                }
            }
            return;
        }
    }
}

COLD static size_t print_cores(char *buf, size_t buflen, const struct arm_core_id *cores,
                               unsigned nb_cores)
{
    size_t pos = 0;
    for (unsigned i = 0; i < nb_cores; i++) {
        if (pos >= buflen)
            break;
        char buf1[20], buf2[20];
        snprintf(buf1, sizeof(buf1), "Implementer 0x%02x", cores[i].implementer);
        snprintf(buf2, sizeof(buf2), "Part 0x%03x", cores[i].part);
        const char *implementer = buf1, *part = buf2;
        find_implementer_part(&cores[i], &implementer, &part);
        pos += snprintf(buf + pos, buflen - pos, "%s%s %s", i > 0 ? ", " : "",
                        implementer, part);
    }
    return pos;
}

  #ifdef __linux__
COLD const char *checkasm_get_arm_cpuinfo(char *buf, size_t buflen, int affinity)
{
    FILE *f = fopen("/proc/cpuinfo", "r");
    if (!f)
        return NULL;

    int                implementer = -1, part = -1, processor = -1;
    char               model[100] = "";
    struct arm_core_id cores[5];
    unsigned           nb_cores = 0;

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        /* Trim out the trailing \n that fgets includes */
        if (len > 0 && line[len - 1] == '\n')
            line[--len] = '\0';
        else
            continue;

        char *ptr;
        if ((ptr = strchr(line, ':')) != NULL) {
            /* Interpret a line like "processor    : 0". Split the
             * string at the ':' character. */
            *ptr              = '\0';
            const char *value = ptr + 1;
            /* Skip past leading space after ':' */
            while (isspace(*value))
                value++;
            /* Trim out trailing spaces before the ':' */
            while (ptr > line && isspace(ptr[-1]))
                *--ptr = '\0';
            /* Handle the different keys */
            if (!strcmp(line, "CPU implementer")) {
                implementer = (int) strtol(value, NULL, 0);
            } else if (!strcmp(line, "CPU part")) {
                part = (int) strtol(value, NULL, 0);
            } else if (!strcmp(line, "processor")) {
                processor = (int) strtol(value, NULL, 0);
            } else if (!strcmp(line, "Model")) {
                snprintf(model, sizeof(model), "%s", value);
            }
        } else if (line[0] == '\0') {
            /* We got an empty line; interpret that as terminating the
             * current processor. */
            if (implementer != -1 && part != -1
                && (affinity < 0 || affinity == processor)) {
                struct arm_core_id core = { implementer, part };
                unsigned           i;
                for (i = 0; i < nb_cores; i++)
                    if (memcmp(&cores[i], &core, sizeof(core)) == 0)
                        break;
                /* If the new core differs from all of the earlier seen ones,
                 * store the new one. */
                if (i == nb_cores && nb_cores < ARRAY_SIZE(cores))
                    cores[nb_cores++] = core;
            }
            /* Reset variables for the next processor. */
            implementer = part = processor = -1;
        }
    }

    fclose(f);

    if (model[0] == '\0' && nb_cores == 0)
        return NULL;

    if (nb_cores == 0) {
        snprintf(buf, buflen, "%s", model);
        return buf;
    }

    size_t pos = print_cores(buf, buflen, cores, nb_cores);

    if (pos < buflen && model[0] != '\0')
        pos += snprintf(buf + pos, buflen - pos, " (%s)", model);

    return buf;
}
  #endif

  #ifdef _WIN32
COLD const char *checkasm_get_arm_win32_reg(char *buf, size_t buflen, int affinity)
{
    HKEY key;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "Hardware\\Description\\System\\CentralProcessor", 0, KEY_READ,
                      &key)
        != ERROR_SUCCESS)
        return NULL;
    DWORD index = 0;

    char               cpu_name[100] = "";
    struct arm_core_id cores[5];
    unsigned           nb_cores = 0;

    while (1) {
        /* Iterate over all subkeys. They are named 0, 1, 2, etc, one for each
         * processor. */
        char  name[10];
        DWORD name_size = sizeof(name);
        if (RegEnumKeyExA(key, index, name, &name_size, NULL, NULL, NULL, NULL)
            != ERROR_SUCCESS)
            break;
        index++;
        unsigned processor = strtoul(name, NULL, 0);

        if (affinity >= 0 && (unsigned) affinity != processor)
            continue;

        DWORD64 midr_el1;
        DWORD   midr_el1_size = sizeof(midr_el1);
        /* Read the register CP 4000 aka MIDR_EL1. */
        if (RegGetValueA(key, name, "CP 4000", RRF_RT_QWORD, NULL, &midr_el1,
                         &midr_el1_size)
            == ERROR_SUCCESS) {
            unsigned implementer = (midr_el1 >> 24) & 0xff;
            unsigned part        = (midr_el1 >> 4) & 0xfff;

            struct arm_core_id core = { implementer, part };
            unsigned           i;
            for (i = 0; i < nb_cores; i++)
                if (memcmp(&cores[i], &core, sizeof(core)) == 0)
                    break;
            /* If the new core differs from all of the earlier seen ones,
             * store the new one. */
            if (i == nb_cores && nb_cores < ARRAY_SIZE(cores))
                cores[nb_cores++] = core;
        }
        DWORD cpu_name_size = sizeof(cpu_name);
        if (RegGetValueA(key, name, "ProcessorNameString", RRF_RT_REG_SZ, NULL, cpu_name,
                         &cpu_name_size)
            != ERROR_SUCCESS)
            cpu_name[0] = '\0';
        /* Not including "Identifier", which usually has a nondescriptive string like
         * "ARMv8 (64-bit) Family 8 Model 800 Revision A01".
         * Not including "VendorIdentifier"; it doesn't add much extra value,
         * and may contain a longer string like "Qualcomm Technologies Inc". */
    }
    RegCloseKey(key);

    if (cpu_name[0] == '\0' && nb_cores == 0)
        return NULL;

    if (nb_cores == 0) {
        snprintf(buf, buflen, "%s", cpu_name);
        return buf;
    }

    size_t pos = print_cores(buf, buflen, cores, nb_cores);

    if (pos < buflen && cpu_name[0] != '\0')
        pos += snprintf(buf + pos, buflen - pos, " (%s)", cpu_name);

    return buf;
}
  #endif
#endif
