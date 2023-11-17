include(oomph_git_submodule)
include(oomph_external_project)

if(OOMPH_GIT_SUBMODULE)
    update_git_submodules()
endif()

# ---------------------------------------------------------------------
# MPI setup
# ---------------------------------------------------------------------
find_package(MPI REQUIRED COMPONENTS CXX)

# ---------------------------------------------------------------------
# Boost setup
# ---------------------------------------------------------------------
find_package(Boost REQUIRED)

#------------------------------------------------------------------------------
# Find Threads
#------------------------------------------------------------------------------
find_package(Threads REQUIRED)

# ---------------------------------------------------------------------
# hwmalloc setup
# ---------------------------------------------------------------------
cmake_dependent_option(OOMPH_USE_BUNDLED_HWMALLOC "Use bundled hwmalloc lib." ON
    "OOMPH_USE_BUNDLED_LIBS" OFF)
if(OOMPH_USE_BUNDLED_HWMALLOC)
    check_git_submodule(hwmalloc ext/hwmalloc)
    add_subdirectory(ext/hwmalloc)
    add_library(HWMALLOC::hwmalloc ALIAS hwmalloc)
else()
    find_package(HWMALLOC REQUIRED)
endif()

# ---------------------------------------------------------------------
# google test setup
# ---------------------------------------------------------------------
find_package(GTest QUIET)
message("GTest FOUND ${GTest_FOUND}")
if (NOT GTest_FOUND)
    include(FetchContent)
    FetchContent_Declare(
      googletest
      GIT_REPOSITORY https://github.com/google/googletest.git
      GIT_TAG main
      GIT_SHALLOW TRUE)
    # For Windows: Prevent overriding the parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()
