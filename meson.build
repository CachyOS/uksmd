project(
  'uksmd',
  'c',
  version: '0.0.0',
)

project_source_files = [
  'uksmd.c'
]

project_dependencies = [
  dependency('libprocps'),
  dependency('libcap-ng'),
]

systemd = dependency('systemd', required: false)

if systemd.found()
  systemd_system_unit_dir = systemd.get_pkgconfig_variable('systemdsystemunitdir')
  unit_files = [
    'uksmd.service'
  ]


  foreach u: unit_files
    configure_file(
      copy: true,
      input: u,
      install: true,
      install_dir: systemd_system_unit_dir,
      output: u,
    )
  endforeach

  project_dependencies += systemd
endif

build_args = [
  '-DPROJECT_NAME=' + meson.project_name(),
  '-DPROJECT_VERSION=' + meson.project_version(),
]

project_target = executable(
  meson.project_name(),
  project_source_files,
  dependencies: project_dependencies,
  install : true,
  c_args : build_args,
)

install_data(sources : 'uksmdstats', install_dir : '/usr/bin')
install_data(sources : 'COPYING', rename : 'LICENSE', install_dir : '/usr/share/licenses/uksmd')
