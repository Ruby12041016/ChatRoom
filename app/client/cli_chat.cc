#include "cli_chat.h"

void Cli_Chat::private_chat(uint64_t receiver_id, const std::string& message, int message_type) {
    nlohmann::json request;
    request["msg_type"] = MSG_PRIVATE_CHAT;
    request["data"] = {{"receiver_id", receiver_id},
                       {"message", message},
                       {"message_type", message_type}};
    net_->send(request.dump());
}

void Cli_Chat::group_chat(uint64_t group_id, const std::string& message, int message_type) {
    nlohmann::json request;
    request["msg_type"] = MSG_GROUP_CHAT;
    request["data"] = {{"group_id", group_id},
                       {"message", message},
                       {"message_type", message_type}};
    net_->send(request.dump());
}

void Cli_Chat::get_history(int chat_type, uint64_t target_id, int limit, uint64_t before_msg_id, uint64_t after_msg_id) {
    nlohmann::json request;
    request["msg_type"] = MSG_GET_HISTORY_MSG;
    request["data"] = {{"chat_type", chat_type},
                       {"target_id", target_id},
                       {"limit", limit},
                       {"before_msg_id", before_msg_id},
                       {"after_msg_id", after_msg_id}};
    net_->send(request.dump());
}

void Cli_Chat::send_file(bool is_group, uint64_t target, const std::string& filepath) {
    std::string fname = filepath;
    uint64_t fsize = 0;
    if (std::filesystem::exists(filepath)) {
        fsize = std::filesystem::file_size(filepath);
    }
    nlohmann::json data = {{"file_name", fname}, {"file_size", fsize}};
    if (is_group)
        data["group_id"] = target;
    else
        data["receiver_id"] = target;

    nlohmann::json request;
    request["msg_type"] = MSG_FILE_MSG;
    request["data"] = data;
    net_->send(request.dump());
}

void Cli_Chat::download_file(uint64_t message_id) {
    nlohmann::json request;
    request["msg_type"] = MSG_DOWNLOAD_FILE;
    request["data"] = {{"message_id", message_id}};
    net_->send(request.dump());
}

void Cli_Chat::OnDownloadResponse(const nlohmann::json& data) {
    std::string file_name = data.value("file_name", "");
    uint64_t file_size = data.value("file_size", 0ull);

    if (file_name.empty()) {
        std::cerr << "[错误] 服务端返回的文件信息无效" << std::endl;
        return;
    }

    if (on_download)
        on_download(file_name, file_size);
}