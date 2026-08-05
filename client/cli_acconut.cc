#include "cli_account.h"
#include "cli_session.h"

void Cli_Account::Register(const std::string& username, const std::string& email, const std::string& pwd, const std::string& phone){
    nlohmann::json request;
    request["type"] = MSG_REGISTER;
    request["data"] = {{"username", username},
                       {"email", email},
                       {"phone", phone},
                       {"password", pwd}};
    net_->send(request.dump());
}

void Cli_Account::Login(const std::string& account, const std::string& pwd){
    nlohmann::json request;
    request["type"] = MSG_LOGIN;
    request["data"] = {{"username", account}, {"password", pwd}};
    net_->send(request.dump());
}

void Cli_Account::GetCaptcha(const std::string& email){
    nlohmann::json request;
    request["type"] = MSG_GET_CAPTCHA;
    request["data"] = {{"email", email}};
    net_->send(request.dump());
}

void Cli_Account::ResetPassword(const std::string& email, const std::string& captcha, const std::string& new_pwd){
    nlohmann::json request;
    request["type"] = MSG_GET_CAPTCHA;
    request["data"] = {
        {"email", email}, {"captcha", captcha}, {"new_password", new_pwd}};
    net_->send(request.dump());
}

void Cli_Account::OnLoginResponse(const nlohmann::json& data) {
    uint64_t user_id=data["user_id"];
    std::string username = data["username"];

    Cli_Session::instance().set_user(user_id, username);
    if(on_login_success)
        on_login_success();
}

void Cli_Account::OnLogoutResponse(const nlohmann::json& data) {
    // 清除本地会话
    Cli_Session::instance().logout();

    // 通知 UI
    if (on_logout)
        on_logout();
}

void Cli_Account::OnCaptchaResponse(const nlohmann::json& data) {
    int code = data["code"];
    if(code==0){
    if(on_captcha_sent)
            on_captcha_sent();
    }else{
        std::string msg = data.value("msg", "验证码发送错误");
        if(on_error)
            on_error(msg);
    }
}

void Cli_Account::OnDeleteResponse(const nlohmann::json& data) {
    int code = data.value("code", -1);
    if (code == 0) {
        // 清除会话
        Cli_Session::instance().logout();
        if (on_account_deleted)
            on_account_deleted();
    } else {
        std::string msg = data.value("msg", "注销失败");
        if (on_error)
            on_error(msg);
    }
}

