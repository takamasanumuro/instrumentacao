cmake_minimum_required(VERSION 3.0)
project(instrumentation)

#Add the source files to the build
set(SOURCES
    Channel.c
    util.c
    CalibrationHelper.c
    LineProtocol.c
    ADS1115.c
    CsvLogger.c
    OfflineQueue.c
    SocketServer.c
    BatteryMonitor.c 
    Sender.c
    DataQueue.c
    DataPublisher.c
    TimingUtils.c
    HardwareManager.c
    ApplicationManager.c
    ConfigYAML.c
    main.c
)

#Add debug information
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_C_FLAGS_DEBUG "-g -O0 -DDEBUG")

#Find required packages
find_package(CURL REQUIRED)
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(YAML REQUIRED yaml-0.1)

#Add the executable to the build
add_executable(instrumentation ${SOURCES})
target_link_libraries(instrumentation PRIVATE CURL::libcurl Threads::Threads gps ZLIB::ZLIB ${YAML_LIBRARIES} pthread m)
target_include_directories(instrumentation PRIVATE ${YAML_INCLUDE_DIRS})
target_compile_options(instrumentation PRIVATE ${YAML_CFLAGS_OTHER})

# Test executables (optional, controlled by BUILD_TESTS)
option(BUILD_TESTS "Build test executables" ON)
if(BUILD_TESTS)
    # Basic YAML test
    add_executable(yaml-test test_yaml.c ConfigYAML.c Channel.c)
    
    # YAML loader test
    add_executable(yaml-loader-test test_yaml_loader.c ConfigYAML.c Channel.c)
    
    # YAML debug utility
    add_executable(debug-yaml debug_yaml.c)
    
    # YAML validation test
    add_executable(yaml-validation-test test_yaml_validation.c ConfigYAML.c Channel.c)
    
    # Integration test (uses most sources)
    add_executable(integration-test
        test_integration.c
        ConfigYAML.c
        Channel.c
        ApplicationManager.c
        BatteryMonitor.c
        CsvLogger.c
        HardwareManager.c
        DataPublisher.c
        TimingUtils.c
        Sender.c
        DataQueue.c
        OfflineQueue.c
        util.c
    )
    
    # Set common properties for all test executables
    set(TEST_TARGETS yaml-test yaml-loader-test debug-yaml yaml-validation-test integration-test)
    foreach(target ${TEST_TARGETS})
        target_link_libraries(${target} PRIVATE ${YAML_LIBRARIES})
        target_include_directories(${target} PRIVATE ${YAML_INCLUDE_DIRS})
        target_compile_options(${target} PRIVATE ${YAML_CFLAGS_OTHER})
    endforeach()
    
    # Integration test needs additional libraries
    target_link_libraries(integration-test PRIVATE curl pthread m ZLIB::ZLIB)
endif()

#Copy the board configuration files to the build directory
# This ensures that when you run the app from the build directory, it can find the config files.
file(GLOB CONFIG_FILES "${CMAKE_CURRENT_SOURCE_DIR}/configurations*")
foreach (CONFIG_FILE ${CONFIG_FILES})
    get_filename_component(FILE_NAME ${CONFIG_FILE} NAME)
    file(COPY ${CONFIG_FILE} DESTINATION ${CMAKE_BINARY_DIR})
endforeach()
