project('hue2mqtt', 'cpp', default_options : ['cpp_std=c++20'])

deps = [dependency('dbus-1'), dependency('boost'), dependency('libcrypto'), dependency('libssl')]
incdir = include_directories('/home/user/boost_1_86_0', './include')

executable('hue2mqtt', 'src/hue2mqtt.cpp', install : true,
    sources: ['./src/BleManager.cpp', 
              './src/BleDevice.cpp', 
              './src/HueDevice.cpp'], 
    dependencies : deps, include_directories : incdir)
