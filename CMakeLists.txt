cmake_minimum_required(VERSION 3.20)
project(logknife C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(LOGKNIFE_USE_PCRE2 "Use PCRE2 for full regex support (optional)" OFF)

add_executable(logknife
  src/logknife.c
)

if (LOGKNIFE_USE_PCRE2)
  find_package(PCRE2 QUIET)
  if (PCRE2_FOUND)
    target_compile_definitions(logknife PRIVATE LOGKNIFE_USE_PCRE2=1)
    # Prefer 8-bit library (most common)
    if (TARGET PCRE2::pcre2-8)
      target_link_libraries(logknife PRIVATE PCRE2::pcre2-8)
    else()
      target_link_libraries(logknife PRIVATE PCRE2::pcre2)
    endif()
  else()
    message(WARNING "PCRE2 not found; building with built-in regex subset")
  endif()
endif()

if (MSVC)
  target_compile_options(logknife PRIVATE /W4)
else()
  target_compile_options(logknife PRIVATE -Wall -Wextra -Wpedantic)
endif()
