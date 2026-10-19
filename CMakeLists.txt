cmake_minimum_required(VERSION 3.16)
project(qddbconv VERSION 1.2.4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

include(GNUInstallDirs)
find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent)

add_executable(qddbconv WIN32
    src/main.cpp
    src/preset.h
    src/preset.cpp
    src/track.h
    src/track.cpp
    src/smaf2mid.h
    src/smaf2mid.cpp
    src/converter.h
    src/converter.cpp
    src/mainwindow.h
    src/mainwindow.cpp
    $<$<PLATFORM_ID:Windows>:qddbconv.rc>
)

target_include_directories(qddbconv PRIVATE src)
target_link_libraries(qddbconv PRIVATE Qt6::Widgets Qt6::Concurrent)

install(TARGETS qddbconv DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES qddbconv.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES qddbconv.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES qddbconv.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
install(FILES README.md INSTALL.md CHANGELOG.md LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR})

# CPack configuration for generating .deb packages via `cpack -G DEB`
set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_NAME "qddbconv")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_CONTACT "goberghen")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Standalone Qt6 audio converter inspired by DeaDBeeF")
set(CPACK_DEBIAN_PACKAGE_SECTION "sound")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libpng16-16t64 | libpng16-16, libjpeg62-turbo | libjpeg8, qt6-svg-plugins, qt6-image-formats-plugins, ffmpeg")
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "flac, lame, opus-tools, vorbis-tools, wavpack, qt6-qpa-plugins, qt6-wayland, qt6-xdgdesktopportal-platformtheme")
set(CPACK_DEBIAN_PACKAGE_SUGGESTS "timidity | fluidsynth, openmpt123 | xmp, faac, fdkaac, twolame, musepack-tools")
include(CPack)
