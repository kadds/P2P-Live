file(GLOB_RECURSE  DIR_SRCS *.cc *.cpp *.CC *.CPP)

add_executable(edge-server ${DIR_SRCS})
target_link_libraries(edge-server net glog gflags Threads::Threads)
