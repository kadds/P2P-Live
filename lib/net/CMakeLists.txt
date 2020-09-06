file(GLOB_RECURSE  DIR_SRCS *.cc *.cpp *.CC *.CPP)
file(GLOB_RECURSE  DIR_HDRS *.h *.hpp)
find_package( Boost REQUIRED COMPONENTS context)

add_library(net STATIC ${DIR_SRCS})
if (WIN32)
find_package(protobuf NO_MODULE REQUIRED)
else()
find_package(Protobuf REQUIRED)
endif(WIN32)


target_include_directories(net PRIVATE ${PROTOBUF_INCLUDE_DIRS})
target_include_directories(net PUBLIC ${DIR_HDRS})

target_include_directories(net PRIVATE)
target_link_libraries(net PUBLIC Boost::context)
target_link_libraries(net PRIVATE protobuf::libprotoc protobuf::libprotobuf protobuf::libprotobuf-lite)