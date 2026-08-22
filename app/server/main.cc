#include <csignal>
#include <thread>
#include <vector>

#include <cstdlib>
#include "WorkerPool.h"  // WorkerPool 定义（extern声明在server.h中）
#include "account.h"     // AccountHandler
#include "big_file.h"
#include "chat.h"
#include "friend.h"
#include "global.h"
#include "group.h"
#include "logger.h"
#include "mysql_pool.h"
#include "redis_pool.h"
#include "router.h"  // MessageRouter
#include "server.h"  // EventLoop / TCPConnection / MainReactor

// 全局变量，用于信号处理
MainReactor* main_reactor_ = nullptr;
std::vector<EventLoop*> sub_loops_;
std::vector<std::thread> loop_threads_;
BigFileServer big_file_server_(2100);

// 信号处理函数
std::atomic<bool> shutdown_{false};

void signal_handler(int sig) {
    shutdown_.store(true, std::memory_order_relaxed);
    if (main_reactor_ != nullptr) {
        uint64_t one = 1;
        ssize_t ret = write(main_reactor_->getWakeupFd(), &one, sizeof(one));
        (void)ret;
    }
}

int main(int argc, char* argv[]) {
    // 初始化日志
    google::InitGoogleLogging(argv[0]);
    google::SetLogDestination(google::INFO, "./log_info_");  // INFO日志路径
    google::SetLogDestination(google::WARNING, "./log_warn_");
    google::SetLogDestination(google::ERROR, "./log_err_");
    FLAGS_logbufsecs = 0;      // 实时输出
    FLAGS_max_log_size = 100;  // 100MB
    LOG(INFO) << "========== ChatRoom Server Starting ==========";

    // 注册信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    const char* env_host_m = std::getenv("MYSQL_HOST");
    const std::string mysql_host = env_host_m ? env_host_m : "127.0.0.1";
    // 初始化数据库连接池
    MysqlPool& mysql_pool = MysqlPool::GetInstance();
    if (!mysql_pool.Init(mysql_host, 3306, "root", "123456", "chat_room", 8, 32)) {
        LOG(ERROR) << "MySQL pool init failed";
        return EXIT_FAILURE;
    }

    const char* env_host_r = std::getenv("REDIS_HOST");
    const std::string redis_host = env_host_r ? env_host_r : "127.0.0.1";
    RedisPool& redis_pool = RedisPool::GetInstance();
    if (!redis_pool.Init(redis_host, 6379, "1204", 0, 8, 32)) {
        LOG(ERROR) << "Redis pool init failed";
        return EXIT_FAILURE;
    }

    // 注册业务路由
    AccountHandler::registerhandlers();  // 账号模块
    FriendHandler::registerhandlers();   // 朋友模块
    GroupHandler::registerhandlers();    // 群聊模块
    HistoryHandler::registerhandlers();  // 聊天模块

    // 创建SubReactor线程
    int sub_reactor_num = std::max(2u, std::thread::hardware_concurrency());
    std::vector<EventLoop> loops(sub_reactor_num);
    for (int i = 0; i < sub_reactor_num; ++i) {
        sub_loops_.push_back(&loops[i]);
        loop_threads_.emplace_back([&loops, i]() { loops[i].loop(); });
    }

    // 创建MainReactor线程
    MainReactor main_reactor(DEFAULT_SERVER_PORT, sub_loops_);
    main_reactor_ = &main_reactor;

    // 启动主监听
    LOG(INFO) << "Server listening on port " << DEFAULT_SERVER_PORT;
    big_file_server_.Start();
    main_reactor.run();

    if (shutdown_.load()) {
        LOG(INFO) << "Server shutting down...";
    }

    for (auto* loop : sub_loops_) {
        if (loop) {
            loop->Stop();
        }
    }

    big_file_server_.Stop();
    LOG(INFO) << "BigFileServer stopped.";

    for (size_t i = 0; i < loop_threads_.size(); ++i) {
        auto& t = loop_threads_[i];
        if (t.joinable()) {
            t.join();
        }
    }

    LOG(INFO) << "All sub reactors joined.";

    // 清理所有在线用户的 Redis 状态
    for (auto* loop : sub_loops_) {
        loop->clean_online();
    }

    mysql_pool.Destroy();
    redis_pool.Destroy();

    LOG(INFO) << "Server exited normally.";
    google::ShutdownGoogleLogging();

    return 0;
}
