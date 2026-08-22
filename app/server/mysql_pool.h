#ifndef MYSQL_POOL_H
#define MYSQL_POOL_H

#include <mysql/mysql.h>
#include <condition_variable>
#include <mutex>
#include <optional>  // 用于 ExecuteStmtWithNull
#include <queue>
#include <set>
#include <string>
#include <thread>
#include <vector>

// 查询结果结构体
struct QueryResult {
    std::vector<std::string> columns;            // 列名列表
    std::vector<std::vector<std::string>> rows;  // 每行数据
};

class MysqlPool {
   public:
    static MysqlPool& GetInstance();

    // 初始化连接池（min_size: 核心连接数, max_size: 最大连接数）
    bool Init(const std::string& host, int port, const std::string& user, const std::string& pwd, const std::string& db, int min_size = 8, int max_size = 32);

    // 获取 / 归还连接
    MYSQL* GetConn(int timeout_ms = 3000);
    void ReturnConn(MYSQL* conn);

    // 基本操作
    // 参数化查询，返回完整结果集（自动处理长文本）
    QueryResult Query(const std::string& sql, const std::vector<std::string>& params = {});

    // 执行 UPDATE/DELETE 等，返回受影响行数（成功≥0，失败返回-1）
    int ExecuteStmt(const std::string& sql, const std::vector<std::string>& params);

    // 事务内执行（传入已持有的连接，不会重新借连接
    QueryResult Query(MYSQL* conn, const std::string& sql, const std::vector<std::string>& params);
    int ExecuteStmt(MYSQL* conn, const std::string& sql, const std::vector<std::string>& params);
    // 事务（需配合外部 RAII 使用，归还前务必提交/回滚）
    bool StartTrans(MYSQL* conn);
    bool Commit(MYSQL* conn);
    bool Rollback(MYSQL* conn);

    // 监控
    size_t Size() const;  // 当前连接总数（借出+空闲）
    size_t Idle() const;  // 当前空闲连接数

    void Destroy();
    // 查询是否存在，返回匹配行数（失败返回-1），内部自动优化为 COUNT(*)
    int ExecuteExist(const std::string& sql, const std::vector<std::string>& params);
    // 插入并返回自增ID，支持 std::nullopt 插入 NULL
    uint64_t ExecuteInsert(const std::string& sql, const std::vector<std::optional<std::string>>& params);

   private:
    MysqlPool() = default;
    ~MysqlPool();
    MysqlPool(const MysqlPool&) = delete;
    MysqlPool& operator=(const MysqlPool&) = delete;

    MYSQL* CreateOneConn();
    bool IsConnValid(MYSQL* conn);  // 仅检测，不关闭连接
    void IdleCheckThread();

    std::queue<MYSQL*> conn_queue_;  // 空闲连接队列
    std::set<MYSQL*> active_conns_;  // 被借出的连接
    std::mutex m_mtx;
    std::condition_variable m_cv;
    int min_pool_size_ = 8;
    int max_pool_size_ = 32;
    int active_count_ = 0;  // 借出连接数
    bool is_running_ = false;

    std::string m_host;
    int m_port = 3306;
    std::string m_user;
    std::string m_pwd;
    std::string m_db;

    std::thread check_thread_;

    struct StmtResult {
        bool ok = false;
        uint64_t insert_id = 0;
        int affected_rows = 0;
        int num_rows = 0;  // SELECT 结果行数
    };

    StmtResult ExecuteStmtInternal(
        MYSQL* conn,
        const std::string& sql,
        const std::vector<int>& param_types,
        const std::vector<std::string>& params,
        bool get_insert_id,
        bool store_result);
};

// RAII 连接守卫
class MysqlConnGuard {
   public:
    explicit MysqlConnGuard(int timeout_ms = 3000) {
        conn_ = MysqlPool::GetInstance().GetConn(timeout_ms);
    }
    ~MysqlConnGuard() {
        if (conn_)
            MysqlPool::GetInstance().ReturnConn(conn_);
    }
    MYSQL* Get() { return conn_; }
    bool IsValid() { return conn_ != nullptr; }

   private:
    MYSQL* conn_ = nullptr;
};

// RAII事务类
class TransactionGuard {
   public:
    explicit TransactionGuard(MYSQL* conn) : conn_(conn), committed_(false) {
        if (conn_)
            mysql_query(conn_, "START TRANSACTION");
    }
    ~TransactionGuard() {
        if (conn_ && !committed_) {
            mysql_query(conn_, "ROLLBACK");
        }
    }
    bool Commit() {
        if (conn_) {
            committed_ = true;
            return mysql_query(conn_, "COMMIT") == 0;
        }
        return false;
    }

   private:
    MYSQL* conn_;
    bool committed_;
};
#endif
