message("***** tubus *****")

cmake_minimum_required(VERSION 3.10)

project(tubus)
project(${PROJECT_NAME} VERSION 1.2)

include(GNUInstallDirs)

if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    set(TUBUS_INSTALL_LIBDIR ${PROJECT_NAME}-${${PROJECT_NAME}_VERSION}/${CMAKE_INSTALL_LIBDIR})
    set(TUBUS_INSTALL_CMAKEDIR ${PROJECT_NAME}-${${PROJECT_NAME}_VERSION}/${CMAKE_INSTALL_LIBDIR}/cmake)
    set(TUBUS_INSTALL_INCLUDEDIR ${PROJECT_NAME}-${${PROJECT_NAME}_VERSION}/${CMAKE_INSTALL_INCLUDEDIR})
    set(CMAKE_STATIC_LIBRARY_SUFFIX "-static.lib")
    set(HEADERS buffer.h channel.h socket.h)
    add_definitions(
        -D_CRT_NO_VA_START_VALIDATION
        -D_CRT_SECURE_NO_WARNINGS
        -D_WIN32_WINNT=0x0600
        -D_WINSOCK_DEPRECATED_NO_WARNINGS
        -DINCL_EXTRA_HTON_FUNCTIONS
    )
else()
    set(HEADERS buffer.h channel.h socket.h acceptor.h)
    if(CMAKE_INSTALL_PREFIX MATCHES "^(/usr)|(/usr/local)$")
        if(APPLE)
            set(TUBUS_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
            set(TUBUS_INSTALL_CMAKEDIR ${CMAKE_INSTALL_LIBDIR}/cmake)
            set(TUBUS_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
        elseif(CMAKE_INSTALL_LIBDIR MATCHES ${CMAKE_LIBRARY_ARCHITECTURE} OR CMAKE_INSTALL_PREFIX MATCHES "/usr/local")
            set(TUBUS_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR}/${PROJECT_NAME}-${${PROJECT_NAME}_VERSION})
            set(TUBUS_INSTALL_CMAKEDIR ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}-${${PROJECT_NAME}_VERSION})
            set(TUBUS_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}-${${PROJECT_NAME}_VERSION})
        else()
            set(TUBUS_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR}/${CMAKE_LIBRARY_ARCHITECTURE}/${PROJECT_NAME}-${${PROJECT_NAME}_VERSION})
            set(TUBUS_INSTALL_CMAKEDIR ${CMAKE_INSTALL_LIBDIR}/${CMAKE_LIBRARY_ARCHITECTURE}/cmake/${PROJECT_NAME}-${${PROJECT_NAME}_VERSION})
            set(TUBUS_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}-${${PROJECT_NAME}_VERSION})
        endif()
    else()
        set(TUBUS_INSTALL_LIBDIR ${PROJECT_NAME}/${${PROJECT_NAME}_VERSION}/${CMAKE_INSTALL_LIBDIR})
        set(TUBUS_INSTALL_CMAKEDIR ${PROJECT_NAME}/${${PROJECT_NAME}_VERSION}/${CMAKE_INSTALL_LIBDIR}/cmake)
        set(TUBUS_INSTALL_INCLUDEDIR ${PROJECT_NAME}/${${PROJECT_NAME}_VERSION}/${CMAKE_INSTALL_INCLUDEDIR})
    endif()
endif()

string(REGEX REPLACE "[^/\\]+" ".." TUBUS_INSTALL_CMAKEDIR_REVERSE ${TUBUS_INSTALL_CMAKEDIR})

if(BUILD_TESTING)
    find_package(Boost REQUIRED COMPONENTS system thread unit_test_framework)
else()
    find_package(Boost REQUIRED COMPONENTS system thread)
endif()

message("* Boost Version: ${Boost_VERSION}")
message("* Boost Include Dirs: ${Boost_INCLUDE_DIRS}")
message("* Boost Librariy Dirs: ${Boost_LIBRARY_DIRS}")
message("* Boost Libraries: ${Boost_LIBRARIES}")

include_directories(${Boost_INCLUDE_DIRS})
link_directories(${Boost_LIBRARY_DIRS})

set(TUBUS_OBJECT ${PROJECT_NAME}_object)
set(TUBUS_SHARED ${PROJECT_NAME}_shared)
set(TUBUS_STATIC ${PROJECT_NAME}_static)

if(NOT PROJECT_IS_TOP_LEVEL)
    set(TUBUS_OBJECT ${TUBUS_OBJECT} PARENT_SCOPE)
endif()

add_library(${TUBUS_OBJECT} OBJECT channel.cpp)

if(NOT TUBUS_OBJECT_ONLY)
    add_library(${TUBUS_STATIC} STATIC $<TARGET_OBJECTS:${TUBUS_OBJECT}>)
    set_target_properties(${TUBUS_STATIC} PROPERTIES DEBUG_POSTFIX "d")
    set_target_properties(${TUBUS_STATIC} PROPERTIES OUTPUT_NAME ${PROJECT_NAME})

    add_library(${TUBUS_SHARED} SHARED $<TARGET_OBJECTS:${TUBUS_OBJECT}>)
    target_compile_definitions(${TUBUS_SHARED} PRIVATE TUBUS_EXPORTS)
    set_target_properties(${TUBUS_SHARED} PROPERTIES DEBUG_POSTFIX "d")
    set_target_properties(${TUBUS_SHARED} PROPERTIES OUTPUT_NAME ${PROJECT_NAME})

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -Wall -fPIC")
        set(THREADS_PREFER_PTHREAD_FLAG ON)
        find_package(Threads REQUIRED)
    endif()

    if(APPLE AND Boost_USE_STATIC_LIBS)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden")
    endif()

    install(TARGETS ${TUBUS_STATIC} DESTINATION ${TUBUS_INSTALL_LIBDIR})
    install(TARGETS ${TUBUS_SHARED} DESTINATION ${TUBUS_INSTALL_LIBDIR})

    include(CMakePackageConfigHelpers)

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/tubus-config-version.cmake"
        VERSION "${CMAKE_PROJECT_VERSION}"
        COMPATIBILITY AnyNewerVersion)

    install(FILES ${HEADERS} DESTINATION ${TUBUS_INSTALL_INCLUDEDIR}/${PROJECT_NAME})
    configure_package_config_file(${CMAKE_CURRENT_SOURCE_DIR}/tubus-config.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/tubus-config.cmake"
        INSTALL_DESTINATION ${TUBUS_INSTALL_CMAKEDIR}
        PATH_VARS TUBUS_INSTALL_CMAKEDIR_REVERSE TUBUS_INSTALL_INCLUDEDIR TUBUS_INSTALL_LIBDIR)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/tubus-config.cmake" "${CMAKE_CURRENT_BINARY_DIR}/tubus-config-version.cmake" DESTINATION ${TUBUS_INSTALL_CMAKEDIR})
    if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
        install(FILES $<TARGET_PDB_FILE:${TUBUS_SHARED}> CONFIGURATIONS "Debug" "RelWithDebInfo" DESTINATION ${TUBUS_INSTALL_LIBDIR})
    endif()

    if(BUILD_TESTING)
        find_package(OpenSSL REQUIRED)

        message("* OpenSSL Version: ${OPENSSL_VERSION}")
        message("* OpenSSL Include Dir: ${OPENSSL_INCLUDE_DIR}")
        message("* OpenSSL Libraries: ${OPENSSL_LIBRARIES}")

        set(TESTS_NAME tubus_ut)

        if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
            set(TESTS_SOURCES tests/buffer.cpp tests/packet.cpp tests/tubus.cpp tests/socket.cpp)
        else()
            set(TESTS_SOURCES tests/buffer.cpp tests/packet.cpp tests/tubus.cpp tests/socket.cpp tests/acceptor.cpp)
        endif()

        add_executable(${TESTS_NAME} ${TESTS_SOURCES})
        set_target_properties(${TESTS_NAME} PROPERTIES DEBUG_POSTFIX "d")
        target_link_libraries(${TESTS_NAME} ${TUBUS_OBJECT} OpenSSL::Crypto OpenSSL::SSL)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            target_link_libraries(${TESTS_NAME} Threads::Threads Boost::system Boost::thread Boost::unit_test_framework)
        endif()

        enable_testing()
        add_test(NAME ${TESTS_NAME} COMMAND ${TESTS_NAME} WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    endif()
endif()

message("***** tubus done *****")
