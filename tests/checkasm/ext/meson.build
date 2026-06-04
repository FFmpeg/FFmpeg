# Copyright © 2025 Niklas Haas
# Copyright © 2024, Marvin Scholz
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

project('checkasm', 'c',
  version: '1.2.0',
  default_options: ['c_std=c11',
                    'buildtype=debugoptimized',
                    'default_library=static',
                    'b_ndebug=if-release',
                    'warning_level=2'],
  meson_version : '>=0.64.0')

# Project arguments and basic checks
cc = meson.get_compiler('c')

optional_arguments = []

if cc.get_argument_syntax() != 'msvc'
    optional_arguments += [
      '-Wundef',
      '-Werror=vla',
      '-Wno-maybe-uninitialized',
      '-Wno-missing-field-initializers',
      '-Wno-unused-parameter',
      '-Wstrict-prototypes',
      '-Werror=missing-prototypes',
      '-Wshorten-64-to-32',
      '-Wtype-limits',
      '-Wredundant-decls',
      '-Wimplicit-fallthrough=5',
    ]
else
    optional_arguments += [
      '-wd4028', # parameter different from declaration
      '-wd4996', # use of POSIX functions
    ]
endif

add_project_arguments(cc.get_supported_arguments(optional_arguments), language : 'c')

test_args = []
if host_machine.system() in ['linux', 'gnu']
  add_project_arguments(['-D_POSIX_C_SOURCE=200112', '-D_XOPEN_SOURCE=600'], language: 'c')
  test_args += ['-D_POSIX_C_SOURCE=200112', '-D_XOPEN_SOURCE=600']
endif
if host_machine.system() in ['emscripten', 'os/2']
  add_project_arguments('-D_GNU_SOURCE', language: 'c')
  test_args += '-D_GNU_SOURCE'
endif

if get_option('b_sanitize') == 'memory'
  error('asm causes false positives with the memory sanitizer.')
endif

if host_machine.system() not in ['windows', 'os/2']
    if cc.has_argument('-fvisibility=hidden')
        add_project_arguments('-fvisibility=hidden', language: 'c')
    else
        warning('Compiler does not support -fvisibility=hidden, all symbols will be public!')
    endif
endif

# Global paths
checkasm_src_root = meson.current_source_dir()
checkasm_inc_dirs = include_directories('.', 'include')

# Global platform flags
arch_aarch64     = host_machine.cpu_family() == 'aarch64' or host_machine.cpu() == 'arm64'
arch_arm         = host_machine.cpu_family().startswith('arm') and host_machine.cpu() != 'arm64'
arch_x86         = host_machine.cpu_family().startswith('x86')
arch_x86_64      = host_machine.cpu_family() == 'x86_64'
arch_x86_32      = host_machine.cpu_family() == 'x86'
arch_ppc64le     = host_machine.cpu() == 'ppc64le'
arch_riscv       = host_machine.cpu_family().startswith('riscv')
arch_rv32        = host_machine.cpu_family() == 'riscv32'
arch_rv64        = host_machine.cpu_family() == 'riscv64'
arch_loongarch   = host_machine.cpu_family().startswith('loongarch')
arch_loongarch32 = host_machine.cpu_family() == 'loongarch32'
arch_loongarch64 = host_machine.cpu_family() == 'loongarch64'

use_gaspp = false
if ((arch_aarch64 or arch_arm) and
     cc.get_argument_syntax() == 'msvc' and cc.get_id() != 'clang-cl')
    gaspp = find_program('gas-preprocessor.pl')
    use_gaspp = true
    gaspp_args = [
            '-as-type', 'armasm',
            '-arch', host_machine.cpu_family(),
            '--',
            arch_aarch64 ? 'armasm64' : 'armasm',
            '-nologo',
    ]
    gaspp_gen = generator(gaspp,
        output: '@BASENAME@.obj',
        arguments: gaspp_args + [
            '@EXTRA_ARGS@',
            '@INPUT@',
            '-c',
            '-o', '@OUTPUT@'
        ])
endif

# Global component configuration
build_tests = get_option('tests').disable_auto_if(meson.is_subproject())
build_docs  = get_option('docs' ).disable_auto_if(meson.is_subproject())

subdir('include/checkasm')
subdir('src')
subdir('docs')
subdir('tests')

meson.override_dependency(meson.project_name(), checkasm_dep)
