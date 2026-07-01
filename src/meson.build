puls_sources = files(
  'main.c',
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
  # v1.1.1 additions
  'puls-temp-history.c',      # Group C: temperature history
  'puls-surface-scan.c',      # Group E: non-destructive surface scan
  'puls-alert-manager.c',     # Group F: alert manager
  'puls-smart-history.c',     # Group H: SMART attribute trend tracking
)

executable('puls-diskinfo',
  puls_sources,
  gresource,
  dependencies: [
    gtk4_dep,
    libadwaita_dep,
    json_glib_dep,
    gio_dep,
    gio_unix_dep,
    meson.get_compiler('c').find_library('m', required: false),
  ],
  install: true,
)
