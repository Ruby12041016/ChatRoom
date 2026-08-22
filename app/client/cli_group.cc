#include "cli_group.h"

void Cli_Group::create_group(const std::string& group_name, const std::string& group_desc) {
    nlohmann::json request;
    request["msg_type"] = MSG_CREATE_GROUP;
    request["data"] = {{"group_name", group_name}, {"group_desc", group_desc}};
    net_->send(request.dump());
}

void Cli_Group::dismiss_group(uint64_t group_id) {
    nlohmann::json request;
    request["msg_type"] = MSG_DISMISS_GROUP;
    request["data"] = {{"group_id", group_id}};
    net_->send(request.dump());
}

void Cli_Group::apply_join_group(uint64_t group_id, const std::string& message) {
    nlohmann::json request;
    request["msg_type"] = MSG_APPLY_JOIN_GROUP;
    request["data"] = {{"group_id", group_id}, {"message", message}};
    net_->send(request.dump());
}

void Cli_Group::get_group_apply(uint64_t group_id) {
    nlohmann::json request;
    request["msg_type"] = MSG_GET_GROUP_APPLY;
    request["data"] = {{"group_id", group_id}};
    net_->send(request.dump());
}

void Cli_Group::apply_review(uint64_t group_id, uint64_t apply_id, int decision) {
    nlohmann::json request;
    request["msg_type"] = MSG_GROUP_APPLY_REVIEW;
    request["data"] = {
        {"group_id", group_id}, {"apply_id", apply_id}, {"decision", decision}};
    net_->send(request.dump());
}

void Cli_Group::quit_group(uint64_t group_id) {
    nlohmann::json request;
    request["msg_type"] = MSG_QUIT_GROUP;
    request["data"] = {{"group_id", group_id}};
    net_->send(request.dump());
}

void Cli_Group::group_member(uint64_t group_id) {
    nlohmann::json request;
    request["msg_type"] = MSG_GET_GROUP_MEMBER;
    request["data"] = {{"group_id", group_id}};
    net_->send(request.dump());
}

void Cli_Group::set_admin(uint64_t group_id, uint64_t user_id, bool set_admin) {
    nlohmann::json request;
    request["msg_type"] = MSG_GROUP_SET_ADMIN;
    request["data"] = {
        {"group_id", group_id}, {"user_id", user_id}, {"set_admin", set_admin}};
    net_->send(request.dump());
}

void Cli_Group::kick_member(uint64_t group_id, uint64_t user_id) {
    nlohmann::json request;
    request["msg_type"] = MSG_KICK_MEMBER;
    request["data"] = {{"group_id", group_id}, {"user_id", user_id}};
    net_->send(request.dump());
}

void Cli_Group::search_group(const std::string& keyword) {
    nlohmann::json request;
    request["msg_type"] = MSG_SEARCH_GROUP;
    request["data"] = {{"keyword", keyword}};
    net_->send(request.dump());
}