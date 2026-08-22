#include "group.h"
#include "connection_manage.h"

void GroupHandler::registerhandlers() {
    auto& router = MessageRouter::instance();
    router.registerHandler(MSG_CREATE_GROUP, handle_create_group);
    router.registerHandler(MSG_DISMISS_GROUP, handle_dismiss_group);
    router.registerHandler(MSG_APPLY_JOIN_GROUP, handle_apply_join_group);
    router.registerHandler(MSG_GET_GROUP_APPLY, handle_get_group_applies);
    router.registerHandler(MSG_GROUP_APPLY_REVIEW, handle_review_join_apply);
    router.registerHandler(MSG_QUIT_GROUP, handle_quit_group);
    router.registerHandler(MSG_GET_GROUP_LIST, handle_get_my_groups);
    router.registerHandler(MSG_GET_GROUP_MEMBER, handle_get_group_members);
    router.registerHandler(MSG_GROUP_SET_ADMIN, handle_set_admin);
    router.registerHandler(MSG_KICK_MEMBER, handle_kick_member);
    router.registerHandler(MSG_SEARCH_GROUP, handle_search_group);
}

// 创建群
void GroupHandler::handle_create_group(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json) {
    if (!conn->isLogin()) {
        send_error(conn, ERR_PERMISSION, MSG_CREATE_GROUP, "请先登录");
        return;
    }

    auto data = json.value("data", nlohmann::json{});
    std::string group_name = data.value("group_name", "");
    std::string group_desc = data.value("group_desc", "");

    if (group_name.empty()) {
        send_error(conn, ERR_PARAM, MSG_CREATE_GROUP, "群名称不能为空");
        return;
    }

    uint64_t user_id = conn->getUserId();
    MysqlPool& pool = MysqlPool::GetInstance();

    std::string insert_sql =
        "INSERT INTO `groups` (owner_id, name, description) VALUES (?, ?, ?)";
    uint64_t group_id = pool.ExecuteInsert(
        insert_sql, {std::to_string(user_id), group_name, group_desc});

    if (group_id == 0) {
        send_error(conn, ERR_SERVER_BUSY, MSG_CREATE_GROUP, "创建群失败");
        return;
    }

    std::string insert_member_sql =
        "INSERT INTO group_member (group_id, member_id, status) VALUES (?, ?, "
        "0)";
    int affected = pool.ExecuteStmt(
        insert_member_sql, {std::to_string(group_id), std::to_string(user_id)});
    if (affected <= 0) {
        send_error(conn, ERR_SERVER_BUSY, MSG_CREATE_GROUP, "添加群主失败");
        return;
    }

    nlohmann::json resp;
    resp["group_id"] = group_id;
    resp["group_name"] = group_name;
    send_ok(conn, MSG_CREATE_GROUP, resp);
}

// 解散群
void GroupHandler::handle_dismiss_group(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json) {
    if (!conn->isLogin()) {
        send_error(conn, ERR_PERMISSION, MSG_DISMISS_GROUP, "请先登录");
        return;
    }

    auto data = json.value("data", nlohmann::json{});
    uint64_t group_id = data.value("group_id", 0ull);
    if (group_id == 0) {
        send_error(conn, ERR_PARAM, MSG_DISMISS_GROUP, "无效的群ID");
        return;
    }

    uint64_t user_id = conn->getUserId();
    MysqlPool& pool = MysqlPool::GetInstance();

    std::string check_sql = "SELECT owner_id FROM `groups` WHERE id = ?";
    QueryResult check_result =
        pool.Query(check_sql, {std::to_string(group_id)});
    if (check_result.rows.empty()) {
        send_error(conn, ERR_GROUP_NOT_EXIST, MSG_DISMISS_GROUP, "群不存在");
        return;
    }

    uint64_t owner_id = std::stoull(check_result.rows[0][0]);
    if (owner_id != user_id) {
        send_error(conn, ERR_PERMISSION, MSG_DISMISS_GROUP,
                   "只有群主可以解散群");
        return;
    }

    MysqlConnGuard guard(3000);
    MYSQL* conn_mysql = guard.Get();
    TransactionGuard tx(conn_mysql);
    std::string delete_members_sql =
        "DELETE FROM group_member WHERE group_id = ?";
    pool.ExecuteStmt(conn_mysql, delete_members_sql,
                     {std::to_string(group_id)});

    std::string delete_group_sql = "DELETE FROM `groups` WHERE id = ?";
    int affected = pool.ExecuteStmt(conn_mysql, delete_group_sql,
                                    {std::to_string(group_id)});
    if (affected <= 0) {
        send_error(conn, ERR_SERVER_BUSY, MSG_DISMISS_GROUP, "解散群失败");
        return;
    }

    if (!tx.Commit()) {
        LOG(ERROR) << "事务提交失败";
        send_error(conn, ERR_SERVER_BUSY, MSG_DISMISS_GROUP, "事务提交失败");
        return;
    }

    nlohmann::json resp;
    resp["group_id"] = group_id;
    send_ok(conn, MSG_DISMISS_GROUP, resp);
}

// 申请加入群
void GroupHandler::handle_apply_join_group(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json) {
    if (!conn->isLogin()) {
        send_error(conn, ERR_PERMISSION, MSG_APPLY_JOIN_GROUP, "请先登录");
        return;
    }

    uint64_t apply_user_id = conn->getUserId();
    auto data = json.value("data", nlohmann::json{});
    uint64_t group_id = data.value("group_id", 0ull);
    std::string message = data.value("message", "");

    if (group_id == 0) {
        send_error(conn, ERR_PARAM, MSG_APPLY_JOIN_GROUP, "无效的目标群");
        return;
    }

    MysqlPool& pool = MysqlPool::GetInstance();
    std::string exist_sql = "SELECT id FROM `groups` WHERE id = ?";
    int exists = pool.ExecuteExist(exist_sql, {std::to_string(group_id)});
    if (exists <= 0) {
        send_error(conn, ERR_GROUP_NOT_EXIST, MSG_APPLY_JOIN_GROUP, "群不存在");
        return;
    }

    std::string sql =
        "SELECT * FROM group_member WHERE group_id=? AND member_id=?";
    QueryResult result = pool.Query(
        sql, {std::to_string(group_id), std::to_string(apply_user_id)});
    if (!result.rows.empty()) {
        send_error(conn, ERR_PARAM, MSG_APPLY_JOIN_GROUP, "已经在该群了");
        return;
    }

    std::string sql_check =
        "SELECT 1 FROM group_apply WHERE apply_id=? AND group_id=? AND "
        "status=0";
    int applieds = pool.ExecuteExist(
        sql_check, {std::to_string(apply_user_id), std::to_string(group_id)});
    if (applieds > 0) {
        send_error(conn, ERR_PARAM, MSG_APPLY_JOIN_GROUP,
                   "您已经发送过入群申请，请等待群主或管理员处理");
        return;
    }

    // 插入申请记录
    std::string insert_apply_sql =
        "INSERT INTO group_apply (group_id, apply_id, message, status, "
        "is_pushed) VALUES (?, ?, ?, 0, 0)";
    uint64_t apply_record_id = pool.ExecuteInsert(
        insert_apply_sql,
        {std::to_string(group_id), std::to_string(apply_user_id), message});
    if (apply_record_id == 0) {
        send_error(conn, ERR_SERVER_BUSY, MSG_APPLY_JOIN_GROUP,
                   "申请加入群失败");
        return;
    }

    // 查询管理员和群主
    std::string admin_sql =
        "SELECT member_id FROM group_member WHERE group_id = ? AND status IN "
        "(0,1)";
    QueryResult admins = pool.Query(admin_sql, {std::to_string(group_id)});

    // 为每个管理员创建通知
    for (auto& row : admins.rows) {
        uint64_t admin_id = std::stoull(row[0]);
        pool.ExecuteStmt(
            "INSERT INTO group_notifications (receiver_id, apply_id, type, "
            "is_read) VALUES (?, ?, 1, 0)",
            {std::to_string(admin_id), std::to_string(apply_record_id)});
    }

    // 实时推送在线管理员
    nlohmann::json notify;
    notify["type"] = "new_group_apply";
    notify["apply_id"] = apply_record_id;
    notify["group_id"] = group_id;
    notify["apply_user_id"] = apply_user_id;
    notify["message"] = message;
    ConnectionManager& conn_mgr = ConnectionManager::GetInstance();

    for (auto& row : admins.rows) {
        uint64_t admin_id = std::stoull(row[0]);
        auto admin_conn = conn_mgr.GetConnByUserId(admin_id);
        if (admin_conn && admin_conn->isLogin()) {
            send_ok(admin_conn, PUSH_GROUP_APPLY, notify);
            pool.ExecuteStmt(
                "UPDATE group_notifications SET is_read = 1 WHERE receiver_id "
                "= ? AND apply_id = ? AND type = 1",
                {std::to_string(admin_id), std::to_string(apply_record_id)});
        }
    }

    nlohmann::json resp;
    resp["apply_id"] = apply_record_id;
    send_ok(conn, MSG_APPLY_JOIN_GROUP, resp);
}

// 查看入群申请列表（管理员/群主）
void GroupHandler::handle_get_group_applies(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json) {
    if (!conn->isLogin()) {
        send_error(conn, ERR_PERMISSION, MSG_GET_GROUP_APPLY, "请先登录");
        return;
    }

    auto data = json.value("data", nlohmann::json{});
    uint64_t group_id = data.value("group_id", 0ull);
    uint64_t user_id = conn->getUserId();

    std::string check_sql =
        "SELECT status FROM group_member WHERE group_id=? AND member_id=?";
    MysqlPool& pool = MysqlPool::GetInstance();
    QueryResult check_result = pool.Query(
        check_sql, {std::to_string(group_id), std::to_string(user_id)});
    if (check_result.rows.empty()) {
        send_error(conn, ERR_NOT_IN_GROUP, MSG_GET_GROUP_APPLY, "您不在该群中");
        return;
    }
    int role = std::stoi(check_result.rows[0][0]);
    if (role != 0 && role != 1) {
        send_error(conn, ERR_PERMISSION, MSG_GET_GROUP_APPLY,
                   "您不是管理员或群主");
        return;
    }

    std::string sql =
        "SELECT id, apply_id, message, status FROM group_apply WHERE group_id "
        "= ? AND status = 0";
    QueryResult result = pool.Query(sql, {std::to_string(group_id)});

    nlohmann::json resp;
    for (const auto& row : result.rows) {
        nlohmann::json apply;
        apply["id"] = std::stoull(row[0]);
        apply["apply_id"] = std::stoull(row[1]);
        apply["message"] = row[2];
        apply["status"] = std::stoi(row[3]);
        resp["applies"].push_back(apply);
    }

    send_ok(conn, MSG_GET_GROUP_APPLY, resp);
}

// 审批入群申请（同意/拒绝）
void GroupHandler::handle_review_join_apply(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json) {
    if (!conn->isLogin()) {
        send_error(conn, ERR_PERMISSION, MSG_GROUP_APPLY_REVIEW, "请先登录");
        return;
    }

    auto data = json.value("data", nlohmann::json{});
    uint64_t group_id = data.value("group_id", 0ull);
    uint64_t user_id = conn->getUserId();
    uint64_t apply_record_id = data.value("apply_id", 0ull);
    int decision = data.value("decision", -1);  // 2: 拒绝, 1: 同意

    if (group_id == 0 || apply_record_id == 0 ||
        (decision != 2 && decision != 1)) {
        send_error(conn, ERR_PARAM, MSG_GROUP_APPLY_REVIEW, "参数错误");
        return;
    }

    std::string check_sql =
        "SELECT status FROM group_member WHERE group_id=? AND member_id=?";
    MysqlPool& pool = MysqlPool::GetInstance();
    QueryResult check_result = pool.Query(
        check_sql, {std::to_string(group_id), std::to_string(user_id)});
    if (check_result.rows.empty()) {
        send_error(conn, ERR_NOT_IN_GROUP, MSG_GROUP_APPLY_REVIEW,
                   "您不在该群中");
        return;
    }
    int role = std::stoi(check_result.rows[0][0]);
    if (role != 0 && role != 1) {
        send_error(conn, ERR_PERMISSION, MSG_GROUP_APPLY_REVIEW,
                   "您不是管理员或群主");
        return;
    }

    MysqlConnGuard guard(3000);
    MYSQL* conn_mysql = guard.Get();
    if (!conn_mysql) {
        send_error(conn, ERR_SERVER_BUSY, MSG_GROUP_APPLY_REVIEW, "服务器繁忙");
        return;
    }

    TransactionGuard tx(conn_mysql);

    std::string sql =
        "SELECT apply_id FROM group_apply WHERE id = ? AND group_id = ? AND "
        "status = 0";
    QueryResult result =
        pool.Query(conn_mysql, sql,
                   {std::to_string(apply_record_id), std::to_string(group_id)});
    if (result.rows.empty()) {
        send_error(conn, ERR_PARAM, MSG_GROUP_APPLY_REVIEW,
                   "申请不存在或已处理");
        return;
    }
    uint64_t applicant_id = std::stoull(result.rows[0][0]);

    std::string update_sql =
        "UPDATE group_apply "
        "SET status = ?, is_pushed = 0, handle_id = ? "
        "WHERE id = ?";
    int up_ret =
        pool.ExecuteStmt(conn_mysql, update_sql,
                         {std::to_string(decision), std::to_string(user_id),
                          std::to_string(apply_record_id)});
    if (up_ret < 0) {
        send_error(conn, ERR_SERVER_BUSY, MSG_GROUP_APPLY_REVIEW,
                   "更新申请状态失败");
        return;
    }

    if (decision == 1) {
        std::string insert_member_sql =
            "INSERT INTO group_member (group_id, member_id, status) VALUES (?, "
            "?, 2)";
        int add_ret = pool.ExecuteStmt(
            conn_mysql, insert_member_sql,
            {std::to_string(group_id), std::to_string(applicant_id)});
        if (add_ret <= 0) {
            send_error(conn, ERR_SERVER_BUSY, MSG_GROUP_APPLY_REVIEW,
                       "添加成员失败");
            return;
        }
    }

    if (!tx.Commit()) {
        LOG(ERROR) << "事务提交失败";
        send_error(conn, ERR_SERVER_BUSY, MSG_GROUP_APPLY_REVIEW,
                   "事务提交失败");
        return;
    }

    // 通知申请人（在线推送，离线靠 is_pushed=0 拉取）
    bool is_online = false;
    auto target_conn =
        ConnectionManager::GetInstance().GetConnByUserId(applicant_id);
    if (target_conn && target_conn->isLogin())
        is_online = true;

    nlohmann::json notice_data;
    notice_data["type"] = decision == 1 ? "group_agree" : "group_refuse";
    notice_data["user_id"] = user_id;
    notice_data["group_id"] = group_id;
    notice_data["decision"] = decision;
    if (is_online) {
        send_ok(target_conn, PUSH_GROUP_AGREE, notice_data);
        pool.ExecuteStmt("UPDATE group_apply SET is_pushed = 1 WHERE id = ?", {std::to_string(apply_record_id)});

        LOG(INFO) << "已在线推送并标记为已读";
    } else {
        LOG(INFO) << "申请人 " << applicant_id << " 不在线，上线后自动拉取通知";
    }

    // 通知其他管理员申请已被处理
    QueryResult other_admins = pool.Query(
        "SELECT member_id FROM group_member WHERE group_id = ? AND status IN "
        "(0,1) AND member_id != ?",
        {std::to_string(group_id), std::to_string(user_id)});

    for (auto& row : other_admins.rows) {
        uint64_t admin_id = std::stoull(row[0]);
        pool.ExecuteStmt(
            "INSERT INTO group_notifications (receiver_id, apply_id, type, "
            "is_read) VALUES (?, ?, 2, 0)",
            {std::to_string(admin_id), std::to_string(apply_record_id)});
    }

    nlohmann::json notify_others;
    notify_others["type"] = "group_apply_result";
    notify_others["apply_id"] = apply_record_id;
    notify_others["group_id"] = group_id;
    notify_others["handler_id"] = user_id;
    notify_others["result"] = decision == 1 ? "agreed" : "refused";
    ConnectionManager& conn_mgr = ConnectionManager::GetInstance();
    for (auto& row : other_admins.rows) {
        uint64_t admin_id = std::stoull(row[0]);
        auto admin_conn = conn_mgr.GetConnByUserId(admin_id);
        if (admin_conn && admin_conn->isLogin()) {
            send_ok(admin_conn, PUSH_GROUP_APPLY, notify_others);
            pool.ExecuteStmt(
                "UPDATE group_notifications SET is_read = 1 WHERE receiver_id "
                "= ? AND apply_id = ? AND type = 2",
                {std::to_string(admin_id), std::to_string(apply_record_id)});
        } else {
            LOG(INFO) << "Admin " << admin_id << "不在线，等待上线自动推送";
        }
    }

    nlohmann::json resp;
    resp["msg"] = decision == 1 ? "已同意" : "已拒绝";
    send_ok(conn, MSG_GROUP_APPLY_REVIEW, resp);
}

// 退出群
void GroupHandler::handle_quit_group(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json) {
    if (!conn->isLogin()) {
        send_error(conn, ERR_PERMISSION, MSG_QUIT_GROUP, "请先登录");
        return;
    }

    auto data = json.value("data", nlohmann::json{});
    uint64_t group_id = data.value("group_id", 0ull);
    if (group_id == 0) {
        send_error(conn, ERR_PARAM, MSG_QUIT_GROUP, "无效的群ID");
        return;
    }

    uint64_t user_id = conn->getUserId();
    MysqlPool& pool = MysqlPool::GetInstance();

    std::string check_sql = "SELECT owner_id FROM `groups` WHERE id = ?";
    QueryResult check_result =
        pool.Query(check_sql, {std::to_string(group_id)});
    if (check_result.rows.empty()) {
        send_error(conn, ERR_GROUP_NOT_EXIST, MSG_QUIT_GROUP, "群不存在");
        return;
    }

    uint64_t owner_id = std::stoull(check_result.rows[0][0]);
    if (owner_id == user_id) {
        send_error(conn, ERR_PERMISSION, MSG_QUIT_GROUP, "群主不可以退群");
        return;
    }

    std::string delete_member_sql =
        "DELETE FROM group_member WHERE group_id = ? AND member_id = ?";
    int affected = pool.ExecuteStmt(
        delete_member_sql, {std::to_string(group_id), std::to_string(user_id)});
    if (affected <= 0) {
        send_error(conn, ERR_SERVER_BUSY, MSG_QUIT_GROUP, "退群失败");
        return;
    }

    nlohmann::json resp;
    resp["group_id"] = group_id;
    send_ok(conn, MSG_QUIT_GROUP, resp);
}

// 查看我的群列表
void GroupHandler::handle_get_my_groups(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json) {
    if (!conn->isLogin()) {
        send_error(conn, ERR_PERMISSION, MSG_GET_GROUP_LIST, "请先登录");
        return;
    }

    uint64_t user_id = conn->getUserId();
    MysqlPool& pool = MysqlPool::GetInstance();

    std::string sql =
        "SELECT g.id, g.name, g.description, gm.status "
        "FROM group_member gm JOIN `groups` g ON gm.group_id = g.id "
        "WHERE gm.member_id = ?";
    QueryResult result = pool.Query(sql, {std::to_string(user_id)});

    nlohmann::json resp;
    for (const auto& row : result.rows) {
        nlohmann::json group;
        group["group_id"] = std::stoull(row[0]);
        group["group_name"] = row[1];
        group["group_desc"] = row[2];
        group["status"] = std::stoi(row[3]);  // 角色
        resp["groups"].push_back(group);
    }
    send_ok(conn, MSG_GET_GROUP_LIST, resp);
}

// 查看群成员列表
void GroupHandler::handle_get_group_members(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json) {
    if (!conn->isLogin()) {
        send_error(conn, ERR_PERMISSION, MSG_GET_GROUP_MEMBER, "请先登录");
        return;
    }

    auto data = json.value("data", nlohmann::json{});
    uint64_t group_id = data.value("group_id", 0ull);
    if (group_id == 0) {
        send_error(conn, ERR_PARAM, MSG_GET_GROUP_MEMBER, "无效的群ID");
        return;
    }

    uint64_t user_id = conn->getUserId();
    MysqlPool& pool = MysqlPool::GetInstance();

    // 检查群是否存在，以及用户是否在群内
    std::string check_sql = "SELECT id FROM `groups` WHERE id = ?";
    QueryResult check_result =
        pool.Query(check_sql, {std::to_string(group_id)});
    if (check_result.rows.empty()) {
        send_error(conn, ERR_GROUP_NOT_EXIST, MSG_GET_GROUP_MEMBER, "群不存在");
        return;
    }

    std::string sql =
        "SELECT gm.member_id, u.username, gm.status "
        "FROM group_member gm JOIN users u ON gm.member_id = u.id "
        "WHERE gm.group_id = ?";
    QueryResult result = pool.Query(sql, {std::to_string(group_id)});

    nlohmann::json resp;
    RedisPool& redis = RedisPool::GetInstance();
    for (const auto& row : result.rows) {
        nlohmann::json member;
        member["user_id"] = std::stoull(row[0]);
        member["username"] = row[1];
        member["status"] = std::stoi(row[2]);  // 0群主,1管理员,2普通
        member["online"] = redis.set_ismember("online_users", row[0]);
        resp["members"].push_back(member);
    }
    send_ok(conn, MSG_GET_GROUP_MEMBER, resp);
}

// 设置/取消管理员
void GroupHandler::handle_set_admin(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json) {
    if (!conn->isLogin()) {
        send_error(conn, ERR_PERMISSION, MSG_GROUP_SET_ADMIN, "请先登录");
        return;
    }

    auto data = json.value("data", nlohmann::json{});
    uint64_t group_id = data.value("group_id", 0ull);
    uint64_t target_user_id = data.value("user_id", 0ull);
    bool set_admin = data.value("set_admin", true);  // true设为管理员，false取消

    if (group_id == 0 || target_user_id == 0) {
        send_error(conn, ERR_PARAM, MSG_GROUP_SET_ADMIN, "参数错误");
        return;
    }

    uint64_t user_id = conn->getUserId();
    MysqlPool& pool = MysqlPool::GetInstance();

    std::string check_sql = "SELECT owner_id FROM `groups` WHERE id = ?";
    QueryResult check_result =
        pool.Query(check_sql, {std::to_string(group_id)});
    if (check_result.rows.empty()) {
        send_error(conn, ERR_GROUP_NOT_EXIST, MSG_GROUP_SET_ADMIN, "群不存在");
        return;
    }

    uint64_t owner_id = std::stoull(check_result.rows[0][0]);
    if (owner_id != user_id) {
        send_error(conn, ERR_PERMISSION, MSG_GROUP_SET_ADMIN, "你不是群主，无法设置管理员");
        return;
    }

    std::string check_user =
        "SELECT 1 FROM group_member WHERE group_id=? AND member_id=?";
    int exists = pool.ExecuteExist(
        check_user, {std::to_string(group_id), std::to_string(target_user_id)});
    if (exists <= 0) {
        send_error(conn, ERR_PARAM, MSG_GROUP_SET_ADMIN, "目标用户不在群中");
        return;
    }

    int new_status = set_admin ? 1 : 2;  // 1管理员,2普通
    std::string update_sql =
        "UPDATE group_member SET status = ? WHERE group_id = ? AND member_id = "
        "?";
    int affected = pool.ExecuteStmt(
        update_sql, {std::to_string(new_status), std::to_string(group_id),
                     std::to_string(target_user_id)});
    if (affected <= 0) {
        send_error(conn, ERR_SERVER_BUSY, MSG_GROUP_SET_ADMIN, "设置管理员失败");
        return;
    }

    nlohmann::json resp;
    resp["msg"] = set_admin ? "已设为管理员" : "已取消管理员";
    send_ok(conn, MSG_GROUP_SET_ADMIN, resp);
}

// 踢出成员
void GroupHandler::handle_kick_member(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json) {
    if (!conn->isLogin()) {
        send_error(conn, ERR_PERMISSION, MSG_KICK_MEMBER, "请先登录");
        return;
    }

    auto data = json.value("data", nlohmann::json{});
    uint64_t group_id = data.value("group_id", 0ull);
    uint64_t target_user_id = data.value("user_id", 0ull);
    if (group_id == 0 || target_user_id == 0) {
        send_error(conn, ERR_PARAM, MSG_KICK_MEMBER, "参数错误");
        return;
    }

    uint64_t user_id = conn->getUserId();
    MysqlPool& pool = MysqlPool::GetInstance();

    // 检查操作者角色
    std::string role_sql =
        "SELECT status FROM group_member WHERE group_id = ? AND member_id = ?";
    QueryResult role_res = pool.Query(
        role_sql, {std::to_string(group_id), std::to_string(user_id)});
    if (role_res.rows.empty()) {
        send_error(conn, ERR_NOT_IN_GROUP, MSG_KICK_MEMBER, "您不在该群中");
        return;
    }
    int my_role = std::stoi(role_res.rows[0][0]);
    if (my_role != 0 && my_role != 1) {
        send_error(conn, ERR_PERMISSION, MSG_KICK_MEMBER, "您不是管理员或群主");
        return;
    }

    // 检查被踢者角色，不能踢群主或权限比自己高的
    QueryResult target_role_res = pool.Query(
        role_sql, {std::to_string(group_id), std::to_string(target_user_id)});
    if (target_role_res.rows.empty()) {
        send_error(conn, ERR_PARAM, MSG_KICK_MEMBER, "目标用户不在群内");
        return;
    }
    int target_role = std::stoi(target_role_res.rows[0][0]);
    if (target_role == 0) {
        send_error(conn, ERR_PERMISSION, MSG_KICK_MEMBER, "不能踢群主");
        return;
    }
    if (my_role == 1 && target_role <= 1) {
        send_error(conn, ERR_PERMISSION, MSG_KICK_MEMBER,
                   "管理员不能踢管理员或群主");
        return;
    }

    std::string delete_sql =
        "DELETE FROM group_member WHERE group_id = ? AND member_id = ?";
    int affected = pool.ExecuteStmt( delete_sql, {std::to_string(group_id), std::to_string(target_user_id)});
    if (affected <= 0) {
        send_error(conn, ERR_SERVER_BUSY, MSG_KICK_MEMBER, "踢出成员失败");
        return;
    }

    nlohmann::json resp;
    resp["msg"] = "已踢出成员";
    send_ok(conn, MSG_KICK_MEMBER, resp);
}

// 搜索群聊
void GroupHandler::handle_search_group(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json) {
    if (!conn->isLogin()) {
        send_error(conn, ERR_PERMISSION, MSG_SEARCH_GROUP, "请先登录");
        return;
    }

    auto data = json.value("data", nlohmann::json{});
    std::string keyword = data.value("keyword", "");
    if (keyword.empty()) {
        send_error(conn, ERR_PARAM, MSG_SEARCH_GROUP, "搜索关键词不能为空");
        return;
    }

    std::string like_keyword = "%" + keyword + "%";
    MysqlPool& pool = MysqlPool::GetInstance();
    QueryResult result = pool.Query(
        "SELECT id, name, owner_id, description FROM `groups` WHERE name LIKE "
        "? LIMIT 20",
        {like_keyword});

    nlohmann::json group_list = nlohmann::json::array();
    for (const auto& row : result.rows) {
        nlohmann::json group;
        group["group_id"] = std::stoull(row[0]);
        group["group_name"] = row[1];
        group["owner_id"] = std::stoull(row[2]);
        group["description"] = row[3];
        group_list.push_back(group);
    }

    nlohmann::json resp;
    resp["groups"] = group_list;
    send_ok(conn, MSG_SEARCH_GROUP, resp);
}