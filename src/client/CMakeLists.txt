
file(GLOB_RECURSE  DIR_SRCS *.cc *.cpp *.CC *.CPP)

add_executable(client ${DIR_SRCS})
target_link_libraries(client net glog gflags Threads::Threads)
