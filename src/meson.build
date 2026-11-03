puls_core_sources = files(
  'puls-application.c',
  'puls-window.c',
  'puls-smart-data.c',
  'puls-smart-parser.c',
  'puls-disk-manager.c',
  'puls-utils.c',
  'puls-health-indicator.c',
  'puls-temperature-widget.c',
  'puls-smart-table.c',
  'puls-disk-info-view.c',
  'puls-disk-selector.c',
  'puls-about-dialog.c',
  'puls-preferences-window.c',
  'puls-settings.c',
  'puls-benchmark.c',
  'puls-io-graph.c',
  'puls-temp-history.c',
  'puls-surface-scan.c',
  'puls-alert-manager.c',
  'puls-smart-history.c',
  'puls-mock-data.c',
  'puls-i18n.c',
)

src_inc = include_directories('.')

m_dep = meson.get_compiler('c').find_library('m', required: false)

puls_deps = [
  gtk4_dep,
  libadwaita_dep,
  json_glib_dep,
  gio_dep,
  gio_unix_dep,
  m_dep,
]

puls_lib = static_library('puls_core',
  puls_core_sources,
  gresource,
  include_directories: src_inc,
  dependencies: puls_deps,
)

puls_dep = declare_dependency(
  link_with: puls_lib,
  include_directories: src_inc,
  dependencies: puls_deps,
)

executable('puls-diskinfo',
  'main.c',
  dependencies: puls_dep,
  install: true,
)
