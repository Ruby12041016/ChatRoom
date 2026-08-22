#include "cli_friend.h"

void Cli_Friend::seack_user(const std::string& account) {
    nlohmann::json request;
    request["msg_type"] = MSG_SEARCH_USER;
    request["data"] = {{"keyword", account}};
    net_->send(request.dump());
}

void Cli_Friend::add_friend(uint64_t friend_id, uint64_t user_id, const std::string& msg) {
    nlohmann::json request;
    request["msg_type"] = MSG_ADD_FRIEND;
    request["data"] = {{"user_id", friend_id}, {"message", msg}};
    net_->send(request.dump());
}

void Cli_Friend::agree_friend(uint64_t apply_id, uint64_t agree_id) {
    nlohmann::json request;
    request["msg_type"] = MSG_AGREE_FRIEND;
    request["data"] = {{"user_id", apply_id}};
    net_->send(request.dump());
}

void Cli_Friend::refuse_friend(uint64_t apply_id, uint64_t agree_id) {
    nlohmann::json request;
    request["msg_type"] = MSG_REFUSE_FRIEND;
    request["data"] = {{"user_id", apply_id}};
    net_->send(request.dump());
}

void Cli_Friend::delete_friend(uint64_t user_id) {
    nlohmann::json request;
    request["msg_type"] = MSG_DEL_FRIEND;
    request["data"] = {{"user_id", user_id}};
    net_->send(request.dump());
}

void Cli_Friend::set_friend_mute(uint64_t friend_id, bool blocked) {
    nlohmann::json request;
    request["msg_type"] = MSG_SET_FRIEND_MUTE;
    request["data"] = {{"friend_id", friend_id}, {"blocked", blocked}};
    net_->send(request.dump());
}

void Cli_Friend::set_friend_remark(uint64_t friend_id, const std::string& remark) {
    nlohmann::json request;
    request["msg_type"] = MSG_SET_FRIEND_REMARK;
    request["data"] = {{"friend_id", friend_id}, {"remark", remark}};
    net_->send(request.dump());
}
