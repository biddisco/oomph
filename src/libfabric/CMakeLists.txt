find_package(Boost REQUIRED CONFIG COMPONENTS thread)

# dummy library of our private headers
add_library(oomph_private_libfabric_headers INTERFACE)
target_include_directories(oomph_private_libfabric_headers INTERFACE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>")

# actual library (created in oomph_libfabric.cmake) source files,
# depends on dummy library
target_link_libraries(oomph_libfabric PRIVATE oomph_private_libfabric_headers)
target_link_libraries(oomph_libfabric PRIVATE Boost::thread)

# we need to include a binary dir for the oomph_config_defines.hpp file
target_include_directories(oomph_libfabric INTERFACE
    "$<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/src/libfabric>")

list(TRANSFORM oomph_sources PREPEND ${CMAKE_CURRENT_SOURCE_DIR}/../
    OUTPUT_VARIABLE oomph_sources_libfabric)
target_sources(oomph_libfabric PRIVATE ${oomph_sources_libfabric})
target_sources(oomph_libfabric PRIVATE context.cpp)
target_sources(oomph_libfabric PRIVATE operation_context.cpp)
target_sources(oomph_libfabric PRIVATE locality.cpp)

# if we are using GPU, then the libfabric library was probably
# built with gpu support, and we should link to cuda to prevent
# link errors
if (HWMALLOC_ENABLE_DEVICE)
  include(CheckLanguage)
  check_language(CUDA)

  if(CMAKE_CUDA_COMPILER)
    enable_language(CUDA)
  else()
     message(STATUS "No CUDA support")
    return()
  endif()

  find_package(CUDAToolkit)
  target_link_libraries(oomph_libfabric PRIVATE CUDA::cudart)
endif()

add_executable(check_libfabric test/check_libfabric.cpp)
target_link_libraries(check_libfabric PUBLIC oomph_libfabric)
target_include_directories(check_libfabric PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
