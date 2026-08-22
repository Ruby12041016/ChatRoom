#include "cli_network.h"

bool Cli_Network::connected(std::string& ip, int port){
    client_.setServer(ip, port);
    client_.set_MassageHandle([this](const std::string& raw) {
        std::lock_guard<std::mutex> lock(mutex_);
        msg_queue_.push(raw);
        cond_.notify_one();
    });
    return client_.connectServer();
}

void Cli_Network::send(const std::string& json) {
    if (!client_.isConnected())
        return;

    client_.send_json(json);
}

bool Cli_Network::try_pop(std::string& msg){
    std::lock_guard<std::mutex> lock(mutex_);
    if (msg_queue_.empty()) {
        return false;
    }
    msg = msg_queue_.front();
    msg_queue_.pop();
    return true;
}
