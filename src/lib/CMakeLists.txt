# generated by cmake-generator
cmake_minimum_required(VERSION 3.16.3)

add_executable(relay-daemon 
  daemon.c
  main.c)

include(Tornado.cmake)
set_tornado(relay-daemon)

target_include_directories(relay-daemon PRIVATE ../include)


target_link_libraries(relay-daemon PUBLIC 
  relay-server-lib
  udp-server
  guise-client-udp)

