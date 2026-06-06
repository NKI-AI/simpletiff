project('simpletiff', 'cpp',
  version : '0.0.1',
  license : 'Apache-2.0',
  meson_version : '>=1.1',
  default_options : [
    'warning_level=2',
    'cpp_std=c++20',
    'default_library=shared',
  ])

is_wasm = host_machine.cpu_family() == 'wasm32' or host_machine.cpu_family() == 'wasm64'

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
# aifocore provides the header-only `aifocore/status/result.h` and
# `aifocore/platform/portability.h` used throughout simpletiff. It is not on
# wrapdb, so it is resolved from the standalone NKI-AI/aifocore repo via
# subprojects/aifocore.wrap.
# All wrap files in subprojects/ declare a `[provide]` section, so a plain
# `dependency()` transparently falls back to the bundled wrap when the library
# is not found on the system.
aifocore_dep = dependency('aifocore')
zlib_dep = dependency('zlib')
zstd_dep = dependency('libzstd')
png_dep = dependency('libpng')

simpletiff_deps = [aifocore_dep, zlib_dep, zstd_dep, png_dep]
simpletiff_defines = ['-DSIMPLETIFF_HAS_PNG']

# JPEG decoder backend.
jpeg_extra_inc = []
if get_option('jpeg_decoder') == 'libjpeg'
  jpeg_dep = dependency('libjpeg')  # libjpeg-turbo wrap provides `libjpeg`
  simpletiff_deps += jpeg_dep
  jpeg_source = 'src/io_utils_libjpeg.cpp'
else
  # jpgd ships in the NKI-AI/jpeg-compressor repo and is pulled in via
  # subprojects/jpeg-compressor.wrap (provides the `jpgd` dependency).
  jpgd_dep = dependency('jpgd', fallback : ['jpeg-compressor', 'jpgd_dep'])
  simpletiff_deps += jpgd_dep
  # io_utils_jpgd.cpp includes "jpeg-compressor/jpgd.h", but the subproject
  # exports its headers at its own root. The wrap unpacks the repo to
  # subprojects/jpeg-compressor/, so add subprojects/ to the include path to
  # make the `jpeg-compressor/` prefix resolve.
  jpeg_extra_inc = [include_directories('subprojects')]
  jpeg_source = 'src/io_utils_jpgd.cpp'
endif

# OpenJPEG (JPEG2000). Not on wrapdb, so this is a system dependency only.
# Always disabled for wasm targets.
openjpeg_opt = get_option('with_openjpeg')
if is_wasm
  openjpeg_opt = openjpeg_opt.disable_auto_if(true)
endif
openjpeg_dep = dependency('libopenjp2', required : openjpeg_opt)
if openjpeg_dep.found()
  simpletiff_deps += openjpeg_dep
  simpletiff_defines += '-DSIMPLETIFF_HAS_OPENJPEG'
endif

# ---------------------------------------------------------------------------
# Library
# ---------------------------------------------------------------------------
inc = include_directories('include')

simpletiff_sources = [
  'src/ccitt.cpp',
  'src/ccitt_tables.cpp',
  'src/deflate.cpp',
  'src/image_writer.cpp',
  'src/index.cpp',
  'src/io_utils_common.cpp',
  'src/jpeg2000.cpp',
  'src/lzw.cpp',
  'src/ndpi_mcu_tiling.cpp',
  'src/predictor.cpp',
  'src/reader.cpp',
  'src/tiff_parser.cpp',
  'src/zstd.cpp',
  jpeg_source,
]

simpletiff_lib = library('simpletiff',
  simpletiff_sources,
  include_directories : [inc] + jpeg_extra_inc,
  dependencies : simpletiff_deps,
  cpp_args : simpletiff_defines + ['-Wno-unused-parameter'],
  install : true)

install_subdir('include/simpletiff', install_dir : get_option('includedir'))

# `simpletiff_defines` are public (consumers gate optional includes such as
# `image_writer.h` on `SIMPLETIFF_HAS_PNG`), mirroring Bazel's public `defines`.
simpletiff_dep = declare_dependency(
  include_directories : inc,
  link_with : simpletiff_lib,
  compile_args : simpletiff_defines,
  dependencies : simpletiff_deps)

meson.override_dependency('simpletiff', simpletiff_dep)

import('pkgconfig').generate(simpletiff_lib,
  name : 'simpletiff',
  description : 'Fast TIFF reader library for whole-slide imaging',
  subdirs : 'simpletiff')

# ---------------------------------------------------------------------------
# Optional components
# ---------------------------------------------------------------------------
if get_option('build_tests')
  subdir('tests')
endif

if get_option('build_examples')
  subdir('examples')
endif

if get_option('build_benchmarks')
  subdir('benchmarks')
endif
