#include "server.h"
#include "WorkerPool.h"
#include "message.h"  // 提供 dispatch_message 和 send_json 等
#include "redis_pool.h"

WorkerPool worker_pool(std::thread::hardware_concurrency());

// 阻塞等待事件并按照fd类型分发事件
void EventLoop::loop() {
    thread_id = std::this_thread::get_id();
    std::vector<epoll_event> events(200);

    while (!stop_) {
        int nfd = epoll_wait(epfd_, events.data(), events.size(), -1);
        if (nfd < 0) {
            if (errno == EINTR) {
                continue;
            }
            LOG(ERROR) << "epoll_wait";
            continue;
        }
        for (int i = 0; i < nfd; i++) {
            int fd = events[i].data.fd;
            if (fd == wakeupfd) {
                handleWakeup();
            } else if (fd == timefd) {
                handle_time();
            } else {
                // 获取 shared_ptr，保证处理期间对象存活
                auto it = connections_.find(fd);
                if (it == connections_.end())
                    continue;

                auto conn = it->second;
                if (conn) {
                    conn->handleEvent(events[i].events);
                }
            }
        }
    }
}

// 外部线程向本Loop投递任务的接口，将任务推向任务队列并唤醒阻塞等待的任务
void EventLoop::addtask(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(mtx);
        work_tasks.push(std::move(task));
    }
    wakeup();
}

// 封装epoll_mod和epoll_add
void EventLoop::updateEpollEvent(int fd, int events) {
    epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;

    if (epoll_ctl(epfd_, EPOLL_CTL_MOD, fd, &ev) < 0) {
        // 如果返回ENOENT，说明还没添加，尝试ADD
        if (errno == ENOENT) {
            epoll_ctl(epfd_, EPOLL_CTL_ADD, fd, &ev);
        } else {
            LOG(ERROR) << "updateEpollEvent";
        }
    }
}

// epoll监听项移除
void EventLoop::removeEpollEvent(int fd) {
    epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr);
}

// 连接映射表的添加
void EventLoop::addConnection(int fd, std::shared_ptr<TCPConnection> conn) {
    connections_[fd] = conn;
}

// 连接映射表移除
void EventLoop::removeConnection(int fd) {
    connections_.erase(fd);
}

// 判断是不是当前EventLoop绑定的线程
bool EventLoop::isInLoopThread() {
    return std::this_thread::get_id() == thread_id;
}

// 触发可读事件，唤醒阻塞的epoll_wait
void EventLoop::wakeup() {
    uint64_t size = 1;
    ssize_t n = write(wakeupfd, &size, sizeof(size));
    if (n != sizeof(size)) {
        LOG(ERROR) << "eventfd write";
    }
}

// 循环读
void EventLoop::handleWakeup() {
    uint64_t value;

    while (true) {
        ssize_t n = read(wakeupfd, &value, sizeof(value));

        if (n == sizeof(value)) {
            // eventfd里面还有数据，继续读
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            // 已经读空
            break;
        }
        // 其它情况
        break;
    }

    std::queue<std::function<void()>> task;
    {
        std::lock_guard<std::mutex> lock(mtx);
        work_tasks.swap(task);  // 无锁转移任务列表
    }

    while (!task.empty()) {  // 依次执行任务
        task.front()();
        task.pop();
    }
}

void EventLoop::init_timer() {
    timefd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (timefd < 0) {
        LOG(ERROR) << "timefd create failed";
        abort();
    }
    epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = timefd;
    epoll_ctl(epfd_, EPOLL_CTL_ADD, timefd, &ev);

    // 设置每 5 秒触发一次
    struct itimerspec its;
    its.it_value.tv_sec = 5;
    its.it_value.tv_nsec = 0;
    its.it_interval.tv_sec = 5;
    its.it_interval.tv_nsec = 0;
    timerfd_settime(timefd, 0, &its, nullptr);
}

void EventLoop::handle_time() {
    uint64_t clock;
    read(timefd, &clock, sizeof(clock));
    auto now = std::chrono::steady_clock::now();
    std::vector<int> timeout_fds;
    for (auto& [fd, conn] : connections_) {
        if (conn->status != TCPConnection::connect_)
            continue;
        auto cha = std::chrono::duration_cast<std::chrono::seconds>( now - conn->getLastActive()) .count();
        if (cha > 600) {
            timeout_fds.push_back(fd);
        }
    }
    for (int fd : timeout_fds) {
        auto it = connections_.find(fd);
        if (it != connections_.end()) {
            it->second->closeConnection();
            // closeConnection 内部会 removeConnection，所以这里不需要再 erase
        }
    }
}

void EventLoop::Stop() {
    stop_ = true;
    wakeup();  // 唤醒 epoll_wait
}

void EventLoop::clean_online() {
    for (auto& [fd, conn] : connections_) {
        if (conn && conn->isLogin()) {
            uint64_t user_id = conn->getUserId();
            std::string uid_str = std::to_string(user_id);
            RedisPool::GetInstance().set_rem("online_users", uid_str);
            RedisPool::GetInstance().string_del("heartbeat:" + uid_str);
            LOG(INFO) << "用户 " << user_id << " 服务器关闭，清理在线状态";
        }
    }
}

// 分发触发的不同类型epoll事件
void TCPConnection::handleEvent(uint32_t event) {
    if (event & EPOLLRDHUP) {
        closeConnection();
        return;
    }
    // 如果发生错误或连接断开
    if (event & (EPOLLERR | EPOLLHUP)) {
        closeConnection();
        return;
    }
    // 如果可读
    if (event & EPOLLIN) {
        handle_read();
    }
    // 如果可写
    if (event & EPOLLOUT) {
        handle_write();
    }
}

// 发送函数，如果在本线程直接发，不在放进任务队列
void TCPConnection::send(const std::string& massage) {
    if (loop_->isInLoopThread()) {
        // 如果就在本EventLoop线程，直接发送
        send_loop(massage);
    } else {
        // 跨线程：把发送操作包装成任务，丢给所属EventLoop
        // 使用shared_from_this()确保对象在回调期间不被销毁
        auto self = shared_from_this();
        loop_->addtask([self, massage]() { self->send_loop(massage); });
    }
}

// 关闭TCP连接
void TCPConnection::closeConnection() {
    if (status != connect_)
        return;
    status = unconnect_;
    // 如果用户已登录，则执行下线清理
    if (user_id_ != 0) {
        // 从 Redis 删除心跳和在线状态
        RedisPool::GetInstance().set_rem("online_users", std::to_string(user_id_));
        RedisPool::GetInstance().string_del("heartbeat:" + std::to_string(user_id_));
        LOG(INFO) << "用户 " << user_id_ << " 断开连接，清理在线状态";
    }
    loop_->removeConnection(fd_);
    loop_->removeEpollEvent(fd_);
    ::close(fd_);
    fd_ = -1;
    write_buf.clear();
    read_buf.clear();
}

void TCPConnection::handle_read() {
    char buf[65536];
    while (true) {
        ssize_t n = ::read(fd_, buf, sizeof(buf));
        if (n > 0) {
            read_buf.append(buf, n);  // 追加到接收缓冲区
            updateLastActive();
        } else if (n == 0) {
            closeConnection();
            return;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 边缘触发下必须读到EAGAIN才算读完
                break;
            }
            // 其他错误
            LOG(ERROR) << "read";
            closeConnection();
            break;
        }
    }
    processFrame();  // 尝试提取完整消息帧
}

// 被动事件回调，是内核socket有位置空出来触发的epoll通知
void TCPConnection::handle_write() {
    while (!write_buf.empty()) {
        ssize_t n = ::write(fd_, write_buf.data(), write_buf.size());
        if (n > 0) {
            write_buf.erase(0, n);
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            // 内核发送缓冲区满
            break;
        } else {
            LOG(ERROR) << "write";
            closeConnection();
            return;
        }
    }
    if (write_buf.empty()) {
        // 数据全部发送完成
        loop_->updateEpollEvent(fd_, EPOLLIN | EPOLLET);
    } else {
        // 还有数据，下次继续发送
        loop_->updateEpollEvent(fd_, EPOLLIN | EPOLLOUT | EPOLLET);
    }
}

// 线程内部的发送函数，业务发消息的时候用，是主动的
void TCPConnection::send_loop(const std::string& message) {
    if (status != connect_)
        return;
    if (write_buf.empty()) {  // 是空的向内核发数据
        size_t sent = 0;
        while (sent < message.size()) {
            ssize_t n =
                ::write(fd_, message.data() + sent, message.size() - sent);
            if (n > 0) {
                sent += n;
            } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                break;
            } else {
                LOG(ERROR) << "send_loop";
                closeConnection();
                return;
            }
        }
        if (sent < message.size()) {
            write_buf.append(message.data() + sent, message.size() - sent);
        }
    } else {  // 不是空的要追加，不能直接写
        write_buf.append(message);
    }
    if (!write_buf.empty()) {  // 注册EPOLLOUT，下次发
        loop_->updateEpollEvent(fd_, EPOLLIN | EPOLLOUT | EPOLLET);
    }
}

// 解析TCP数据包
void TCPConnection::processFrame() {
    while (read_buf.size() >= PACK_HEAD_LEN) {
        // 取出前四字节(固定的头，里面包含这条JSON信息的长度)
        uint32_t body_len;  // 表示当前帧还有多少没有读完
        memcpy(&body_len, read_buf.data(), sizeof(body_len));
        body_len = ntohl(body_len);

        if (body_len == 0) {  // 表示收到了完整的一帧
            read_buf.erase(0, PACK_HEAD_LEN);
            continue;
        }
        if (body_len > MAX_JSON_BODY_LEN) {
            closeConnection();
            return;
        }
        if (read_buf.size() < PACK_HEAD_LEN + body_len) {
            break;
        }

        std::string frame = read_buf.substr(PACK_HEAD_LEN, body_len);
        read_buf.erase(0, PACK_HEAD_LEN + body_len);

        auto self = shared_from_this();
        worker_pool.enqueue([self, frame = std::move(frame)] {
            try {
                dispatch_message(self, frame);
            } catch (const std::exception& e) {
                LOG(ERROR) << "dispatch_message exception: " << e.what();
                send_error(self, ERR_SERVER_BUSY, 0, "服务器内部错误");
            } catch (...) {
                LOG(ERROR) << "dispatch_message unknown exception";
                send_error(self, ERR_SERVER_BUSY, 0, "服务器内部错误");
            }
        });
    }
}

void MainReactor::run() {
    std::vector<epoll_event> event(16);
    while (!stop_ && !shutdown_.load(std::memory_order_relaxed)) {
        // 等待事件
        int n = epoll_wait(epfd, event.data(), event.size(), -1);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            LOG(ERROR) << "MainReactor epoll_wait";
            break;
        }
        for (int i = 0; i < n; i++) {
            // 只处理监听socket的可读事件就是listenfd的EPOLLIN
            if (event[i].data.fd == listenfd) {
                acceptConn();
            } else if (event[i].data.fd == wakeupfd_) {
                uint64_t val;
                while (read(wakeupfd_, &val, sizeof(val)) == sizeof(val)) {
                }

                if (shutdown_.load(std::memory_order_relaxed)) {
                    stop_ = true;
                    break;
                }
            }
        }
    }
    LOG(INFO) << "MainReactor::run() exited";
}

void MainReactor::stop() {
    stop_ = true;
    uint64_t one = 1;
    ssize_t n = write(wakeupfd_, &one, sizeof(one));
    if (n != sizeof(one) && errno != EAGAIN) {
        LOG(ERROR) << "MainReactor wakeup failed";
    }
}

void MainReactor::acceptConn() {
    while (true) {
        sockaddr_in clientAddr;
        socklen_t len = sizeof(clientAddr);
        // accept4直接创建非阻塞socket
        int connFd =
            accept4(listenfd, (sockaddr*)&clientAddr, &len, SOCK_NONBLOCK);

        if (connFd < 0) {
            // 没有更多连接可接受
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            LOG(ERROR) << "accept";
            break;
        }

        // 轮询选择一个SubReactor（负载均衡）
        EventLoop* loop = subloops[round];
        round = (round + 1) % subloops.size();

        // 创建TcpConnection并注册到该EventLoop
        // 必须通过addTask跨线程投递，因为EventLoop运行在不同线程
        loop->addtask([loop, connFd]() {
            // 先创建 shared_ptr
            auto conn = std::make_shared<TCPConnection>(connFd, loop);
            // 把 shared_ptr 存进 EventLoop，让它持有一份引用
            loop->addConnection(connFd, conn);
            // conn 离开作用域，但 EventLoop 里还有一份，所以对象不会析构
        });
    }
}