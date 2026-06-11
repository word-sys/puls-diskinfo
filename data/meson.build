# Compile GResource
gresource = gnome.compile_resources(
  'puls-diskinfo-resources',
  'puls-diskinfo.gresource.xml',
  c_name: 'puls_diskinfo',
)

# Install desktop file
install_data(
  'io.github.puls.diskinfo.desktop',
  install_dir: get_option('datadir') / 'applications',
)

# Install metainfo
install_data(
  'io.github.puls.diskinfo.metainfo.xml',
  install_dir: get_option('datadir') / 'metainfo',
)

# Install polkit policy
install_data(
  'io.github.puls.diskinfo.policy',
  install_dir: get_option('datadir') / 'polkit-1' / 'actions',
)
