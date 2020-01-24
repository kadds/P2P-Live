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

add_executable(source-client ${DIR_SRCS} ${WINDOW_UI})

target_include_directories(source-client PRIVATE ${QT_INCLUDES})
target_include_directories(source-client PRIVATE ${FFMPEG_INCLUDE_DIR} )

target_link_libraries(source-client PRIVATE ${FFMPEG_LIBRARY} Qt5::Widgets Qt5::Core Qt5::Gui)
