#ifndef SERVER_H
#define SERVER_H

#include "connection_manage.h"
#include "global.h"
#include "logger.h"

class TCPConnection;  // 前置声明

inline void setNonBlock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

inline int create_fd() {
    int efd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (efd < 0) {
        LOG(ERROR) << "eventfd create";
        abort();
    }
    return efd;
}

class EventLoop {
   public:
    EventLoop()
        : epfd_(epoll_create1(EPOLL_CLOEXEC)),
          wakeupfd(create_fd()),
          stop_(false) {
        epoll_event ev;
        ev.events = EPOLLIN | EPOLLET;  // 边缘触发模式
        ev.data.fd = wakeupfd;
        epoll_ctl(epfd_, EPOLL_CTL_ADD, wakeupfd, &ev);
        init_timer();
    }
    ~EventLoop() {
        close(timefd);
        close(epfd_);
        close(wakeupfd);
    }

    void loop();
    void addtask(std::function<void()> task);
    void updateEpollEvent(int fd, int events);
    void removeEpollEvent(int fd);
    void addConnection(int fd, std::shared_ptr<TCPConnection> conn);
    void removeConnection(int fd);
    bool isInLoopThread();
    void Stop();
    void clean_online();

   private:
    void wakeup();
    void handleWakeup();
    void init_timer();
    void handle_time();

    int epfd_;
    int wakeupfd;
    int timefd;
    std::atomic<bool> stop_{false};
    std::thread::id thread_id;
    std::mutex mtx;
    std::queue<std::function<void()>> work_tasks;
    std::unordered_map<int, std::shared_ptr<TCPConnection>> connections_;
};

// 继承enable_shared_from_this是为了在回调中安全地获取自身的shared_ptr，防止对象在回调执行过程中被销毁
class TCPConnection : public std::enable_shared_from_this<TCPConnection> {
   public:
    enum Status { connect_, unconnect_ };
    Status status;
    TCPConnection(int fd, EventLoop* loop)
        : fd_(fd),
          loop_(loop),
          status(connect_),
          last_time(std::chrono::steady_clock::now()) {
        setNonBlock(fd);
        // 初始只监听可读事件（边缘触发）
        loop_->updateEpollEvent(fd_, EPOLLIN | EPOLLET);
    }

    ~TCPConnection() {
        // 如果该连接已绑定user_id，主动移除
        if (user_id_ != 0) {
            ConnectionManager::GetInstance().Remove(user_id_);
        }
        if (fd_ >= 0)
            ::close(fd_);
    }

    void updateLastActive() { last_time = std::chrono::steady_clock::now(); }
    std::chrono::steady_clock::time_point getLastActive() const {
        return last_time;
    }

    void handleEvent(uint32_t event);
    void send(const std::string& massage);
    void closeConnection();

    // 登录态相关
    void setUserId(uint64_t uid) { user_id_ = uid; }
    uint64_t getUserId() const { return user_id_; }
    bool isLogin() const { return user_id_ != 0; }

   private:
    void handle_read();
    void handle_write();
    void send_loop(const std::string& massage);
    void processFrame();  // 解析缓冲区里的完整帧

    int fd_;
    EventLoop* loop_;
    std::string write_buf;
    std::string read_buf;
    std::chrono::steady_clock::time_point last_time;
    uint64_t user_id_ = 0;  // 登录后绑定用户ID，0表示未登录
};

class MainReactor {
   public:
    MainReactor(int port, std::vector<EventLoop*>& loop)
        : subloops(loop), round(0) {
        // 创建监听socket（非阻塞模式）
        listenfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
        if (listenfd < 0) {
            perror("socket");
            abort();
        }

        // 设置SO_REUSEADDR选项，允许端口快速重用
        int opt = 1;
        setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        // 设置地址结构
        sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;          // IPv4
        addr.sin_addr.s_addr = INADDR_ANY;  // 监听所有网卡
        addr.sin_port = htons(port);        // 转换为网络字节序

        // 绑定地址
        if (bind(listenfd, (sockaddr*)&addr, sizeof(addr)) < 0) {
            LOG(ERROR) << "bind";
            abort();
        }
        // 开始监听，SOMAXCONN是系统允许的最大连接数
        if (listen(listenfd, SOMAXCONN) < 0) {
            LOG(ERROR) << "listen";
            abort();
        }

        // 创建wakeup eventfd，用于stop时唤醒epoll_wait
        wakeupfd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);

        // 创建主Reactor专用的epoll实例
        epfd = epoll_create1(EPOLL_CLOEXEC);
        epoll_event ev;
        ev.events = EPOLLIN | EPOLLET;  // 边缘触发监听可读
        ev.data.fd = listenfd;
        epoll_ctl(epfd, EPOLL_CTL_ADD, listenfd, &ev);

        // 把wakeupfd也加入epoll监听
        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = wakeupfd_;
        epoll_ctl(epfd, EPOLL_CTL_ADD, wakeupfd_, &ev);
    }

    int getWakeupFd() const { return wakeupfd_; }

    ~MainReactor() {
        close(epfd);
        close(listenfd);
        close(wakeupfd_);
    }

    void run();
    void stop();

   private:
    void acceptConn();

    int epfd;
    int listenfd;
    int round;
    int wakeupfd_;
    std::vector<EventLoop*> subloops;
    std::atomic<bool> stop_{false};
};

#endif