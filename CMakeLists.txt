cmake_minimum_required(VERSION 3.20)

project(task_graph)


include(cmake/CheckCompiler.cmake)
include(cmake/CompileOptions.cmake)
include(cmake/Sanitize.cmake)

set(source_dir "${PROJECT_SOURCE_DIR}/taskgraph/src/")
file(GLOB source_files "${source_dir}/*.cpp")

add_executable(${PROJECT_NAME} ${source_files})

