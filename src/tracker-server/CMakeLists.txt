file(GLOB_RECURSE  DIR_SRCS *.cc *.cpp *.CC *.CPP)

add_executable(tracker-server ${DIR_SRCS})
find_package(GLOG NO_MODULE REQUIRED)

target_link_libraries(tracker-server net glog::glog gflags Threads::Threads)
