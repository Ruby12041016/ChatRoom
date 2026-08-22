#include "big_file.h"
#include "WorkerPool.h"

#include <filesystem>
#include <netinet/tcp.h>

#define MAX_NUM  5  // listen的backlog参数，表示监听队列的最大长度，最多同时处理5个待处理的连接
#define BUF_SIZE 1024*1024  // 读写缓冲区的大小，和客户端一致
#define THREAD_MAX 10  // 线程池的最大线程数
#define MAX_SIZE  10000  // epoll的最大事件数，最多同时处理10000个事件，也就是最多支持10000个客户端连接

static WorkerPool pool(THREAD_MAX);

// 清理字符串末尾换行符
static void delete_(std::string& s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n'))
        s.pop_back();
}

// 分割命令参数，和客户端类似
std::vector<std::string> split_cmd(const std::string& s, char flag) {
    std::vector<std::string> results;
    std::stringstream ss(s);
    std::string result;
    while (std::getline(ss, result, flag)) {
        delete_(result);
        if (!result.empty())
            results.push_back(result);
    }
    return results;
}

// 客户端状态结构体
struct Client {
    int cli_fd;            // 客户端的控制连接fd
    bool nowuser = false;  // 是否已经输入了用户名，等待密码
    bool islogin = false;  // 是否已经登录成功
    std::string username;  // 客户端的用户名
    std::mutex
        mutex;  // 保护这个客户端状态的互斥锁，避免多线程同时修改这个客户端的状态导致数据竞争
    std::string readbuf;    // 读缓冲区，非阻塞IO下，保存没读完的客户端数据
    std::string writebuf;   // 写缓冲区，非阻塞IO下，保存没发完的响应数据
    bool quit = false;      // 是否已经收到QUIT命令，准备关闭连接
    off_t rest_offset = 0;  // 断点续传起始字节偏移，由REST命令设置
};

// 保护clients map的互斥锁，多个线程可能同时访问这个map，所以需要加锁
std::mutex clients_mutex;
// 存储所有客户端的map，键是客户端的控制连接fd，值是客户端的状态结构体
std::map<int, Client> clients;
// epoll的事件数组，用于存储 epoll_wait返回的就绪事件
struct epoll_event events[MAX_SIZE];

// 将文件描述符设置为非阻塞模式
void set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int epfd = -1;

// 封装epoll的添加函数，将fd添加到epoll的监听列表，监听指定的事件
void epoll_add(int fd, uint32_t events) {
    struct epoll_event envent;
    envent.events = events;
    envent.data.fd = fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &envent);
}

// 封装epoll的删除函数，将fd从epoll的监听列表中删除
void epoll_delete(int fd, uint32_t events) {
    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
}

// 封装epoll的修改函数，修改fd的监听事件
void epoll_mod(int fd, uint32_t events) {
    struct epoll_event envent;
    envent.events = events;
    envent.data.fd = fd;
    epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &envent);
}

// 处理epoll的可写事件，也就是把writebuf里没发完的数据发送出去
void epoll_write(int fd) {
    ssize_t n;
    // 加锁，保护这个客户端的状态，避免多线程同时修改
    std::lock_guard<std::mutex> lock(clients[fd].mutex);
    // 循环发送writebuf里的数据，直到缓冲区空了
    while (!clients[fd].writebuf.empty()) {
        n = send(fd, clients[fd].writebuf.data(), clients[fd].writebuf.size(), 0);
        // 如果发送成功，把已经发出去的数据从缓冲区里删掉
        if (n > 0) {
            clients[fd].writebuf.erase(0, n);
        } else if (n < 0) {  // 如果发送返回EAGAIN，说明内核发送缓冲区满了，暂时发不出去，break等下次可写的时候再发
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            // 如果是其他错误，说明连接断开了
            close(fd);
            {
                std::lock_guard<std::mutex> map_lock(clients_mutex);
                clients.erase(fd);
            }
            epoll_delete(fd, 0);
            return;
        }
    }
    if (clients[fd].writebuf.empty()) {
        if (clients[fd].quit) {  // QUIT后所有数据已发完，关闭连接
            close(fd);
            std::lock_guard<std::mutex> map_lock(clients_mutex);
            clients.erase(fd);
            epoll_delete(fd, 0);
            return;
        }
        epoll_mod(fd, EPOLLET | EPOLLIN);
    } else {  // 修改epoll的事件，只监听读事件，因为没有数据要发了
        epoll_mod(fd, EPOLLIN | EPOLLET | EPOLLOUT);
    }
}
// 处理RETR命令（直接在控制连接上传输数据）
void do_retr(int connfd, const std::vector<std::string>& commonds) {
    // 线程池中运行，设为阻塞模式，避免sendfile/send返回EAGAIN
    int flags = fcntl(connfd, F_GETFL, 0);
    fcntl(connfd, F_SETFL, flags & ~O_NONBLOCK);

    int flag = 1;
    setsockopt(connfd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
    int sndbuf = 4 * 1024 * 1024;
    setsockopt(connfd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));

    // 读取续传偏移量
    off_t offset = 0;
    {
        std::lock_guard<std::mutex> lock(clients[connfd].mutex);
        offset = clients[connfd].rest_offset;
    }

    std::string filename = commonds[1];
    std::string filepath = "./files/" + filename;

    FILE* fp = fopen(filepath.c_str(), "rb");  // 打开要下载的文件
    if (!fp) {
        const char* err_msg = "550 File not found\r\n";
        send(connfd, err_msg, strlen(err_msg), 0);
        close(connfd);
        {
            std::lock_guard<std::mutex> map_lock(clients_mutex);
            clients.erase(connfd);
        }
        return;
    }
    int file_fp = fileno(fp);

    struct stat st;
    fstat(file_fp, &st);

    // 偏移合法性校验
    if (offset > st.st_size) {
        fclose(fp);
        const char* err_msg = "550 Restart offset exceeds file size\r\n";
        send(connfd, err_msg, strlen(err_msg), 0);
        close(connfd);
        {
            std::lock_guard<std::mutex> map_lock(clients_mutex);
            clients.erase(connfd);
        }
        return;
    }

    posix_fadvise(file_fp, offset, st.st_size - offset, POSIX_FADV_SEQUENTIAL);

    // 发送准备传输响应（直接 send，fd 已不在 epoll 中）
    const char* ready_msg = "150 Retr directory!\r\n";
    send(connfd, ready_msg, strlen(ready_msg), 0);

    while (offset < st.st_size) {
        ssize_t sent = sendfile(connfd, file_fp, &offset, st.st_size - offset);
        if (sent > 0)
            continue;
        if (sent < 0 && errno == EINTR)
            continue;
        LOG(ERROR) << "sendfile失败: " << strerror(errno) << std::endl;
        break;
    }

    fclose(fp);

    // 直接关闭写端通知客户端传输结束，不发送任何响应字符串
    // 避免 "226..." 混入文件数据流导致数据损坏
    shutdown(connfd, SHUT_WR);
    close(connfd);
    {
        std::lock_guard<std::mutex> map_lock(clients_mutex);
        clients.erase(connfd);
    }
}

// 处理客户端的STOR命令（直接在控制连接上传输数据）
void do_stor(int connfd, const std::vector<std::string>& commonds) {
    // 线程池中运行，设为阻塞模式，避免recv返回EAGAIN
    int flags = fcntl(connfd, F_GETFL, 0);
    fcntl(connfd, F_SETFL, flags & ~O_NONBLOCK);

    // TCP优化：增大接收缓冲区，填满高延迟链路的带宽管道
    int rcvbuf = 4 * 1024 * 1024;
    setsockopt(connfd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

    // 发送准备接收响应（直接 send，fd 已不在 epoll 中）
    const char* ready_msg = "150 STOR directory!\r\n";
    send(connfd, ready_msg, strlen(ready_msg), 0);

    // 读取续传偏移量
    off_t offset = 0;
    {
        std::lock_guard<std::mutex> lock(clients[connfd].mutex);
        offset = clients[connfd].rest_offset;
    }

    std::string filename = commonds[1];
    std::filesystem::create_directories("./files");
    std::string fullpath = "./files/" + filename;

    std::ofstream fp;
    if (offset == 0) {
        // 从头上传：默认截断模式
        fp.open(fullpath, std::ios::binary | std::ios::trunc);
    } else {
        // 断点续传：文件必须已存在，以读写模式打开（不截断），定位到偏移处
        if (!std::filesystem::exists(fullpath)) {
            const char* err_msg = "550 File does not exist for restart\r\n";
            send(connfd, err_msg, strlen(err_msg), 0);
            close(connfd);
            {
                std::lock_guard<std::mutex> map_lock(clients_mutex);
                clients.erase(connfd);
            }
            return;
        }
        fp.open(fullpath, std::ios::binary | std::ios::in | std::ios::out);
        if (fp.is_open()) {
            fp.seekp(offset);
            if (fp.fail()) {
                fp.close();
                const char* err_msg = "550 Invalid restart offset\r\n";
                send(connfd, err_msg, strlen(err_msg), 0);
                close(connfd);
                {
                    std::lock_guard<std::mutex> map_lock(clients_mutex);
                    clients.erase(connfd);
                }
                return;
            }
        }
    }

    if (!fp) {
        const char* err_msg = "550 Cannot create file\r\n";
        send(connfd, err_msg, strlen(err_msg), 0);
        close(connfd);
        {
            std::lock_guard<std::mutex> map_lock(clients_mutex);
            clients.erase(connfd);
        }
        return;
    }

    char buf[BUF_SIZE];
    // 循环从控制连接读取客户端发来的文件数据，写入本地文件，直到读完所有数据
    while (1) {
        int len = recv(connfd, buf, sizeof(buf), 0);
        if (len <= 0)
            break;
        fp.write(buf, len);
    }
    fp.close();

    // 发送完成响应，然后关闭连接
    const char* done_msg = "226 Stor compare!\r\n";
    send(connfd, done_msg, strlen(done_msg), 0);
    close(connfd);
    {
        std::lock_guard<std::mutex> map_lock(clients_mutex);
        clients.erase(connfd);
    }
}

void epoll_read(int connfd) {
    char buf[BUF_SIZE];
    while (1) {
        // 循环读取数据，因为是边缘触发，所以要一次性把所有可读的数据都读完，不然就不会再触发事件了
        memset(buf, 0, sizeof(buf));
        int n = recv(connfd, buf, sizeof(buf) - 1, 0);
        // 读取成功的话，把数据加到客户端的读缓冲区，因为非阻塞，可能一次读不完一个完整的命令，所以要存起来
        if (n > 0) {
            std::lock_guard<std::mutex> map_lock(clients_mutex);
            clients[connfd].readbuf.append(buf, n);
        } else if (n == 0) {
            // n=0说明客户端关闭了连接，输出提示，关闭fd，清理客户端的状态
            LOG(INFO) << "Client disconnect\n";
            close(connfd);
            std::lock_guard<std::mutex> map_lock(clients_mutex);
            clients.erase(connfd);
            epoll_delete(connfd, 0);
            return;
        } else {  // 如果返回 EAGAIN，说明所有数据都读完了
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;  // 读完所有数据
            else
                return;  // 出错直接返回
        }

        // 提取所有完整行（加锁）
        std::vector<std::string> lines;
        {
            std::lock_guard<std::mutex> map_lock(clients_mutex);
            std::string& rbuf = clients[connfd].readbuf;
            size_t pos;
            // ftp的命令都是以\r\n结尾的，所以按这个分割，把完整的命令行拿出来，剩下的不完整的留在缓冲区里，等下次数据到了再处理
            while ((pos = rbuf.find("\r\n")) != std::string::npos) {
                lines.push_back(rbuf.substr(0, pos));
                rbuf.erase(0, pos + 2);
            }
        }  // 释放锁

        // 处理每一行命令（不加锁，避免死锁）
        for (const auto& line : lines) {
            LOG(INFO) << "Client:" << line << std::endl;
            std::vector<std::string> commonds = split_cmd(line, ' ');
            if (commonds.empty())
                continue;
            if (commonds[0] == "SIZE" && commonds.size() >= 2) {
                // SIZE命令：查询远端文件大小，客户端续传前必须调用
                std::string filename = commonds[1];
                std::string filepath = "./files/" + filename;
                struct stat st;
                if (stat(filepath.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
                    std::lock_guard<std::mutex> lock(clients[connfd].mutex);
                    clients[connfd].writebuf +=
                        "213 " + std::to_string(st.st_size) + "\r\n";
                    epoll_mod(connfd, EPOLLIN | EPOLLOUT | EPOLLET);
                } else {
                    // 文件不存在 → 回复 "550 File not found"，客户端不会卡死
                    clients[connfd].writebuf += "550 File not found\r\n";
                    epoll_mod(connfd, EPOLLIN | EPOLLOUT | EPOLLET);
                }
            } else if (commonds[0] == "REST" && commonds.size() >= 2) {
                // REST命令：设置下一次传输的断点偏移
                off_t offset = std::stoll(commonds[1]);
                std::lock_guard<std::mutex> lock(clients[connfd].mutex);
                clients[connfd].rest_offset = offset;
                clients[connfd].writebuf += "350 Restarting at " +
                                            std::to_string(offset) +
                                            ". Send STOR/RETR to start.\r\n";
                epoll_mod(connfd, EPOLLIN | EPOLLOUT | EPOLLET);
            } else if (commonds[0] == "STOR" && commonds.size() >= 2) {
                // 先刷新写缓冲区，确保REST的350响应已发送
                epoll_write(connfd);
                {
                    std::lock_guard<std::mutex> map_lock(clients_mutex);
                    if (clients.find(connfd) == clients.end())
                        continue;
                }
                // 从epoll移除fd，防止主循环和线程池同时操作同一个fd
                epoll_delete(connfd, 0);
                pool.enqueue([connfd, commonds] { do_stor(connfd, commonds); });
            } else if (commonds[0] == "RETR" && commonds.size() >= 2) {
                // 先刷新写缓冲区，确保REST的350响应已发送
                epoll_write(connfd);
                {
                    std::lock_guard<std::mutex> map_lock(clients_mutex);
                    if (clients.find(connfd) == clients.end())
                        continue;
                }
                // 从epoll移除fd，防止主循环和线程池同时操作同一个fd
                epoll_delete(connfd, 0);
                pool.enqueue([connfd, commonds] { do_retr(connfd, commonds); });
            } else {
                std::lock_guard<std::mutex> lock(clients[connfd].mutex);
                clients[connfd].writebuf += "502 Command not Found\r\n";
                epoll_mod(connfd, EPOLLIN | EPOLLOUT | EPOLLET);
            }
        }
    }
}

BigFileServer::BigFileServer(int port) : port_(port), running_(false) {}

BigFileServer::~BigFileServer() {
    Stop();
}

void BigFileServer::Start() {
    if (running_.load())
        return;
    running_.store(true);
    worker_ = std::thread(&BigFileServer::Run, this);
}

void BigFileServer::Stop() {
    running_.store(false);
    if (worker_.joinable())
        worker_.join();
}

void BigFileServer::Run() {
    int listenfd, connfd;
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    addr.sin_addr.s_addr = INADDR_ANY;
    bind(listenfd, (sockaddr*)&addr, sizeof(addr));
    listen(listenfd, MAX_NUM);
    set_nonblock(listenfd);
    epfd = epoll_create1(0);
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = listenfd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, listenfd, &ev);

    LOG(INFO) << "FTP server listening on port " << port_;
    while (running_.load()) {
        int nfds = epoll_wait(epfd, events, MAX_SIZE, 1000);
        if (nfds < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        for (int i = 0; i < nfds; i++) {
            int fd = events[i].data.fd;
            if (fd == listenfd) {
                while (1) {
                    sockaddr_in cliAddr;
                    socklen_t cliLen = sizeof(cliAddr);
                    connfd = accept(listenfd, (sockaddr*)&cliAddr, &cliLen);
                    if (connfd == -1) {
                        break;
                    }
                    set_nonblock(connfd);
                    {
                        std::lock_guard<std::mutex> map_lock(clients_mutex);
                        clients[connfd].cli_fd = connfd;
                    }
                    {
                        std::lock_guard<std::mutex> lock(clients[connfd].mutex);
                        clients[connfd].writebuf +=
                            "220 Welcome to My FTP Server!\r\n";
                        epoll_add(connfd, EPOLLIN | EPOLLOUT | EPOLLET);
                    }
                    LOG(INFO)
                        << "New FTP Client: " << inet_ntoa(cliAddr.sin_addr);
                }
            }
            if (events[i].events & EPOLLIN) {
                epoll_read(fd);
            }
            if (events[i].events & EPOLLOUT) {
                epoll_write(fd);
            }
        }
    }
    close(epfd);
    close(listenfd);
    LOG(INFO) << "FTP server stopped.";
}