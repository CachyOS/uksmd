project(
  'uksmd',
  'c',
  version: '1.2.5',
  license : 'GPLv3',
  meson_version : '>= 0.64.0'
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
systemd = dependency('systemd', required: false)
gettext_package = 'uksmdstats'

if systemd.found()
  systemd_system_unit_dir = systemd.get_variable(pkgconfig : 'systemdsystemunitdir')

  install_data(
    [
      'uksmd.service',
    ],
    install_dir: systemd_system_unit_dir
  )

  project_dependencies += [
    dependency('libsystemd')
  ]

  build_args += [
    '-DHAVE_SYSTEMD',
  ]
endif

install_data(
  [
    'uksmdstats',
  ],
  install_dir: '/usr/bin'
)

install_data(
  [
    'COPYING',
  ],
  rename : 'LICENSE',
  install_dir: '/usr/share/licenses/uksmd'
)

subdir('locale')

project_target = executable(
  meson.project_name(),
  project_source_files,
  dependencies: project_dependencies,
  install : true,
  c_args : build_args,
)
