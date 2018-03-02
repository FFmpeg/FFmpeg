{
  'targets': [
    {
      'target_name': 'ffmpeg',
      'sources': [
        'main.cpp',
      ],
      'include_dirs': [
        "<!(node -e \"require('nan')\")",
        '<(module_root_dir)',
      ],
      'library_dirs': [
        '<(module_root_dir)/libavcodec',
        '<(module_root_dir)/libavfilter',
        '<(module_root_dir)/libavformat',
        '<(module_root_dir)/libavutil',
        '<(module_root_dir)/libswscale',
      ],
      'libraries': [
        '-lavcodec',
        '-lavfilter',
        '-lavformat',
        '-lavutil',
        '-lswscale',
      ],
      'ldflags': [
        '-Wl,-Bsymbolic',
        # '-Wl,-R<(module_root_dir)/node_modules/native-openvr-deps/bin/linux64',
      ],
    }
  ]
}
