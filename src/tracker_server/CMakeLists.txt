file(GLOB_RECURSE  DIR_SRCS *.cc *.cpp *.CC *.CPP)
find_package(gflags REQUIRED)
include_directories(${gflags_INCLUDE_DIR})

add_executable(tracker_server ${DIR_SRCS})

target_link_libraries(tracker_server net glog pthread gflags)
