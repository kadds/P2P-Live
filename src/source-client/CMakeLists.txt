file(GLOB_RECURSE  DIR_SRCS *.cc *.cpp *.CC *.CPP)
file(GLOB_RECURSE  WINDOW_SRCS *-window.cc *-window.cpp *-window.CC *-window.CPP)
file(GLOB_RECURSE  WINDOW_UI *-window.ui *-window.UI)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

FIND_PACKAGE(Qt5 COMPONENTS Core Gui Widgets REQUIRED)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

if (MSVC)
    add_executable(source-client WIN32 ${DIR_SRCS} ${WINDOW_UI})
else()
    add_executable(source-client ${DIR_SRCS} ${WINDOW_UI})
endif(MSVC)

find_package(GLOG NO_MODULE REQUIRED)

target_include_directories(source-client PUBLIC ${QT_INCLUDES} )
target_include_directories(source-client PUBLIC ${FFMPEG_INCLUDE_DIR})

target_link_libraries(source-client PUBLIC ${FFMPEG_LIBRARY} Qt5::Widgets Qt5::Core Qt5::Gui net glog::glog gflags Threads::Threads)

add_custom_command(TARGET source-client POST_BUILD
COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Qt5::Core> $<TARGET_FILE_DIR:source-client>
COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Qt5::Gui> $<TARGET_FILE_DIR:source-client>
COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Qt5::Widgets> $<TARGET_FILE_DIR:source-client>
)