cmake_minimum_required(VERSION 3.25)
project(RacecabFrontend VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SDL3 CONFIG REQUIRED)
find_package(SDL3_image CONFIG REQUIRED)
find_package(SDL3_ttf CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(FFMPEG REQUIRED)
find_package(ZLIB REQUIRED)

set(SOURCES
    src/main.cpp
    src/core/Util.cpp
    src/core/Log.cpp
    src/core/Settings.cpp
    src/core/Backup.cpp
    src/core/ConfigShare.cpp
    src/core/System.cpp
    src/library/Database.cpp
    src/library/Library.cpp
    src/theme/Theme.cpp
    src/render/TextureCache.cpp
    src/render/Text.cpp
    src/render/Recorder.cpp
    src/render/Shapes.cpp
    src/ui/Wheel.cpp
    src/input/Input.cpp
    src/launcher/Launcher.cpp
    src/admin/AdminUI.cpp
    src/app/App.cpp
    src/net/Http.cpp
    src/media/Scraper.cpp
    src/media/Providers.cpp
    src/media/LaunchBox.cpp
    src/media/Zip.cpp
    src/media/YtVideo.cpp
    src/video/VideoPlayer.cpp
    src/video/Remux.cpp
    src/video/CropDetect.cpp
    src/profiles/Profile.cpp
)

add_executable(RacecabFrontend WIN32 ${SOURCES})
target_include_directories(RacecabFrontend PRIVATE src ${FFMPEG_INCLUDE_DIRS})
target_link_directories(RacecabFrontend PRIVATE ${FFMPEG_LIBRARY_DIRS})
target_compile_definitions(RacecabFrontend PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
target_compile_options(RacecabFrontend PRIVATE /utf-8 /W3 /permissive- /MP)
target_link_libraries(RacecabFrontend PRIVATE
    SDL3::SDL3
    $<IF:$<TARGET_EXISTS:SDL3_image::SDL3_image-shared>,SDL3_image::SDL3_image-shared,SDL3_image::SDL3_image-static>
    SDL3_ttf::SDL3_ttf
    unofficial::sqlite3::sqlite3
    nlohmann_json::nlohmann_json
    imgui::imgui
    ${FFMPEG_LIBRARIES}
    ZLIB::ZLIB
    psapi shell32 winhttp
)

# Outil de génération d'une vidéo de test (Media Foundation, intégré à Windows)
add_executable(make_test_video tools/make_test_video.cpp)
target_compile_definitions(make_test_video PRIVATE UNICODE _UNICODE)

# Tests automatisés des entrées (événements simulés, sans matériel)
enable_testing()
add_executable(input_tests tests/InputTests.cpp src/input/Input.cpp src/core/Util.cpp src/core/Log.cpp src/core/Settings.cpp)
target_include_directories(input_tests PRIVATE src)
target_compile_definitions(input_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
target_compile_options(input_tests PRIVATE /utf-8 /W3 /permissive-)
target_link_libraries(input_tests PRIVATE SDL3::SDL3 nlohmann_json::nlohmann_json)
add_test(NAME input_tests COMMAND input_tests)

# Tests de la récupération des médias (services réels sans compte + serveur de simulation local)
add_executable(media_tests tests/MediaTests.cpp src/media/Scraper.cpp src/media/Providers.cpp src/media/LaunchBox.cpp src/media/Zip.cpp src/media/YtVideo.cpp src/video/Remux.cpp src/video/CropDetect.cpp src/net/Http.cpp src/core/Util.cpp src/core/Log.cpp)
target_include_directories(media_tests PRIVATE src ${FFMPEG_INCLUDE_DIRS})
target_link_directories(media_tests PRIVATE ${FFMPEG_LIBRARY_DIRS})
target_compile_definitions(media_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
target_compile_options(media_tests PRIVATE /utf-8 /W3 /permissive-)
target_link_libraries(media_tests PRIVATE SDL3::SDL3 nlohmann_json::nlohmann_json unofficial::sqlite3::sqlite3 ZLIB::ZLIB winhttp ${FFMPEG_LIBRARIES})

# Tests de la sauvegarde / restauration et des archives ZIP
add_executable(system_tests tests/SystemTests.cpp src/core/Backup.cpp src/core/ConfigShare.cpp src/library/Library.cpp src/library/Database.cpp src/media/Zip.cpp src/core/Util.cpp src/core/Log.cpp src/core/Settings.cpp)
target_include_directories(system_tests PRIVATE src)
target_compile_definitions(system_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
target_compile_options(system_tests PRIVATE /utf-8 /W3 /permissive-)
target_link_libraries(system_tests PRIVATE nlohmann_json::nlohmann_json unofficial::sqlite3::sqlite3 ZLIB::ZLIB)
add_test(NAME system_tests COMMAND system_tests)
