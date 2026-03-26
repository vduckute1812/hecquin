cmake_minimum_required(VERSION 3.10)
project(hecquin_sound LANGUAGES C CXX)

# Sử dụng chuẩn C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Tùy chọn: bật nhiều cảnh báo hơn
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif ()

# Sử dụng bản whisper.cpp build cục bộ trong project:
#   - Source, build và cài đặt vào thư mục .env/whisper-install
set(WHISPER_INSTALL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/.env/whisper-install)

# Thêm đường dẫn tìm kiếm package
list(APPEND CMAKE_PREFIX_PATH ${WHISPER_INSTALL_DIR})

# Tìm whisper package
find_package(whisper CONFIG QUIET)

if(whisper_FOUND)
    message(STATUS "Found whisper: ${WHISPER_INSTALL_DIR}")
else()
    # Nếu không tìm thấy package, sử dụng đường dẫn thủ công
    message(STATUS "whisper package not found, using manual paths")
    
    set(WHISPER_INCLUDE_DIR ${WHISPER_INSTALL_DIR}/include)
    set(WHISPER_LIB_DIR ${WHISPER_INSTALL_DIR}/lib)
    
    # Tìm thư viện whisper
    find_library(WHISPER_LIBRARY 
        NAMES whisper
        PATHS ${WHISPER_LIB_DIR}
        NO_DEFAULT_PATH
    )
    
    if(NOT WHISPER_LIBRARY)
        message(WARNING "whisper library not found. Run: ./dev.sh whisper:clone && ./dev.sh whisper:build")
    endif()
endif()

# Model directory
set(MODELS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/.env/models)

# Tìm SDL2
find_package(SDL2 REQUIRED)

# Voice detector executable
add_executable(voice_detector voice_detector.cpp)

if(whisper_FOUND)
    target_link_libraries(voice_detector PRIVATE whisper SDL2::SDL2)
else()
    target_include_directories(voice_detector PRIVATE ${WHISPER_INCLUDE_DIR})
    target_link_libraries(voice_detector PRIVATE ${WHISPER_LIBRARY} SDL2::SDL2)
endif()

# Định nghĩa đường dẫn mặc định tới model
target_compile_definitions(voice_detector PRIVATE 
    DEFAULT_MODEL_PATH="${MODELS_DIR}/ggml-base.bin"
)
