#ifndef CLI_NETWORK_H
#define CLI_NETWORK_H

#include "global.h"
#include "client.h"

class Cli_Network{
public:
 Cli_Network() : client_("0.0.0.0", 8888) {}  // 默认值，后面会 Connect 覆盖
 bool connected(std::string& ip, int port);
 void send(const std::string& json);
 bool try_pop(std::string& msg);
 bool isConnected() const { return client_.isConnected(); }
 void Close() { client_.close_conn();
 }

private:
 Client client_;
 std::queue<std::string> msg_queue_;
 std::mutex mutex_;
 std::condition_variable cond_;
};

#endif