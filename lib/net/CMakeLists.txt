file(GLOB_RECURSE  DIR_SRCS *.cc *.cpp *.CC *.CPP)
file(GLOB_RECURSE  DIR_HDRS *.h *.hpp)
file(GLOB_RECURSE PROTOFILES ../../*.proto)

find_package(Boost REQUIRED COMPONENTS context)
find_package(Protobuf REQUIRED)
if (PROTOBUF_FOUND)
    message("protobuf found")
else ()
    message(FATAL_ERROR "Cannot find Protobuf")
endif ()

PROTOBUF_GENERATE_CPP(PROTO_SRCS PROTO_HDRS ${PROTOFILES})

add_library(net STATIC ${DIR_SRCS}  ${PROTO_SRCS} ${PROTO_HDRS})


target_include_directories(net PRIVATE ${PROTOBUF_INCLUDE_DIRS})
target_include_directories(net PUBLIC ${DIR_HDRS})
target_include_directories(net PUBLIC ${PROTOBUF_INCLUDE_DIRS})
target_include_directories(net PUBLIC ${CMAKE_CURRENT_BINARY_DIR})

target_include_directories(net PRIVATE)

target_link_libraries(net PUBLIC Boost::context)
target_link_libraries(net PRIVATE protobuf::libprotoc protobuf::libprotobuf protobuf::libprotobuf-lite)