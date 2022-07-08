project(
  'uksmd',
  'c',
  version: '0.0.0',
)

project_source_files = [
  'uksmd.c'
]

systemd = dependency('systemd')
systemd_system_unit_dir = systemd.get_pkgconfig_variable('systemdsystemunitdir')

unit_files = [
  'uksmd.service'
]

foreach u : unit_files
  configure_file(
    copy: true,
    input: u,
    install: true,
    install_dir: systemd_system_unit_dir,
    output: u,
    )
endforeach

project_dependencies = [
  dependency('libprocps'),
  dependency('libcap-ng'),
  systemd
]

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
