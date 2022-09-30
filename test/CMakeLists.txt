add_subdirectory(mpi_runner)

set(OOMPH_TEST_LEAK_GPU_MEMORY OFF CACHE BOOL "Do not free memory (bug on Piz Daint)")

# ---------------------------------------------------------------------
# compile tests
# ---------------------------------------------------------------------

# list of serial tests to be executed
set(serial_tests test_unique_function test_unsafe_shared_ptr)

# list of parallel tests to be executed
set(parallel_tests test_context test_send_recv test_send_multi test_cancel test_locality)
#test_tag_range)
if (OOMPH_ENABLE_BARRIER)
    list(APPEND parallel_tests test_barrier)
endif()

# creates an object library (i.e. *.o file)
function(compile_test t_)
    set(t ${t_}_obj)
    add_library(${t} OBJECT ${t_}.cpp)
    oomph_target_compile_options(${t})
    if (OOMPH_TEST_LEAK_GPU_MEMORY)
        target_compile_definitions(${t} PRIVATE OOMPH_TEST_LEAK_GPU_MEMORY)
    endif()
    target_link_libraries(${t} PRIVATE ext-gtest)
    target_link_libraries(${t} PUBLIC oomph)
endfunction()

# compile an object library for each test
# tests will be compiled only once and then linked against all enabled oomph backends
list(APPEND all_tests ${serial_tests} ${parallel_tests})
list(REMOVE_DUPLICATES all_tests)
foreach(t ${all_tests})
    compile_test(${t})
endforeach()

# ---------------------------------------------------------------------
# link and register tests
# ---------------------------------------------------------------------

function(reg_serial_test t)
    add_executable(${t} $<TARGET_OBJECTS:${t}_obj>)
    oomph_target_compile_options(${t})
    target_link_libraries(${t} PRIVATE ext-gtest)
    target_link_libraries(${t} PRIVATE oomph_common)
    add_test(
        NAME ${t}
        COMMAND $<TARGET_FILE:${t}>)
endfunction()

foreach(t ${serial_tests})
    reg_serial_test(${t})
endforeach()

# creates an executable by linking to object file and to selected oomph backend
function(reg_parallel_test t_ lib n)
    set(t ${t_}_${lib})
    add_executable(${t} $<TARGET_OBJECTS:${t_}_obj>)
    oomph_target_compile_options(${t})
    target_link_libraries(${t} PRIVATE gtest_main_mpi)
    target_link_libraries(${t} PRIVATE oomph_${lib})
    add_test(
        NAME ${t}
        COMMAND ${MPIEXEC_EXECUTABLE} ${MPIEXEC_NUMPROC_FLAG} ${n} ${MPIEXEC_PREFLAGS}
            $<TARGET_FILE:${t}> ${MPIEXEC_POSTFLAGS})
    set_tests_properties(${t} PROPERTIES RUN_SERIAL TRUE)
endfunction()

if (OOMPH_WITH_MPI)
    foreach(t ${parallel_tests})
        reg_parallel_test(${t} mpi 2)
    endforeach()
endif()

if (OOMPH_WITH_UCX)
    foreach(t ${parallel_tests})
        reg_parallel_test(${t} ucx 2)
    endforeach()
endif()

if (OOMPH_WITH_LIBFABRIC)
    foreach(t ${parallel_tests})
        reg_parallel_test(${t} libfabric 2)
    endforeach()
endif()

add_subdirectory(bindings)
