cmake_minimum_required(VERSION 3.18)
project(sonora VERSION 0.3.4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

include(GNUInstallDirs)
find_package(Qt6 REQUIRED COMPONENTS Widgets DBus)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET gstreamer-1.0 gstreamer-pbutils-1.0)

add_executable(sonora
  src/main.cpp
  src/MainWindow.cpp       src/MainWindow.h
  src/LevelMeter.cpp       src/LevelMeter.h
  src/SpectrumAnalyzer.cpp src/SpectrumAnalyzer.h
  src/Player.cpp           src/Player.h
  src/PlaylistModel.cpp    src/PlaylistModel.h
  src/PlaylistWidget.cpp   src/PlaylistWidget.h
  src/MetadataLoader.cpp   src/MetadataLoader.h
  src/AudioOutput.cpp      src/AudioOutput.h
  src/PreferencesDialog.cpp src/PreferencesDialog.h
  src/Config.cpp           src/Config.h
  src/Mpris.cpp            src/Mpris.h
  src/VizDelay.cpp         src/VizDelay.h
  src/TrackInfo.h          src/TransportIcons.h)
target_compile_definitions(sonora PRIVATE SONORA_VERSION="${PROJECT_VERSION}")
target_link_libraries(sonora PRIVATE Qt6::Widgets Qt6::DBus PkgConfig::GST)

install(TARGETS sonora RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/sonora.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/sonora.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
