cmake_minimum_required (VERSION 2.8)
project(SDLVideoPlayer)

# set output path
set(PROJECT_OUTPUT_DIR ${PROJECT_BINARY_DIR}/${CMAKE_SYSTEM_PROCESSOR})
set(PROJECT_INCLUDE_DIR ${PROJECT_OUTPUT_DIR}/include)
message("-- output path:  ${PROJECT_OUTPUT_DIR} ")

# mkdir directory
file(MAKE_DIRECTORY ${PROJECT_INCLUDE_DIR})
file(MAKE_DIRECTORY ${PROJECT_OUTPUT_DIR}/bin)
file(MAKE_DIRECTORY ${PROJECT_OUTPUT_DIR}/lib)

# set the target path
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_OUTPUT_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_OUTPUT_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_OUTPUT_DIR}/lib)

IF (WIN32)
    include_directories(
        ${PROJECT_SOURCE_DIR}/src/third_party/win32/ffmpeg/include
        ${PROJECT_SOURCE_DIR}/src/third_party/win32/sdl/include
    )

    link_directories(
        ${PROJECT_SOURCE_DIR}/src/third_party/win32/ffmpeg/lib
        ${PROJECT_SOURCE_DIR}/src/third_party/win32/sdl/lib
    )

    # copy dynamic library to output bin dir
    file(MAKE_DIRECTORY ${PROJECT_OUTPUT_DIR}/bin/Debug)
    file(MAKE_DIRECTORY ${PROJECT_OUTPUT_DIR}/bin/Release)
    file(
            GLOB PUBLIC_LIB
            ${PROJECT_SOURCE_DIR}/src/third_party/win32/ffmpeg/bin/*.dll
            ${PROJECT_SOURCE_DIR}/src/third_party/win32/sdl/lib/*.dll
    )

    file(COPY ${PUBLIC_LIB} DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Debug)
    file(COPY ${PUBLIC_LIB} DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Release)

    SET (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SAFESEH:NO")
    SET (CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /SAFESEH:NO")
    SET (CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} /SAFESEH:NO")
ENDIF ()


aux_source_directory(${PROJECT_SOURCE_DIR}/src DIR_SRCS)
file (GLOB DIR_SRC_HEADERS "src/*.h")
# set build target
add_executable(${PROJECT_NAME} ${DIR_SRCS} ${DIR_SRC_HEADERS})

set(PUBLIC_LINK_LIB avcodec avformat avutil swscale swresample SDL2 SDL2main)
target_link_libraries(${PROJECT_NAME} ${PUBLIC_LINK_LIB})