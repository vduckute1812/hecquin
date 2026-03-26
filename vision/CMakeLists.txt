cmake_minimum_required(VERSION 3.10)
project(hecquin_autoplay LANGUAGES CXX)

# Sử dụng chuẩn C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Tùy chọn: bật nhiều cảnh báo hơn
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif ()

# Sử dụng bản OpenCV build cục bộ trong project:
#   - Source, build và cài đặt vào thư mục .env/opencv-install
set(OpenCV_DIR
    ${CMAKE_CURRENT_SOURCE_DIR}/.env/opencv-install/lib/cmake/opencv4
)

find_package(OpenCV REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS})
