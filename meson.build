project('hue2mqtt', ['cpp', 'c'], default_options: ['cpp_std=c++20'])

deps = [dependency('dbus-1'), dependency('libcrypto'), dependency('libssl')]
incdir = include_directories('./include')

executable(
    'hue2mqtt',
    'src/hue2mqtt.cpp',
    install: true,
    sources: [
        './src/BleManager.cpp',
        './src/BleDevice.cpp',
        './src/HueDevice.cpp',
        './src/mqtt.cpp',
    ],
    dependencies: deps,
    include_directories: incdir,
)

executable(
    'testing',
    'src/testing.cpp',
    install: true,
    sources: [
        './src/BleManager.cpp',
        './src/BleDevice.cpp',
        './src/HueDevice.cpp',
        './src/mqtt.cpp',
    ],
    dependencies: deps,
    include_directories: incdir,
)

install_data('S99hue2mqtt', install_dir: '/etc/init.d')

