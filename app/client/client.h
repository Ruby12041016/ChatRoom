#ifndef CLIENT_H
#define CLIENT_H

#include "global.h"
#include "logger.h"

using MessageHandler = std::function<void(const std::string& json_msg)>;

class Client{
    public:
     
     Client(const std::string& ip, int port):ip_(ip),port_(port),sockfd(-1){}

     ~Client() { close_conn(); }

     bool connectServer();  // 建立连接，返回是否成功
     void close_conn();    // 主动关闭

     // 发送JSON消息（会自动添加长度头）
     bool send_json(const std::string& json_massage);
     // 注册消息处理回调
     void set_MassageHandle(MessageHandler handler);

     bool isConnected() const { return connected_.load(); }

     void setServer(const std::string& ip, int port);

    private:
     void recvLoop();  // 接收线程函数
     // 打包：4字节长度头 + body
     std::string pack(const std::string& body);
     // 低层发送（处理阻塞写入）
     bool send_all(const std::string& data);
     // 统一断开
     void disconnect();
     void processLoop();

     std::atomic<int> sockfd{-1};
     std::string ip_;
     int port_;
     std::atomic<bool> connected_{false};
     std::thread recv_thread_;
     MessageHandler msg_handler_;
     std::mutex send_mutex_;

     std::queue<std::string> msg_queue_;
     std::mutex queue_mutex_;
     std::condition_variable cond_;
     std::thread worker_thread_;
};

#endif
