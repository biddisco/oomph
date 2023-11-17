add_library(gtest_main_mpi OBJECT ./gtest_main_mpi.cpp)
target_link_libraries(gtest_main_mpi PRIVATE GTest::gtest MPI::MPI_CXX)
