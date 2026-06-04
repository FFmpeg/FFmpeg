# Copyright © 2025 Niklas Haas
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

api_header_file_names = [
  'attributes.h',
  'checkasm.h',
  'test.h',
  'utils.h',
]

extra_header_names = [
  'header_config.h',

  'perf.h',
  'perf/aarch64.h',
  'perf/arm.h',
  'perf/loongarch32.h',
  'perf/loongarch64.h',
  'perf/ppc64le.h',
  'perf/x86.h',

  'platform.h',
  'platform/aarch64.h',
  'platform/arm.h',
  'platform/loongarch.h',
  'platform/ppc64le.h',
  'platform/riscv.h',
  'platform/x86_32.h',
  'platform/x86_64.h',
]

checkasm_api_headers = files(api_header_file_names + extra_header_names)

# Export these for the Doxygen build process (see docs/meson.build)
checkasm_include_dir = meson.current_source_dir()
checkasm_api_header_paths = []
foreach file_name : api_header_file_names
  checkasm_api_header_paths += checkasm_include_dir / file_name
endforeach

if not meson.is_subproject()
  install_headers(checkasm_api_headers, subdir: meson.project_name(), preserve_path: true)
endif
