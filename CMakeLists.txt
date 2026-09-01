cmake_minimum_required(VERSION 3.20)
project(sih26027 LANGUAGES CXX)
if(POLICY CMP0144)
  cmake_policy(SET CMP0144 NEW)
endif()

option(SIH_WITH_ORTOOLS "Build the OR-Tools CP-SAT implementation" ON)
enable_testing()
add_subdirectory(optimizer)
