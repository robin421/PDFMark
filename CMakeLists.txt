cmake_minimum_required(VERSION 3.20)
project(PDFMark VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(WIN32)
    add_compile_definitions(NOMINMAX)
endif()

# Enable Qt Automoc / Autouic / Autorcc
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Icon assets
set(APP_ICON_ICNS "${CMAKE_CURRENT_SOURCE_DIR}/resources/app_icon.icns")
set(APP_ICON_PNG  "${CMAKE_CURRENT_SOURCE_DIR}/resources/app_icon.png")
set(APP_ICON_RC   "${CMAKE_CURRENT_SOURCE_DIR}/resources/pdfmark.rc")
set(APP_RESOURCES_QRC "${CMAKE_CURRENT_SOURCE_DIR}/resources/resources.qrc")

# Find Qt6 components
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Test)
# Find or setup PDFium
set(PDFIUM_VERSION "8035")
set(LOCAL_PDFIUM "${CMAKE_CURRENT_SOURCE_DIR}/third_party/pdfium")
# Determine whether the platform-specific pdfium binary actually exists locally.
# The repository ships only the macOS dylib; on Windows/Linux we must download.
set(LOCAL_PDFIUM_OK FALSE)
if(EXISTS "${LOCAL_PDFIUM}/PDFiumConfig.cmake")
    if(WIN32 AND EXISTS "${LOCAL_PDFIUM}/lib/pdfium.dll.lib" AND EXISTS "${LOCAL_PDFIUM}/bin/pdfium.dll")
        set(LOCAL_PDFIUM_OK TRUE)
    elseif(APPLE AND EXISTS "${LOCAL_PDFIUM}/lib/libpdfium.dylib")
        set(LOCAL_PDFIUM_OK TRUE)
    elseif(NOT WIN32 AND NOT APPLE AND EXISTS "${LOCAL_PDFIUM}/lib/libpdfium.so")
        set(LOCAL_PDFIUM_OK TRUE)
    endif()
endif()

if(LOCAL_PDFIUM_OK)
    set(PDFium_DIR "${LOCAL_PDFIUM}" CACHE PATH "Path to PDFium" FORCE)
    list(APPEND CMAKE_PREFIX_PATH "${LOCAL_PDFIUM}")
else()
    include(FetchContent)
    if(WIN32)
        set(PDFIUM_URL "https://github.com/bblanchon/pdfium-binaries/releases/download/chromium%2F${PDFIUM_VERSION}/pdfium-win-x64.tgz")
    elseif(APPLE)
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
            set(PDFIUM_URL "https://github.com/bblanchon/pdfium-binaries/releases/download/chromium%2F${PDFIUM_VERSION}/pdfium-mac-arm64.tgz")
        else()
            set(PDFIUM_URL "https://github.com/bblanchon/pdfium-binaries/releases/download/chromium%2F${PDFIUM_VERSION}/pdfium-mac-x64.tgz")
        endif()
    else()
        set(PDFIUM_URL "https://github.com/bblanchon/pdfium-binaries/releases/download/chromium%2F${PDFIUM_VERSION}/pdfium-linux-x64.tgz")
    endif()
    message(STATUS "Fetching PDFium from ${PDFIUM_URL}")
    FetchContent_Declare(
        pdfium_prebuilt
        URL ${PDFIUM_URL}
    )
    FetchContent_MakeAvailable(pdfium_prebuilt)
    set(PDFium_DIR "${pdfium_prebuilt_SOURCE_DIR}" CACHE PATH "Path to PDFium" FORCE)
    list(APPEND CMAKE_PREFIX_PATH "${pdfium_prebuilt_SOURCE_DIR}")
endif()

find_package(PDFium REQUIRED)
# Common library containing business logic
add_library(pdfmark_core STATIC
    src/common/Common.h
    src/pdf/PdfLibrary.h
    src/pdf/PdfLibrary.cpp
    src/pdf/PdfDocument.h
    src/pdf/PdfDocument.cpp
    src/pdf/PdfRenderer.h
    src/pdf/PdfRenderer.cpp
    src/pdf/PdfWriter.h
    src/pdf/PdfWriter.cpp
    src/watermark/WatermarkConfig.h
    src/watermark/WatermarkTileLayout.h
    src/watermark/WatermarkTileLayout.cpp
    src/watermark/WatermarkRenderer.h
    src/watermark/WatermarkRenderer.cpp
    src/task/TaskManager.h
    src/task/TaskManager.cpp
    src/task/WorkerPool.h
    src/task/WorkerPool.cpp
    src/diagnostics/Diagnostics.h
    src/diagnostics/Diagnostics.cpp
)

target_include_directories(pdfmark_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(pdfmark_core PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    pdfium
)

set(APP_SRCS
    main.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    src/ui/PasswordDialog.h
    src/ui/PasswordDialog.cpp
    ${APP_RESOURCES_QRC}
)

if(APPLE)
    set_source_files_properties(${APP_ICON_ICNS} PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources"
    )
    list(APPEND APP_SRCS ${APP_ICON_ICNS})
elseif(WIN32)
    list(APPEND APP_SRCS ${APP_ICON_RC})
endif()

# Main Executable - macOS gets a proper .app bundle
add_executable(PdfMark ${APP_SRCS})
set_target_properties(PdfMark PROPERTIES MACOSX_BUNDLE TRUE)

if(APPLE)
    set_target_properties(PdfMark PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/MacOSXBundleInfo.plist.in"
        MACOSX_BUNDLE_ICONFILE "app_icon.icns"
        MACOSX_BUNDLE_BUNDLE_NAME "PDFMark"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.pdfmark.app"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_COPYRIGHT "Copyright (c) 2026 PDFMark Authors"
    )
endif()

target_link_libraries(PdfMark PRIVATE
    pdfmark_core
)

# Fix macOS RPATH for PDFium dynamic library
if(APPLE)
    set_target_properties(PdfMark PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "@loader_path/../Frameworks;@loader_path/."
    )
    # Copy dylib next to executable in build tree, fix loader path, and sign on macOS
    add_custom_command(TARGET PdfMark POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:pdfium>
            $<TARGET_FILE_DIR:PdfMark>
        COMMAND /bin/bash "${CMAKE_CURRENT_SOURCE_DIR}/cmake/macos_post_build.sh"
            "$<TARGET_BUNDLE_DIR:PdfMark>"
            "$<TARGET_FILE:PdfMark>"
            "$<TARGET_FILE_DIR:PdfMark>/libpdfium.dylib"
    )
elseif(WIN32)
    # Copy dll next to executable in build tree
    add_custom_command(TARGET PdfMark POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PDFium_LIBRARY}"
            $<TARGET_FILE_DIR:PdfMark>
    )
endif()

# Tests
enable_testing()
add_subdirectory(tests)
