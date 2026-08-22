cmake_minimum_required(VERSION 3.10)
project(ChatRoom)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Debug)

# 统一头文件路径
include_directories(
    ${PROJECT_SOURCE_DIR}/shared
    ${PROJECT_SOURCE_DIR}/server
    ${PROJECT_SOURCE_DIR}/client
    ${PROJECT_SOURCE_DIR}/thirdparty
    /usr/include/mysql
    /usr/local/include
)

# 库文件搜索路径
link_directories(/usr/local/lib)

# 查找第三方库
find_package(glog REQUIRED)
find_package(gflags REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)

# ========== 服务端源文件 ==========
set(SERVER_SRC
    server/main.cc
    server/server.cc
    shared/message.cc
    server/router.cc
    server/account.cc
    shared/utils.cc
    shared/logger.cc
    server/mysql_pool.cc
    server/redis_pool.cc
    server/connection_manage.cc
    server/friend.cc
    server/group.cc
    server/chat.cc
    server/big_file.cc
)

add_executable(chat_server ${SERVER_SRC})
target_compile_definitions(chat_server PRIVATE GLOG_USE_GFLAGS)
target_link_libraries(chat_server
    gflags
    glog::glog
    OpenSSL::SSL
    OpenSSL::Crypto
    CURL::libcurl
    mysqlclient
    hiredis
    pthread
)

# ========== 客户端 ==========
add_executable(chat_cli
    client/cli_main.cc
    client/cli_network.cc
    client/cli_acconut.cc
    client/cli_friend.cc
    client/cli_group.cc
    client/cli_chat.cc
    client/cli_menu.cc
    client/cli_big_file.cc
    client/client.cc
    shared/utils.cc
    shared/logger.cc
)
target_compile_definitions(chat_cli PRIVATE GLOG_USE_GFLAGS)
target_link_libraries(chat_cli
    gflags
    glog::glog
    OpenSSL::Crypto
    CURL::libcurl
    pthread
)

