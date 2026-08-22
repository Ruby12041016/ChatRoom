#ifndef ACCOUNT_H
#define ACCOUNT_H

#include <regex>
#include "chat.h"
#include "connection_manage.h"
#include "global.h"
#include "message.h"
#include "router.h"
#include "server.h"
#include "utils.h"

class AccountHandler {
   public:
    static void registerhandlers();
    // 注册
    static void handle_register(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json);
    // 登陆
    static void handle_login(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json);
    // 登出
    static void handle_logout(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json);
    // 获取验证码
    static void handle_get_captcha(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json);
    // 重置密码
    static void handle_reset_password(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json);
    // 注销账号
    static void handle_delete_account(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json);
};
#endif