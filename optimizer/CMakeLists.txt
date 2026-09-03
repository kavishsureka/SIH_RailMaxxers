add_library(sih-optimizer-core src/engine.cpp)
target_compile_features(sih-optimizer-core PUBLIC cxx_std_20)
target_include_directories(sih-optimizer-core PUBLIC include)
target_compile_options(sih-optimizer-core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(sih-optimizer src/main.cpp)
target_link_libraries(sih-optimizer PRIVATE sih-optimizer-core)

add_executable(sih-optimizer-tests tests/engine_test.cpp)
target_link_libraries(sih-optimizer-tests PRIVATE sih-optimizer-core)

if(SIH_WITH_ORTOOLS)
  find_package(ortools CONFIG REQUIRED)
  target_link_libraries(sih-optimizer-core PUBLIC ortools::ortools)
  target_compile_definitions(sih-optimizer-core PRIVATE SIH_WITH_ORTOOLS=1)
endif()

enable_testing()
add_test(NAME optimizer_requires_ml_priorities
  COMMAND sih-optimizer benchmark
    --data ${CMAKE_SOURCE_DIR}/data/scenarios/scenario-alpha
    --config ${CMAKE_SOURCE_DIR}/config/optimizer.conf)
set_tests_properties(optimizer_requires_ml_priorities PROPERTIES WILL_FAIL TRUE)
add_test(NAME optimizer_unit COMMAND sih-optimizer-tests)
