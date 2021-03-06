#
#  Copyright (c) 2020 Siddharth Chandrasekaran <siddharth@embedjournal.com>
#
#  SPDX-License-Identifier: Apache-2.0
#

# Generate osdp_config.h in build dir.
configure_file(include/osdp_config.h.in include/osdp_config.h @ONLY)

set(LIB_OSDP osdp)
set(LIB_OSDP_STATIC osdpstatic)
list(APPEND LIB_OSDP_SRC
	osdp_common.c
	osdp_phy.c
	osdp_cp.c
	osdp_pd.c
)
if(CONFIG_OSDP_SC_ENABLED)
	list(APPEND LIB_OSDP_SRC
		osdp_sc.c
		osdp_aes.c
	)
endif()

## build libosdpstatic.a

add_library(${LIB_OSDP_STATIC} STATIC ${LIB_OSDP_SRC})
target_link_libraries(${LIB_OSDP_STATIC} utils)
target_include_directories(${LIB_OSDP_STATIC}
	PUBLIC
		$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
		$<INSTALL_INTERFACE:include>
	PRIVATE
		${CMAKE_SOURCE_DIR}/utils/include
		${CMAKE_CURRENT_SOURCE_DIR}/include
		${CMAKE_BINARY_DIR}/src/include
)

## build libosdp.so

# keep all symbols hidden by default (-fvisibility=hidden) for shared library
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_C_VISIBILITY_PRESET   hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN 1)

# For shared library (gcc/linux), utils must be recompiled with -fPIC. Right
# now cmake doesn't support `--whole-archvive ... --no-whole-archive` directive
# to linker (see https://gitlab.kitware.com/cmake/cmake/-/issues/20078).
#
# Note: Mac seems to be impervious to this issue, I suppose it treats all
# objects relocatable by default (?).
#
list(APPEND LIB_OSDP_UTILS_SRC
	${CMAKE_SOURCE_DIR}/utils/src/utils.c
	${CMAKE_SOURCE_DIR}/utils/src/strutils.c
	${CMAKE_SOURCE_DIR}/utils/src/list.c
	${CMAKE_SOURCE_DIR}/utils/src/queue.c
	${CMAKE_SOURCE_DIR}/utils/src/slab.c
)

# add source files of utils instead of linking it. See comment above.
add_library(${LIB_OSDP} SHARED ${LIB_OSDP_SRC} ${LIB_OSDP_UTILS_SRC})

set_target_properties(${LIB_OSDP} PROPERTIES
	VERSION ${PROJECT_VERSION}
	SOVERSION ${PROJECT_VERSION_MAJOR}
	PUBLIC_HEADER ${CMAKE_SOURCE_DIR}/include/osdp.h
)
target_include_directories(${LIB_OSDP}
	PUBLIC
		$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
		$<INSTALL_INTERFACE:include>
	PRIVATE
		${CMAKE_SOURCE_DIR}/utils/include
		${CMAKE_CURRENT_SOURCE_DIR}/include
		${CMAKE_BINARY_DIR}/src/include
)

# generate osdp_export.h for OSDP_EXPORT() macro
# must be placed after the target definitions.
include(GenerateExportHeader)
generate_export_header(${LIB_OSDP}
	EXPORT_MACRO_NAME OSDP_EXPORT
	EXPORT_FILE_NAME ${CMAKE_BINARY_DIR}/src/include/osdp_export.h
)

## Package Configuration

include(CMakePackageConfigHelpers)
set(LIBOSDP_CMAKE_CONFIG_DIR "lib/cmake/libosdp")
set(LIBOSDP_PKG_CONFIG_DIR   "lib/pkgconfig")
set(LIBOSDP_DEFINITIONS      "")
set(LIBOSDP_INCLUDE_DIR      "include")
set(LIBOSDP_LIBRARY          ${LIB_OSDP})
set(LIBOSDP_LIBRARY_STATIC   ${LIB_OSDP_STATIC})
set(LIBOSDP_LIBRARY_DIR      "lib")
set(LIBOSDP_USE_FILE         "lib/cmake/libosdp/UseLibOSDP.cmake")
set(LIBOSDP_ROOT_DIR         ${CMAKE_INSTALL_PREFIX})
set(LIBOSDP_VERSION_STRING   ${PROJECT_VERSION})
set(LIBOSDP_VERSION_MAJOR    ${PROJECT_VERSION_MAJOR})
set(LIBOSDP_VERSION_MINOR    ${PROJECT_VERSION_MINOR})
set(LIBOSDP_VERSION_PATCH    ${PROJECT_VERSION_PATCH})

configure_package_config_file(
	${CMAKE_SOURCE_DIR}/cmake/LibOSDPConfig.cmake.in
	${CMAKE_BINARY_DIR}/LibOSDPConfig.cmake
	INSTALL_DESTINATION ${LIBOSDP_CMAKE_CONFIG_DIR}
	PATH_VARS
		LIBOSDP_ROOT_DIR
		LIBOSDP_INCLUDE_DIR
		LIBOSDP_LIBRARY_DIR
	NO_CHECK_REQUIRED_COMPONENTS_MACRO
	INSTALL_PREFIX ${CMAKE_INSTALL_PREFIX}
)

write_basic_package_version_file (
	${CMAKE_BINARY_DIR}/LibOSDPConfigVersion.cmake
	VERSION ${PROJECT_VERSION}
	COMPATIBILITY AnyNewerVersion
)

# pkg-config file
configure_file(
	${CMAKE_SOURCE_DIR}/misc/libosdp.pc.in
	${CMAKE_BINARY_DIR}/libosdp.pc @ONLY
)

## Install targets

install(
	TARGETS ${LIB_OSDP} ${LIB_OSDP_STATIC}
	LIBRARY DESTINATION ${LIBOSDP_LIBRARY_DIR}
	ARCHIVE DESTINATION ${LIBOSDP_LIBRARY_DIR}
	PUBLIC_HEADER DESTINATION ${LIBOSDP_INCLUDE_DIR}
	COMPONENT distributabes
)
install(
	FILES
		${CMAKE_BINARY_DIR}/LibOSDPConfig.cmake
		${CMAKE_BINARY_DIR}/LibOSDPConfigVersion.cmake
		${CMAKE_SOURCE_DIR}/cmake/UseLibOSDP.cmake
	DESTINATION ${LIBOSDP_CMAKE_CONFIG_DIR}
	COMPONENT config_files
)
install(
	FILES
		${CMAKE_BINARY_DIR}/libosdp.pc
	DESTINATION ${LIBOSDP_PKG_CONFIG_DIR}
	COMPONENT config_files
)
