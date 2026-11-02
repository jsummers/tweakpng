cmake_minimum_required(VERSION 3.16)

project(TweakPNG VERSION 1.4.6 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(TWEAKPNG_BUILD_TESTS "Build the test suite" ON)
option(TWEAKPNG_FETCH_DEPS "Download zlib and libpng if they are not installed" ON)
option(TWEAKPNG_BUNDLED_ZLIB "Always build zlib from source, ignoring any system copy" OFF)
option(TWEAKPNG_BUNDLED_PNG "Always build libpng from source, ignoring any system copy" OFF)
set(TWEAKPNG_QT_VERSION "" CACHE STRING "Force a Qt major version (5 or 6); empty prefers Qt 6")

# Qt 6 is preferred; Qt 5.15 is the fallback.
set(_qt_components Core Gui Widgets)
if(TWEAKPNG_BUILD_TESTS)
	list(APPEND _qt_components Test)
endif()

# find_package(QT NAMES Qt6 Qt5) can't be used for this: it takes whichever is
# found first on the search path, which is not always Qt 6.
if(TWEAKPNG_QT_VERSION)
	find_package(Qt${TWEAKPNG_QT_VERSION} REQUIRED COMPONENTS ${_qt_components})
	set(QT_VERSION_MAJOR ${TWEAKPNG_QT_VERSION})
else()
	find_package(Qt6 QUIET COMPONENTS ${_qt_components})
	if(Qt6_FOUND)
		set(QT_VERSION_MAJOR 6)
	else()
		find_package(Qt5 REQUIRED COMPONENTS ${_qt_components})
		set(QT_VERSION_MAJOR 5)
	endif()
endif()
message(STATUS "Building against Qt ${Qt${QT_VERSION_MAJOR}_VERSION}")

if(QT_VERSION_MAJOR EQUAL 5 AND Qt5_VERSION VERSION_LESS 5.15)
	message(FATAL_ERROR "TweakPNG needs Qt 5.15 or newer")
endif()

# zlib and libpng are preferred from the system. Where there is no system copy,
# as on Windows, pinned releases are built statically.
if(NOT TWEAKPNG_BUNDLED_ZLIB)
	find_package(ZLIB QUIET)
endif()
if(NOT TWEAKPNG_BUNDLED_PNG)
	find_package(PNG QUIET)
endif()

if((NOT ZLIB_FOUND OR NOT PNG_FOUND) AND TWEAKPNG_FETCH_DEPS)
	include(FetchContent)
	set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
	# Both libraries are linked statically, so neither should be installed.
	set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)

	if(NOT ZLIB_FOUND)
		message(STATUS "Fetching zlib")
		set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
		FetchContent_Declare(zlib
			GIT_REPOSITORY https://github.com/madler/zlib.git
			GIT_TAG v1.3.1
			GIT_SHALLOW TRUE)
		FetchContent_MakeAvailable(zlib)
		if(NOT TARGET ZLIB::ZLIB)
			add_library(ZLIB::ZLIB ALIAS zlibstatic)
		endif()
		target_include_directories(zlibstatic INTERFACE "${zlib_SOURCE_DIR}" "${zlib_BINARY_DIR}")
		# These let libpng's own find_package(ZLIB) find the copy just built.
		# zlib.h is in the source folder but zconf.h is generated in the build
		# folder, and libpng takes a single folder, so put them together.
		set(_zlib_include "${CMAKE_BINARY_DIR}/zlib-include")
		file(COPY "${zlib_SOURCE_DIR}/zlib.h" "${zlib_BINARY_DIR}/zconf.h" DESTINATION "${_zlib_include}")
		set(ZLIB_INCLUDE_DIR "${_zlib_include}" CACHE PATH "" FORCE)
		set(ZLIB_LIBRARY zlibstatic CACHE STRING "" FORCE)
		set(ZLIB_FOUND TRUE)
	endif()

	message(STATUS "Fetching libpng")
	set(PNG_SHARED OFF CACHE BOOL "" FORCE)
	set(PNG_STATIC ON CACHE BOOL "" FORCE)
	set(PNG_TESTS OFF CACHE BOOL "" FORCE)
	set(PNG_TOOLS OFF CACHE BOOL "" FORCE)
	set(PNG_EXECUTABLES OFF CACHE BOOL "" FORCE)
	# A universal macOS build compiles for two architectures at once, which
	# the per-architecture SIMD selection in libpng doesn't support.
	set(PNG_HARDWARE_OPTIMIZATIONS OFF CACHE BOOL "" FORCE)
	set(PNG_FRAMEWORK OFF CACHE BOOL "" FORCE)
	FetchContent_Declare(libpng
		GIT_REPOSITORY https://github.com/pnggroup/libpng.git
		GIT_TAG v1.6.43
		GIT_SHALLOW TRUE)
	FetchContent_MakeAvailable(libpng)
	add_library(tweakpng_png INTERFACE)
	target_link_libraries(tweakpng_png INTERFACE png_static ZLIB::ZLIB)
	target_include_directories(tweakpng_png INTERFACE "${libpng_SOURCE_DIR}" "${libpng_BINARY_DIR}")
	add_library(PNG::PNG ALIAS tweakpng_png)
elseif(NOT ZLIB_FOUND OR NOT PNG_FOUND)
	message(FATAL_ERROR "zlib and libpng are required (or set TWEAKPNG_FETCH_DEPS=ON)")
endif()

# Set here, not at the top, so that it applies only to our own targets and not
# to zlib and libpng, which have nothing for moc or rcc to do.
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

add_library(tweakpng_core STATIC
	src/core/chunk.cpp
	src/core/document.cpp
	src/core/iccprof.cpp
	src/core/pngimage.cpp
	src/core/pngimage.h
	src/core/pngtodib.cpp
	src/core/pngtodib.h
	src/core/twpng.h)
target_include_directories(tweakpng_core PUBLIC src/core)
target_link_libraries(tweakpng_core PUBLIC Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Gui PRIVATE PNG::PNG ZLIB::ZLIB)

add_library(tweakpng_ui STATIC
	src/ui/chunkdialogs.cpp
	src/ui/chunkdialogs.h
	src/ui/mainwindow.cpp
	src/ui/mainwindow.h
	src/ui/metrics.cpp
	src/ui/metrics.h
	src/ui/palettedialog.cpp
	src/ui/palettedialog.h
	src/ui/settings.cpp
	src/ui/settings.h
	src/ui/tools.cpp
	src/ui/tools.h
	src/ui/viewer.cpp
	src/ui/viewer.h)
target_include_directories(tweakpng_ui PUBLIC src/ui)
target_link_libraries(tweakpng_ui PUBLIC tweakpng_core Qt${QT_VERSION_MAJOR}::Widgets PRIVATE ZLIB::ZLIB)

if(MSVC)
	target_compile_definitions(tweakpng_core PUBLIC _CRT_SECURE_NO_WARNINGS NOMINMAX)
	target_compile_options(tweakpng_core PUBLIC /W3 /utf-8)
else()
	target_compile_options(tweakpng_core PUBLIC -Wall -Wextra -Wno-unused-parameter)
endif()

# Resources go in the executable: linkers drop an object from a static library
# when nothing refers to it by name, and that is all a resource file has.
set(_app_sources src/ui/main.cpp resources/resources.qrc)
if(WIN32)
	list(APPEND _app_sources packaging/windows/tweakpng.rc)
elseif(APPLE)
	set(_icns "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/tweakpng.icns")
	list(APPEND _app_sources "${_icns}")
	set_source_files_properties("${_icns}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
endif()

add_executable(tweakpng WIN32 MACOSX_BUNDLE ${_app_sources})
target_link_libraries(tweakpng PRIVATE tweakpng_ui)

if(APPLE)
	set_target_properties(tweakpng PROPERTIES
		OUTPUT_NAME TweakPNG
		MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/Info.plist.in"
		MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
		MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
endif()

include(GNUInstallDirs)
install(TARGETS tweakpng
	BUNDLE DESTINATION .
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(UNIX AND NOT APPLE)
	install(FILES packaging/linux/tweakpng.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
	install(FILES packaging/linux/tweakpng.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo RENAME tweakpng.appdata.xml)
	install(FILES resources/icons/tweakpng-256.png
		DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps RENAME tweakpng.png)
endif()

if(TWEAKPNG_BUILD_TESTS)
	enable_testing()
	add_subdirectory(tests)
endif()
