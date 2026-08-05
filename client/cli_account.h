#ifndef CLI_ACCOUNT_H
#define CLI_ACCOUNT_H

#include "global.h"
#include "cli_network.h"

class Cli_Account {
public:
 Cli_Account(Cli_Network* net) : net_(net){}

 void Register(const std::string& username, const std::string& email, const std::string& pwd, const std::string& phone);
 void Login(const std::string& account, const std::string& pwd);
 void GetCaptcha(const std::string& email); 
 void ResetPassword(const std::string& email, const std::string& captcha, const std::string& new_pwd);

 // 处理服务端响应
 void OnLoginResponse(const nlohmann::json& data);
 void OnLogoutResponse(const nlohmann::json& data);
 void OnCaptchaResponse(const nlohmann::json& data);
 void OnDeleteResponse(const nlohmann::json& data);

 // 回调：通知 UI
 std::function<void()> on_login_success;
 std::function<void()> on_logout;
 std::function<void()> on_captcha_sent;
 std::function<void(const std::string&)> on_error;
 std::function<void()> on_account_deleted;

private:
 Cli_Network* net_;
};

#endif
