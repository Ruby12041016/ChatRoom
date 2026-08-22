#include "router.h"

MessageRouter& MessageRouter::instance() {
    static MessageRouter instance;
    return instance;
} 

// 建立msg_type和业务函数的映射
void MessageRouter::registerHandler(uint32_t type,MessageHandler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    handlers_[type] = std::move(handler);
}

// 根据msg_type找业务函数并执行
void MessageRouter::dispatch(std::shared_ptr<TCPConnection> conn,const nlohmann::json& root) {
    if (!root.contains("msg_type") || !root["msg_type"].is_number_unsigned()) {
        LOG(ERROR) << "JSON消息缺少msg_type字段或类型错误";
        return;
    }
    uint32_t msg_type = root["msg_type"].get<uint32_t>();
    MessageHandler handler;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = handlers_.find(msg_type);
        if (it != handlers_.end())
            handler = it->second;
    }
    if (handler)
        handler(conn, root);
    else
        LOG(ERROR) << "未注册的消息类型: " << msg_type;
}
