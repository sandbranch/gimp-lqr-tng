project('gimp-lqr-tng', 'c',
  version: '0.1.0',
  license: 'GPL-3.0-or-later',
  meson_version: '>= 0.60.0',
  default_options: ['c_std=gnu11', 'warning_level=2'],
)

cc = meson.get_compiler('c')
# callbacks of GTK, GLib and liblqr have parameters they do not use
add_project_arguments('-Wno-unused-parameter', language: 'c')
glib_dep = dependency('glib-2.0', version: '>= 2.68')
math_dep = cc.find_library('m', required: false)

# liblqr: the system library, or built from source and linked in, which
# makes the plug-in independent of the system (e.g. in Flatpak)
if get_option('bundled_liblqr').enabled()
  lqr_dep = subproject('liblqr', default_options: ['warning_level=0']).get_variable('liblqr_dep')
else
  lqr_dep = dependency('lqr-1', version: '>= 0.4.0',
                       required: get_option('bundled_liblqr').disabled())
  if not lqr_dep.found()
    lqr_dep = subproject('liblqr', default_options: ['warning_level=0']).get_variable('liblqr_dep')
  endif
endif

# the seam carving, on its own
carve_lib = static_library('carve', 'src/carve.c', 'src/masks.c',
  dependencies: [glib_dep, lqr_dep, math_dep])
carve_dep = declare_dependency(link_with: carve_lib,
  include_directories: include_directories('src'),
  dependencies: [glib_dep, lqr_dep, math_dep])

test('carve', executable('test-carve', 'tests/unit/test-carve.c',
                         dependencies: carve_dep),
     timeout: 120)
test('masks', executable('test-masks', 'tests/unit/test-masks.c',
                         dependencies: carve_dep))

gimp_dep = dependency('gimp-3.0', version: '>= 3.0.0',
                      required: get_option('plugin'))
gimpui_dep = dependency('gimpui-3.0', version: '>= 3.0.0',
                        required: get_option('plugin'))

if gimp_dep.found() and gimpui_dep.found()
  subdir('src')
  subdir('po')
endif
