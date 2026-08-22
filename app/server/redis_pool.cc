#include "redis_pool.h"
#include <chrono>
#include <cstring>
#include "logger.h"
#include <random>

RedisPool& RedisPool::GetInstance() {
    static RedisPool instance;
    return instance;
}

bool RedisPool::Init(const std::string& host, int port, const std::string& pwd, int redis_db, int min_size, int max_size) {
    m_host = host;
    m_port = port;
    m_pwd = pwd;
    m_db = redis_db;
    min_pool_size_ = min_size;
    max_pool_size_ = max_size;
    active_count_ = 0;
    is_running_ = true;

    for (int i = 0; i < min_pool_size_; ++i) {
        redisContext* conn = CreateOneConn();
        if (!conn) {
            Destroy();
            return false;
        }
        conn_queue_.push(conn);
    }

    check_thread_ = std::thread(&RedisPool::IdleCheckThread, this);
    LOG(INFO) << "Redis连接池初始化完成, 核心:" << min_pool_size_
              << ", 最大:" << max_pool_size_;
    return true;
}

redisContext* RedisPool::CreateOneConn() {
    struct timeval conn_timeout = {5, 0};
    redisContext* conn = redisConnectWithTimeout(m_host.c_str(), m_port, conn_timeout);
    if (!conn || conn->err) {
        LOG(ERROR) << "redis connect error: " << (conn ? conn->errstr : "null");
        if (conn)
            redisFree(conn);
        return nullptr;
    }
    struct timeval cmd_timeout = {3, 0};
    redisSetTimeout(conn, cmd_timeout);

    if (!m_pwd.empty()) {
        redisReply* reply = (redisReply*)redisCommand(conn, "AUTH %b", m_pwd.data(), m_pwd.size());
        if (!reply || reply->type == REDIS_REPLY_ERROR) {
            LOG(ERROR) << "redis auth fail";
            if (reply)
                freeReplyObject(reply);
            redisFree(conn);
            return nullptr;
        }
        freeReplyObject(reply);
    }

    if (m_db != 0) {
        redisReply* reply = (redisReply*)redisCommand(conn, "SELECT %d", m_db);
        if (reply && reply->type == REDIS_REPLY_ERROR) {
            LOG(ERROR) << "redis select db fail";
            freeReplyObject(reply);
            redisFree(conn);
            return nullptr;
        }
        if (reply)
            freeReplyObject(reply);
    }
    return conn;
}

bool RedisPool::IsConnValid(redisContext* conn) {
    if (!conn || conn->err)
        return false;
    redisReply* reply = (redisReply*)redisCommand(conn, "PING");
    bool ok = (reply && reply->type == REDIS_REPLY_STATUS);
    if (reply)
        freeReplyObject(reply);
    return ok;
}

redisContext* RedisPool::GetConn(int timeout_ms) {
    std::unique_lock<std::mutex> lock(m_mtx);
    // 动态创建临时连接
    if (conn_queue_.empty() && (active_count_ + conn_queue_.size()) < max_pool_size_) {
        redisContext* conn = CreateOneConn();
        if (conn) {
            active_conns_.insert(conn);
            active_count_++;
            return conn;
        }
    }
    // 等待空闲连接
    if (!m_cv.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                       [this] { return !conn_queue_.empty(); })) {
        LOG(WARNING) << "获取Redis连接超时";
        return nullptr;
    }
    redisContext* conn = conn_queue_.front();
    conn_queue_.pop();
    if (!IsConnValid(conn)) {
        redisFree(conn);
        conn = CreateOneConn();
    }
    if (conn) {
        active_conns_.insert(conn);
        active_count_++;
    }
    return conn;
}

void RedisPool::ReturnConn(redisContext* conn) {
    if (!conn)
        return;

    if (!IsConnValid(conn)) {
        redisFree(conn);
        conn = CreateOneConn();
    }

    std::lock_guard<std::mutex> lock(m_mtx);
    active_conns_.erase(conn);
    active_count_--;

    if (conn) {
        // 动态收缩：空闲超过核心数则释放
        if (conn_queue_.size() >= static_cast<size_t>(min_pool_size_)) {
            redisFree(conn);
            LOG(INFO) << "归还多余Redis连接，空闲:" << conn_queue_.size();
        } else {
            conn_queue_.push(conn);
        }
    } else {
        LOG(ERROR) << "归还连接失败：连接无效且重建失败";
    }
    m_cv.notify_one();
}

// String操作
// 读取key对应值
std::string RedisPool::string_get(const std::string& key) {
    RedisConnGuard guard;
    if (!guard.IsValid())
        return "";
    redisReply* reply = (redisReply*)redisCommand(guard.Get(), "GET %b", key.data(), key.size());
    std::string res;
    if (reply && reply->type == REDIS_REPLY_STRING)
        res.assign(reply->str, reply->len);
    if (reply)
        freeReplyObject(reply);
    return res;
}
// 带过期时间的key-value
bool RedisPool::string_setex(const std::string& key, const std::string& value, int seconds) {
    RedisConnGuard guard;
    if (!guard.IsValid())
        return false;
    redisReply* reply = (redisReply*)redisCommand( guard.Get(), "SETEX %b %d %b", key.data(), key.size(), seconds, value.data(), value.size());
    bool ok = (reply && reply->type == REDIS_REPLY_STATUS);
    if (reply)
        freeReplyObject(reply);
    return ok;
}
// 删除值
bool RedisPool::string_del(const std::string& key) {
    RedisConnGuard guard;
    if (!guard.IsValid())
        return false;
    redisReply* reply = (redisReply*)redisCommand(guard.Get(), "DEL %b", key.data(), key.size());
    bool ok = (reply && reply->type == REDIS_REPLY_INTEGER);
    if (reply)
        freeReplyObject(reply);
    return ok;
}

// Set操作
bool RedisPool::set_add(const std::string& key, const std::string& member) {
    RedisConnGuard guard;
    if (!guard.IsValid())
        return false;
    redisReply* reply =
        (redisReply*)redisCommand(guard.Get(), "SADD %b %b", key.data(), key.size(), member.data(), member.size());
    bool ok = (reply && reply->type == REDIS_REPLY_INTEGER);
    if (reply)
        freeReplyObject(reply);
    return ok;
}

bool RedisPool::set_rem(const std::string& key, const std::string& member) {
    RedisConnGuard guard;
    if (!guard.IsValid())
        return false;
    redisReply* reply = (redisReply*)redisCommand(guard.Get(), "SREM %b %b", key.data(), key.size(), member.data(), member.size());
    bool ok = (reply && reply->type == REDIS_REPLY_INTEGER);
    if (reply)
        freeReplyObject(reply);
    return ok;
}

bool RedisPool::set_ismember(const std::string& key, const std::string& member) {
    RedisConnGuard guard;
    if (!guard.IsValid())
        return false;
    redisReply* reply =
        (redisReply*)redisCommand(guard.Get(), "SISMEMBER %b %b", key.data(), key.size(), member.data(), member.size());
    bool ok = (reply && reply->type == REDIS_REPLY_INTEGER && reply->integer == 1);
    if (reply)
        freeReplyObject(reply);
    return ok;
}

// ----------------- 监控 & 销毁 -----------------
size_t RedisPool::Size() const {
    return active_count_ + conn_queue_.size();
}
size_t RedisPool::Idle() const {
    return conn_queue_.size();
}

void RedisPool::IdleCheckThread() {
    while (is_running_) {
        std::unique_lock<std::mutex> lock(m_mtx);
            if (m_cv.wait_for(lock, std::chrono::seconds(60),[this] { 
                return !is_running_; 
            })){
                break;
            }
        int sz = conn_queue_.size();
        for (int i = 0; i < sz; ++i) {
            redisContext* conn = conn_queue_.front();
            conn_queue_.pop();
            if (!IsConnValid(conn)) {
                redisFree(conn);
                conn = CreateOneConn();
                if (!conn) {
                    LOG(ERROR) << "巡检重建Redis连接失败";
                    continue;
                }
            }
            conn_queue_.push(conn);
        }
    }
}

void RedisPool::Destroy() {
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        if(!is_running_){
            return;
        }
        is_running_ = false;
    }
    // 立即唤醒 IdleCheckThread
    m_cv.notify_all();

    if (check_thread_.joinable())
        check_thread_.join();

    std::lock_guard<std::mutex> lock(m_mtx);
    while (!conn_queue_.empty()) {
        redisContext* conn = conn_queue_.front();
        conn_queue_.pop();
        redisFree(conn);
    }
    for (redisContext* c : active_conns_) {
        redisFree(c);
    }
    active_conns_.clear();
    active_count_ = 0;
    LOG(INFO) << "Redis pool destroyed.";
}

RedisPool::~RedisPool() {
    Destroy();
}
