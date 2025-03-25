sizes = [32, 48]

foreach size : sizes
  install_data(
    '@0@x@0@'.format(size) / 'xfce4-timer-plugin.png',
    install_dir: get_option('prefix') / get_option('datadir') / 'icons' / 'hicolor' / '@0@x@0@'.format(size) / 'apps',
  )
endforeach

install_data(
  'scalable' / 'xfce4-timer-plugin.svg',
  install_dir: get_option('prefix') / get_option('datadir') / 'icons' / 'hicolor' / 'scalable' / 'apps',
)
