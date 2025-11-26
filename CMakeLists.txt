# 设置项目所需的最低 CMake 版本
cmake_minimum_required(VERSION 3.10)

# 设置项目名称为 Compiler，这是评测机要求的可执行文件名
# 确保最终生成的可执行文件名为 'Compiler'
project(Compiler CXX)

# 设置 C++ 标准为 C++17
# 评测机默认支持 C++17，这里显式设置确保一致性
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 声明项目的所有源文件
set(SOURCE_FILES
        compiler.cpp
        MipsGenerator.cpp  # **添加 MipsGenerator.cpp**
)

# 添加可执行文件
# Compiler 是目标名称
# ${SOURCE_FILES} 包含了所有源文件
add_executable(Compiler ${SOURCE_FILES})

# 如果 MipsGenerator.h 不在与 CMakeLists.txt 相同的目录下，
# 或者它在某个子目录，您可能还需要添加头文件搜索路径：
# target_include_directories(Compiler PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})