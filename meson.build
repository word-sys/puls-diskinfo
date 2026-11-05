project('puls-diskinfo', 'c',
  version: '1.1.2',
  license: 'GPL-3.0-or-later',
  meson_version: '>= 0.59',
  default_options: [
    'c_std=gnu11',
    'warning_level=2',
  ],
)

gnome = import('gnome')
cc = meson.get_compiler('c')

warning_flags = [
  '-Werror=implicit-function-declaration',
  '-Werror=return-type',
  '-Wundef',
  '-Wformat=2',
  '-Wno-format-nonliteral',
  '-Wformat-security',
  '-Wpointer-arith',
  '-Winit-self',
  '-Wmissing-declarations',
  '-Wredundant-decls',
]

add_project_arguments(
  cc.get_supported_arguments(warning_flags),
  '-DPULS_VERSION="' + meson.project_version() + '"',
  language: 'c',
)

profile = get_option('profile')
if profile == 'development'
  add_project_arguments('-DPULS_DEVELOPMENT_BUILD=1', language: 'c')
endif

sanitize_opt = get_option('b_sanitize')
if sanitize_opt != 'none'
  message('Building with sanitizers: ' + sanitize_opt)
endif

gtk4_dep      = dependency('gtk4', version: '>= 4.0')
libadwaita_dep = dependency('libadwaita-1', version: '>= 1.1.0')
json_glib_dep = dependency('json-glib-1.0')
gio_dep       = dependency('gio-2.0')
gio_unix_dep  = dependency('gio-unix-2.0')

subdir('data')
subdir('src')
subdir('helper')
subdir('tests')
