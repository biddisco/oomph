include(oomph_git_submodule)
include(oomph_external_project)
include(ExternalProject)

if(OOMPH_GIT_SUBMODULE)
#    update_git_submodules()
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

# ------------------------------------------------------------------------------
# Build/Download HWMalloc
# ------------------------------------------------------------------------------
get_external_project(
  PROJECT_NAME
  "hwmalloc"
  FOLDER_NAME
  "hwmalloc"
  GIT_REPO
  "https://github.com/ghex-org/hwmalloc.git"
  GIT_TAG
  "master"
)

add_library(HWMALLOC::hwmalloc ALIAS hwmalloc)

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
