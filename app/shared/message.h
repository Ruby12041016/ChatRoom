#ifndef MESSAGE_H
#define MESSAGE_H

#include "global.h"
#include "server.h"
#include <nlohmann/json.hpp>

void send_json(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json);

void dispatch_message(std::shared_ptr<TCPConnection> conn,const std::string& body);

void send_error(std::shared_ptr<TCPConnection> conn, int error_code, int request_type, const std::string& msg, int64_t seq = -1);

void send_ok(std::shared_ptr<TCPConnection> conn, int request_type, const nlohmann::json& data,int64_t seq = -1);

#endif
