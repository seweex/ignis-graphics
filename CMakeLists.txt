cmake_minimum_required (VERSION 3.18...4.2)

set (IGNIS_VERSION_MAJOR 0)
set (IGNIS_VERSION_MINOR 0)
set (IGNIS_VERSION_PATCH 1)

project (ignis-graphics
    DESCRIPTION
        "This useful and compact library for graphics based on Vulkan.\
         It hides the complexity of Vulkan and provides to you easy-to-understand interface."
    VERSION
        ${IGNIS_VERSION_MAJOR}.${IGNIS_VERSION_MINOR}.${IGNIS_VERSION_PATCH}
    LANGUAGES
        CXX)

add_library (ignis INTERFACE)

target_include_directories (ignis
    INTERFACE
        $<INSTALL_INTERFACE:include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_compile_definitions (ignis
    INTERFACE cxx_std_20)

include (GNUInstallDirs)
include (CMakePackageConfigHelpers)

install (
    TARGETS ignis
    EXPORT ignis-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR})

install (
    EXPORT ignis-targets
    FILE ignis-targets.cmake
    NAMESPACE ignis::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ignis)

write_basic_package_version_file (
    ignis-config-version.cmake
    VERSION ${IGNIS_VERSION_MAJOR}.${IGNIS_VERSION_MINOR}.${IGNIS_VERSION_PATCH}
    COMPATIBILITY SameMajorVersion)

install (
    FILES ${CMAKE_CURRENT_BINARY_DIR}/ignis-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ignis)