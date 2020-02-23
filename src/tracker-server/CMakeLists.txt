file(GLOB_RECURSE  DIR_SRCS *.cc *.cpp *.CC *.CPP)

add_executable(tracker-server ${DIR_SRCS})

target_link_libraries(tracker-server net glog gflags Threads::Threads)
