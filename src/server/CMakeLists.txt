file(GLOB_RECURSE  DIR_SRCS *.cc *.cpp *.CC *.CPP)

add_executable(server ${DIR_SRCS})

target_link_libraries(server net glog pthread)
