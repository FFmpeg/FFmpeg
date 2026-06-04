# Copyright © 2025 Niklas Haas
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

doxygen = find_program('doxygen', required: build_docs)
if not doxygen.found()
  subdir_done()
endif

doxygen_file_names = [
  'introduction.md',
  'getting-started.md',
  'integration.md',
  'writing-tests.md',
  'benchmarking.md',
]

doxygen_paths = checkasm_api_header_paths
foreach file_name : doxygen_file_names
  doxygen_paths += meson.current_source_dir() / file_name
endforeach

# Escape input files for "INPUT" option in Doxyfile
doxygen_inputs = []
foreach path : doxygen_paths
  path = path.replace('\\', '\\\\')
  path = path.replace('"', '\\"')
  doxygen_inputs += ['"' + path + '"']
endforeach

docs_conf = configuration_data()
docs_conf.set('INPUT', ' '.join(doxygen_inputs))
docs_conf.set('VERSION', meson.project_version())
docs_conf.set_quoted('BUILDDIR', meson.current_build_dir())
docs_conf.set_quoted('INCDIR', checkasm_include_dir)

doxygen_layout = meson.current_source_dir() / 'DoxygenLayout.xml'
docs_conf.set_quoted('LAYOUT_FILE', doxygen_layout)

doxyfile = configure_file(
  input: 'Doxyfile.in',
  output: 'Doxyfile',
  configuration: docs_conf,
  install: false
)

custom_target('docs',
  input: doxyfile,
  output: 'html',
  command: [doxygen, '@INPUT@'],
  depend_files: files(doxygen_paths, doxygen_layout),
  build_by_default: false,
  install: false,
  console: true,
)

message('Run "meson compile docs" to build HTML documentation')
