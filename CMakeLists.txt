cmake_minimum_required(VERSION 3.17)
# CMake version is set at 3.17 because of find_package(CUDAToolkit)
project(oomph VERSION 0.1 LANGUAGES CXX)

# ---------------------------------------------------------------------
# CMake setup, C++ version, build type, modules, etc
# ---------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE) # AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Choose the type of build." FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

set(OOMPH_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
list(APPEND CMAKE_MODULE_PATH "${OOMPH_MODULE_PATH}")

# ---------------------------------------------------------------------
# Define main oomph library
# ---------------------------------------------------------------------
add_library(oomph INTERFACE)
add_library(oomph::oomph ALIAS oomph)
include(oomph_common)

# ---------------------------------------------------------------------
# MPI setup
# ---------------------------------------------------------------------
find_package(MPI REQUIRED COMPONENTS CXX)
target_link_libraries(oomph INTERFACE MPI::MPI_CXX)

# ---------------------------------------------------------------------
# HWMALLOC setup
# ---------------------------------------------------------------------
include(hwmalloc)
target_link_libraries(oomph INTERFACE hwmalloc)

# ---------------------------------------------------------------------
# Boost setup
# ---------------------------------------------------------------------
find_package(Boost REQUIRED)
target_link_libraries(oomph INTERFACE Boost::boost)

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
# testing
# ---------------------------------------------------------------------
set(OOMPH_WITH_TESTING OFF CACHE BOOL "True if tests shall be built")
if (OOMPH_WITH_TESTING)
    set(CTEST_TEST_TIMEOUT 30)
    include(CTest)
    add_subdirectory(test)
endif()

# ---------------------------------------------------------------------
# benchmarks
# ---------------------------------------------------------------------
set(OOMPH_WITH_BENCHMARKS OFF CACHE BOOL "Build benchmarks")
if (OOMPH_WITH_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

# ---------------------------------------------------------------------
# install rules
# ---------------------------------------------------------------------
install(EXPORT oomph-targets
    FILE oomph-targets.cmake
    NAMESPACE oomph::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake)

configure_package_config_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/oomphConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/oomphConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake)

write_basic_package_version_file(oomphConfigVersion.cmake
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)

install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/oomphConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/oomphConfigVersion.cmake
        ${CMAKE_CURRENT_LIST_DIR}/cmake/FindLibfabric.cmake
        ${CMAKE_CURRENT_LIST_DIR}/cmake/FindUCP.cmake
        ${CMAKE_CURRENT_LIST_DIR}/cmake/FindPMIx.cmake
    DESTINATION
        ${CMAKE_INSTALL_LIBDIR}/cmake)

export(EXPORT oomph-targets
    FILE "${CMAKE_CURRENT_BINARY_DIR}/oomph-targets.cmake")
