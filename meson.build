project('puls-diskinfo', 'c',
  version: '1.0.0',
  license: 'GPL-3.0-or-later',
  meson_version: '>= 0.59',
  default_options: [
    'c_std=gnu11',
    'warning_level=2',
  ],
)

gnome = import('gnome')

# Version define
add_project_arguments(
  '-DPULS_VERSION="' + meson.project_version() + '"',
  language: 'c',
)

# Dependencies
gtk4_dep      = dependency('gtk4', version: '>= 4.0')
libadwaita_dep = dependency('libadwaita-1', version: '>= 1.1.0')
json_glib_dep = dependency('json-glib-1.0')
gio_dep       = dependency('gio-2.0')
gio_unix_dep  = dependency('gio-unix-2.0')

subdir('data')
subdir('src')
subdir('helper')
