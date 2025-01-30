message("***** tubus *****")

##############################################################################################################

cmake_minimum_required(VERSION 3.21)

project(tubus VERSION 1.2.2 DESCRIPTION "Self obfuscated streaming transport protocol based on UDP" LANGUAGES CXX)

include(GNUInstallDirs)

option(TUBUS_BUILD_TESTS "Build tubus tests" OFF)
option(TUBUS_INSTALL "Generate target for installing tubus" ${PROJECT_IS_TOP_LEVEL})

if(DEFINED tubus_USE_SHARED_LIBS)
    set(BUILD_SHARED_LIBS ${tubus_USE_SHARED_LIBS})
endif()

##############################################################################################################

if(TUBUS_BUILD_TESTS)
    find_package(OpenSSL REQUIRED)

    message("* OpenSSL Version: ${OPENSSL_VERSION}")
    message("* OpenSSL Include Dir: ${OPENSSL_INCLUDE_DIR}")
    message("* OpenSSL Libraries: ${OPENSSL_LIBRARIES}")

    find_package(Boost REQUIRED COMPONENTS system unit_test_framework)
else()
    find_package(Boost REQUIRED COMPONENTS system)
endif()

message("* Boost Version: ${Boost_VERSION}")
message("* Boost Include Dirs: ${Boost_INCLUDE_DIRS}")
message("* Boost Librariy Dirs: ${Boost_LIBRARY_DIRS}")
message("* Boost Libraries: ${Boost_LIBRARIES}")

##############################################################################################################

add_library(${PROJECT_NAME})
add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

include(GenerateExportHeader)
set(EXPORT_FILE "export_shared.h")

if(NOT BUILD_SHARED_LIBS)
    set(EXPORT_FILE "export_static.h")
endif()

generate_export_header(${PROJECT_NAME} EXPORT_FILE_NAME ${PROJECT_NAME}/${EXPORT_FILE})

set(SOURCES
    ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}/${EXPORT_FILE}
    src/tubus/channel.h
    src/tubus/buffer.h
    src/tubus/packet.h
    src/tubus/export.h
    src/tubus/channel.cpp
    )

if(MSVC)
    set(HEADERS
        ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}/${EXPORT_FILE}
        src/tubus/export.h
        src/tubus/channel.h
        src/tubus/buffer.h
        src/tubus/socket.h
        )
    set(TUBUS_INSTALL_LIBDIR ${PROJECT_NAME}-${${PROJECT_NAME}_VERSION}/${CMAKE_INSTALL_LIBDIR})
    set(TUBUS_INSTALL_CMAKEDIR ${PROJECT_NAME}-${${PROJECT_NAME}_VERSION}/${CMAKE_INSTALL_LIBDIR}/cmake)
    set(TUBUS_INSTALL_INCLUDEDIR ${PROJECT_NAME}-${${PROJECT_NAME}_VERSION}/${CMAKE_INSTALL_INCLUDEDIR}/)
    add_definitions(-D_WIN32_WINNT=0x0601 -DINCL_EXTRA_HTON_FUNCTIONS)
else()
    set(HEADERS
        ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}/${EXPORT_FILE}
        src/tubus/export.h
        src/tubus/channel.h
        src/tubus/buffer.h
        src/tubus/socket.h
        src/tubus/acceptor.h
    )
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

if(NOT DEFINED CMAKE_BUILD_TYPE AND NOT DEFINED CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

if(NOT DEFINED CMAKE_CXX_VISIBILITY_PRESET)
    set(CMAKE_CXX_VISIBILITY_PRESET hidden)
endif()
if(NOT DEFINED CMAKE_VISIBILITY_INLINES_HIDDEN)
    set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES DEBUG_POSTFIX "d")

include(CMakePackageConfigHelpers)

target_sources(${PROJECT_NAME} PRIVATE ${SOURCES})
target_compile_definitions(${PROJECT_NAME} PUBLIC "$<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:TUBUS_STATIC_DEFINE>")

target_include_directories(${PROJECT_NAME} PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>"
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>"
        PRIVATE ${Boost_INCLUDE_DIRS})

set_target_properties(${PROJECT_NAME} PROPERTIES SOVERSION ${PROJECT_VERSION_MAJOR} VERSION ${PROJECT_VERSION})

##############################################################################################################

if(TUBUS_INSTALL AND NOT CMAKE_SKIP_INSTALL_RULES)
    configure_package_config_file(cmake/tubus-config.cmake.in tubus-config.cmake INSTALL_DESTINATION "${TUBUS_INSTALL_CMAKEDIR}")

    write_basic_package_version_file(tubus-config-version.cmake COMPATIBILITY SameMajorVersion)

    install(TARGETS tubus EXPORT tubus_export
        RUNTIME DESTINATION ${TUBUS_INSTALL_LIBDIR} 
        LIBRARY DESTINATION ${TUBUS_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${TUBUS_INSTALL_LIBDIR}
        INCLUDES DESTINATION ${TUBUS_INSTALL_INCLUDEDIR})

    set(TARGET_FILE "tubus-shared-targets.cmake")

    if(NOT BUILD_SHARED_LIBS)
        set(TARGET_FILE "tubus-static-targets.cmake")
    endif()

    install(EXPORT tubus_export
        FILE "${TARGET_FILE}"
        DESTINATION "${TUBUS_INSTALL_CMAKEDIR}"
        NAMESPACE tubus::)

    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/tubus-config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/tubus-config-version.cmake"
        DESTINATION "${TUBUS_INSTALL_CMAKEDIR}")

    install(FILES ${HEADERS} DESTINATION ${TUBUS_INSTALL_INCLUDEDIR})

    if(MSVC)
        set(PDB_FILE "")
        set(PDB_FILE_DEST "")

        if(BUILD_SHARED_LIBS)
            set(PDB_FILE "$<TARGET_PDB_FILE:${PROJECT_NAME}>")
            set(PDB_FILE_DEST "${TUBUS_INSTALL_LIBDIR}")
        else()
            set(PDB_FILE "$<TARGET_FILE_DIR:${PROJECT_NAME}>/$<TARGET_FILE_PREFIX:${PROJECT_NAME}>$<TARGET_FILE_BASE_NAME:${PROJECT_NAME}>-static.pdb")
            set(PDB_FILE_DEST "${TUBUS_INSTALL_LIBDIR}")
        endif()

        install(FILES "${PDB_FILE}"
            CONFIGURATIONS Debug RelWithDebInfo
            DESTINATION "${PDB_FILE_DEST}"
            OPTIONAL)
    endif()
endif()

##############################################################################################################

if(TUBUS_BUILD_TESTS)
    set(TESTS_NAME tubus_ut)

    if(MSVC)
        set(TESTS_SOURCES tests/buffer.cpp tests/packet.cpp tests/tubus.cpp tests/socket.cpp)
    else()
        set(TESTS_SOURCES tests/buffer.cpp tests/packet.cpp tests/tubus.cpp tests/socket.cpp tests/acceptor.cpp)
    endif()

    add_executable(${TESTS_NAME} ${TESTS_SOURCES})
    set_target_properties(${TESTS_NAME} PROPERTIES DEBUG_POSTFIX "d")
    target_link_libraries(${TESTS_NAME} PRIVATE ${PROJECT_NAME} OpenSSL::Crypto OpenSSL::SSL Boost::unit_test_framework)

    if(WIN32)
        target_link_libraries(${TESTS_NAME} PRIVATE Crypt32)
    endif()

    if(NOT MSVC)
        target_link_libraries(${TESTS_NAME} PRIVATE Boost::system)
    endif()

    enable_testing()
    add_test(NAME ${TESTS_NAME} COMMAND ${TESTS_NAME} WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)
endif()

##############################################################################################################

message("***** tubus done *****")
