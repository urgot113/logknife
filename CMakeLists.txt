cmake_minimum_required(VERSION 3.20)
project(logknife C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(logknife
  src/logknife.c
)

if (MSVC)
  target_compile_options(logknife PRIVATE /W4)
else()
  target_compile_options(logknife PRIVATE -Wall -Wextra -Wpedantic)
endif()
