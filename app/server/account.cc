#include "account.h"
#include <random>
#include "friend.h"
#include "mysql_pool.h"
#include "redis_pool.h"

// 调用router注册6个账号消息类型和对应事件处理函数的映射
void AccountHandler::registerhandlers() {
    auto& router = MessageRouter::instance();
    router.registerHandler(MSG_REGISTER, handle_register);
    router.registerHandler(MSG_LOGIN, handle_login);
    router.registerHandler(MSG_LOGOUT, handle_logout);
    router.registerHandler(MSG_GET_CAPTCHA, handle_get_captcha);
    router.registerHandler(MSG_RESET_PWD, handle_reset_password);
    router.registerHandler(MSG_DELETE_ACCOUNT, handle_delete_account);
}

void AccountHandler::handle_register(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json) {
        
    auto data = json.value("data", nlohmann::json{});
    std::string username = data.value("username", "");
    std::string password = data.value("password", "");
    std::string email = data.value("email", "");
    std::string phone = data.value("phone", "");

     LOG(INFO) << "params: user=" << username << " email=" << email << " phone=" << phone;

    // 依次校验并打印
    if (!is_valid_username(username)) {
        send_error(conn, ERR_PARAM, MSG_REGISTER, "用户名不合法(3-20位)");
        return;
    }
    if (!is_valid_password(password)) {
        send_error(conn, ERR_PARAM, MSG_REGISTER, "密码格式错误(6-12位,至少一个字母和一个数字)");
        return;
    }
    if (email.empty() && phone.empty()) {
        send_error(conn, ERR_PARAM, MSG_REGISTER, "邮箱或手机号至少填一个");
        return;
    }
    if (!email.empty() && !is_valid_email(email)) {
        send_error(conn, ERR_PARAM, MSG_REGISTER, "邮箱格式错误");
        return;
    }
    if (!phone.empty() && !is_valid_phone(phone)) {
        send_error(conn, ERR_PARAM, MSG_REGISTER, "手机号格式错误(11位)");
        return;
    }

    // 加密
    std::string salt_hex;
    std::string hashed = password_encrypt(password, salt_hex);
    

    // 查重
    MysqlPool& pool = MysqlPool::GetInstance();
    const char* checkSql = R"(
        SELECT id FROM users
        WHERE username = ?
            OR (? != '' AND email = ?)
            OR (? != '' AND phone = ?)
        LIMIT 1
    )";

    int exist = pool.ExecuteExist(checkSql, {username, email, email, phone, phone});
    if (exist == -1) {
        send_error(conn, ERR_SERVER_BUSY, MSG_REGISTER, "服务器繁忙");
        return;
    }
    if (exist > 0) {
        send_error(conn, ERR_ACCOUNT_EXIST, MSG_REGISTER, "用户已存在");
        return;
    }

    // 插入
    uint64_t new_id = pool.ExecuteInsert(
        "INSERT INTO users (username, password, password_salt, email, "
        "phone) "
        "VALUES (?, ?, ?, ?, ?)",
        {username, hashed, salt_hex,
         email.empty() ? std::optional<std::string>{} : email,
         phone.empty() ? std::optional<std::string>{} : phone});

    if (new_id == 0) {
        send_error(conn, ERR_SERVER_BUSY, MSG_REGISTER, "注册失败");
        return;
    }

    nlohmann::json resp;
    resp["user_id"] = new_id;
    resp["username"] = username;
    send_ok(conn, MSG_REGISTER, resp);
}

void AccountHandler::handle_login(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json) {
   
    auto data = json.value("data", nlohmann::json{});
    std::string keyword = data.value("username", "");
    std::string password = data.value("password", "");

    std::string sql =
        "SELECT id, username, password, email, phone, password_salt FROM users "
        "WHERE username = ? OR "
        "email = ? OR phone = ? "
        "LIMIT 1";

    if (keyword.empty()) {
        send_error(conn, ERR_PARAM, MSG_LOGIN, "用户名、邮箱或手机号至少填写一个");
        return;
    }
    MysqlPool& pool = MysqlPool::GetInstance();
    QueryResult res = pool.Query(sql, {keyword,keyword,keyword});
    if (res.rows.empty()) {
        send_error(conn, ERR_ACCOUNT_NOT_EXIST, MSG_LOGIN, "账号不存在");
        return;
    }

    // 获取第一行数据
    std::vector<std::string>& row = res.rows[0];
    std::string db_password_hash = row[2];
    std::string db_password_salt = row[5];

    if (!password_verify(password, db_password_salt, db_password_hash)) {
        send_error(conn, ERR_PWD_WRONG, MSG_LOGIN, "密码错误");
        return;
    }

    uint64_t user_id = std::stoull(row[0]);  // row 是查询结果
    std::string uid_str = std::to_string(user_id);

    // 记录登录状态到 Redis
    RedisPool& redis = RedisPool::GetInstance();
    bool is_online = redis.set_ismember("online_users", uid_str);
    if (is_online) {
        send_error(conn, ERR_PWD_WRONG, MSG_LOGIN, "该账户已登陆");
        return;
    }

    // 将 user_id 绑定到连接
    conn->setUserId(user_id);

    redis.set_add("online_users", uid_str);
    redis.string_setex("heartbeat:" + uid_str, "1", 3000);

    // 更新最后登录时间
    std::string update_sql = "UPDATE users SET update_time = NOW() WHERE id = ?";
    pool.ExecuteStmt(update_sql, {std::to_string(user_id)});

    nlohmann::json respData;
    respData["user_id"] = std::stoull(row[0]);
    respData["username"] = row[1];
    respData["email"] = row[3];
    respData["phone"] = row[4];

    QueryResult all_users = pool.Query("SELECT id, username FROM users");
    nlohmann::json user_arr = nlohmann::json::array();
    for (const auto& row : all_users.rows) {
        nlohmann::json u;
        u["user_id"] = std::stoull(row[0]);
        u["username"] = row[1];
        user_arr.push_back(u);
    }
    respData["all_users"] = user_arr;

    QueryResult all_groups = pool.Query("SELECT id, name FROM `groups`");
    nlohmann::json group_arr = nlohmann::json::array();
    for (const auto& row : all_groups.rows) {
        nlohmann::json g;
        g["group_id"] = std::stoull(row[0]);
        g["group_name"] = row[1];
        group_arr.push_back(g);
    }
    respData["all_groups"] = group_arr;
    send_ok(conn, MSG_LOGIN, respData);

    // 推送离线私聊消息
    HistoryHandler::pushOfflinePrivateMsgs(conn, user_id);
    // 推送离线群聊消息
    HistoryHandler::pushRecentGroupMsgs(conn, user_id);
    // 推送离线群通知（新申请/处理结果）
    HistoryHandler::pushOfflineGroupMsgs(conn, user_id);
    // 推送离线好友申请
    FriendHandler::pushOfflineFriendMsgs(conn, user_id);

    // 将当前连接注册到管理器
    ConnectionManager::GetInstance().Add(user_id, conn);
    LOG(INFO) << "用户登录成功: id=" << row[0] << ", username=" << row[1];
}

void AccountHandler::handle_logout(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json) {
    if (!conn->isLogin()) {
        send_error(conn, ERR_PERMISSION, MSG_LOGOUT, "未登录");
        return;
    }

    uint64_t user_id = conn->getUserId();
    std::string user_id_str = std::to_string(user_id);

    // 删除 Redis 中的 session_token，表示用户已登出
    RedisPool& redis = RedisPool::GetInstance();
    redis.set_rem("online_users", user_id_str);
    redis.string_del("heartbeat:" + user_id_str);

    // 从管理器中移除
    ConnectionManager::GetInstance().Remove(user_id);
    conn->setUserId(0);  // 解绑

    // 返回结果
    nlohmann::json respData;
    respData["user_id"] = user_id;
    send_ok(conn, MSG_LOGOUT, respData);
    LOG(INFO) << "用户登出成功: id=" << user_id;
}

void AccountHandler::handle_get_captcha(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json) {

    auto data = json.value("data", nlohmann::json{});
    std::string email = data.value("email", "");
    std::string phone = data.value("phone", "");
    std::string type = data.value("type", "");
    uint64_t user_id = data.value("user_id", 0ull);

    if (email.empty() && phone.empty()) {
        send_error(conn, ERR_PARAM, MSG_GET_CAPTCHA, "邮箱和手机号至少填写一个");
        return;
    }

    if (type != "register" && type != "reset") {
        send_error(conn, ERR_PARAM, MSG_GET_CAPTCHA, "type参数不合法");
        return;
    }

    if (!is_valid_email(email)) {
        send_error(conn, ERR_PARAM, MSG_GET_CAPTCHA, "邮箱格式不正确");
        return;
    }

    int captcha_code = rand_range(100000, 999999);
    std::string code = std::to_string(captcha_code);
    bool ok = sendMail(email,  //  这是收件人
                       "验证码", "您的验证码是：" + code + ",5分钟有效。",
                       "smtp.qq.com", 465,
                       "2671530264@qq.com",  // 这是发件邮箱
                       "tzhanxrakodfebib",   // 这是发件邮箱的授权码
                       true);
    if (!ok) {
        send_error(conn, ERR_SERVER_BUSY, MSG_GET_CAPTCHA, "发送验证码失败，请稍后重试");
        return;
    }

    std::string redis_key = "captcha:" + email;
    RedisPool& redis_pool = RedisPool::GetInstance();
    if (!redis_pool.string_setex(redis_key, code, 300)) {  // 300秒过期
        send_error(conn, ERR_SERVER_BUSY, MSG_GET_CAPTCHA, "服务器繁忙，请稍后重试");
        return;
    }

    send_ok(conn, MSG_GET_CAPTCHA, {{"captcha", code}});
    LOG(INFO) << "验证码发送成功: email=" << email << ", code=" << code;
}

void AccountHandler::handle_reset_password(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json) {

    auto data = json.value("data", nlohmann::json{});
    std::string email = data.value("email", "");
    std::string captcha = data.value("captcha", "");
    std::string new_password = data.value("new_password", "");

    if (email.empty() || captcha.empty() || new_password.empty()) {
        send_error(conn, ERR_PARAM, MSG_RESET_PWD, "缺少必要参数");
        return;
    }
    if (!is_valid_email(email)) {
        send_error(conn, ERR_PARAM, MSG_RESET_PWD, "邮箱格式不正确");
        return;
    }
    if (!is_valid_password(new_password)) {
        send_error(conn, ERR_PARAM, MSG_RESET_PWD, "6-12位,至少一个字母和一个数字");
        return;
    }

    // 验证验证码
    std::string redis_key = "captcha:" + email;
    RedisPool& redis_pool = RedisPool::GetInstance();
    std::string stored_captcha = redis_pool.string_get(redis_key);
    if (stored_captcha.empty() || stored_captcha != captcha) {
        send_error(conn, ERR_CAPTCHA_INVALID, MSG_RESET_PWD, "验证码错误或过期");
        return;
    }

    // 验证成功，删除验证码
    RedisPool::GetInstance().string_del("captcha:" + email);

    std::string salt_hex;
    std::string new_hash = password_encrypt(new_password, salt_hex);

    if (new_hash.empty()) {
        send_error(conn, ERR_SERVER_BUSY, MSG_RESET_PWD, "服务器繁忙");
        return;
    }
    
    MysqlPool& pool = MysqlPool::GetInstance();
    std::string updateSql =
        "UPDATE users "
        "SET password = ?, password_salt = ? "
        "WHERE email = ?";
    if (!pool.ExecuteStmt(updateSql, {new_hash, salt_hex, email})) {
        send_error(conn, ERR_SERVER_BUSY, MSG_RESET_PWD, "修改密码失败");
        return;
    }
    send_ok(conn, MSG_RESET_PWD, {});
}

void AccountHandler::handle_delete_account(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json) {
    if (!conn->isLogin()) {
        send_error(conn, ERR_PERMISSION, MSG_DELETE_ACCOUNT, "请先登录");
        return;
    }
    uint64_t user_id = conn->getUserId();

    //  从 MySQL 删除用户（删除好友、群组关系、消息等）
    MysqlPool& pool = MysqlPool::GetInstance();
    int ok = pool.ExecuteStmt("DELETE FROM users WHERE id = ?", {std::to_string(user_id)});
    if (ok==-1) {
        send_error(conn, ERR_SERVER_BUSY, MSG_DELETE_ACCOUNT, "注销失败");
        return;
    }

    // 清理 Redis 中的在线状态、心跳、离线消息等
    RedisPool& redis = RedisPool::GetInstance();
    std::string uid_str = std::to_string(user_id);
    redis.set_rem("online_users", uid_str);
    redis.string_del("heartbeat:" + uid_str);
    redis.string_del("offline_msg:" + uid_str);
    redis.string_del("offline_notify:" + uid_str);
    ConnectionManager::GetInstance().Remove(user_id);
    // 返回成功，然后关闭连接
    send_ok(conn, MSG_DELETE_ACCOUNT, {});
    LOG(INFO) << "用户注销成功: id=" << user_id;

    // 断开连接（不能在 send 之后立即删除连接对象，因为 send 是异步的）
    // 可以直接调用 closeConnection()，它会清理连接并触发 TCP 关闭
    conn->closeConnection();
}
