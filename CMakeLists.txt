cmake_minimum_required(VERSION 3.16)

project(HTTPServer
    VERSION     1.0.0
    DESCRIPTION "High-Performance Multi-threaded HTTP Server in C++17"
    LANGUAGES   CXX
)

# ── Language standard ────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD          17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

# ── Default build type ───────────────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type (Release|Debug)" FORCE)
endif()

# ── Source files ─────────────────────────────────────────────────────────────
set(SERVER_SOURCES
    src/main.cpp
    src/server.cpp
    src/socket.cpp
    src/parser.cpp
    src/router.cpp
    src/logger.cpp
    src/threadpool.cpp
    src/config.cpp
)

# ── Executable ───────────────────────────────────────────────────────────────
add_executable(HTTPServer ${SERVER_SOURCES})
target_include_directories(HTTPServer PRIVATE include)

# ── Compiler warnings ────────────────────────────────────────────────────────
# -Wconversion is intentionally omitted: POSIX APIs mix ssize_t/size_t freely
# and the resulting noise obscures real warnings.
target_compile_options(HTTPServer PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wnull-dereference
    -Wdouble-promotion
    -Wformat=2
    $<$<CONFIG:Release>:-O2>
    $<$<CONFIG:Debug>:-O0 -g -fsanitize=address,undefined>
)

# ── Linker flags (sanitisers require both compile and link flags) ─────────────
target_link_options(HTTPServer PRIVATE
    $<$<CONFIG:Debug>:-fsanitize=address,undefined>
)

# ── Dependencies ─────────────────────────────────────────────────────────────
find_package(Threads REQUIRED)
target_link_libraries(HTTPServer PRIVATE Threads::Threads)

# GCC < 9 requires explicit linking of std::filesystem
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND
   CMAKE_CXX_COMPILER_VERSION VERSION_LESS "9.0")
    target_link_libraries(HTTPServer PRIVATE stdc++fs)
endif()

# ── Tests ────────────────────────────────────────────────────────────────────
enable_testing()

# Smoke test: binary exists and handles a missing config gracefully
add_test(NAME SmokeTest_MissingConfig
    COMMAND ${CMAKE_BINARY_DIR}/HTTPServer /tmp/nonexistent_config_xyz.json
)
set_tests_properties(SmokeTest_MissingConfig PROPERTIES
    PASS_REGULAR_EXPRESSION "Warning"
    TIMEOUT 5
)

# ── Install ──────────────────────────────────────────────────────────────────
install(TARGETS HTTPServer RUNTIME DESTINATION bin)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/public/ DESTINATION share/HTTPServer/public)
install(FILES     ${CMAKE_SOURCE_DIR}/config.json DESTINATION share/HTTPServer)

# ── Summary ──────────────────────────────────────────────────────────────────
message(STATUS "────────────────────────────────────────────")
message(STATUS " Project  : ${PROJECT_NAME} ${PROJECT_VERSION}")
message(STATUS " Build    : ${CMAKE_BUILD_TYPE}")
message(STATUS " Compiler : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS " C++      : ${CMAKE_CXX_STANDARD}")
message(STATUS "────────────────────────────────────────────")
