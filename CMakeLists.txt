cmake_minimum_required(VERSION 3.16)

if(WIN32 AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(_vcpkg_candidates
        "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        "$ENV{VCPKG_INSTALLATION_ROOT}/scripts/buildsystems/vcpkg.cmake"
        "$ENV{VSINSTALLDIR}VC/vcpkg/scripts/buildsystems/vcpkg.cmake"
        "C:/vcpkg/scripts/buildsystems/vcpkg.cmake"
    )
    foreach(_candidate IN LISTS _vcpkg_candidates)
        if(EXISTS "${_candidate}")
            set(CMAKE_TOOLCHAIN_FILE "${_candidate}" CACHE STRING "Vcpkg toolchain file")
            break()
        endif()
    endforeach()
    unset(_vcpkg_candidates)
    unset(_candidate)
endif()

project(CREWN LANGUAGES CXX)

find_package(SDL2 CONFIG REQUIRED)
find_package(OpenGL REQUIRED)

add_executable(CREWN
    main.cpp
    audio.h
    gfx.h
    textures.h
    track.h
    util.h
    world.h
)

target_compile_features(CREWN PRIVATE cxx_std_17)
target_compile_definitions(CREWN PRIVATE NOMINMAX)
target_link_libraries(CREWN PRIVATE SDL2::SDL2 OpenGL::GL)

if(WIN32)
    add_custom_command(TARGET CREWN POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:SDL2::SDL2>"
            "$<TARGET_FILE_DIR:CREWN>"
        COMMENT "Copying SDL2 runtime beside CREWN"
    )
endif()

if(WIN32 AND TARGET SDL2::SDL2main)
    target_link_libraries(CREWN PRIVATE SDL2::SDL2main)
endif()
