cmake_minimum_required(VERSION 4.0)
project(xww CXX)

set(CMAKE_GENERATOR_PLATFORM x64)
add_definitions(-DNTDDI_VERSION=NTDDI_WIN11_GE -D_WIN32_WINNT=0x0A00)

find_package(WDK)

set(CMAKE_CXX_STANDARD 23)

wdk_add_driver(xww Main.cpp kmemory/memory.cpp utils/utils.cpp process/process.cpp handle/handle.cpp)
set_target_properties(xww PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/../../out
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/../../out
        ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/../../out
)
// 这里弄一个自签都行 要不然EAC 不会让游戏启动
add_custom_command(TARGET xww POST_BUILD
        COMMAND signtool sign /f "D:\\EfiGuard\\root.pfx" /p zw191128 /td SHA256 /fd SHA256 ${CMAKE_BINARY_DIR}/../../out/xww.sys
)