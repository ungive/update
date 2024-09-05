cmake_minimum_required(VERSION 3.8)
project(ungive_update)

option(LIBUPDATE_BUILD_TESTS "Build unit tests" OFF)

# Full ungive_update library with updater and manager dependencies.
add_library(ungive_update INTERFACE)
# Partial library with only manager dependencies.
add_library(ungive_update_manager INTERFACE)

target_include_directories(ungive_update INTERFACE ./include)
target_include_directories(ungive_update_manager INTERFACE ./include)

target_include_directories(ungive_update
                           INTERFACE third_party/yhirose+cpp-httplib/include)
target_compile_definitions(ungive_update INTERFACE CPPHTTPLIB_OPENSSL_SUPPORT)
target_compile_definitions(ungive_update INTERFACE UPDATE_WITH_GITHUB)
target_include_directories(ungive_update
                           INTERFACE third_party/nlohmann+json/include)
find_package(OpenSSL REQUIRED)
target_link_libraries(ungive_update INTERFACE OpenSSL::SSL OpenSSL::Crypto)

# minizip on windows for extracting release files
if(WIN32)
    find_package(ZLIB REQUIRED)
    target_link_libraries(ungive_update INTERFACE ZLIB::ZLIB)
    set(MZ_ZLIB ON CACHE BOOL "" FORCE)
    set(MZ_DECOMPRESS_ONLY ON CACHE BOOL "" FORCE)
    set(MZ_COMPAT OFF CACHE BOOL "" FORCE)
    set(MZ_BZIP2 OFF CACHE BOOL "" FORCE)
    set(MZ_LZMA OFF CACHE BOOL "" FORCE)
    set(MZ_ZSTD OFF CACHE BOOL "" FORCE)
    set(MZ_FETCH_LIBS OFF CACHE BOOL "" FORCE)
    set(MZ_LIBCOMP OFF CACHE BOOL "" FORCE)
    set(MZ_PKCRYPT OFF CACHE BOOL "" FORCE)
    set(MZ_WZAES OFF CACHE BOOL "" FORCE)
    add_subdirectory(third_party/zlib-ng+minizip-ng)
    target_link_libraries(ungive_update INTERFACE MINIZIP::minizip)
endif()

set_property(TARGET ungive_update PROPERTY CXX_STANDARD 17)
set_property(TARGET ungive_update PROPERTY CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(ungive_update INTERFACE LIBUPDATE_TEST_BUILD)
endif()

if(LIBUPDATE_BUILD_TESTS)
    # Prevent overriding the parent project's compiler/linker settings (win)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    add_subdirectory(third_party/google+googletest EXCLUDE_FROM_ALL)
    include(GoogleTest)
    enable_testing()
    set(TEST_SOURCES test/update_test.cpp test/common_test.cpp
                     test/integration_test.cpp)
    if(WIN32)
        list(APPEND TEST_SOURCES test/win_test.cpp)
    endif()

    add_executable(ungive_update_test ${TEST_SOURCES})
    target_compile_definitions(ungive_update_test PRIVATE LIBUPDATE_TEST_BUILD)
    target_link_libraries(ungive_update_test ungive_update)
    target_link_libraries(ungive_update_test GTest::gmock_main)
    target_include_directories(
        ungive_update_test PRIVATE ${gtest_SOURCE_DIR}/include
                                   ${gtest_SOURCE_DIR})
    # We need to be able to parse JSON in tests.
    target_include_directories(ungive_update_test
                               INTERFACE third_party/nlohmann+json/include)

    set_property(TARGET ungive_update_test PROPERTY CXX_STANDARD 17)
    set_property(TARGET ungive_update_test PROPERTY CMAKE_CXX_STANDARD_REQUIRED
                                                    ON)

    # add_custom_command(
    #     TARGET ungive_update_test
    #     POST_BUILD
    #     COMMAND
    #         ${CMAKE_COMMAND} -E copy_if_different
    #         $<TARGET_RUNTIME_DLLS:ungive_update_test>
    #         $<TARGET_FILE_DIR:ungive_update_test>
    #     COMMAND_EXPAND_LISTS)

    add_test(ungive_update_test ungive_update_test)
    gtest_discover_tests(ungive_update_test)
endif()
