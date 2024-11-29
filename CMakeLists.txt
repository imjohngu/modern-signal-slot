cmake_minimum_required(VERSION 3.5)

project(SigSlot LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (WIN32)
    add_definitions("-DCORE_WIN -DCORE_HAVE_THREAD_LOCAL")
elseif (APPLE)
    add_definitions("-DCORE_POSIX -DCORE_HAVE_THREAD_LOCAL")
else()
    add_definitions("-DCORE_POSIX -DCORE_HAVE_THREAD_LOCAL")
endif()

# Add GoogleTest
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG release-1.12.1
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# Source files
file(GLOB_RECURSE SRC_FILES 
    ${CMAKE_CURRENT_SOURCE_DIR}/signal-slot/core/*.cpp 
    ${CMAKE_CURRENT_SOURCE_DIR}/signal-slot/core/*.cxx
    ${CMAKE_CURRENT_SOURCE_DIR}/signal-slot/core/*.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/signal-slot/core/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/signal-slot/core/*.hxx
    ${CMAKE_CURRENT_SOURCE_DIR}/signal-slot/core/*.hpp
)

# Add example executable
add_executable(SigSlotExample
    ${SRC_FILES}
    examples.cpp)

if (WIN32)
    target_link_libraries(SigSlotExample winmm.lib)
endif()

# Enable testing
enable_testing()

# Add test executable
add_executable(signal_slot_test 
    ${SRC_FILES}
    test/test_signal_slot.cpp
)

target_link_libraries(signal_slot_test
    PRIVATE
    GTest::gtest
    GTest::gtest_main
)

if (WIN32)
    target_link_libraries(signal_slot_test winmm.lib)
endif()

# Add tests
include(GoogleTest)
gtest_discover_tests(signal_slot_test)

# Installation
include(GNUInstallDirs)
install(TARGETS SigSlotExample
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Install header files
install(FILES 
    signal-slot/signal_slot_api.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/signal-slot
)

install(DIRECTORY 
    signal-slot/core/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/signal-slot/core
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)
