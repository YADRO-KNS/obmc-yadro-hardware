project('obmc-yadro-hardware', 'cpp',
    default_options: [
        'warning_level=3',
        'werror=true',
        'cpp_std=c++17',
    ],
    license: 'Apache-2.0',
    version: '2.0',
)

boost = dependency('boost', required: true)
pdi_dep = dependency('phosphor-dbus-interfaces')
sdbusplus_dep = dependency('sdbusplus', required: true)
sdbusplusplus_prog = find_program('sdbus++')
sdbusgen_prog = find_program('sdbus++-gendir')
realpath_prog = find_program('realpath')
generated_files = []

yadro_hw_ifaces = files(
    run_command(
        sdbusgen_prog,
        '--tool', sdbusplusplus_prog,
        '--output', meson.current_build_dir(),
        'com',
        check: true
    ).stdout().strip().split('\n')
)
foreach f : yadro_hw_ifaces
    rel_path = run_command(
        realpath_prog,
        '--relative-to', meson.current_source_dir(),
        f,
    ).stdout().strip().split('\n')[-1]
    generated_files += rel_path
endforeach

cpp_args = [
    '-DBOOST_SYSTEM_NO_DEPRECATED',
    '-DBOOST_ERROR_CODE_HEADER_ONLY',
    '-DBOOST_NO_RTTI',
    '-DBOOST_NO_TYPEID',
    '-DBOOST_ALL_NO_LIB',
    '-DBOOST_ASIO_DISABLE_THREADS'
]

systemd = dependency('systemd')
systemd_system_unit_dir = systemd.get_pkgconfig_variable(
    'systemdsystemunitdir',
    define_variable: ['prefix', get_option('prefix')])
subdir('service_files')

incdir = include_directories('src')

executable('yadro-hw-manager',
    'src/hw/main.cpp',
    'src/hw/hw_mngr.cpp',
    'src/hw/objects.cpp',
    generated_files,
    include_directories : incdir,
    cpp_args: cpp_args,
    dependencies: [
        boost,
        sdbusplus_dep,
    ],
    install: true,
)

executable('yadro-storage-manager',
    'src/storage/main.cpp',
    'src/storage/inventory.cpp',
    generated_files,
    include_directories : incdir,
    cpp_args: cpp_args,
    dependencies: [
        boost,
        sdbusplus_dep,
        pdi_dep
    ],
    install: true,
)
