# COPYRIGHT (c) 2019 Habanalabs Ltd. See COPYING.md file

file(GLOB SRC *.c) # compile all files with *.c suffix

include_directories(
    ${CMAKE_SOURCE_DIR}/include/specs
)

# Build shared/static libraries from all specified source files
add_library(${HLTHUNK_ERR_INJECT_TARGET} SHARED ${SRC})
add_library(${HLTHUNK_ERR_INJECT_TARGET}-static STATIC ${SRC})
