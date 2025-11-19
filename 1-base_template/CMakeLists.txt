cmake_minimum_required(VERSION 3.15)

project(opencv-mobile-test)

set(CMAKE_CXX_STANDARD 17)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

set(OpenCV_DIR "../opencv-mobile-4.12.0-luckfox-pico/lib/cmake/opencv4")
find_package(OpenCV REQUIRED)


add_executable(opencv-mobile-test main.cpp)

target_include_directories(${PROJECT_NAME} PRIVATE 
    ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} ${OpenCV_LIBS})