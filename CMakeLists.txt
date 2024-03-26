cmake_minimum_required(VERSION 3.17)
# CMake version is set at 3.17 because of find_package(CUDAToolkit)

if (NOT ${CMAKE_VERSION} VERSION_LESS 3.27)
    # new in 3.27: additionally use uppercase <PACKAGENAME>_ROOT
    # environment and CMake variables for find_package
    cmake_policy(SET CMP0144 NEW)
endif()

set(OOMPH_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
list(APPEND CMAKE_MODULE_PATH "${OOMPH_MODULE_PATH}")

include(CMakeDependentOption)
include(oomph_version)
make_version(OOMPH 0 3 0)

project(oomph VERSION ${OOMPH_VERSION})
enable_language(CXX)

# ---------------------------------------------------------------------
# CMake setup, C++ version, build type, modules, etc
# ---------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE) # AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------
# External dependencies
# ---------------------------------------------------------------------
option(OOMPH_GIT_SUBMODULE "Check submodules during build" ON)
option(OOMPH_USE_BUNDLED_LIBS "Use bundled 3rd party libraries" OFF)
include(oomph_external_dependencies)
include(ExternalProject)

# ---------------------------------------------------------------------
# Define main oomph library
# ---------------------------------------------------------------------
add_library(oomph INTERFACE)
add_library(oomph::oomph ALIAS oomph)
target_link_libraries(oomph INTERFACE HWMALLOC::hwmalloc)
target_link_libraries(oomph INTERFACE MPI::MPI_CXX)
target_link_libraries(oomph INTERFACE Boost::boost)
include(oomph_common)

# ---------------------------------------------------------------------
# include paths
# ---------------------------------------------------------------------
target_include_directories(oomph INTERFACE
    $<INSTALL_INTERFACE:include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>)

# ---------------------------------------------------------------------
# oomph MPI variant
# ---------------------------------------------------------------------
include(oomph_mpi)

# ---------------------------------------------------------------------
# oomph UCX variant
# ---------------------------------------------------------------------
include(oomph_ucx)

# ---------------------------------------------------------------------
# oomph LibFabric variant
# ---------------------------------------------------------------------
include(oomph_libfabric)

# ---------------------------------------------------------------------
# main src subdir
# ---------------------------------------------------------------------
add_subdirectory(src)

# ---------------------------------------------------------------------
# generate config file
# ---------------------------------------------------------------------
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/config.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/oomph/config.hpp @ONLY)
install(FILES ${PROJECT_BINARY_DIR}/include/oomph/config.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/oomph)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmake_config.inc.in 
    ${CMAKE_CURRENT_BINARY_DIR}/include/oomph/cmake_config.inc)

# ---------------------------------------------------------------------
# fortan bindings
# ---------------------------------------------------------------------
include(oomph_fortran)
add_subdirectory(bindings)

# ---------------------------------------------------------------------
# testing
# ---------------------------------------------------------------------
set(OOMPH_WITH_TESTING OFF CACHE BOOL "True if tests shall be built")
if (OOMPH_WITH_TESTING)
    set(CTEST_TEST_TIMEOUT 30)
    include(CTest)
    enable_testing()
    add_subdirectory(test)
endif()

# ---------------------------------------------------------------------
# benchmarks
# ---------------------------------------------------------------------
set(OOMPH_WITH_BENCHMARKS OFF CACHE BOOL "Build benchmarks")
cmake_dependent_option(OOMPH_BENCHMARKS_MT "Multithreaded benchmarks"
    ON "OOMPH_WITH_BENCHMARKS" OFF)
if (OOMPH_WITH_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

# ---------------------------------------------------------------------
# install rules
# ---------------------------------------------------------------------
install(EXPORT oomph-targets
    FILE oomph-targets.cmake
    NAMESPACE oomph::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/oomph/cmake)

configure_package_config_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/oomphConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/oomphConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/oomph/cmake)

write_basic_package_version_file(oomphConfigVersion.cmake
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)

install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/oomphConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/oomphConfigVersion.cmake
        ${CMAKE_CURRENT_LIST_DIR}/cmake/FindLibfabric.cmake
        ${CMAKE_CURRENT_LIST_DIR}/cmake/FindUCX.cmake
        ${CMAKE_CURRENT_LIST_DIR}/cmake/FindPMIx.cmake
    DESTINATION
        ${CMAKE_INSTALL_LIBDIR}/oomph/cmake)

export(EXPORT oomph-targets
    FILE "${CMAKE_CURRENT_BINARY_DIR}/oomph-targets.cmake")
