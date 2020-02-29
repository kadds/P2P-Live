file(GLOB_RECURSE  DIR_SRCS *.cc *.cpp *.CC *.CPP)
file(GLOB_RECURSE  WINDOW_SRCS *-window.cc *-window.cpp *-window.CC *-window.CPP)
file(GLOB_RECURSE  WINDOW_UI *-window.ui *-window.UI)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

FIND_PACKAGE(Qt5Widgets REQUIRED)
FIND_PACKAGE(Qt5Gui REQUIRED)
FIND_PACKAGE(Qt5Core REQUIRED)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

FIND_PACKAGE(SDL2 REQUIRED)

add_executable(client ${DIR_SRCS} ${WINDOW_UI})

target_include_directories(client PUBLIC ${QT_INCLUDES} )
target_include_directories(client PUBLIC ${FFMPEG_INCLUDE_DIR})

target_link_libraries(client PUBLIC ${FFMPEG_LIBRARY} Qt5::Widgets Qt5::Core Qt5::Gui glog net glog gflags Threads::Threads SDL2)

