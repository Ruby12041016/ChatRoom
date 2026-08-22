# SQLite 总结与常用语法
## 一、SQLite 概述
SQLite 是一款**嵌入式、零配置、单文件**的轻量关系型数据库，没有独立的服务进程，直接编译进程序中运行，非常适合客户端本地缓存（如聊天历史）、嵌入式设备、小型应用场景。

核心特点：
- 单文件存储：整个数据库就是一个 `.db` 文件，复制、备份、迁移极其方便
- 零配置：无需安装、无需启动服务、无需账号权限
- 兼容标准 SQL92 大部分语法，支持事务、索引、视图等常用关系型数据库特性
- 跨平台：Windows/Linux/macOS/移动端全平台通用
- 极致轻量：编译后库体积仅几百KB，资源占用极低

## 二、数据类型（动态类型系统）
SQLite 采用**类型亲和**机制，列声明的类型只是“建议”，实际存储类型由值本身决定，核心只有 5 种存储类型：

| 存储类型 | 说明 | 对应项目场景 |
| :--- | :--- | :--- |
| `NULL` | 空值 | 可选字段为空 |
| `INTEGER` | 有符号整数 | 消息ID、用户ID、状态码、类型枚举 |
| `REAL` | 浮点数 | 金额、坐标等小数 |
| `TEXT` | 字符串 | 消息内容、文件名、时间戳、路径 |
| `BLOB` | 二进制数据 | 一般不推荐存大文件，建议存磁盘路径 |

> 常见类型亲和规则：`INT/INTEGER/BIGINT` → 归为 INTEGER；`VARCHAR/TEXT/CHAR` → 归为 TEXT；`FLOAT/DOUBLE` → 归为 REAL。

## 三、DDL 数据定义语法
### 1. 建表 `CREATE TABLE`
```sql
CREATE TABLE IF NOT EXISTS 表名 (
    列名1 类型 约束,
    列名2 类型 约束,
    ...
    [主键 / 唯一约束声明]
);
```
常用约束：
- `PRIMARY KEY`：主键，值唯一且非空
- `NOT NULL`：非空约束，不允许为空
- `DEFAULT 默认值`：插入时不赋值则使用默认值
- `UNIQUE`：唯一约束，值不能重复


### 2. 删表 `DROP TABLE`
```sql
DROP TABLE IF EXISTS 表名;
```
会永久删除表结构、所有数据、关联索引，不可恢复。

### 3. 改表 `ALTER TABLE`
SQLite 对改表支持非常有限，仅支持两种操作：
- 重命名表：`ALTER TABLE 旧表名 RENAME TO 新表名;`
- 新增列：`ALTER TABLE 表名 ADD COLUMN 列名 类型 约束;`

> ❌ 不支持：删除列、修改列类型/约束、重命名列（需通过“新建表→迁移数据→删旧表”的方式实现）。

### 4. 索引
索引是查询加速的核心手段，针对高频查询条件创建。
```sql
-- 普通联合索引
CREATE INDEX IF NOT EXISTS 索引名 ON 表名(列1, 列2 DESC);

-- 唯一索引（值不可重复）
CREATE UNIQUE INDEX IF NOT EXISTS 索引名 ON 表名(列名);

-- 删除索引
DROP INDEX IF EXISTS 索引名;
```

**项目示例（会话查询加速索引）**：
```sql
CREATE INDEX IF NOT EXISTS idx_chat_session 
ON local_messages(chat_type, target_id, message_id DESC);
```

## 四、DML 数据操作语法
### 1. 插入数据 `INSERT`
```sql
-- 基础插入
INSERT INTO 表名(列1, 列2) VALUES (值1, 值2);

-- 主键冲突则覆盖（项目消息去重核心用法）
INSERT OR REPLACE INTO 表名(列1, 列2) VALUES (值1, 值2);

-- 主键冲突则忽略
INSERT OR IGNORE INTO 表名(列1, 列2) VALUES (值1, 值2);

-- 批量插入
INSERT INTO 表名(列1, 列2) 
VALUES (v1, v2), (v3, v4), (v5, v6);
```

### 2. 删除数据 `DELETE`
```sql
-- 按条件删除
DELETE FROM 表名 WHERE 条件;

-- 清空整张表（保留表结构和索引）
DELETE FROM 表名;
```

### 3. 更新数据 `UPDATE`
```sql
UPDATE 表名 SET 列1 = 新值1, 列2 = 新值2 WHERE 条件;
```

### 4. 查询数据 `SELECT`
#### 基础语法
```sql
SELECT 列名1, 列名2   -- 用 * 代表查询所有列
FROM 表名
WHERE 筛选条件
ORDER BY 排序列 ASC/DESC  -- ASC升序，DESC降序
LIMIT 数量 OFFSET 偏移量;  -- 分页/限制返回条数
```

#### 常用查询示例
- 条件查询：
  ```sql
  SELECT * FROM local_messages 
  WHERE chat_type = 1 AND target_id = 1002;
  ```
- 排序+分页：
  ```sql
  -- 取最新20条消息
  SELECT * FROM local_messages 
  WHERE chat_type = 2 AND target_id = 2001
  ORDER BY message_id DESC
  LIMIT 20;
  ```
- 统计与聚合：
  ```sql
  SELECT COUNT(*) FROM local_messages;       -- 总消息数
  SELECT MAX(message_id) FROM local_messages; -- 本地最大消息ID（增量同步用）
  ```
- 模糊查询：
  ```sql
  -- % 匹配任意长度字符，_ 匹配单个字符
  SELECT * FROM local_messages WHERE content LIKE '%你好%';
  ```

## 五、高级特性
### 1. 事务
批量操作时开启事务可大幅提升写入性能（避免每次写入都刷盘），同时保证操作原子性。
```sql
BEGIN;    -- 开启事务
-- 多条 SQL 操作
COMMIT;   -- 提交事务，全部生效

ROLLBACK; -- 回滚事务，出错时撤销所有操作
```
> 项目场景：批量导入服务端历史消息时，外层包裹事务，插入速度可提升几十倍。

### 2. 常用内置函数
| 函数 | 作用 |
| :--- | :--- |
| `COUNT(列/*)` | 统计行数 |
| `MAX(列)` / `MIN(列)` | 求最大/最小值 |
| `SUM(列)` / `AVG(列)` | 求和 / 求平均值 |
| `LENGTH(字符串)` | 获取字符串长度 |
| `IFNULL(列, 默认值)` | 空值替换 |
| `DATETIME()` | 获取当前时间 |

### 3. `VACUUM` 空间整理
清空大量数据后，数据库文件不会自动缩小，执行 `VACUUM` 可回收空闲空间、重建索引、优化文件结构：
```sql
VACUUM;
```

## 六、命令行常用指令
进入数据库控制台：`sqlite3 数据库文件.db`

| 命令 | 作用 |
| :--- | :--- |
| `.tables` | 查看当前库所有表 |
| `.schema 表名` | 查看表的建表语句（表结构） |
| `.indices 表名` | 查看表的所有索引 |
| `.quit` / `.exit` | 退出控制台 |
| `.dump 表名` | 导出表的完整 SQL 语句 |
| `.header on` | 查询结果显示列名 |
| `.mode column` | 查询结果按列对齐显示 |

## 七、C++函数接口
SQLite 官方原生提供 C 语言 API，C++ 项目可直接调用，是性能最高、最通用的使用方式。


### 1. 打开/创建数据库：sqlite3_open
```c
int sqlite3_open(const char *filename, sqlite3 **ppDb);
```
- **功能**：打开一个数据库文件；文件不存在则自动创建。
- **参数**：
  - `filename`：数据库文件路径（如 `"chat_history.db"`）
  - `ppDb`：输出参数，返回数据库句柄指针
- **返回值**：成功返回 `SQLITE_OK`，失败返回错误码。

### 2. 关闭数据库：sqlite3_close
```c
int sqlite3_close(sqlite3 *db);
```
- **功能**：关闭数据库连接，释放所有资源。
- **注意**：关闭前必须销毁所有预处理语句（`sqlite3_finalize`），否则会返回错误。

### 3. 直接执行（适合无参数 DDL/简单语句）
#### sqlite3_exec
```c
int sqlite3_exec(
    sqlite3 *db,
    const char *sql,
    int (*callback)(void*, int, char**, char**),
    void *arg,
    char **errmsg
);
```
- **功能**：直接执行一条或多条 SQL 语句，适合建表、删表、事务控制等无参数场景。
- **参数**：
  - `db`：数据库句柄
  - `sql`：要执行的 SQL 字符串
  - `callback`：结果回调函数（查询语句用，DDL 语句传 `nullptr` 即可）
  - `arg`：传递给回调的自定义参数
  - `errmsg`：输出参数，返回错误信息字符串，用完需 `sqlite3_free()` 释放
- **项目示例（建表）**：
  ```cpp
  char* err = nullptr;
  int rc = sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS local_messages (...)", 
                        nullptr, nullptr, &err);
  if (rc != SQLITE_OK) {
      std::cerr << "错误: " << err << std::endl;
      sqlite3_free(err);
  }
  ```

### 4. 预处理参数化查询（推荐，防注入、高性能）
适合带参数的增删改查，是项目主力用法。
#### 1. 编译SQL语句：sqlite3_prepare_v2
```c
int sqlite3_prepare_v2(
    sqlite3 *db,
    const char *zSql,
    int nByte,
    sqlite3_stmt **ppStmt,
    const char **pzTail
);
```
- **功能**：将 SQL 语句编译为预处理语句对象（`sqlite3_stmt`），后续可重复执行、绑定参数。
- **参数**：
  - `zSql`：SQL 字符串，用 `?` 占位参数
  - `nByte`：SQL 长度，传 `-1` 表示自动计算到字符串结尾
  - `ppStmt`：输出参数，返回预处理语句句柄
  - `pzTail`：未使用的 SQL 尾部，一般传 `nullptr`
- **项目示例**：
  ```cpp
  sqlite3_stmt* stmt = nullptr;
  const char* sql = "INSERT OR REPLACE INTO local_messages (message_id, content) VALUES (?, ?)";
  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
  ```

#### 2. 销毁预处理语句：sqlite3_finalize
```c
int sqlite3_finalize(sqlite3_stmt *pStmt);
```
- **功能**：释放预处理语句资源，**必须调用，否则内存泄漏**。

### 5、参数绑定
给预处理语句的 `?` 占位符传值，**参数索引从 1 开始计数**（注意不是从0开始）。

| 函数 | 功能 | 原型 |
| :--- | :--- | :--- |
| `sqlite3_bind_int` | 绑定 int 整数 | `int sqlite3_bind_int(sqlite3_stmt*, int pos, int value);` |
| `sqlite3_bind_int64` | 绑定 64位整数（ID、时间戳用） | `int sqlite3_bind_int64(sqlite3_stmt*, int pos, sqlite3_int64 value);` |
| `sqlite3_bind_text` | 绑定字符串 | `int sqlite3_bind_text(sqlite3_stmt*, int pos, const char* value, int len, void(*)(void*));` |
| `sqlite3_bind_null` | 绑定空值 | `int sqlite3_bind_null(sqlite3_stmt*, int pos);` |
| `sqlite3_bind_blob` | 绑定二进制数据 | `int sqlite3_bind_blob(sqlite3_stmt*, int pos, const void* value, int len, void(*)(void*));` |

### 关键说明
- 最后一个参数（析构函数）一般传 `SQLITE_TRANSIENT`，表示让 SQLite 内部拷贝一份字符串/二进制数据，外部变量生命周期结束也不影响。
- **项目示例**：
  ```cpp
  // 绑定第1个参数：message_id（64位整数）
  sqlite3_bind_int64(stmt, 1, 1001);
  // 绑定第2个参数：消息内容（字符串）
  sqlite3_bind_text(stmt, 2, "hello", -1, SQLITE_TRANSIENT);
  ```

### 6. 执行语句：sqlite3_step
```c
int sqlite3_step(sqlite3_stmt *pStmt);
```
- **功能**：执行预处理语句。
- **核心返回值**：
  - `SQLITE_DONE`：语句执行完成（增删改、无结果集的场景）
  - `SQLITE_ROW`：查询到一行结果，可通过 `sqlite3_column_xxx` 读取
  - 其他值：执行错误

### 7. 读取结果列
读取当前行的列值，**列索引从 0 开始计数**。

| 函数 | 功能 | 原型 |
| :--- | :--- | :--- |
| `sqlite3_column_int` | 读取 int 整数 | `int sqlite3_column_int(sqlite3_stmt*, int col);` |
| `sqlite3_column_int64` | 读取 64位整数 | `sqlite3_int64 sqlite3_column_int64(sqlite3_stmt*, int col);` |
| `sqlite3_column_text` | 读取字符串 | `const unsigned char* sqlite3_column_text(sqlite3_stmt*, int col);` |
| `sqlite3_column_bytes` | 读取二进制/字符串长度 | `int sqlite3_column_bytes(sqlite3_stmt*, int col);` |

### 注意事项
- `sqlite3_column_text` 返回的指针是临时的，**下次调用 `sqlite3_step` 或 `sqlite3_finalize` 后会失效**，需要长期保存必须自己拷贝一份。
- **项目示例（循环读取查询结果）**：
  ```cpp
  while (sqlite3_step(stmt) == SQLITE_ROW) {
      uint64_t msg_id = (uint64_t)sqlite3_column_int64(stmt, 0);
      std::string content = (const char*)sqlite3_column_text(stmt, 1);
      // 处理每一行数据
  }
  ```

### 8、事务控制
SQLite 没有专门的事务函数，直接通过执行 SQL 语句实现：
```cpp
// 开启事务
sqlite3_exec(db, "BEGIN;", nullptr, nullptr, nullptr);

// 提交事务
sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);

// 回滚事务
sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
```
> 批量插入大量消息时，外层包裹事务可将写入速度提升几十倍。

## 八、常用工具函数
### 1. 获取错误信息：sqlite3_errmsg
```c
const char *sqlite3_errmsg(sqlite3 *db);
```
- 返回最近一次错误的描述字符串，用于调试打印。

### 2. 获取受影响行数：sqlite3_changes
```c
int sqlite3_changes(sqlite3 *db);
```
- 返回最近一次 INSERT/UPDATE/DELETE 影响的行数。

### 3. 获取最后插入的行ID：sqlite3_last_insert_rowid
```c
sqlite3_int64 sqlite3_last_insert_rowid(sqlite3 *db);
```
- 返回最后一次插入操作生成的自增ID。

## 九、核心使用流程（增删改查通用模板）
1.  `sqlite3_open` 打开数据库
2.  `sqlite3_prepare_v2` 编译带占位符的 SQL
3.  `sqlite3_bind_xxx` 绑定参数
4.  `sqlite3_step` 执行语句
5.  若为查询：循环 `sqlite3_step` + `sqlite3_column_xxx` 读取每行
6.  `sqlite3_finalize` 释放语句
7.  `sqlite3_close` 关闭数据库

## 十、常见避坑点
1.  **索引起点**：绑定参数从 `1` 开始，读取结果列从 `0` 开始。
2.  **内存释放**：`sqlite3_prepare_v2` 对应 `sqlite3_finalize`，`sqlite3_exec` 的错误信息对应 `sqlite3_free`，缺一不可。
3.  **字符串生命周期**：绑定字符串时用 `SQLITE_TRANSIENT`；读取结果字符串时及时拷贝。
4.  **写入串行**：SQLite 多线程写入需加锁串行执行，否则容易出现 `SQLITE_BUSY` 错误。
5.  **批量操作开事务**：单次循环插入大量数据时，一定要开事务，否则性能极差。
