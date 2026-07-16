cmake_minimum_required(VERSION 3.16)
project(comic-chat-qt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

# Default art path: sibling modern tree (override with -DCOMIC_ART_DIR=...)
set(COMIC_ART_DIR "${CMAKE_SOURCE_DIR}/../v1.0-pre-modern/comicart"
    CACHE PATH "Path to ComicArt (avatars + backdrop)")

add_library(comic_platform STATIC
    platform/QtCanvas.cpp
)
target_include_directories(comic_platform PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(comic_platform PUBLIC Qt6::Widgets)

add_library(comic_engine STATIC
    engine/vector2d.cpp
    engine/bbox.cpp
    engine/pe.cpp
    engine/traj.cpp
    engine/arc.cpp
    engine/spline.cpp
    engine/image.cpp
    engine/art_paths.cpp
    engine/pose.cpp
    engine/avatario.cpp
    engine/backdrop_qt.cpp
    engine/scene.cpp
)
target_include_directories(comic_engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(comic_engine PUBLIC comic_platform)
target_compile_definitions(comic_engine PRIVATE
    COMIC_ART_DIR="${COMIC_ART_DIR}"
)

add_library(comic_net STATIC
    net/IrcClient.cpp
    net/RpgActorClient.cpp
    net/FreeqAuth.cpp
)
target_include_directories(comic_net PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(comic_net PUBLIC comic_engine Qt6::Network Qt6::Widgets)

add_executable(comic-chat-qt
    app/main.cpp
    app/MainWindow.cpp
    app/ComicWidget.cpp
)
target_link_libraries(comic-chat-qt PRIVATE comic_engine comic_platform comic_net Qt6::Widgets Qt6::Network)
target_compile_definitions(comic-chat-qt PRIVATE
    COMIC_ART_DIR="${COMIC_ART_DIR}"
)

install(TARGETS comic-chat-qt RUNTIME DESTINATION bin)
