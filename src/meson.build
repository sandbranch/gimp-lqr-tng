plugindir = get_option('plugindir')
if plugindir == ''
  plugindir = gimp_dep.get_variable(pkgconfig: 'gimplibdir') / 'plug-ins'
endif
# GIMP 3 loads a plug-in from a folder of its own name
bindir = plugindir / meson.project_name()

conf = configuration_data()
conf.set_quoted('GETTEXT_PACKAGE', meson.project_name())
configure_file(output: 'config.h', configuration: conf)

executable(meson.project_name(),
  'main.c',
  'dialog.c',
  'layer-io.c',
  'paint-view.c',
  include_directories: include_directories('.'),
  dependencies: [carve_dep, gimp_dep, gimpui_dep],
  install: true,
  install_dir: bindir,
)
