# Copyright © 2018, VideoLAN and dav1d authors
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this
#    list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
# ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# Dependencies
libm_dependency     = cc.find_library('m', required: false)
rt_dependency       = cc.find_library('rt', required: false)
libdl_dependency    = cc.find_library('dl', required: false)
thread_dependency   = dependency('threads')

have_pthread_np = cc.check_header('pthread_np.h')
if have_pthread_np
  test_args += '-DHAVE_PTHREAD_NP_H'
endif

have_pthread_setaffinity_np = cc.has_function('pthread_setaffinity_np',
  prefix: '''
    #include <pthread.h>
    #ifdef HAVE_PTHREAD_NP_H
    #include <pthread_np.h>
    #endif
  ''',
  args: test_args + '-D_GNU_SOURCE',
  dependencies: thread_dependency,
)
if not have_pthread_setaffinity_np
  thread_dependency = []
endif


have_clock_gettime = cc.has_function('clock_gettime', prefix : '#include <time.h>', args : test_args, dependencies : rt_dependency)
have_ioctl = cc.has_function('ioctl', prefix : '#include <sys/ioctl.h>', args : test_args)
have_isatty = cc.has_function('isatty', prefix : '#include <unistd.h>', args : test_args)
have_prctl = cc.has_function('prctl', prefix : '#include <sys/prctl.h>', args : test_args)
have_sigaction = cc.has_function('sigaction', prefix : '#include <signal.h>', args : test_args)
have_siglongjmp = cc.has_function('siglongjmp', prefix : '#include <setjmp.h>', args : test_args)

have_getauxval = false
have_elf_aux_info = false
if (arch_aarch64 or arch_arm or arch_loongarch or arch_ppc64le or arch_riscv)
    have_getauxval = cc.has_function('getauxval', prefix : '#include <sys/auxv.h>', args : test_args)
    have_elf_aux_info = cc.has_function('elf_aux_info', prefix : '#include <sys/auxv.h>', args : test_args)
endif

have_stdbit_h = cc.check_header('stdbit.h')

have_sys_hwprobe_h = false
have_asm_hwprobe_h = false
if arch_riscv
    have_sys_hwprobe_h = cc.check_header('sys/hwprobe.h')
    have_asm_hwprobe_h = cc.check_header('asm/hwprobe.h')
endif

api_export_flags = []
api_export_flags_asm = []
if host_machine.system() == 'windows'
    if get_option('default_library') != 'static'
        # nasm's dllexport doesn't match the mingw conventions on x86_32,
        # so don't do explicit dllexport at all, just let it export all symbols.
        if cc.get_argument_syntax() == 'msvc' or not arch_x86_32
            api_export_flags = ['-DCHECKASM_BUILDING_DLL', '-DBUILDING_DLL']
            api_export_flags_asm = ['-DBUILDING_DLL']
        endif
    endif
endif

if build_tests.allowed()
    api_export_flags     += '-DCHECKASM_BUILDING_TESTS'
    api_export_flags_asm += '-DCHECKASM_BUILDING_TESTS'
endif

have_linux_perf = false
if host_machine.system() == 'linux'
  have_linux_perf = cc.has_header_symbol('linux/perf_event.h', 'PERF_COUNT_HW_CPU_CYCLES', args: test_args)
endif

# Build configuration
cdata = configuration_data()
cdata.set('PREFIX',                         cc.symbols_have_underscore_prefix())
cdata.set_quoted('CHECKASM_VERSION',        meson.project_version())
cdata.set10('ARCH_AARCH64',                 arch_aarch64)
cdata.set10('ARCH_ARM',                     arch_arm)
cdata.set10('ARCH_X86',                     arch_x86)
cdata.set10('ARCH_X86_64',                  arch_x86_64)
cdata.set10('ARCH_X86_32',                  arch_x86_32)
cdata.set10('ARCH_PPC64LE',                 arch_ppc64le)
cdata.set10('ARCH_RISCV',                   arch_riscv)
cdata.set10('ARCH_RV32',                    arch_rv32)
cdata.set10('ARCH_RV64',                    arch_rv64)
cdata.set10('ARCH_LOONGARCH',               arch_loongarch)
cdata.set10('ARCH_LOONGARCH32',             arch_loongarch32)
cdata.set10('ARCH_LOONGARCH64',             arch_loongarch64)
cdata.set10('HAVE_PTHREAD_NP_H',            have_pthread_np)
cdata.set10('HAVE_PTHREAD_SETAFFINITY_NP',  have_pthread_setaffinity_np)
cdata.set10('HAVE_CLOCK_GETTIME',           have_clock_gettime)
cdata.set10('HAVE_IOCTL',                   have_ioctl)
cdata.set10('HAVE_ISATTY',                  have_isatty)
cdata.set10('HAVE_SIGACTION',               have_sigaction)
cdata.set10('HAVE_SIGLONGJMP',              have_siglongjmp)
cdata.set10('HAVE_GETAUXVAL',               have_getauxval)
cdata.set10('HAVE_ELF_AUX_INFO',            have_elf_aux_info)
cdata.set10('HAVE_LINUX_PERF',              have_linux_perf)
cdata.set10('HAVE_STDBIT_H',                have_stdbit_h)
cdata.set10('HAVE_PRCTL',                   have_prctl)

if arch_x86
  cdata_asm = configuration_data()
  cdata_asm.set('PREFIX', cc.symbols_have_underscore_prefix())
  cdata_asm.set10('ARCH_X86_64', arch_x86_64)
  cdata_asm.set10('ARCH_X86_32', arch_x86_32)
  cdata_asm.set10('PIC', true)
  cdata_asm.set10('FORCE_VEX_ENCODING', cc.get_define('__AVX__').strip() != '')
endif

if arch_aarch64
  if use_gaspp
    python3 = import('python').find_installation()
  endif
  aarch64_extensions = {
      'sve': ['whilelt p0.s, x0, x1'],
      'sme': ['smstart', 'cntb x0'],
  }
  foreach name, instrs : aarch64_extensions
    if use_gaspp
      instr = '\n'.join(instrs)
      f = configure_file(
        command: [python3, '-c', 'import sys; print(sys.argv[1])', '@0@'.format(instr)],
        output: 'test-@0@.S'.format(name),
        capture: true)
      r = run_command(gaspp, gaspp_args, f, '-c', '-o', meson.current_build_dir() / 'test-' + name + '.obj', check: false)
      message('Checking for gaspp/armasm64 ' + name.to_upper() + ': ' + (r.returncode() == 0 ? 'YES' : 'NO'))
      archext_supported = false
      supported = r.returncode() == 0
    else
      instr = '\\n'.join(instrs)
      code = f'__asm__ (".arch_extension @name@\\n@instr@\\n");'
      supported = cc.compiles(code, name: name.to_upper())
      archext_supported = supported
    endif
    cdata.set10('HAVE_AS_ARCHEXT_' + name.to_upper() + '_DIRECTIVE', archext_supported)
    cdata.set10('HAVE_' + name.to_upper(), supported)
  endforeach
endif

if arch_riscv
  cdata.set10('HAVE_SYS_HWPROBE_H', have_sys_hwprobe_h)
  cdata.set10('HAVE_ASM_HWPROBE_H', have_asm_hwprobe_h)
endif

checkasm_config = []
checkasm_config += configure_file(
  output: 'checkasm_config_generated.h',
  configuration: cdata,
  install: false,
)

# Build definition
checkasm_asm_objs = []
checkasm_sources = files(
  'arm/cpu.c',
  'checkasm.c',
  'cpu.c',
  'function.c',
  'perf.c',
  'perf/arm.c',
  'perf/linux.c',
  'perf/macos_kperf.c',
  'riscv/cpu.c',
  'signal.c',
  'stackguard.c',
  'stats.c',
  'utils.c',
  'x86/cpu.c',
)

checkasm_asm_sources = []
if arch_aarch64
  checkasm_asm_sources = files('arm/checkasm_64.S')
elif arch_arm
  checkasm_asm_sources = files('arm/checkasm_32.S')
elif arch_loongarch
  checkasm_sources += files('loongarch/checkasm.S')
elif arch_riscv
  checkasm_sources += files('riscv/callcheck.S')
elif arch_x86
  add_languages('nasm', native: false)
  configure_file(
    output: 'checkasm_config_generated.asm',
    output_format: 'nasm',
    configuration: cdata_asm,
    install: false,
  )
  checkasm_sources += files(
    'x86/checkasm.asm',
  )
endif

if use_gaspp
  gaspp_args = api_export_flags_asm + [
    '-I@0@'.format(meson.project_source_root() + '/include'),
    '-I@0@'.format(meson.current_source_dir()),
    '-I@0@'.format(meson.current_build_dir()),
    '-DCHECKASM_HAVE_GENERATED_H',
  ]
  checkasm_asm_objs += gaspp_gen.process(checkasm_asm_sources, extra_args: gaspp_args)
else
  checkasm_sources += checkasm_asm_sources
endif

checkasm_dependencies = [
  thread_dependency,
  rt_dependency,
  libdl_dependency,
  libm_dependency,
]

checkasm_library = library(meson.project_name(),
  checkasm_sources,
  checkasm_config,
  checkasm_asm_objs,
  c_args : [api_export_flags, '-DCHECKASM_HAVE_GENERATED_H'],
  nasm_args : [api_export_flags_asm, '-DCHECKASM_HAVE_GENERATED_H'],
  include_directories: checkasm_inc_dirs,
  dependencies: checkasm_dependencies,
  install: not meson.is_subproject(),
)

checkasm_dep = declare_dependency(
  link_with: checkasm_library,
  include_directories: checkasm_inc_dirs,
)

if not meson.is_subproject()
  pkg = import('pkgconfig')
  pkg.generate(
    name: meson.project_name(),
    description: 'checkasm for all your asm checking needs',
    libraries: checkasm_library,
    version: meson.project_version(),
  )
endif
