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

if build_tests.disabled()
  subdir_done()
endif

libm_dependency = cc.find_library('m', required: false)

optional_arguments = []
if cc.get_argument_syntax() != 'msvc'
    optional_arguments += [
      '-Wno-unused-parameter',
    ]
endif
extra_arguments = cc.get_supported_arguments(optional_arguments)

checkasm_asm_objs = []
checkasm_asm_sources = []
checkasm_test_sources = files(
  'selftest.c',
  'generic.c',
  'arm/32/tests.c',
  'arm/64/tests.c',
  'riscv/tests.c',
  'x86/tests.c',
)

if arch_x86
  checkasm_test_sources += files(
    'x86/tests_asm.asm',
  )
elif arch_riscv
  checkasm_test_sources += files(
    'riscv/tests_asm.S',
  )
elif arch_aarch64
  checkasm_asm_sources = files('arm/64/tests_asm.S')
elif arch_arm
  checkasm_asm_sources = files('arm/32/tests_asm.S')
else
  warning('There are no assembly functions to test on the current platform.')
endif

if use_gaspp and checkasm_asm_sources.length() > 0
  gaspp_args = [
    '-I@0@'.format(meson.project_source_root() + '/include'),
    '-I@0@'.format(meson.project_source_root()),
    '-I@0@'.format(meson.project_build_root()),
    '-I@0@'.format(meson.project_build_root() + '/src'),
    '-DCHECKASM_HAVE_GENERATED_H',
  ]
  checkasm_asm_objs += gaspp_gen.process(checkasm_asm_sources, extra_args: gaspp_args)
else
  checkasm_test_sources += checkasm_asm_sources
endif

checkasm_test = executable('checkasm-selftest',
  checkasm_test_sources,
  checkasm_asm_objs,
  checkasm_config,
  c_args: [extra_arguments, '-DCHECKASM_HAVE_GENERATED_H'],
  nasm_args: '-DCHECKASM_HAVE_GENERATED_H',
  dependencies: [checkasm_dep, libm_dependency],
  include_directories: include_directories('../src'),
  build_by_default: false,
  install: false,
)

test('selftest',      checkasm_test, suite: 'checkasm', args: ['--verbose'])
benchmark('selftest', checkasm_test, suite: 'checkasm', args: ['--bench', '--verbose'])
