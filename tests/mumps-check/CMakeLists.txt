cmake_minimum_required(VERSION 3.21)
project(mumps-check LANGUAGES CXX)

# Consumed the way an application would: from the installed package, through
# find_package. That the config file resolves at all is half of what this test
# checks; which solver ends up running is the other half.
find_package(sparselizard REQUIRED)

add_executable(mumps-check main.cpp)
target_link_libraries(mumps-check PRIVATE sparselizard::sparselizard)

# The mesh sits beside the source and the program opens it by relative path, so
# it has to land beside the binary too.
add_custom_command(TARGET mumps-check POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/capacitor.msh" "$<TARGET_FILE_DIR:mumps-check>")
