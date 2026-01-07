cmake_minimum_required(VERSION 3.20)

project(jobkit VERSION 0.1.0 LANGUAGES CXX)

option(JOBKIT_BUILD_TESTS "Build jobkit tests" ON)
option(JOBKIT_ENABLE_TELEMETRY "Enable jobkit telemetry" OFF)

set(CMAKE_CXX_EXTENSIONS OFF)

add_library(jobkit
    core/src/JobSystem.cpp
)

add_library(jobkit::jobkit ALIAS jobkit)

target_include_directories(jobkit
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/core/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(jobkit PUBLIC cxx_std_20)

if(JOBKIT_ENABLE_TELEMETRY)
    target_compile_definitions(jobkit PUBLIC JOBSYS_TELEMETRY=1)
endif()

include(GNUInstallDirs)

install(TARGETS jobkit
    EXPORT jobkitTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY core/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT jobkitTargets
    FILE jobkitTargets.cmake
    NAMESPACE jobkit::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/jobkit
)

include(CMakePackageConfigHelpers)

configure_package_config_file(
    cmake/jobkitConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/jobkitConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/jobkit
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/jobkitConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/jobkitConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/jobkitConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/jobkit
)

if(JOBKIT_BUILD_TESTS)
    enable_testing()
    add_executable(jobkit_tests tests/test_jobsystem.cpp)
    target_link_libraries(jobkit_tests PRIVATE jobkit)
    add_test(NAME jobkit_tests COMMAND jobkit_tests)
endif()
