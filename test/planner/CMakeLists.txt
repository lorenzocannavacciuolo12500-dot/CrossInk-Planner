add_executable(planner_tests
  PlannerTest.cpp
  ${REPO_ROOT}/lib/Planner/PlannerDate.cpp
  ${REPO_ROOT}/lib/Planner/PlannerJson.cpp
  ${REPO_ROOT}/lib/Planner/PlannerTodos.cpp
  ${REPO_ROOT}/lib/JsonParser/StreamingJsonParser.cpp
)

target_include_directories(planner_tests PRIVATE
  ${REPO_ROOT}/lib/Planner
  ${REPO_ROOT}/lib/JsonParser
)

target_link_libraries(planner_tests PRIVATE crosspoint_test_common)
target_compile_options(planner_tests PRIVATE -UNDEBUG)

add_test(NAME planner_tests COMMAND planner_tests)
