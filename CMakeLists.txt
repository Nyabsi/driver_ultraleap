cmake_minimum_required(VERSION 3.26)

if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules/")

project(driver_leapex)

set(LeapSDK_DIR ${CMAKE_SOURCE_DIR}/3rdparty/LeapSDK/lib/cmake/LeapSDK)
set(OpenVR_ROOT ${CMAKE_SOURCE_DIR}/3rdparty/OpenVR)
set(GLM_ROOT ${CMAKE_SOURCE_DIR}/3rdparty/GLM)
set(JSON_ROOT ${CMAKE_SOURCE_DIR}/3rdparty/json)

find_package(OpenVR REQUIRED)
find_package(LeapSDK 5 REQUIRED)

add_subdirectory(${JSON_ROOT} EXCLUDE_FROM_ALL)
add_subdirectory(${GLM_ROOT})

# Include rest of the CMake bindings
add_subdirectory(3rdparty)

option(BUILD_TEST_APPS "Build test applications" OFF)

add_library(driver_leapex SHARED
        src/driver/LeapDriverFactory.cpp
        src/driver/LeapDeviceProvider.cpp
        src/driver/LeapDeviceProvider.h
        src/driver/LeapDeviceDriver.cpp
        src/driver/LeapDeviceDriver.h
        src/driver/LeapHandDriver.cpp
        src/driver/LeapHandDriver.h
        src/driver/LeapElbowDriver.cpp
        src/driver/LeapElbowDriver.h
        src/driver/LeapDevice.cpp
        src/driver/LeapDevice.h
        src/driver/OsUtils.cpp
        src/driver/OsUtils.h
        src/driver/VrUtils.h
        src/driver/VrLogging.h
        src/driver/VrMaths.h
        src/driver/VrHand.cpp
        src/driver/VrHand.h
        src/driver/LeapDriverSettings.cpp
        src/driver/LeapDriverSettings.h
        src/driver/RequestDeserializer.cpp
        src/driver/RequestDeserializer.h
        src/driver/LeapTrackedDriver.cpp
        src/driver/LeapTrackedDriver.h
)

target_compile_features(driver_leapex PUBLIC cxx_std_20)
target_link_libraries(driver_leapex
    PRIVATE
        OpenVR::Driver
        LeapSDK::LeapC
        glm::glm
        nlohmann_json::nlohmann_json
)

add_executable(leapex_overlay
    src/overlay/Main.cpp
)

target_compile_features(leapex_overlay PUBLIC cxx_std_20)
target_link_libraries(leapex_overlay
    PRIVATE
        OpenVR::API
        nlohmann_json::nlohmann_json
        ImGui
)

add_dependencies(leapex_overlay driver_leapex)

# Force all compilers to output to binary folder without additional output (like Windows adds "Debug" and "Release" folders)
set(driver_binary_dir ${CMAKE_BINARY_DIR}/leapex/bin/win64)
set_target_properties(driver_leapex PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${driver_binary_dir})
foreach(output_config ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${output_config} output_config)
    set_target_properties(driver_leapex PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${output_config} ${driver_binary_dir})
endforeach()

# Copy the required DLL dependencies.
add_custom_command(
        TARGET driver_leapex POST_BUILD
        COMMENT "Copying DLLs"
        COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:driver_leapex> $<TARGET_RUNTIME_DLLS:driver_leapex>
        COMMAND_EXPAND_LISTS
)

add_custom_target(driver_leapex_resources)
add_custom_command(
        TARGET driver_leapex_resources POST_BUILD
        COMMENT "Copying Resources & Manifest"
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different ${CMAKE_SOURCE_DIR}/leapex ${CMAKE_BINARY_DIR}/leapex
)
add_dependencies(driver_leapex driver_leapex_resources)

# Build test applications if requested.
if (BUILD_TEST_APPS)
    add_executable(test_debug_request test/TestDebugRequest.cpp)
    target_compile_features(test_debug_request PUBLIC cxx_std_20)
    target_link_libraries(test_debug_request
        PRIVATE
            OpenVR::API
            nlohmann_json::nlohmann_json
    )

    # Copy the required DLL dependencies.
    add_custom_command(
        TARGET test_debug_request POST_BUILD
        COMMENT "Copying DLLs"
        COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:test_debug_request> $<TARGET_RUNTIME_DLLS:test_debug_request>
        COMMAND_EXPAND_LISTS
    )
    add_dependencies(test_debug_request driver_leapex)
endif()
