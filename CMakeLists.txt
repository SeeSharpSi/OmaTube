cmake_minimum_required(VERSION 3.21)

project(yt_client VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.8 REQUIRED COMPONENTS
    Core
    Gui
    Network
    Quick
    QuickControls2
    Sql
)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_library(ytclient_core STATIC)

target_sources(ytclient_core PRIVATE
    src/domain.h
    src/models/categorymodel.cpp
    src/models/categorymodel.h
    src/models/channelmodel.cpp
    src/models/channelmodel.h
    src/models/feedmodel.cpp
    src/models/feedmodel.h
    src/models/livechannelmodel.cpp
    src/models/livechannelmodel.h
    src/refreshservice.cpp
    src/refreshservice.h
    src/repository.cpp
    src/repository.h
    src/youtubeclient.cpp
    src/youtubeclient.h
)

target_include_directories(ytclient_core PUBLIC src)

target_link_libraries(ytclient_core PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Sql
)

qt_add_executable(yt-client
    src/main.cpp
)

qt_add_qml_module(yt-client
    URI YtClient
    VERSION 1.0
    SOURCES
        src/appcontroller.cpp
        src/appcontroller.h
    QML_FILES
        qml/AddChannelDialog.qml
        qml/Main.qml
        qml/ManageDialog.qml
        qml/SettingsDialog.qml
)

target_link_libraries(yt-client PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Sql
    ytclient_core
)

set_target_properties(yt-client PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "YT Client"
    MACOSX_BUNDLE_GUI_IDENTIFIER "dev.ytclient.app"
    WIN32_EXECUTABLE TRUE
)

include(CTest)

if(BUILD_TESTING)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Test)

    qt_add_executable(repository_tests
        tests/repository_test.cpp
    )
    target_link_libraries(repository_tests PRIVATE
        Qt6::Test
        ytclient_core
    )
    add_test(NAME repository_tests COMMAND repository_tests)

    qt_add_executable(youtube_tests
        tests/youtube_test.cpp
    )
    target_link_libraries(youtube_tests PRIVATE
        Qt6::Test
        ytclient_core
    )
    add_test(NAME youtube_tests COMMAND youtube_tests)

    add_test(NAME qml_smoke_test COMMAND yt-client --quit-after-startup)
    set_tests_properties(qml_smoke_test PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 10
    )
endif()
