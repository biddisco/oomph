#-------------------------------------------------------
# Slurm job launching script generator
#
# We would like to generate a script which can be used to generate batch jobs
# to submit benchmarks to slurm using many combinations of settings.
#
# We add a custom command which takes our template script and
# expands out all the variables we need to pass into it.
# Unfortunately, due to the way cmake works, some variables are only known
# at build time, and not at cmake configure time. Using a custom command which
# calls cmake to run our script at build time, allows us to pass variables
# into the final script which is placed in our build dir.
#
# Note that we generate these scripts in the build dir instead
# of the install dir as they are intended for development testing.
# A version could be supported for installation later.
#-------------------------------------------------------

set(OOMPH_BENCHMARK_SCRIPTS_PATH "${PROJECT_BINARY_DIR}/scripts" CACHE PATH "Directory to place batch scripts in")
mark_as_advanced(OOMPH_BENCHMARK_SCRIPTS_PATH)

# Make sure scripts dir exists
execute_process(COMMAND "${CMAKE_COMMAND}" -E make_directory "${OOMPH_BENCHMARK_SCRIPTS_PATH}")

# The MPI backend is always enabled and uses no extension on the benchmarks
# it will be added automatically by the script runner
set(BENCHMARK_SUFFIXES)

if (OOMPH_WITH_LIBFABRIC)
    list(APPEND BENCHMARK_SUFFIXES _libfabric)
endif()
if (OOMPH_WITH_UCX)
    list(APPEND BENCHMARK_SUFFIXES _ucx)
endif()

#--------------------------------------------------
# Slurm script generator for benchmarks
#--------------------------------------------------
set(SCRIPTS "generate-oomph.py")

foreach(script ${SCRIPTS})
  ADD_CUSTOM_COMMAND(
    DEPENDS   "${CMAKE_CURRENT_SOURCE_DIR}/${script}"
    COMMAND   "${CMAKE_COMMAND}"
    ARGS
              # needed by the benchmark script
              -DBIN_DIR=${PROJECT_BINARY_DIR}/benchmarks
              -DRUN_DIR=${OOMPH_BENCHMARK_SCRIPTS_PATH}
              -DMPIEXEC="${MPIEXEC}"

              # needed by the copy script
              -DSCRIPT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
              -DSCRIPT_NAME=${script}
              -DSCRIPT_DEST_DIR="${OOMPH_BENCHMARK_SCRIPTS_PATH}"

              # quoted as it might be a list
              "-DBENCHMARK_SUFFIXES=${BENCHMARK_SUFFIXES}"
              -DJOB_OPTIONS1="${SLURM_JOB_OPTIONS1}"
              -P "${CMAKE_CURRENT_SOURCE_DIR}/copy_script.cmake"

    OUTPUT    "${OOMPH_BENCHMARK_SCRIPTS_PATH}/${script}"
    VERBATIM
  )

  add_custom_target(script-${script}
    DEPENDS "${OOMPH_BENCHMARK_SCRIPTS_PATH}/${script}"
  )
endforeach(script)
