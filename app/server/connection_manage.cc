#include "connection_manage.h"
#include "server.h"  // 包含TCPConnection 定义

ConnectionManager& ConnectionManager::GetInstance() {
    static ConnectionManager instance;
    return instance;
}

void ConnectionManager::Add(uint64_t user_id, std::shared_ptr<TCPConnection> conn) {
    if (!conn)
        return;
    std::lock_guard<std::mutex> lock(m_mtx);
    connections_[user_id] = conn;  // 覆盖或插入
}

void ConnectionManager::Remove(uint64_t user_id) {
    std::lock_guard<std::mutex> lock(m_mtx);
    connections_.erase(user_id);
}

std::shared_ptr<TCPConnection> ConnectionManager::GetConnByUserId(uint64_t user_id) {
    std::lock_guard<std::mutex> lock(m_mtx);
    auto it = connections_.find(user_id);
    if (it == connections_.end()) {
        return nullptr;
    }
    // weak_ptr 需要 lock() 才能提升为 shared_ptr
    return it->second.lock();
}
