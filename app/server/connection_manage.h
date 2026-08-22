#ifndef CONNECTION_MANAGER_H
#define CONNECTION_MANAGER_H

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>

// 前置声明告诉编译器TCPConnection是个类，具体定义在 .cpp 找
class TCPConnection;

class ConnectionManager {
   public:
    // 获取单例实例
    static ConnectionManager& GetInstance();
    // 禁止拷贝
    ConnectionManager(const ConnectionManager&) = delete;
    ConnectionManager& operator=(const ConnectionManager&) = delete;
    // 添加/更新在线用户连接
    void Add(uint64_t user_id, std::shared_ptr<TCPConnection> conn);
    // 移除离线用户的连接
    void Remove(uint64_t user_id);
    // 根据用户ID获取连接指针（如果在线）
    std::shared_ptr<TCPConnection> GetConnByUserId(uint64_t user_id);
   private:
    ConnectionManager() = default;
    ~ConnectionManager() = default;
    std::mutex m_mtx;
    // 使用 weak_ptr 防止循环引用，不影响连接自身的析构生命周期
    std::unordered_map<uint64_t, std::weak_ptr<TCPConnection>> connections_;
};

#endif 
