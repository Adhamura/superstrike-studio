cmake_minimum_required(VERSION 3.20)
# The one place for the version: shown in the app (About), the Windows exe and the release.
project(superstrike_studio VERSION 1.1.1 LANGUAGES CXX)
if(WIN32)
  enable_language(RC)
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE MinSizeRel)
endif()

if(WIN32)
  # MSYS2 GCC 16: libstdc++-6.dll lacks some std::string members that the headers declare as
  # explicitly instantiated (link error "undefined reference to basic_string(basic_string&&)" at
  # -Os). _GLIBCXX_ASSERTIONS turns those declarations off (and adds cheap bounds checks).
  add_compile_definitions(_GLIBCXX_ASSERTIONS)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(DEPS REQUIRED IMPORTED_TARGET libadwaita-1 gtk4 librsvg-2.0 json-glib-1.0)

# Core (no GTK UI): HID++ transport with one backend per OS, device, profile model.
set(S ${CMAKE_SOURCE_DIR}/src)
set(CORE ${S}/hidpp.cpp ${S}/hid_sim.cpp ${S}/device.cpp ${S}/profile.cpp ${S}/keytables.cpp)
file(GLOB UI ${S}/*.cpp)
list(REMOVE_ITEM UI ${CORE} ${S}/hidpp_linux.cpp ${S}/hidpp_win.cpp ${S}/hidpp_mac.cpp ${S}/platform_win.cpp
     ${S}/platform_mac.cpp ${S}/http_gio.cpp ${S}/http_win.cpp ${S}/http_mac.cpp)
if(WIN32)
  list(APPEND CORE ${S}/hidpp_win.cpp)
  list(APPEND UI ${S}/platform_win.cpp ${S}/http_win.cpp)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  list(APPEND CORE ${S}/hidpp_linux.cpp)
  list(APPEND UI ${S}/http_gio.cpp)
elseif(APPLE)
  list(APPEND CORE ${S}/hidpp_mac.cpp)
  list(APPEND UI ${S}/platform_mac.cpp ${S}/http_mac.cpp)
else()
  message(FATAL_ERROR "SuperStrike Studio supports Linux, Windows and macOS only")
endif()

add_library(sscore STATIC ${CORE})
target_link_libraries(sscore PUBLIC PkgConfig::DEPS)

add_executable(superstrike-studio ${UI})
target_link_libraries(superstrike-studio PRIVATE sscore)
# C++ only (the Windows resource compiler would choke on these)
target_compile_definitions(superstrike-studio PRIVATE $<$<COMPILE_LANGUAGE:CXX>:SS_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets">
                                                   $<$<COMPILE_LANGUAGE:CXX>:SS_VERSION="${PROJECT_VERSION}">)
target_compile_options(sscore PRIVATE -Wall -Wextra)
target_compile_options(superstrike-studio PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wno-missing-field-initializers>)
if(APPLE)
  target_link_options(superstrike-studio PRIVATE $<$<CONFIG:MinSizeRel>:-Wl,-dead_strip>)
else()
  target_link_options(superstrike-studio PRIVATE $<$<CONFIG:MinSizeRel>:-s> -Wl,--as-needed)
endif()

if(APPLE)
  # IOKit HID, CoreFoundation, gettext (Homebrew), system libcurl for the update check.
  find_library(INTL_LIB intl REQUIRED HINTS /opt/homebrew/lib /usr/local/lib)
  find_path(INTL_INCLUDE libintl.h HINTS /opt/homebrew/include /usr/local/include)
  target_include_directories(sscore PUBLIC ${INTL_INCLUDE})
  target_link_libraries(sscore PUBLIC "-framework IOKit" "-framework CoreFoundation" ${INTL_LIB})
  target_link_libraries(superstrike-studio PRIVATE curl)
endif()

if(WIN32)
  # Native Win32 HID API and gettext (MSYS2 UCRT64); GUI exe without a console; icon + version info.
  find_library(INTL_LIB intl REQUIRED)
  target_link_libraries(sscore PUBLIC hid setupapi ${INTL_LIB})
  target_link_libraries(superstrike-studio PRIVATE winhttp)  # update check
  set_target_properties(superstrike-studio PROPERTIES WIN32_EXECUTABLE ON)
  target_include_directories(superstrike-studio PRIVATE ${CMAKE_SOURCE_DIR}/assets/windows)
  configure_file(${CMAKE_SOURCE_DIR}/assets/windows/app.rc.in ${CMAKE_BINARY_DIR}/app.rc @ONLY)
  target_sources(superstrike-studio PRIVATE ${CMAKE_BINARY_DIR}/app.rc)
  set_source_files_properties(${CMAKE_BINARY_DIR}/app.rc PROPERTIES
    OBJECT_DEPENDS ${CMAKE_SOURCE_DIR}/assets/windows/app.ico)
endif()

# Translations: po/<lang>.po -> assets/locale/<lang>/LC_MESSAGES/superstrike-studio.mo (part of the default build).
# English is the source language and has no .po. po/update-pot.sh refreshes po/superstrike-studio.pot.
find_program(MSGFMT msgfmt)
file(GLOB PO_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/po/*.po)
set(MO_FILES "")
if(PO_FILES AND NOT MSGFMT)
  message(WARNING "msgfmt not found: translations will not be compiled")
elseif(PO_FILES)
  foreach(po ${PO_FILES})
    get_filename_component(lang ${po} NAME_WE)
    set(mo_dir ${CMAKE_SOURCE_DIR}/assets/locale/${lang}/LC_MESSAGES)
    set(mo ${mo_dir}/superstrike-studio.mo)
    add_custom_command(OUTPUT ${mo}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${mo_dir}
      COMMAND ${MSGFMT} --check -o ${mo} ${po}
      DEPENDS ${po}
      COMMENT "Compiling translation ${lang}")
    list(APPEND MO_FILES ${mo})
  endforeach()
  add_custom_target(translations ALL DEPENDS ${MO_FILES})
  add_dependencies(superstrike-studio translations)
endif()

if(WIN32)
  # One-file release exe (tools/windows/onefile.c): ss-pack appends the portable folder to the
  # ss-onefile launcher. tools/windows/build-windows.sh runs it.
  set(ONEFILE ${CMAKE_SOURCE_DIR}/tools/windows/onefile.c)
  set_source_files_properties(${ONEFILE} PROPERTIES LANGUAGE CXX)  # no C compiler needed
  add_executable(ss-onefile ${ONEFILE} ${CMAKE_BINARY_DIR}/app.rc)
  target_include_directories(ss-onefile PRIVATE ${CMAKE_SOURCE_DIR}/assets/windows)
  target_compile_definitions(ss-onefile PRIVATE $<$<COMPILE_LANGUAGE:CXX>:SS_VERSION="${PROJECT_VERSION}">)
  set_target_properties(ss-onefile PROPERTIES WIN32_EXECUTABLE ON)
  target_link_options(ss-onefile PRIVATE -municode -static -s)
  target_link_libraries(ss-onefile PRIVATE cabinet)
  add_executable(ss-pack ${ONEFILE})
  target_compile_definitions(ss-pack PRIVATE ONEFILE_PACKER)
  target_link_options(ss-pack PRIVATE -municode -static -s)
  target_link_libraries(ss-pack PRIVATE cabinet)
endif()

# read-only device check (no writes): build/sscheck
add_executable(sscheck tools/sscheck.cpp)
target_link_libraries(sscheck PRIVATE sscore)
