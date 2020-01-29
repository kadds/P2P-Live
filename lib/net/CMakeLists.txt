file(GLOB_RECURSE  DIR_SRCS *.cc *.cpp *.CC *.CPP)
find_package(Libcopp CONFIG REQUIRED)
add_library(net SHARED ${DIR_SRCS})

target_include_directories(net PRIVATE ${Libcopp_INCLUDE_DIRS})
target_link_libraries(net PRIVATE ${Libcotask_LIBRARIES} ${Libcopp_LIBRARIES})

