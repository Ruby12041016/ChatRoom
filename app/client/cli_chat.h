#ifndef CLI_CHAT_H
#define CLI_CHAT_H

#include <fstream>
#include "cli_network.h"
#include "global.h"
#include "utils.h"

class Cli_Chat {
   public:
    Cli_Chat(Cli_Network* net) : net_(net) {}
    void private_chat(uint64_t receiver_id, const std::string& message, int message_type);
    void group_chat(uint64_t group_id, const std::string& message, int message_type);
    void get_history(int chat_type,
                     uint64_t target_id,
                     int limit,
                     uint64_t before_msg_id = 0,
                     uint64_t after_msg_id = 0);
    void send_file(bool is_group, uint64_t target, const std::string& filepath);
    void download_file(uint64_t message_id);

    void OnDownloadResponse(const nlohmann::json& data);

    std::function<void(const std::string& file_name, uint64_t file_size)> on_download;

   private:
    Cli_Network* net_;
};

#endif