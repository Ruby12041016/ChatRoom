#ifndef REDIS_POOL_H
#define REDIS_POOL_H

#include <hiredis/hiredis.h>
#include <condition_variable>
#include <map>
#include <mutex>
#include <queue>
#include <set>
#include <string>
#include <thread>
#include <vector>

class RedisPool {
   public:
    static RedisPool& GetInstance();

    bool Init(const std::string& host, int port, const std::string& pwd, int redis_db, int min_size = 8, int max_size = 32);

    redisContext* GetConn(int timeout_ms = 3000);
    void ReturnConn(redisContext* conn);

    // String
    bool string_setex(const std::string& key, const std::string& value, int seconds);
    std::string string_get(const std::string& key);
    bool string_del(const std::string& key);

    // Set
    bool set_add(const std::string& key, const std::string& member);
    bool set_rem(const std::string& key, const std::string& member);
    bool set_ismember(const std::string& key, const std::string& member);

    // 监控
    size_t Size() const;
    size_t Idle() const;

    void Destroy();

   private:
    RedisPool() = default;
    ~RedisPool();
    RedisPool(const RedisPool&) = delete;
    RedisPool& operator=(const RedisPool&) = delete;

    redisContext* CreateOneConn();
    bool IsConnValid(redisContext* conn);
    void IdleCheckThread();

    std::queue<redisContext*> conn_queue_;
    std::set<redisContext*> active_conns_;
    std::mutex m_mtx;
    std::condition_variable m_cv;
    int min_pool_size_ = 8;
    int max_pool_size_ = 32;
    int active_count_ = 0;
    bool is_running_ = false;

    std::string m_host;
    int m_port = 6379;
    std::string m_pwd;
    int m_db = 0;

    std::thread check_thread_;
};

class RedisConnGuard {
   public:
    explicit RedisConnGuard(int timeout_ms = 3000) {
        conn_ = RedisPool::GetInstance().GetConn(timeout_ms);
    }
    ~RedisConnGuard() {
        if (conn_)
            RedisPool::GetInstance().ReturnConn(conn_);
    }
    redisContext* Get() { return conn_; }
    bool IsValid() { return conn_ != nullptr; }

   private:
    redisContext* conn_ = nullptr;
};

#endif
