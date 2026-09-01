add_executable(sih-optimizer src/main.cpp src/engine.cpp)
target_compile_features(sih-optimizer PRIVATE cxx_std_20)
target_include_directories(sih-optimizer PRIVATE include)
target_compile_options(sih-optimizer PRIVATE -Wall -Wextra -Wpedantic)

if(SIH_WITH_ORTOOLS)
  find_package(ortools CONFIG REQUIRED)
  target_link_libraries(sih-optimizer PRIVATE ortools::ortools)
  target_compile_definitions(sih-optimizer PRIVATE SIH_WITH_ORTOOLS=1)
endif()

enable_testing()
add_test(NAME optimizer_smoke
  COMMAND sih-optimizer benchmark
    --data ${CMAKE_SOURCE_DIR}/data/demo
    --config ${CMAKE_SOURCE_DIR}/config/optimizer.conf)

