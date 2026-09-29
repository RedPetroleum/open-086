# Lua 5.4.7, see LICENSE. The desktop runner (../../runner) compiles the same sources.
file(GLOB srcs "${CMAKE_CURRENT_LIST_DIR}/src/*.c")
idf_component_register(SRCS ${srcs} INCLUDE_DIRS "src")
target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -Wno-unused-function)
