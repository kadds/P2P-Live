file(GLOB_RECURSE  DIR_SRCS *.cc *.cpp *.CC *.CPP)
find_package(GTest REQUIRED)

add_executable(test-net ${DIR_SRCS})
target_link_libraries(test-net net pthread ${GTEST_LIBRARIES} ${GTEST_MAIN_LIBRARIES})
gtest_discover_tests(test-net)