project(
  'uksmd',
  'c',
  version: '1.2.11',
  license : 'GPLv3',
  meson_version : '>= 1.1.0'
)

project_source_files = [
  'uksmd.c'
]

project_dependencies = [
  dependency('libproc2'),
  dependency('libcap-ng'),
]

build_args = [
  '-DPROJECT_NAME="' + meson.project_name() + '"',
  '-DPROJECT_VERSION="' + meson.project_version() + '"',
]

i18n = import('i18n')
systemd = dependency('systemd', required: get_option('systemd'))
gettext_package = 'uksmdstats'

if systemd.found()
  systemd_system_unit_dir = systemd.get_variable(pkgconfig : 'systemdsystemunitdir')

  install_data('uksmd.service', install_dir: systemd_system_unit_dir)

  project_dependencies += [
    dependency('libsystemd')
  ]

  build_args += [
    '-DHAVE_SYSTEMD',
  ]
endif

libalpm = dependency('libalpm', required: get_option('libalpm'))

if libalpm.found() and systemd.found()
  install_data('uksmd-systemd-restart', install_dir: '/usr/share/libalpm/scripts')

  install_data('90-uksmd-upgrade.hook', install_dir: '/usr/share/libalpm/hooks')

  project_dependencies += [
    dependency('libsystemd', 'libalpm')
  ]

  build_args += [
    '-DHAVE_SYSTEMD',
  ]
endif

install_data('uksmdstats', install_dir: '/usr/bin')

install_data(
  [
    'COPYING',
  ],
  rename : 'LICENSE',
  install_dir : get_option('datadir') / 'licenses' / 'uksmd'
)

subdir('locale')

project_target = executable(
  meson.project_name(),
  project_source_files,
  dependencies: project_dependencies,
  install : true,
  c_args : build_args,
)
