file(GLOB_RECURSE  DIR_SRCS *.cc *.cpp *.CC *.CPP)

add_executable(edge-server ${DIR_SRCS})
find_package(GLOG NO_MODULE REQUIRED)
target_link_libraries(edge-server net glog::glog gflags Threads::Threads)
