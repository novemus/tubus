message("***** tubus ***** ")

########## setup ##########

cmake_minimum_required(VERSION 3.21)

project(tubus VERSION 1.2.5 DESCRIPTION "Self obfuscated streaming transport protocol based on UDP" LANGUAGES CXX)

option(TUBUS_USE_SHARED_LIB "Build shared library" ${BUILD_SHARED_LIBS})
option(TUBUS_SKIP_TEST_RULES "Don't generate test rules" ON)
option(TUBUS_SKIP_INSTALL_RULES "Don't generate install rules" OFF)
option(TUBUS_SKIP_PACKAGE_RULES "Don't generate package rules" ON)

if (TUBUS_USE_SHARED_LIB)
    set(BUILD_SHARED_LIBS ON)
endif()

########## dependencies ##########

if (NOT MSVC AND NOT BUILD_SHARED_LIBS)
    set(Boost_USE_STATIC_LIBS ON)
endif()

if(NOT TUBUS_SKIP_TEST_RULES)
    find_package(OpenSSL REQUIRED)

    message("* OpenSSL Version: ${OPENSSL_VERSION}")
    message("* OpenSSL Include Dir: ${OPENSSL_INCLUDE_DIR}")
    message("* OpenSSL Libraries: ${OPENSSL_LIBRARIES}")

    find_package(Boost 1.74 CONFIG REQUIRED COMPONENTS system unit_test_framework)
else()
    find_package(Boost 1.74 CONFIG REQUIRED COMPONENTS system)
endif()

message("* Boost Version: ${Boost_VERSION}")
message("* Boost Include Dirs: ${Boost_INCLUDE_DIRS}")
message("* Boost Library Dirs: ${Boost_LIBRARY_DIRS}")
message("* Boost Libraries: ${Boost_LIBRARIES}")

########## build ##########

include(GenerateExportHeader)

set(LIBTUBUS lib${PROJECT_NAME})

add_library(${LIBTUBUS})
add_library(${PROJECT_NAME}::${LIBTUBUS} ALIAS ${LIBTUBUS})

generate_export_header(${LIBTUBUS} EXPORT_FILE_NAME export_header/tubus/export.h)

if (NOT BUILD_SHARED_LIBS)
    target_compile_definitions(${LIBTUBUS} PUBLIC LIBTUBUS_STATIC_DEFINE)
endif()

set(SOURCES "${CMAKE_CURRENT_BINARY_DIR}/export_header/tubus/export.h"
    src/tubus/utils.h
    src/tubus/utils.cpp
    src/tubus/channel.h
    src/tubus/buffer.h
    src/tubus/udp/proto.h
    src/tubus/tcp/proto.h
    src/tubus/udp/channel.cpp
    src/tubus/tcp/channel.cpp
    )

set(HEADERS "${CMAKE_CURRENT_BINARY_DIR}/export_header/tubus/export.h"
    src/tubus/channel.h
    src/tubus/buffer.h
    src/tubus/socket.h
    )

if(NOT MSVC)
    list(APPEND HEADERS src/tubus/acceptor.h)
else()
    set(CMAKE_STATIC_LIBRARY_PREFIX "lib")
    set(CMAKE_SHARED_LIBRARY_PREFIX "lib")
    set(CMAKE_STATIC_LIBRARY_SUFFIX "-static.lib")
    add_definitions(-D_WIN32_WINNT=0x0601 -DINCL_EXTRA_HTON_FUNCTIONS)
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

target_sources(${LIBTUBUS} PRIVATE ${SOURCES})
target_include_directories(${LIBTUBUS} PUBLIC 
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/export_header>"
    PRIVATE "${Boost_INCLUDE_DIRS}")

set_target_properties(${LIBTUBUS} PROPERTIES DEBUG_POSTFIX "d" OUTPUT_NAME ${PROJECT_NAME} IMPORT_PREFIX "lib")
set_target_properties(${LIBTUBUS} PROPERTIES SOVERSION ${PROJECT_VERSION_MAJOR} VERSION ${PROJECT_VERSION})
set_target_properties(${LIBTUBUS} PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)

target_compile_features(${LIBTUBUS} PRIVATE cxx_std_17)

########## install ##########

if(NOT TUBUS_SKIP_INSTALL_RULES AND NOT CMAKE_SKIP_INSTALL_RULES)

    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    set(LIBTUBUS_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}")

    configure_package_config_file(cmake/tubus-config.cmake.in tubus-config.cmake INSTALL_DESTINATION "${LIBTUBUS_INSTALL_CMAKEDIR}")

    write_basic_package_version_file(tubus-config-version.cmake COMPATIBILITY SameMajorVersion)

    install(TARGETS ${LIBTUBUS} EXPORT tubus_export
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT runtime_files
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT library_files
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT archive_files
        INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

    set(TARGET_FILE tubus-shared-targets.cmake)

    if(NOT BUILD_SHARED_LIBS)
        set(TARGET_FILE tubus-static-targets.cmake)
    endif()

    install(EXPORT tubus_export COMPONENT cmake_files
        FILE ${TARGET_FILE}
        DESTINATION "${LIBTUBUS_INSTALL_CMAKEDIR}"
        NAMESPACE tubus::)

    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/tubus-config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/tubus-config-version.cmake"
        COMPONENT cmake_files
        DESTINATION "${LIBTUBUS_INSTALL_CMAKEDIR}")

    install(FILES ${HEADERS} DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}" COMPONENT header_files)

    if(MSVC)
        if(BUILD_SHARED_LIBS)
            install(FILES "$<TARGET_PDB_FILE:${LIBTUBUS}>" COMPONENT pdb_files
                CONFIGURATIONS Debug RelWithDebInfo
                DESTINATION "${CMAKE_INSTALL_LIBDIR}"
                OPTIONAL)
        else()
            install(FILES "$<TARGET_FILE_DIR:${LIBTUBUS}>/$<TARGET_FILE_PREFIX:${LIBTUBUS}>$<TARGET_FILE_BASE_NAME:${LIBTUBUS}>-static.pdb" COMPONENT pdb_files
                CONFIGURATIONS Debug RelWithDebInfo
                DESTINATION "${CMAKE_INSTALL_LIBDIR}"
                OPTIONAL)
        endif()
    endif()
endif()

########## tests ##########

if(NOT TUBUS_SKIP_TEST_RULES)
    set(TUBUS_TEST tubus_ut)

    if(MSVC)
        set(TESTS_SOURCES tests/buffer.cpp tests/proto.cpp tests/tubus.cpp tests/socket.cpp)
    else()
        set(TESTS_SOURCES tests/buffer.cpp tests/proto.cpp tests/tubus.cpp tests/socket.cpp tests/acceptor.cpp)
    endif()

    add_executable(${TUBUS_TEST} ${TESTS_SOURCES})
    target_link_libraries(${TUBUS_TEST} PRIVATE $<TARGET_OBJECTS:${LIBTUBUS}> OpenSSL::Crypto OpenSSL::SSL Boost::unit_test_framework)
    target_include_directories(${TUBUS_TEST} PRIVATE "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>" "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/export_header>")

    set_target_properties(${TUBUS_TEST} PROPERTIES DEBUG_POSTFIX "d")
    target_compile_features(${TUBUS_TEST} PRIVATE cxx_std_17)

    if(WIN32)
        target_link_libraries(${TUBUS_TEST} PRIVATE Crypt32)
    endif()

    enable_testing()
    add_test(NAME ${TUBUS_TEST} COMMAND ${TUBUS_TEST} WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/tests")
endif()

########## package ##########

if(NOT TUBUS_SKIP_PACKAGE_RULES AND NOT TUBUS_SKIP_INSTALL_RULES AND NOT CMAKE_SKIP_INSTALL_RULES)
    if(NOT CPACK_PACKAGE_RELEASE)
        set(CPACK_PACKAGE_RELEASE "1")
    endif()

    set(DEVEL_DOC_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
        "${CMAKE_CURRENT_SOURCE_DIR}/CHANGELOG.md"
        )

    if(CMAKE_HOST_SYSTEM_NAME MATCHES "Linux")
        set(CPACK_COMPONENTS_ALL_IN_ONE_PACKAGE ON)
        cmake_host_system_information(RESULT DISTR QUERY DISTRIB_ID)
        if(DISTR MATCHES "(debian)|(ubuntu)|(kali)|(astra)")
            cmake_host_system_information(RESULT DISTR_CODENAME QUERY DISTRIB_VERSION_CODENAME)
            set(CPACK_DEBIAN_PACKAGE_RELEASE "${CPACK_PACKAGE_RELEASE}+${DISTR_CODENAME}")
            set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
            set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
            set(CPACK_DEBIAN_PACKAGE_GENERATE_SHLIBS ON)
            set(CPACK_DEBIAN_DEBUGINFO_PACKAGE ON)
            set(CPACK_DEB_COMPONENT_INSTALL ON)

            set(DEBIAN_DOC_FILES
                "${CMAKE_CURRENT_SOURCE_DIR}/linux/copyright"
                "${CMAKE_CURRENT_SOURCE_DIR}/linux/README.Debian"
            )
            if(BUILD_SHARED_LIBS)
                install(FILES ${DEBIAN_DOC_FILES} DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/${LIBTUBUS}" COMPONENT lib_deb_files)
            else()
                install(FILES ${DEBIAN_DOC_FILES} DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/${LIBTUBUS}-dev" COMPONENT dev_deb_files)
                install(FILES ${DEVEL_DOC_FILES} DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/${LIBTUBUS}-dev" COMPONENT dev_doc_files)
            endif()
        else()
            cmake_host_system_information(RESULT DISTR_VERSION QUERY DISTRIB_VERSION_ID)
            if (DISTR MATCHES "fedora")
                set(CPACK_RPM_PACKAGE_RELEASE "${CPACK_PACKAGE_RELEASE}.fc${DISTR_VERSION}")
            elseif(DISTRO_ID MATCHES "rhel|centos|almalinux|rocky")
                string(REGEX REPLACE "\\." "" DISTR_VERSION "${DISTR_VERSION}")
                set(CPACK_RPM_PACKAGE_RELEASE "${CPACK_PACKAGE_RELEASE}.el${DISTR_VERSION}")
            else()
                set(CPACK_RPM_PACKAGE_RELEASE "${CPACK_PACKAGE_RELEASE}")
            endif()

            if(NOT BUILD_SHARED_LIBS)
                install(FILES ${DEVEL_DOC_FILES} DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/${LIBTUBUS}-devel" COMPONENT dev_doc_files)
            endif()

            set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
            set(CPACK_RPM_PACKAGE_GROUP "Development/Libraries")
            set(CPACK_RPM_PACKAGE_LICENSE "Apache 2.0")
            set(CPACK_RPM_COMPONENT_INSTALL ON)
            set(CPACK_RPM_PACKAGE_AUTOREQ "1")
            set(CPACK_RPM_BUILD_SOURCE_DIRS_PREFIX "/src/tubus")
        endif()
    elseif(NOT BUILD_SHARED_LIBS)
        install(FILES ${DEVEL_DOC_FILES} DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/${LIBTUBUS}" COMPONENT dev_doc_files)
    endif()

    string(TOLOWER "${PROJECT_NAME}-${PROJECT_VERSION}-${CPACK_PACKAGE_RELEASE}-${CMAKE_HOST_SYSTEM_NAME}-${CMAKE_HOST_SYSTEM_PROCESSOR}" CPACK_ARCHIVE_FILE_NAME)

    include(CPack)
endif()
