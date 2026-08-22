#include "message.h"
#include "router.h"


// 所有业务接口统一调用一下函数返回数据给客户端
// 将 JSON 对象打包为 "4字节长度头 + JSON字符串"，生成完整可直接发送的TCP数据包
static std::string pack_json(const nlohmann::json& json) {
    std::string body = json.dump();
    uint32_t body_len = htonl(body.size());  // 转为网络字节序
    std::string packet;
    packet.resize(4 + body.size());
    memcpy(&packet[0], &body_len, 4);
    memcpy(&packet[4], body.data(), body.size());
    return packet;
}

//发送JSON消息，调用TCP连接的send方法发送
void send_json(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json){
    conn->send(pack_json(json));
}

// 接受JSON消息并分发
void dispatch_message(std::shared_ptr<TCPConnection> conn, const std::string& body) {
    nlohmann::json json;
    try {
        json = nlohmann::json::parse(body);
    } catch (const nlohmann::json::parse_error& e) {
        LOG(ERROR) << "JSON解析错误: " << e.what();
        send_error(conn, ERR_PARAM, 0, "JSON解析错误");
        return;
    }
    // LOG(INFO) << "路由消息类型: " << json.value("msg_type", 0);
    MessageRouter::instance().dispatch(conn, json);
}

// 错误消息发送
void send_error(std::shared_ptr<TCPConnection> conn, int error_code, int request_type, const std::string& msg, int64_t seq) {
    nlohmann::json json;
    json["code"] = error_code;
    json["msg_type"] = request_type;
    json["msg"] = msg;
    if (seq >= 0) {
        json["seq"] = seq;
    }
    send_json(conn,json);
}

// 成功消息发送
void send_ok(std::shared_ptr<TCPConnection> conn, int request_type, const nlohmann::json& data, int64_t seq) {
    nlohmann::json json;
    json["code"] = SUCCESS;
    json["msg_type"] = request_type;
    json["data"] = data;
    if (seq >= 0) {
        json["seq"] = seq;
    }
    send_json(conn,json);
}
