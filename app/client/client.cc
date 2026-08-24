#include "client.h"
#include <netdb.h>
#include <cstring>

// 建立连接，返回是否成功
bool Client::connectServer() {
    // 防止重复连接
    if (connected_)
        return true;
    // 上一次没有清理
    if (sockfd != -1)
        close_conn();
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd<0){
        LOG(ERROR) << "Client socket filed";
        return false;
    }

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    std::string port_str = std::to_string(port_);
    if (getaddrinfo(ip_.c_str(), port_str.c_str(), &hints, &res) != 0) {
        LOG(ERROR) << "无法解析地址: " << ip_;
        close(sockfd);
        sockfd = -1;
        return false;
    }

    if (::connect(sockfd, res->ai_addr, res->ai_addrlen) < 0) {
        freeaddrinfo(res);
        LOG(ERROR) << "连接服务器失败";
        connected_ = false;
        close(sockfd);
        sockfd = -1;
        return false;
    }
    connected_ = true;
    LOG(INFO) << "已连接到服务器 " << ip_ << ":" << port_;
    recv_thread_ = std::thread(&Client::recvLoop, this);
    worker_thread_ = std::thread(&Client::processLoop, this);
    return true;
}

// 主动关闭
void Client::close_conn() {
    disconnect();
    
    if (recv_thread_.joinable() &&
        recv_thread_.get_id() != std::this_thread::get_id()) {
        recv_thread_.join();
    }
    if (worker_thread_.joinable() &&
        worker_thread_.get_id() != std::this_thread::get_id()) {
        worker_thread_.join();
    }
}

// 发送JSON消息（会自动添加长度头）
bool Client::send_json(const std::string& json_massage) {
    if(!connected_)
        return false;
    std::string all_mass = pack(json_massage);
    std::lock_guard<std::mutex> lock(send_mutex_);
    bool ok = send_all(all_mass);
    if (!ok) {
        LOG(ERROR) << "send failed";
        disconnect();
    }
    return ok;
}

// 注册消息处理回调
void Client::set_MassageHandle(MessageHandler handler) {
    msg_handler_ = std::move(handler);
}

// 接收线程函数
void Client::recvLoop() {
    std::string read_buf;
    char temp[4096];
    while (connected_) {
        // 原子读取一次，存入局部fd，后续循环内只用局部变量，防止fd竞争
        int fd = sockfd.load();
        if (fd == -1) {
            break;
        }

        ssize_t size = recv(fd, temp, sizeof(temp),0);
        if (size == 0) {
            LOG(INFO) << "\nServer closed";
            disconnect();
            break;
        }
        if(size<0){
            if (errno == EINTR)
                continue;
            LOG(INFO) << "Server disconnected";
            disconnect();
            break;
        }
        read_buf.append(temp, size);
        while (read_buf.size() >= PACK_HEAD_LEN){
            uint32_t body_len;
            memcpy(&body_len, read_buf.data(), PACK_HEAD_LEN);
            body_len = ntohl(body_len);
            if (body_len==0||body_len > MAX_PACKET_SIZE) {
                LOG(ERROR) << "数据包错误";
                disconnect();
                return;
            }
            if(read_buf.size()<PACK_HEAD_LEN+body_len){   
                break;
            }
            std::string body = read_buf.substr(PACK_HEAD_LEN, body_len);
            read_buf.erase(0, PACK_HEAD_LEN + body_len);

            {
                std::lock_guard<std::mutex> lock(queue_mutex_);
                msg_queue_.push(std::move(body));
            }
            cond_.notify_one();
        }
    }
}

// 打包：4字节长度头 + body
std::string Client::pack(const std::string& body) {
    std::string result;
    result.resize(PACK_HEAD_LEN);
    uint32_t len = htonl(body.size());
    memcpy(&result[0], &len, PACK_HEAD_LEN);
    result.append(body);
    return result;
}

// 低层发送（处理阻塞写入）
bool Client::send_all(const std::string& data) {
    size_t num = 0;
    while(num<data.size()){
        int fd = sockfd.load();
        if (fd == -1) {
            return false;
        }
        ssize_t n = send(fd, data.data()+num, data.size()-num,MSG_NOSIGNAL);
        if (n == 0) {
            return false;
        }
        if(n<0){
            if (errno == EINTR)
                continue;
            LOG(ERROR) << "客户端写入失败";
            return false;
        }
        num += n;
    }
    return true;
}

void Client::disconnect() {
    // 无论 connected_ 当前是什么状态，都先标记为断开
    connected_.store(false);
    // 只允许一个线程真正拿走 socket
    int fd = sockfd.exchange(-1);
    
    if (fd != -1) {
        shutdown(fd, SHUT_RDWR);
        close(fd);
    }
    cond_.notify_all();
}

void Client::processLoop(){
    while(connected_){
        std::string mass;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            cond_.wait(lock, [&] { return !msg_queue_.empty() || !connected_; });
            if(!connected_){
                break;
            }

            mass = std::move(msg_queue_.front());
            msg_queue_.pop();
        }
        if (msg_handler_){
            msg_handler_(mass);
        }
    }
}

void Client::setServer(const std::string& ip, int port) {
    ip_ = ip;
    port_ = port;
}