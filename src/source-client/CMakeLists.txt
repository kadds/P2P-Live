file(GLOB_RECURSE  DIR_SRCS *.cc *.cpp *.CC *.CPP)




add_executable(source-client ${DIR_SRCS})

target_include_directories(source-client PRIVATE ${FFMPEG_INCLUDE_DIR} )

target_link_libraries(source-client PRIVATE ${FFMPEG_LIBRARY} )

MESSAGE(${AVFORMAT_LIBRARY})

