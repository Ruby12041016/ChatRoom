#ifndef ROUTER_H
#define ROUTER_H

#include "global.h"
#include "server.h"
#include "nlohmann/json.hpp"

using MessageHandler = std::function<void(std::shared_ptr<TCPConnection>, const nlohmann::json&)>;

class MessageRouter {
   public:
    static MessageRouter& instance();

    void registerHandler(uint32_t type, MessageHandler handler);

    void dispatch(std::shared_ptr<TCPConnection>, const nlohmann::json&);

   private:
    std::unordered_map<uint32_t, MessageHandler> handlers_;

    std::mutex mutex_;
};

#endif
