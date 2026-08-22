#include "mysql_pool.h"
#include <chrono>
#include <cstring>
#include "logger.h"

MysqlPool& MysqlPool::GetInstance() {
    static MysqlPool instance;
    return instance;
}

bool MysqlPool::Init(const std::string& host, int port, const std::string& user, const std::string& pwd, const std::string& db, int min_size, int max_size) {
    m_host = host;
    m_port = port;
    m_user = user;
    m_pwd = pwd;
    m_db = db;
    min_pool_size_ = min_size;
    max_pool_size_ = max_size;
    active_count_ = 0;
    is_running_ = true;

    // 创建核心连接
    for (int i = 0; i < min_pool_size_; ++i) {
        MYSQL* conn = CreateOneConn();
        if (!conn) {
            Destroy();
            return false;
        }
        conn_queue_.push(conn);
    }

    check_thread_ = std::thread(&MysqlPool::IdleCheckThread, this);
    LOG(INFO) << "MySQL连接池初始化完成, 核心:" << min_pool_size_ << ", 最大:" << max_pool_size_;
    return true;
}

MYSQL* MysqlPool::CreateOneConn() {
    MYSQL* conn = mysql_init(nullptr);
    if (!conn)
        return nullptr;

    conn = mysql_real_connect(conn, m_host.c_str(), m_user.c_str(), m_pwd.c_str(), m_db.c_str(), m_port, nullptr, 0);
    if (!conn) {
        LOG(ERROR) << "MySQL连接失败: "
                   << (conn ? mysql_error(conn) : "unknown");
        if (conn)
            mysql_close(conn);
        return nullptr;
    }
    mysql_query(conn, "SET NAMES utf8mb4");
    return conn;
}

bool MysqlPool::IsConnValid(MYSQL* conn) {
    if (!conn)
        return false;
    return mysql_ping(conn) == 0;  // 只检测，不关闭
}

MYSQL* MysqlPool::GetConn(int timeout_ms) {
    std::unique_lock<std::mutex> lock(m_mtx);
    // 如果队列为空且未达上限，动态创建临时连接（先释放锁，避免CreateOneConn阻塞其他线程）
    if (conn_queue_.empty() &&
        (active_count_ + conn_queue_.size()) < max_pool_size_) {
        lock.unlock();
        MYSQL* conn = CreateOneConn();
        lock.lock();
        if (conn) {
            active_conns_.insert(conn);
            active_count_++;
            return conn;
        }
    }
    // 否则等待空闲连接
    if (!m_cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this] { return !conn_queue_.empty(); })) {
        LOG(WARNING) << "获取MySQL连接超时";
        return nullptr;
    }
    MYSQL* conn = conn_queue_.front();
    conn_queue_.pop();
    // 出队时校验有效性（同样先释放锁再创建连接）
    if (!IsConnValid(conn)) {
        mysql_close(conn);
        lock.unlock();
        conn = CreateOneConn();
        lock.lock();
    }
    if (conn) {
        active_conns_.insert(conn);
        active_count_++;
    }
    return conn;
}

void MysqlPool::ReturnConn(MYSQL* conn) {
    if (!conn)
        return;

    // 强制回滚可能残留的事务，保证连接干净
    mysql_query(conn, "ROLLBACK");

    std::lock_guard<std::mutex> lock(m_mtx);
    active_conns_.erase(conn);
    active_count_--;

    // 动态收缩：如果空闲连接已达核心数，直接关闭多余连接
    if (conn_queue_.size() >= static_cast<size_t>(min_pool_size_)) {
        mysql_close(conn);
        LOG(INFO) << "归还多余连接，当前空闲:" << conn_queue_.size();
    } else {
        conn_queue_.push(conn);
    }
    m_cv.notify_one();
}

// 执行查询，返回结果集
QueryResult MysqlPool::Query(const std::string& sql, const std::vector<std::string>& params) {
    MysqlConnGuard guard(3000);              // 自动借
    return Query(guard.Get(), sql, params);  // 借到了就调用下面那个函数
}

int MysqlPool::ExecuteStmt(const std::string& sql, const std::vector<std::string>& params) {
    MysqlConnGuard guard(3000);
    return ExecuteStmt(guard.Get(), sql, params);
}

// 事务专用（传入连接，不借不还）
QueryResult MysqlPool::Query(MYSQL* conn, const std::string& sql, const std::vector<std::string>& params) {
    QueryResult result;
    if (!conn)
        return result;  // 传入的连接空了直接返回

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt)
        return result;
    if (mysql_stmt_prepare(stmt, sql.c_str(), sql.size()) != 0) {
        mysql_stmt_close(stmt);
        return result;
    }
    std::vector<MYSQL_BIND> bind_params(params.size());
    std::vector<unsigned long> lengths(params.size());
    for (size_t i = 0; i < params.size(); ++i) {
        lengths[i] = params[i].size();
        memset(&bind_params[i], 0, sizeof(MYSQL_BIND));
        bind_params[i].buffer_type = MYSQL_TYPE_STRING;
        bind_params[i].buffer = const_cast<char*>(params[i].c_str());
        bind_params[i].buffer_length = lengths[i];
        bind_params[i].length = &lengths[i];
    }
    if (!params.empty() &&
        mysql_stmt_bind_param(stmt, bind_params.data()) != 0) {
        mysql_stmt_close(stmt);
        return result;
    }
    if (mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        return result;
    }
    if (mysql_stmt_store_result(stmt) != 0) {
        mysql_stmt_close(stmt);
        return result;
    }
    MYSQL_RES* metadata = mysql_stmt_result_metadata(stmt);
    if (!metadata) {
        mysql_stmt_free_result(stmt);
        mysql_stmt_close(stmt);
        return result;
    }
    int num_cols = mysql_num_fields(metadata);
    MYSQL_FIELD* fields = mysql_fetch_fields(metadata);
    for (int i = 0; i < num_cols; ++i)
        result.columns.push_back(fields[i].name);
    mysql_free_result(metadata);

    std::vector<MYSQL_BIND> bind_cols(num_cols);
    std::vector<std::vector<char>> col_buffers(num_cols);
    std::vector<unsigned long> col_lengths(num_cols);
    memset(bind_cols.data(), 0, num_cols * sizeof(MYSQL_BIND));
    const size_t INITIAL_BUF_SIZE = 4096;
    for (int i = 0; i < num_cols; ++i) {
        col_buffers[i].resize(INITIAL_BUF_SIZE);
        bind_cols[i].buffer_type = MYSQL_TYPE_STRING;
        bind_cols[i].buffer = col_buffers[i].data();
        bind_cols[i].buffer_length = INITIAL_BUF_SIZE;
        bind_cols[i].length = &col_lengths[i];
    }
    if (mysql_stmt_bind_result(stmt, bind_cols.data()) != 0) {
        mysql_stmt_free_result(stmt);
        mysql_stmt_close(stmt);
        return result;
    }
    while (true) {
        int ret = mysql_stmt_fetch(stmt);
        if (ret == MYSQL_NO_DATA)
            break;
        if (ret != 0 && ret != MYSQL_DATA_TRUNCATED)
            break;

        std::vector<std::string> row(num_cols);
        for (int i = 0; i < num_cols; ++i) {
            if (col_lengths[i] == 0) {
                row[i] = "";
                continue;
            }
            if (col_lengths[i] >= col_buffers[i].size()) {
                col_buffers[i].resize(col_lengths[i] + 1);
                bind_cols[i].buffer = col_buffers[i].data();
                bind_cols[i].buffer_length = col_buffers[i].size();
                if (mysql_stmt_fetch_column(stmt, &bind_cols[i], i, 0) != 0) {
                    row[i] = "";
                    continue;
                }
            }
            row[i] = std::string(col_buffers[i].data(), col_lengths[i]);
        }
        result.rows.push_back(std::move(row));
    }
    mysql_stmt_free_result(stmt);
    mysql_stmt_close(stmt);
    return result;
}

int MysqlPool::ExecuteStmt(MYSQL* conn, const std::string& sql, const std::vector<std::string>& params) {
    if (!conn)
        return -1;
    std::vector<int> types(params.size(), MYSQL_TYPE_STRING);
    auto res = ExecuteStmtInternal(conn, sql, types, params, false, false);
    return res.ok ? res.affected_rows : -1;
}

size_t MysqlPool::Size() const {
    return active_count_ + conn_queue_.size();
}
size_t MysqlPool::Idle() const {
    return conn_queue_.size();
}

void MysqlPool::IdleCheckThread() {
    std::unique_lock<std::mutex> lock(m_mtx);
    while (is_running_) {
        // 最多每 60 秒巡检一次，
        // Destroy() 时 notify_all() 可以立刻唤醒这里
        if (m_cv.wait_for(lock, std::chrono::seconds(60), [this] { return !is_running_; })) {
            // 被 Destroy() 唤醒
            break;
        }

        int sz = conn_queue_.size();
        for (int i = 0; i < sz; ++i) {
            MYSQL* conn = conn_queue_.front();
            conn_queue_.pop();
            if (!IsConnValid(conn)) {
                mysql_close(conn);
                conn = CreateOneConn();
                if (!conn) {
                    continue;
                }
            }
            conn_queue_.push(conn);
        }
    }
    LOG(INFO) << "MySQL idle check thread stopped.";
}

void MysqlPool::Destroy() {
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        if (!is_running_) {
            // 防止 Destroy() 被调用多次
            return;
        }
        is_running_ = false;
    }

    // 唤醒 IdleCheckThread，让它立即退出
    m_cv.notify_all();

    if (check_thread_.joinable())
        check_thread_.join();

    std::lock_guard<std::mutex> lock(m_mtx);
    while (!conn_queue_.empty()) {
        MYSQL* conn = conn_queue_.front();
        conn_queue_.pop();
        if (conn) {
            mysql_close(conn);
        }
    }
    for (MYSQL* c : active_conns_) {
        if (c) {
            mysql_close(c);
        }
    }
    active_conns_.clear();
    active_count_ = 0;
    LOG(INFO) << "MySQL pool destroyed.";
}

MysqlPool::~MysqlPool() {
    Destroy();
}

// 内部通用预处理执行
MysqlPool::StmtResult MysqlPool::ExecuteStmtInternal(
    MYSQL* conn,
    const std::string& sql,
    const std::vector<int>& param_types,
    const std::vector<std::string>& params,
    bool get_insert_id,
    bool store_result) {
    StmtResult res;
    if (!conn)
        return res;

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt)
        return res;

    if (mysql_stmt_prepare(stmt, sql.c_str(), sql.length()) != 0) {
        mysql_stmt_close(stmt);
        return res;
    }

    std::vector<MYSQL_BIND> bind(params.size());
    memset(bind.data(), 0, bind.size() * sizeof(MYSQL_BIND));
    for (size_t i = 0; i < params.size(); ++i) {
        if (param_types[i] == MYSQL_TYPE_NULL) {
            bind[i].buffer_type = MYSQL_TYPE_NULL;
            bind[i].buffer = nullptr;
            bind[i].buffer_length = 0;
        } else {
            bind[i].buffer_type = MYSQL_TYPE_STRING;
            bind[i].buffer = const_cast<char*>(params[i].c_str());
            bind[i].buffer_length = params[i].size();
        }
    }
    if (params.size() > 0 && mysql_stmt_bind_param(stmt, bind.data()) != 0) {
        mysql_stmt_close(stmt);
        return res;
    }
    if (mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        return res;
    }
    if (get_insert_id)
        res.insert_id = mysql_stmt_insert_id(stmt);
    res.affected_rows = mysql_stmt_affected_rows(stmt);
    if (store_result && mysql_stmt_store_result(stmt) == 0) {
        res.num_rows = mysql_stmt_num_rows(stmt);
        mysql_stmt_free_result(stmt);
    }
    mysql_stmt_close(stmt);
    res.ok = true;
    return res;
}

// 辅助功能
int MysqlPool::ExecuteExist(const std::string& sql, const std::vector<std::string>& params) {
    // 直接执行传入的查询语句（如 SELECT id FROM users WHERE ... LIMIT 1）
    std::vector<int> types(params.size(), MYSQL_TYPE_STRING);
    MysqlConnGuard guard(3000);
    auto res = ExecuteStmtInternal(guard.Get(), sql, types, params, false, true);
    if (!res.ok)
        return -1;
    return res.num_rows;  // num_rows 是 SELECT 返回的实际行数
}

uint64_t MysqlPool::ExecuteInsert(
    const std::string& sql,
    const std::vector<std::optional<std::string>>& params) {
    std::vector<int> types;
    std::vector<std::string> values;
    types.reserve(params.size());
    values.reserve(params.size());
    for (const auto& opt : params) {
        if (opt.has_value()) {
            types.push_back(MYSQL_TYPE_STRING);
            values.push_back(opt.value());
        } else {
            types.push_back(MYSQL_TYPE_NULL);
            values.push_back("");
        }
    }
    MysqlConnGuard guard(3000);
    auto res =
        ExecuteStmtInternal(guard.Get(), sql, types, values, true, false);
    return res.ok ? res.insert_id : 0;
}