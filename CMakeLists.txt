cmake_minimum_required(VERSION 3.16)
project(qddbconv VERSION 1.3.1 LANGUAGES CXX C)

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
    src/yamaha_ma_tables.h
    src/yamaha_ma_fm.h
    src/yamaha_ma_fm.cpp
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

# Optional 32-bit native PE loader helper (qddbconv-smaf32) for M5_EmuSmw5.dll + M5_EmuHw.dll
if(WIN32)
    find_program(MINGW32_GCC i686-w64-mingw32-gcc)
    if(MINGW32_GCC)
        add_custom_command(
            OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/qddbconv-smaf32.exe"
            COMMAND "${MINGW32_GCC}" -O2 -static "${CMAKE_CURRENT_SOURCE_DIR}/src/smaf32_helper.c" -o "${CMAKE_CURRENT_BINARY_DIR}/qddbconv-smaf32.exe"
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/smaf32_helper.c"
            COMMENT "Building 32-bit SMAF DLL helper qddbconv-smaf32.exe"
        )
        add_custom_target(qddbconv_smaf32 ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/qddbconv-smaf32.exe")
        install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/qddbconv-smaf32.exe" DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
elseif(UNIX AND NOT APPLE)
    execute_process(
        COMMAND ${CMAKE_C_COMPILER} -m32 -static -O2 "${CMAKE_CURRENT_SOURCE_DIR}/src/smaf32_helper.c" -o "${CMAKE_CURRENT_BINARY_DIR}/qddbconv-smaf32-probe"
        RESULT_VARIABLE M32_STATIC_RES
        OUTPUT_QUIET ERROR_QUIET
    )
    file(REMOVE "${CMAKE_CURRENT_BINARY_DIR}/qddbconv-smaf32-probe")
    if(M32_STATIC_RES EQUAL 0)
        add_custom_command(
            OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/qddbconv-smaf32"
            COMMAND ${CMAKE_C_COMPILER} -m32 -static -O2 "${CMAKE_CURRENT_SOURCE_DIR}/src/smaf32_helper.c" -o "${CMAKE_CURRENT_BINARY_DIR}/qddbconv-smaf32"
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/smaf32_helper.c"
            COMMENT "Building static 32-bit SMAF DLL helper qddbconv-smaf32"
        )
        add_custom_target(qddbconv_smaf32 ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/qddbconv-smaf32")
        install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/qddbconv-smaf32" DESTINATION ${CMAKE_INSTALL_BINDIR})
    else()
        execute_process(
            COMMAND ${CMAKE_C_COMPILER} -m32 -O2 "${CMAKE_CURRENT_SOURCE_DIR}/src/smaf32_helper.c" -o "${CMAKE_CURRENT_BINARY_DIR}/qddbconv-smaf32-probe"
            RESULT_VARIABLE M32_DYN_RES
            OUTPUT_QUIET ERROR_QUIET
        )
        file(REMOVE "${CMAKE_CURRENT_BINARY_DIR}/qddbconv-smaf32-probe")
        if(M32_DYN_RES EQUAL 0)
            add_custom_command(
                OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/qddbconv-smaf32"
                COMMAND ${CMAKE_C_COMPILER} -m32 -O2 "${CMAKE_CURRENT_SOURCE_DIR}/src/smaf32_helper.c" -o "${CMAKE_CURRENT_BINARY_DIR}/qddbconv-smaf32"
                DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/smaf32_helper.c"
                COMMENT "Building 32-bit SMAF DLL helper qddbconv-smaf32"
            )
            add_custom_target(qddbconv_smaf32 ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/qddbconv-smaf32")
            install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/qddbconv-smaf32" DESTINATION ${CMAKE_INSTALL_BINDIR})
        endif()
    endif()
endif()

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
