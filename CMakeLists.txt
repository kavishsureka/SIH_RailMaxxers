cmake_minimum_required(VERSION 3.20)
project(sih26027 LANGUAGES CXX)

option(SIH_WITH_ORTOOLS "Build the OR-Tools CP-SAT implementation" OFF)
enable_testing()
add_subdirectory(optimizer)
