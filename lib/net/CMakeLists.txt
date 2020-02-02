file(GLOB_RECURSE  DIR_SRCS *.cc *.cpp *.CC *.CPP)
find_package( Boost REQUIRED COMPONENTS context)

add_library(net SHARED ${DIR_SRCS})

target_include_directories(net PRIVATE)
target_link_libraries(net PUBLIC Boost::context)

