message("***** tubus *****")

cmake_minimum_required(VERSION 3.10)
project(tubus VERSION 1.0)

set(SOURCES channel.cpp)

if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
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
endif()

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

if(BUILD_SHARED_LIBS)
   add_definitions(-DTUBUS_EXPORTS)
   add_library(${PROJECT_NAME} SHARED ${SOURCES})
else()
   add_library(${PROJECT_NAME} STATIC ${SOURCES})
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES DEBUG_POSTFIX "d")

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -Wall")
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
    target_link_libraries(${PROJECT_NAME} PUBLIC Threads::Threads)
    target_link_libraries(${PROJECT_NAME} PUBLIC Boost::system Boost::thread)
endif()

if(APPLE AND Boost_USE_STATIC_LIBS)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden")
endif()

if(NOT tubus_SKIP_INSTALL_TARGET)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/tubus-config-version.cmake"
        VERSION "${CMAKE_PROJECT_VERSION}"
        COMPATIBILITY AnyNewerVersion)

    if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
        install(TARGETS ${PROJECT_NAME} DESTINATION ${PROJECT_NAME}-${${PROJECT_NAME}_VERSION}/lib)
        install(FILES ${HEADERS} DESTINATION ${PROJECT_NAME}-${${PROJECT_NAME}_VERSION}/include)
        configure_package_config_file(${CMAKE_CURRENT_SOURCE_DIR}/tubus-config.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/tubus-config.cmake" INSTALL_DESTINATION ${PROJECT_NAME}-${${PROJECT_NAME}_VERSION}/cmake)
        install(FILES "${CMAKE_CURRENT_BINARY_DIR}/tubus-config.cmake" "${CMAKE_CURRENT_BINARY_DIR}/tubus-config-version.cmake" DESTINATION ${PROJECT_NAME}-${${PROJECT_NAME}_VERSION}/cmake)
        if(BUILD_SHARED_LIBS AND CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
            install(FILES $<TARGET_PDB_FILE:${PROJECT_NAME}> CONFIGURATIONS "RelWithDebInfo" DESTINATION ${PROJECT_NAME}-${${PROJECT_NAME}_VERSION}/lib)
        endif()
    else()
        install(TARGETS ${PROJECT_NAME} DESTINATION lib)
        install(FILES ${HEADERS} DESTINATION include)
        configure_package_config_file(${CMAKE_CURRENT_SOURCE_DIR}/tubus-config.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/tubus-config.cmake" INSTALL_DESTINATION cmake)
        install(FILES "${CMAKE_CURRENT_BINARY_DIR}/tubus-config.cmake" "${CMAKE_CURRENT_BINARY_DIR}/tubus-config-version.cmake" DESTINATION cmake)
        if(BUILD_SHARED_LIBS AND CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
            install(FILES $<TARGET_PDB_FILE:${PROJECT_NAME}> CONFIGURATIONS "RelWithDebInfo" DESTINATION lib)
        endif()
    endif()
endif()

if(BUILD_TESTING)
    find_package(OpenSSL REQUIRED)
    
    message("* OpenSSL Version: ${OPENSSL_VERSION}")
    message("* OpenSSL Include Dir: ${OPENSSL_INCLUDE_DIR}")
    message("* OpenSSL Libraries: ${OPENSSL_LIBRARIES}")

    set(TESTS_NAME tubus_ut)

    if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
        set(TESTS_SOURCES channel.cpp tests/buffer.cpp tests/packet.cpp tests/tubus.cpp tests/socket.cpp)
    else()
        set(TESTS_SOURCES channel.cpp tests/buffer.cpp tests/packet.cpp tests/tubus.cpp tests/socket.cpp tests/acceptor.cpp)
    endif()

    add_executable(${TESTS_NAME} ${TESTS_SOURCES})
    target_link_libraries(${TESTS_NAME} OpenSSL::Crypto OpenSSL::SSL)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_link_libraries(${TESTS_NAME} Threads::Threads)
        target_link_libraries(${TESTS_NAME} Boost::system Boost::thread Boost::unit_test_framework)
    endif()

    enable_testing()
    add_test(NAME ${TESTS_NAME} COMMAND ${TESTS_NAME} WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)
endif()

message("***** tubus done *****")
