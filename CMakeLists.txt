cmake_minimum_required(VERSION 3.21)

project(Spindle VERSION 0.4.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Homebrew Qt hint on macOS; harmless elsewhere.
if(APPLE AND NOT CMAKE_PREFIX_PATH)
  foreach(_qt_hint /opt/homebrew/opt/qt /usr/local/opt/qt)
    if(EXISTS "${_qt_hint}")
      list(APPEND CMAKE_PREFIX_PATH "${_qt_hint}")
    endif()
  endforeach()
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Xml WebEngineWidgets WebChannel Concurrent)

qt_standard_project_setup()

# --- miniz (vendored, EPUB unzip) -----------------------------------------
add_library(miniz STATIC third_party/miniz/miniz.c)
target_include_directories(miniz PUBLIC third_party/miniz)

# --- application ----------------------------------------------------------
# Platform app-icon sources.
set(SPINDLE_ICON_SOURCES "")
if(APPLE)
  set(SPINDLE_MACOS_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/spindle.icns")
  set_source_files_properties("${SPINDLE_MACOS_ICON}" PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources")
  list(APPEND SPINDLE_ICON_SOURCES "${SPINDLE_MACOS_ICON}")
elseif(WIN32)
  list(APPEND SPINDLE_ICON_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/resources/spindle.rc")
endif()

qt_add_executable(spindle
  ${SPINDLE_ICON_SOURCES}
  src/main.cpp
  src/epub/ZipArchive.cpp
  src/epub/ZipArchive.h
  src/epub/EpubBook.cpp
  src/epub/EpubBook.h
  src/epub/PathUtil.cpp
  src/epub/PathUtil.h
  src/web/EpubSchemeHandler.cpp
  src/web/EpubSchemeHandler.h
  src/web/Bridge.cpp
  src/web/Bridge.h
  src/net/OllamaClient.cpp
  src/net/OllamaClient.h
  resources/spindle.qrc
  src/model/Highlight.cpp
  src/model/Highlight.h
  src/model/HighlightStore.cpp
  src/model/HighlightStore.h
  src/model/TranslationCache.cpp
  src/model/TranslationCache.h
  src/model/Glossary.cpp
  src/model/Glossary.h
  src/model/SummaryStore.cpp
  src/model/SummaryStore.h
  src/core/ChapterText.cpp
  src/core/ChapterText.h
  src/core/BlockIndex.cpp
  src/core/BlockIndex.h
  src/core/Search.cpp
  src/core/Search.h
  src/core/Markdown.cpp
  src/core/Markdown.h
  src/core/KindleImport.cpp
  src/core/KindleImport.h
  src/core/Matcher.cpp
  src/core/Matcher.h
  src/core/AozoraExport.cpp
  src/core/AozoraExport.h
  src/core/TranslatedEpub.cpp
  src/core/TranslatedEpub.h
  src/ui/MainWindow.cpp
  src/ui/MainWindow.h
)

# This is a Widgets/WebEngine app with no QML sources. Skipping Qt's QML import
# scanner avoids running an unnecessary host tool during configure/build.
set_target_properties(spindle PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)

target_include_directories(spindle PRIVATE src)

target_compile_definitions(spindle PRIVATE
  SPINDLE_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(spindle PRIVATE
  Qt6::Widgets
  Qt6::Network
  Qt6::Xml
  Qt6::WebEngineWidgets
  Qt6::WebChannel
  Qt6::Concurrent
  miniz
)

if(APPLE)
  set_target_properties(spindle PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "Spindle"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.spindle.reader"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_ICON_FILE "spindle.icns"
  )
elseif(WIN32)
  # Build as a GUI (WinMain) app so no console window pops up at launch.
  set_target_properties(spindle PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

# --- install / packaging --------------------------------------------------
include(GNUInstallDirs)

install(TARGETS spindle
  BUNDLE  DESTINATION .
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(FILES third_party/miniz/LICENSE
        DESTINATION ${CMAKE_INSTALL_DOCDIR}
        RENAME LICENSE.miniz)

# Linux desktop integration (icon + .desktop launcher).
if(UNIX AND NOT APPLE)
  install(FILES packaging/spindle.desktop
          DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)
  install(FILES resources/spindle.svg
          DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps)
endif()

set(CPACK_PACKAGE_NAME "Spindle")
set(CPACK_PACKAGE_VENDOR "Spindle")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Native cross-platform EPUB reader")
set(CPACK_PACKAGE_FILE_NAME "Spindle-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Spindle")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_VERBATIM_VARIABLES TRUE)

if(WIN32)
  set(CPACK_GENERATOR "ZIP")
  set(CPACK_NSIS_PACKAGE_NAME "Spindle")
  set(CPACK_NSIS_DISPLAY_NAME "Spindle EPUB Reader")
  set(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\\\spindle.exe")
  set(CPACK_NSIS_MUI_FINISHPAGE_RUN "spindle.exe")
elseif(APPLE)
  set(CPACK_GENERATOR "DragNDrop")
else()
  set(CPACK_GENERATOR "TGZ")
  set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Spindle")
  set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
  set(CPACK_DEBIAN_PACKAGE_DESCRIPTION "Native EPUB reader with highlights, search and local AI translation")
endif()

include(CPack)
