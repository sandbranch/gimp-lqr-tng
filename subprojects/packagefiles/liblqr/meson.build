# A meson build of liblqr, which itself builds with autotools, for building
# it into the plug-in. Only the library is built, statically.
project('liblqr', 'c',
  version: '0.4.3',
  license: 'LGPL-3.0-or-later',
  meson_version: '>= 0.60.0',
)

glib_dep = dependency('glib-2.0')
math_dep = meson.get_compiler('c').find_library('m', required: false)

liblqr = static_library('lqr-1',
  'lqr/lqr_carver.c',
  'lqr/lqr_carver_bias.c',
  'lqr/lqr_carver_list.c',
  'lqr/lqr_carver_rigmask.c',
  'lqr/lqr_cursor.c',
  'lqr/lqr_energy.c',
  'lqr/lqr_gradient.c',
  'lqr/lqr_progress.c',
  'lqr/lqr_rwindow.c',
  'lqr/lqr_vmap.c',
  'lqr/lqr_vmap_list.c',
  include_directories: include_directories('.'),
  dependencies: [glib_dep, math_dep],
  c_args: ['-DLQR_DISABLE_DECLSPEC'],
  pic: true,
)

# the headers as installed: <lqr.h>, which includes <lqr/...>
liblqr_dep = declare_dependency(
  link_with: liblqr,
  include_directories: include_directories('.', 'lqr'),
  dependencies: [glib_dep, math_dep],
)
