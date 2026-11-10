cmake_minimum_required(VERSION 3.14)
project(PRIDE_Bank LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Threads REQUIRED)

# ---- MySQL client library ---------------------------------------------------
# Linux:   sudo apt install libmysqlclient-dev      (or libmariadb-dev)
# macOS:   brew install mysql-client
# Windows: install "MySQL Connector/C" (or MariaDB Connector/C), then configure with
#          -DMYSQL_INCLUDE_DIR=<dir containing mysql.h> -DMYSQL_LIB=<path to libmysql / libmariadb>
find_path(MYSQL_INCLUDE_DIR mysql.h
          PATH_SUFFIXES mysql mariadb
          PATHS /usr/include /usr/local/include /opt/homebrew/opt/mysql-client/include
                "C:/Program Files/MySQL/MySQL Server 8.0/include"
                "C:/Program Files/MySQL/MySQL Connector C 6.1/include")
find_library(MYSQL_LIB NAMES mysqlclient libmysql mariadb libmariadb
          PATHS /usr/lib /usr/local/lib /opt/homebrew/opt/mysql-client/lib
                "C:/Program Files/MySQL/MySQL Server 8.0/lib"
                "C:/Program Files/MySQL/MySQL Connector C 6.1/lib")
if(NOT MYSQL_INCLUDE_DIR OR NOT MYSQL_LIB)
  message(FATAL_ERROR "MySQL client library not found. Install it or pass "
                      "-DMYSQL_INCLUDE_DIR=... -DMYSQL_LIB=...")
endif()
message(STATUS "MySQL include: ${MYSQL_INCLUDE_DIR}")
message(STATUS "MySQL library: ${MYSQL_LIB}")
# db_connector.h picks <mysql/mysql.h>, <mariadb/mysql.h> or <mysql.h> automatically.
# Add both the dir and its parent so every spelling resolves.
get_filename_component(MYSQL_INCLUDE_PARENT "${MYSQL_INCLUDE_DIR}" DIRECTORY)

# Everything except main.cpp goes into a library so the executable and the tests share it.
file(GLOB_RECURSE CORE_SRC CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.cpp)
list(FILTER CORE_SRC EXCLUDE REGEX ".*/src/main\\.cpp$")

add_library(banksafe_core STATIC ${CORE_SRC})
# third_party/ holds httplib.h and nlohmann/json.hpp
target_include_directories(banksafe_core PUBLIC include third_party
                           ${MYSQL_INCLUDE_DIR} ${MYSQL_INCLUDE_PARENT})
target_link_libraries(banksafe_core PUBLIC Threads::Threads ${MYSQL_LIB})
if(WIN32)
  target_link_libraries(banksafe_core PUBLIC ws2_32)   # cpp-httplib sockets
endif()
target_compile_options(banksafe_core PRIVATE -Wall -Wextra)

add_executable(PRIDE-Bank src/main.cpp)
target_link_libraries(PRIDE-Bank PRIVATE banksafe_core)

if(EXISTS ${CMAKE_SOURCE_DIR}/benchmark/benchmark_suite.cpp)
  add_executable(benchmark_suite benchmark/benchmark_suite.cpp)
  target_link_libraries(benchmark_suite PRIVATE banksafe_core)
endif()

enable_testing()
foreach(t test_connector test_integration test_recovery test_markov test_markov_persist)
  if(EXISTS ${CMAKE_SOURCE_DIR}/tests/${t}.cpp)
    add_executable(${t} tests/${t}.cpp)
    target_link_libraries(${t} PRIVATE banksafe_core)
    add_test(NAME ${t} COMMAND ${t} WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  endif()
endforeach()

if(EXISTS ${CMAKE_SOURCE_DIR}/tools/markov_cli.cpp)
  add_executable(markov_cli tools/markov_cli.cpp)
  target_link_libraries(markov_cli PRIVATE banksafe_core)
endif()
