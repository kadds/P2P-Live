file(GLOB_RECURSE  DIR_SRCS *.cc *.cpp *.CC *.CPP)

add_library(net SHARED ${DIR_SRCS})
