#include "global.h"
#include "client.h"
#include "message.h"
#include "fstream"
#include "utils.h"

using json = nlohmann::json;

class MessageQueue {
public:
    void push(const std::string& message) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(message);
        cond_var_.notify_one();
    }

    std::string pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_var_.wait(lock, [this] { return !queue_.empty(); });
        std::string message = queue_.front();
        queue_.pop();
        return message;
    }

    bool try_pop(std::string& message) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return false;
        }
        message = queue_.front();
        queue_.pop();
        return true;
    }
    
private:
    std::queue<std::string> queue_;
    std::mutex mutex_;
    std::condition_variable cond_var_;
};

MessageQueue message_queue;  // 全局消息队列

std::atomic<int> g_last_msg_type{0};
std::atomic<bool> g_response_received{false};
std::atomic<uint64_t> g_last_user_id{0};

void process_all_message();

// 等待服务端响应（任何类型），超时返回 false
bool wait_for_response(int timeout_ms = 5000) {
    auto start = std::chrono::steady_clock::now();
    while (!g_response_received) {
        process_all_message();
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - start)
                .count() > timeout_ms) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    g_response_received = false;  // 复位标志，以便下次使用
    return true;
}

// 回调函数，用于将消息放入队列
void enqueue_message(const std::string& message) {
    message_queue.push(message);
}

// 读取一行输入，去掉末尾的回车符
std::string readline(const std::string& prompt) {
    std::cout << prompt;
    std::string line;
    std::getline(std::cin, line);
    size_t start = line.find_first_not_of('\r');
    if (start == std::string::npos) {
        return "";
    }
    size_t end = line.find_last_not_of('\r');
    line = line.substr(0, end + 1);
    return line;
}

// 发送消息的辅助函数
void send_message(Client &client ,uint32_t type,const json &data){
    json j;
    j["msg_type"] = type;
    j["data"] = data;
    client.send_json(j.dump());
}

// 读取整数输入的辅助函数
int read_int(const std::string& prompt) {
    while (true) {
        std::string line = readline(prompt);
        try {
            return std::stoi(line);
        } catch (const std::invalid_argument&) {
            std::cout << "请输入有效的整数。" << std::endl;
        } catch (const std::out_of_range&) {
            std::cout << "输入的整数超出范围。" << std::endl;
        }
    }
}

void process_one_message(const std::string& message){
    g_response_received = true;
    json json_msg=json::parse(message);
    int msg_type = json_msg.value("msg_type", 0);
    int code = json_msg.value("code", -1);
    std::string msg = json_msg.value("msg", "");

    // 如果是错误响应，打印错误
    if (code != 0 && code != -1) {
        std::cout << "[错误] " << msg << std::endl;
        return;
    }

    switch (msg_type){
        case MSG_REGISTER:
            std::cout << "注册成功!用户ID: " << json_msg["data"]["user_id"]<< "，用户名: " << json_msg["data"]["username"] << std::endl;
            break;
        case MSG_LOGIN:
            std::cout << "登陆成功!用户ID: " << json_msg["data"]["user_id"]  << "，用户名: " << json_msg["data"]["username"] << std::endl;
            g_last_user_id = json_msg["data"]["user_id"].get<uint64_t>();
            break;
        case MSG_LOGOUT:
            std::cout << "登出成功!用户ID: " << json_msg["data"]["user_id"] << "，用户名: " << json_msg["data"]["username"]  << std::endl;
            break;
        case MSG_GET_CAPTCHA:
            std::cout << "验证码已发送!" << std::endl;
            break;
        case MSG_RESET_PWD:
            std::cout << "密码已重新设置!" << std::endl;
            break;
        case MSG_DELETE_ACCOUNT:
            std::cout << "账号已注销!用户ID: " << json_msg["data"]["user_id"] << "，用户名: " << json_msg["data"]["username"] << std::endl;
            break;
        case MSG_SEARCH_USER:{ 
            auto users = json_msg["data"]["users"];
            std::cout << "搜索结果：" << std::endl;
            for (auto& u : users) {
                std::cout << "  ID:" << u["user_id"]
                          << " 用户名:" << u["username"]
                          << " 邮箱:" << u.value("email", "")
                          << " 在线:" << (u["online"].get<bool>() ? "是" : "否")
                          << std::endl;
            }
            break;
        }
        case MSG_ADD_FRIEND:
            std::cout << "好友申请已发送!" << std::endl;
            break;
        case MSG_AGREE_FRIEND:
            std::cout << "已同意申请!" << std::endl;
            break;
        case MSG_REFUSE_FRIEND:
            std::cout << "已拒绝申请!" << std::endl;
            break;
        case MSG_DEL_FRIEND:
            std::cout << "好友已删除!" << std::endl;
            break;
        case MSG_GET_FRIEND_LIST:{
            auto friends = json_msg["data"]["users"];
            std::cout << "好友列表：" << std::endl;
            for (auto& f : friends) {
                std::cout << " ID:" << f["user_id"]
                        << " 用户名:" << f["username"]
                        << " 备注:" << f.value("remark", "")
                        << " 在线:" << (f["online"].get<bool>() ? "是" : "否")
                        << " 屏蔽:"
                        << (f["blocked"].get<bool>() ? "是" : "否")
                        << std::endl;
            }
            break;
        }
        case MSG_GET_APPLY_LIST:{
            auto applys = json_msg["data"]["applications"];
            std::cout << "待处理的好友申请：" << std::endl;
            for (auto& a : applys) {
                std::cout << "  申请ID:" << a["request_id"]
                          << " 来自:" << a["username"]
                          << " (用户ID:" << a["apply_id"]
                          << ") 消息:" << a.value("message", "")
                          << " 时间:" << a.value("created_at", "") << std::endl;
            }
            break;
        }
        case MSG_BLACK_USER:
            std::cout << "屏蔽状态已更新!" << std::endl;
            break;
        case MSG_SET_FRIEND_MUTE:
            std::cout << "屏蔽状态已更新!" << std::endl;
            break;
        case MSG_SET_FRIEND_REMARK:
            std::cout << "备注已设置!" << std::endl;
            break;
        case MSG_CREATE_GROUP:
            std::cout << "群已创建, ID:" << json_msg["data"]["group_id"] << std::endl;
            break;
        case MSG_DISMISS_GROUP:
            std::cout << "群已解散!" << std::endl;
            break;
        case MSG_KICK_MEMBER:
            std::cout << json_msg["data"]["msg"] << std::endl;
            break;
        case MSG_QUIT_GROUP:
            std::cout << "已退群!" << std::endl;
            break;
        case MSG_GET_GROUP_LIST: {  
            auto groups = json_msg["data"]["groups"];
            std::cout << "我的群组：" << std::endl;
            for (auto& g : groups) {
                std::cout << "  群ID:" << g["group_id"]
                          << " 群名:" << g["group_name"]
                          << " 角色:" << g["status"] << std::endl;
            }
            break;
        }
        case MSG_GET_GROUP_MEMBER: {  
            auto members = json_msg["data"]["members"];
            std::cout << "群成员列表：" << std::endl;
            for (auto& m : members) {
                std::cout << "  用户ID:" << m["user_id"]
                          << " 用户名:" << m["username"]
                          << " 角色:" << m["status"]
                          << " 在线:" << (m["online"].get<bool>() ? "是" : "否")
                          << std::endl;
            }
            break;
        } 
        case MSG_APPLY_JOIN_GROUP:
            std::cout << "入群申请已发送!" << std::endl;
            break;
        case MSG_GET_GROUP_APPLY: {
            auto apps = json_msg["data"]["applies"];
            std::cout << "入群申请列表：" << std::endl;
            for (auto& a : apps) {
                std::cout << "  申请ID:" << a["id"]
                          << " 申请人ID:" << a["apply_id"]
                          << " 消息:" << a.value("message", "")
                          << " 状态:" << a["status"] << std::endl;
            }
            break;
        }
        case MSG_GROUP_SET_ADMIN:
            std::cout << json_msg["data"]["msg"] << std::endl;
            break;
        case MSG_GROUP_APPLY_REVIEW:
            std::cout << "申请已处理!" << std::endl;
            break;
        case MSG_PRIVATE_CHAT:
            std::cout << "消息发送成功,ID:" << json_msg["data"]["message_id"] << std::endl;
            break;
        case MSG_GROUP_CHAT:
            std::cout << "群消息发送成功,ID:" << json_msg["data"]["message_id"] << std::endl;
            break;
        case MSG_FILE_MSG:
            std::cout << "文件发送成功,消息ID:" << json_msg["data"]["msg_id"] << std::endl;
            break;
        case MSG_GET_HISTORY_MSG: {
            auto msgs = json_msg["data"];
            std::cout << "历史消息：" << std::endl;
            for (auto& m : msgs) {
                std::cout << "  ID:" << m["message_id"]
                          << " 发送者:" << m["from_id"]
                          << " 内容:" << m["content"]
                          << " 类型:" << m["content_type"]
                          << " 时间:" << m["time"] << std::endl;
            }
            break;
        }
        case MSG_DOWNLOAD_FILE: {
            auto d = json_msg["data"];
            std::string filename = d["file_name"];
            std::string data_enc = d["file_data"];
            // 解码 base64
            std::string file_content = base64_decode(data_enc);
            std::ofstream out(filename, std::ios::binary);
            if (out) {
                out.write(file_content.data(), file_content.size());
                out.close();
                std::cout << "文件已保存为: " << filename
                          << " (大小: " << file_content.size() << " 字节)"
                          << std::endl;
            } else {
                std::cout << "文件保存失败" << std::endl;
            }
            break;
        }
        case PUSH_FRIEND_APPLY:
            std::cout << "[推送] 收到好友申请!来自用户ID:" << json_msg["apply_id"] << " 消息:" << json_msg.value("message", "") << std::endl;
            break;
        case PUSH_PRIVATE_MSG:
            std::cout << "[私聊] 来自用户" << json_msg["from_id"] << ": " << json_msg["content"] << std::endl;
            break;
        case PUSH_GROUP_MSG:
            std::cout << "[群聊] 群" << json_msg["to_id"] << " 用户" << json_msg["from_id"] << ": " << json_msg["content"] << std::endl;
            break;
        case PUSH_OFFLINE_NOTICE: {
            std::string ntype = json_msg.value("type", "");
            if (ntype == "new_friend_request") {
                std::cout << "[离线通知] 收到好友申请 来自用户 " << json_msg["from_uid"] << " 消息: " << json_msg.value("message", "") << std::endl;
            } else if (ntype == "friend_agree") {
                std::cout << "[离线通知] 用户 " << json_msg["user_id"] << " 同意了您的好友申请" << std::endl;
            } else if (ntype == "friend_refuse") {
                std::cout << "[离线通知] 用户 " << json_msg["user_id"] << " 拒绝了您的好友申请" << std::endl;
            } else if (ntype == "new_group_apply") {
                std::cout << "[离线通知] 群 " << json_msg["group_id"]
                          << " 收到新入群申请，申请人: "
                          << json_msg["apply_user_id"]
                          << " 消息: " << json_msg.value("message", "")
                          << std::endl;
            } else if (ntype == "group_agree") {
                std::cout << "[离线通知] 您的入群申请（群 " << json_msg["group_id"] << "）已被通过" << std::endl;
            } else if (ntype == "group_refuse") {
                std::cout << "[离线通知] 您的入群申请（群 " << json_msg["group_id"] << "）已被拒绝" << std::endl;
            } else if (ntype == "group_apply_result") {
                std::cout << "[离线通知] 群 " << json_msg["group_id"]
                          << " 的入群申请已被 " << json_msg["handler_id"]
                          << " 处理，结果: "
                          << (json_msg["result"].get<std::string>() == "agreed"
                                  ? "同意"
                                  : "拒绝")
                          << std::endl;
            } else {
                // 未知类型，打印原始 JSON 方便调试
                std::cout << "[离线通知] 未知类型: " << json_msg.dump() << std::endl;
            }
            break;
        }
        case PUSH_GROUP_APPLY:
            std::cout << "[推送] 新的入群申请!群ID:" << json_msg["group_id"] << " 申请人:" << json_msg["apply_user_id"] << std::endl;
            break;
        case PUSH_FRIEND_AGREE:
            std::cout << "[推送] 用户 " << json_msg["user_id"] << " 同意了您的好友申请" << std::endl;
            break;
        case PUSH_FRIEND_REFUSE:
            std::cout << "[推送] 用户 " << json_msg["user_id"] << " 拒绝了您的好友申请" << std::endl;
            break;
    }

    g_last_msg_type = msg_type;
}

void process_all_message(){
    std::string raw;
    while (message_queue.try_pop(raw)){
        process_one_message(raw);
    }
}

int main(int argc,char* argv[]){
    std::string ip = "127.0.0.1";
    int port = 8888;
    if(argc>=2)
        ip = argv[1];
    if(argc>=3)
        port = std::stoi(argv[2]);

    Client client(ip, port);
    client.set_MassageHandle(enqueue_message);

    std::cout << "连接到服务器 " << ip << ":" << port << " ..." << std::endl;
    if (!client.connectServer()) {
        std::cerr << "连接失败！" << std::endl;
        return 1;
    }
    std::cout << "连接成功！" << std::endl;

    bool logged_in = false;
    uint64_t my_id = 0;
    std::string my_name;

    while(true){
        process_all_message();
        if(!logged_in){
            std::cout << "==================== 您还未登录 ====================\n"
                      << "1.注册\n"
                      << "2.登陆\n"
                      << "3.忘记密码\n"
                      << "0.退出\n"
                      << "请选择：";
            int choice = read_int("");

            switch(choice){
                case 1: {
                    std::string uname = readline("用户名: ");
                    std::string pwd = readline("密码: ");
                    std::string email = readline("邮箱: ");
                    std::string phone = readline("手机号: ");
                    send_message(client, MSG_REGISTER,
                            {{"username", uname},
                             {"password", pwd},
                             {"email", email},
                             {"phone", phone}});
                    // 等待服务端响应
                    wait_for_response();
                    break;
                }
                case 2: {
                    std::string account = readline("用户名/邮箱/手机: ");
                    std::string pwd = readline("密码: ");
                    send_message(client, MSG_LOGIN, {{"username", account}, {"password", pwd}});
                    if (wait_for_response() && g_last_msg_type == MSG_LOGIN) {
                        my_id = g_last_user_id;
                        logged_in = true;
                        my_name = account;  // 简单显示用户名
                    } else {
                        std::cout << "登录失败或超时" << std::endl;
                    }
                    break;
                }
                case 3: {
                    std::string email = readline("邮箱: ");
                    std::string captcha = readline("验证码: ");
                    std::string newpwd = readline("新密码: ");
                    send_message(client, MSG_RESET_PWD,{{"email", email}, {"captcha", captcha}, {"new_password", newpwd}});
                    // 等待服务端响应
                    wait_for_response();
                    break;
                }
                case 0:
                    client.close_conn();
                    return 0;
                default:
                    std::cout << "无效选择" << std::endl;
            }
        }else {
            std::cout << "====================菜单(当前用户名称: " << my_name
                      << ")====================\n"
                      << "1.搜索用户\n"
                      << "2.好友管理\n"
                      << "3.群聊管理\n"
                      << "4.聊天\n"
                      << "5.个人信息与退出登陆\n"
                      << "0.退出程序\n"
                      << "请选择：";

            int fchoice = read_int("");
            switch (fchoice) {
                case 1: {
                    std::string kw = readline("搜索关键词: ");
                    send_message(client, MSG_SEARCH_USER, {{"keyword", kw}});
                    // 等待服务端响应
                    wait_for_response();
                    break;
                }
                case 2:{
                    std::cout << "-------------------- 好友管理 --------------------\n"
                              << "1. 查看好友列表\n"
                              << "2. 查看好友申请\n"
                              << "3. 添加好友\n"
                              << "4. 同意好友申请\n"
                              << "5. 拒绝好友申请\n"
                              << "6. 删除好友\n"
                              << "7. 屏蔽/取消屏蔽好友\n"
                              << "8. 设置好友备注\n"
                              << "0. 取消\n"
                              << "请选择: ";
                    int fchoice = read_int("");
                    switch (fchoice) {
                        case 1:
                            send_message(client, MSG_GET_FRIEND_LIST, {});
                            // 等待服务端响应
                            wait_for_response();
                            break;
                        case 2:
                            send_message(client, MSG_GET_APPLY_LIST, {});
                            // 等待服务端响应
                            wait_for_response();
                            break;
                        case 3: {
                            uint64_t uid = read_int("目标用户ID: ");
                            std::string msg = readline("附言: ");
                            send_message(client, MSG_ADD_FRIEND,{{"user_id", uid}, {"message", msg}});
                            // 等待服务端响应
                            wait_for_response();
                            break;
                        }
                        case 4: {
                            uint64_t req = read_int("申请ID: ");
                            send_message(client, MSG_AGREE_FRIEND,{{"user_id", req}});
                            // 等待服务端响应
                            wait_for_response();
                            break;
                        }
                        case 5: {
                            uint64_t req = read_int("申请ID: ");
                            send_message(client, MSG_REFUSE_FRIEND,{{"user_id", req}});
                            // 等待服务端响应
                            wait_for_response();
                            break;
                        }
                        case 6: {
                            uint64_t fid = read_int("好友ID: ");
                            send_message(client, MSG_DEL_FRIEND, {{"user_id", fid}});
                            // 等待服务端响应
                            wait_for_response();
                            break;
                        }
                        case 7: {
                            uint64_t fid = read_int("好友ID: ");
                            int block = read_int("1屏蔽,0取消: ");
                            send_message(client, MSG_SET_FRIEND_MUTE,{{"friend_id", fid}, {"blocked", block == 1}});
                            // 等待服务端响应
                            wait_for_response();
                            break;
                        }
                        case 8: {
                            uint64_t fid = read_int("好友ID: ");
                            std::string remark = readline("备注: ");
                            send_message(client, MSG_SET_FRIEND_REMARK,{{"friend_id", fid}, {"remark", remark}});
                            // 等待服务端响应
                            wait_for_response();
                            break;
                        }
                        case 0:
                            break;
                    }
                    break;
                }
                case 3:{
                    std::cout << "-------------------- 群聊管理 --------------------\n"
                              << "1. 查看我的群聊\n"
                              << "2. 建立群聊\n"
                              << "3. 申请入群\n"
                              << "4. 查看入群申请\n "
                              << "5. 审批入群申请\n"
                              << "6. 查看群成员\n"
                              << "7. 设置管理员\n"
                              << "8. 踢出群成员\n"
                              << "9. 退出群聊\n"
                              << "10.解散群聊\n"
                              << "0. 取消\n"
                              << "请选择: ";
                    int gchoice = read_int("");
                    switch (gchoice) {
                        case 1:
                            send_message(client, MSG_GET_GROUP_LIST, {});
                            // 等待服务端响应
                            wait_for_response();
                            break;
                        case 2: {
                            std::string gname = readline("群名称: ");
                            std::string desc = readline("群介绍: ");
                            send_message(client, MSG_CREATE_GROUP,{{"group_name", gname}, {"group_desc", desc}});
                            // 等待服务端响应
                            wait_for_response();
                            break;
                        }
                        case 3: {
                            uint64_t gid = read_int("群ID: ");
                            std::string msg = readline("附言: ");
                            send_message(client, MSG_APPLY_JOIN_GROUP,{{"group_id", gid}, {"message", msg}});
                            // 等待服务端响应
                            wait_for_response();
                            break;
                        }
                        case 4: {
                            uint64_t gid = read_int("群ID: ");
                            send_message(client, MSG_GET_GROUP_APPLY,{{"group_id", gid}});
                            // 等待服务端响应
                            wait_for_response();
                            break;
                        }
                        case 5: {
                            uint64_t gid = read_int("群ID: ");
                            uint64_t aid = read_int("申请ID: ");
                            int dec = read_int("1-同意,0-拒绝: ");
                            send_message(client, MSG_GROUP_APPLY_REVIEW,{{"group_id", gid}, {"apply_id", aid}, {"decision", dec}});
                            // 等待服务端响应
                            wait_for_response();
                            break;
                        }
                        case 6: {
                            uint64_t gid = read_int("群ID: ");
                            send_message(client, MSG_GET_GROUP_MEMBER,{{"group_id", gid}});
                            // 等待服务端响应
                            wait_for_response();
                            break;
                        }
                        case 7: {
                            uint64_t gid = read_int("群ID: ");
                            uint64_t uid = read_int("用户ID: ");
                            int set = read_int("1-设为管理员,0-取消: ");
                            send_message(client, MSG_GROUP_SET_ADMIN,{{"group_id", gid}, {"user_id", uid}, {"set_admin", set == 1}});
                            // 等待服务端响应
                            wait_for_response();
                            break;
                        }
                        case 8: {
                            uint64_t gid = read_int("群ID: ");
                            uint64_t uid = read_int("用户ID: ");
                            send_message(client, MSG_KICK_MEMBER,{{"group_id", gid}, {"user_id", uid}});
                            // 等待服务端响应
                            wait_for_response();
                            break;
                        }
                        case 9: {
                            uint64_t gid = read_int("群ID: ");
                            send_message(client, MSG_QUIT_GROUP,{{"group_id", gid}});
                            // 等待服务端响应
                            wait_for_response();
                            break;
                        }
                        case 10: {
                            uint64_t gid = read_int("群ID: ");
                            send_message(client, MSG_DISMISS_GROUP,{{"group_id", gid}});
                            // 等待服务端响应
                            wait_for_response();
                            break;
                        }
                        case 0:
                            break;
                    }
                    break;
                }
                case 4:{
                    std::cout << "-------------------- 聊天 --------------------\n"
                            << "1. 私聊\n"
                            << "2. 群聊\n"
                            << "3. 查看私聊历史\n"
                            << "4. 查看群聊历史\n "
                            << "5. 私聊发送文件\n"
                            << "6. 群聊发送文件\n"
                            << "7. 下载文件\n"
                            << "0. 取消\n"
                            << "请选择: ";

                    int hchoice = read_int("");
                    switch(hchoice){
                        case 1: {
                            uint64_t uid = read_int("接收者ID: ");
                            std::string msg = readline("消息: ");
                            send_message(client, MSG_PRIVATE_CHAT,{{"receiver_id", uid}, {"message", msg}, {"message_type", 0}});
                            wait_for_response();
                            break;
                        }
                        case 2: {
                            uint64_t gid = read_int("群ID: ");
                            std::string msg = readline("消息: ");
                            send_message(client, MSG_GROUP_CHAT,{{"group_id", gid}, {"message", msg}, {"message_type", 0}});
                            // 等待服务端响应
                            wait_for_response();
                            break;
                        }
                        case 3: {
                            uint64_t uid = read_int("对方ID: ");
                            send_message(client, MSG_GET_HISTORY_MSG,{{"chat_type", 1}, {"target_id", uid}, {"limit", 20}});
                            // 等待服务端响应
                            wait_for_response();
                            break;
                        }
                        case 4: {
                            uint64_t gid = read_int("群ID: ");
                            send_message(client, MSG_GET_HISTORY_MSG,{{"chat_type", 2}, {"target_id", gid}, {"limit", 20}});
                            // 等待服务端响应
                            wait_for_response();
                            break;
                        }
                        case 5:
                        case 6: {
                            bool is_group = (hchoice == 6);
                            uint64_t target = 0;
                            if (is_group)
                                target = read_int("群ID: ");
                            else
                                target = read_int("接收者ID: ");
                            std::string filepath = readline("文件路径: ");
                            std::ifstream file(filepath, std::ios::binary);
                            if (!file) {
                                std::cout << "文件打开失败" << std::endl;
                                break;
                            }
                            std::string content(
                                (std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
                            file.close();
                            // base64 编码 调用 utils 中的 base64_encode
                            std::string encoded = base64_encode(content);
                            size_t fsize = content.size();
                            std::string fname =
                                filepath.substr(filepath.find_last_of("/\\") + 1);
                            json data = {{"file_name", fname},
                                        {"file_size", fsize},
                                        {"file_content", encoded}};
                            if (is_group)
                                data["group_id"] = target;
                            else
                                data["receiver_id"] = target;
                            send_message(client, MSG_FILE_MSG, data);
                            // 等待服务端响应
                            wait_for_response();
                            break;
                        }
                        case 7: {
                            uint64_t msgid = read_int("文件消息ID: ");
                            send_message(client, MSG_DOWNLOAD_FILE,
                                    {{"message_id", msgid}});
                            // 等待服务端响应
                            wait_for_response();
                            break;
                        }
                        case 0:
                            break;
                    }
                    break;
                }
                case 5:{
                    std::cout<<"------------------- 个人信息与登出 -------------------\n"
                             << "1. 查看我的ID\n" 
                             << "2. 登出\n"
                             << "请选择: ";
                    int schoice = read_int("");
                    if (schoice == 1) {
                        std::cout << "您的用户ID: " << my_id << std::endl;
                    } else if (schoice == 2) {
                        send_message(client, MSG_LOGOUT, {{"user_id", my_id}});
                        // 等待服务端响应
                        wait_for_response();
                        logged_in = false;
                        my_id = 0;
                    }
                    break;
                }
                case 0:
                    client.close_conn();
                     return 0;
                default:
                    std::cout << "无效选择" << std::endl;
                  
            }
        }
        // 短暂等待并处理到来的消息
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        process_all_message();

    }
    return 0;
}
