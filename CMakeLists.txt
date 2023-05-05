cmake_minimum_required(VERSION 3.10)
project(tubus VERSION 1.0)

set(SOURCES channel.cpp)

if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
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

find_package(Boost REQUIRED COMPONENTS system thread)

include_directories(${Boost_INCLUDE_DIRS})
link_directories(${Boost_LIBRARY_DIRS})

option(BUILD_SHARED_LIBS "" ON)
if (BUILD_SHARED_LIBS)
   add_definitions(-DTUBUS_EXPORTS)
   add_library(${PROJECT_NAME} SHARED ${SOURCES})
else()
   add_library(${PROJECT_NAME} STATIC ${SOURCES})
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -Wall")
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
    target_link_libraries(${PROJECT_NAME} PUBLIC Threads::Threads)
    target_link_libraries(${PROJECT_NAME} PUBLIC ${Boost_LIBRARIES})
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/tubus-config-version.cmake"
    VERSION "${CMAKE_PROJECT_VERSION}"
    COMPATIBILITY AnyNewerVersion
)

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    install(TARGETS ${PROJECT_NAME} LIBRARY DESTINATION ${PROJECT_NAME}-${${PROJECT_NAME}_VERSION}/lib)
    install(FILES ${HEADERS} DESTINATION ${PROJECT_NAME}-${${PROJECT_NAME}_VERSION}/include)
    configure_package_config_file(${CMAKE_CURRENT_SOURCE_DIR}/tubus-config.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/tubus-config.cmake" INSTALL_DESTINATION ${${PROJECT_NAME}_VERSION}/cmake)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/tubus-config.cmake" "${CMAKE_CURRENT_BINARY_DIR}/tubus-config-version.cmake" DESTINATION ${${PROJECT_NAME}_VERSION}/cmake)
else()
    install(TARGETS ${PROJECT_NAME} LIBRARY DESTINATION lib)
    install(FILES ${HEADERS} DESTINATION include)
    configure_package_config_file(${CMAKE_CURRENT_SOURCE_DIR}/tubus-config.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/tubus-config.cmake" INSTALL_DESTINATION cmake)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/tubus-config.cmake" "${CMAKE_CURRENT_BINARY_DIR}/tubus-config-version.cmake" DESTINATION cmake)
endif()

option(BUILD_TESTING "" OFF)
if(BUILD_TESTING)
    
    find_package(Boost REQUIRED COMPONENTS unit_test_framework)
    find_package(OpenSSL REQUIRED)

    set(TESTS_NAME tubus_ut)
    
    if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
        set(TESTS_SOURCES channel.cpp tests/buffer.cpp tests/packet.cpp tests/tubus.cpp tests/socket.cpp)
    else()
        set(TESTS_SOURCES channel.cpp tests/buffer.cpp tests/packet.cpp tests/tubus.cpp tests/socket.cpp tests/acceptor.cpp)
    endif()

    add_executable(${TESTS_NAME} ${TESTS_SOURCES})
    target_link_libraries(${TESTS_NAME} OpenSSL::Crypto OpenSSL::SSL)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        target_link_libraries(${TESTS_NAME} Threads::Threads)
        target_link_libraries(${TESTS_NAME} ${Boost_LIBRARIES})
    endif()

    enable_testing()
    add_test(NAME ${TESTS_NAME} COMMAND ${TESTS_NAME} WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)
endif()
