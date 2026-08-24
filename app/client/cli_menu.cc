#include "cli_menu.h"
#include <sys/select.h>
#include <unistd.h>
#include <chrono>
#include <vector>

#define RED "\033[38;2;210;60;60m"
#define GREEN "\033[32m"
#define MINT "\033[38;5;121m"
#define BLUE "\033[34m"
#define SKY_BLUE "\033[38;5;117m"

Cli_Menu::Cli_Menu() : account_(&net_), chat_(&net_), friend_(&net_), group_(&net_) {
    // 设置回调
    account_.on_login_success = [this]() {
        std::cout << GREEN << "登录成功!用户: " << Cli_Session::instance().get_name() << std::endl;
    };
    account_.on_logout = [this]() {
        known_users_.clear();
        uname_to_id.clear();
        known_groups_.clear();
        gname_to_id.clear();
        std::cout << GREEN << "已安全登出" << std::endl;
    };
    account_.on_captcha_sent = [this]() {
        std::cout << GREEN << "验证码已发送，请查收邮箱" << std::endl;
    };
    account_.on_account_deleted = [this]() {
        std::cout << GREEN << "账号已注销" << std::endl;
    };
    big_file_.on_progress = [](long long current, long long total) {
        if (total <= 0)
            return;
        int percentage = (int)(current * 100 / total);
        int total_ = 40;
        int filled = (int)(total_ * current / total);
        std::cout << "\r[" << std::string(filled, '=')
                  << (filled < total_ ? ">" : "")
                  << std::string(total_ - filled - (filled < total_ ? 1 : 0),
                                 ' ')
                  << "] " << percentage << "%  "
                  << current << " B / " << total << " B" << std::flush;
    };
}

void Cli_Menu::Run(int argc, char* argv[]) {
    // std::string ip = "127.0.0.1";
    // int port = 8888;
    // if (argc >= 2)
    std::string ip = argv[1];
    // if (argc >= 3)
    int port = std::stoi(argv[2]);

    server_ip_ = ip;

    std::cout << "正在连接到服务器 " << ip << ":" << port << " ..." << std::endl;
    if (!net_.connected(ip, port)) {
        std::cerr << RED << "连接失败！" << std::endl;
        return;
    }
    std::cout << GREEN << "连接成功！" << std::endl;

    input_thread_ = std::thread(&Cli_Menu::InputThreadFunc, this);

    while (running_) {
        if (!net_.isConnected()) {
            running_ = false;
            break;
        }
        ProcessMessages();
        if (!net_.isConnected()) {
            std::cout << RED << "\n与服务器的连接已断开,程序即将退出..." << std::endl;
            running_ = false;
            break;
        }
        if (!Cli_Session::instance().islogin())
            ShowLoginMenu();
        else
            ShowMainMenu();
    }

    net_.Close();

    if (input_thread_.joinable()) {
        input_thread_.join();
    }
}

// 通用操作同步等待函数
bool Cli_Menu::WaitForOperation(int timeout_sec) {
    op_done_ = false;
    op_success_ = true;  // 默认成功，错误时由ProcessMessages置false
    auto start = std::chrono::steady_clock::now();
    while (!op_done_ && running_) {
        if (!net_.isConnected()) {
            running_ = false;
            return false;
        }
        auto now = std::chrono::steady_clock::now();
        auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - start) .count();
        if (dur >= timeout_sec) {
            std::cout << RED << "\n[提示] 操作超时，请稍后重试" << std::endl;
            return false;
        }
        ProcessMessages();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return op_done_;
}

// 统一推送打印函数
void Cli_Menu::PrintPush(const std::string& content, bool is_show) {
    if (push_buffered_) {
        push_buffer_.push_back(content);
    } else {
        if (is_show || push_count_ < 5) {
            std::cout << "\n" << GREEN << content << std::endl;
            if (!current_prompt_.empty()) {
                std::cout << SKY_BLUE << current_prompt_ << std::flush;
            }
        }
        if (!is_show ) {
            push_count_++;
            if(push_count_==6){
                showed = 1;
                if(!showed){
                    std::cout << "\n" << BLUE << "[提示] 聊天消息较多，后续将不再逐条显示,可前往聊天界面查看" << std::endl;
                }
            }
        }
    }
}

// 批量输出缓存推送
void Cli_Menu::FlushPushBuffer() {
    if (!push_buffer_.empty()) {
        std::cout << BLUE << "\n-------------------- 期间收到 "
                  << push_buffer_.size() << " 条新消息 --------------------"
                  << std::endl;
        for (const auto& msg : push_buffer_) {
            std::cout << BLUE << msg << std::endl;
        }
        std::cout
            << BLUE
            << "-------------------------------------------------------------"
            << std::endl;
        push_buffer_.clear();
    }

    if (push_count_ > 5 && chat_show && !showed) {
        std::cout << BLUE << "\n[提示] 另有 " << (push_count_ - 5)  << " 条推送未显示，进入聊天界面查看详情" << std::endl;
    }
    push_count_ = 0;
}

void Cli_Menu::ProcessMessages() {
    std::string raw;
    while (net_.try_pop(raw)) {
        auto json_msg = nlohmann::json::parse(raw);
        int type = json_msg["msg_type"];
        int code = json_msg.value("code", -1);
        auto& data = json_msg["data"];

        // 错误响应统一处理
        if (code != 0 && code != -1) {
            std::cout << RED << "[错误] " << json_msg.value("msg", "") << std::endl;
            op_done_ = true;  // 失败也标记操作完成，避免死等
            op_success_ = false;
            continue;
        }

        switch (type) {
            case MSG_REGISTER: {
                uint64_t uid = data["user_id"].get<uint64_t>();
                std::string uname = data["username"].get<std::string>();
                known_users_[uid] = uname;
                uname_to_id[uname] = uid;
                std::cout << GREEN << "注册成功!用户ID: " << uid << "，用户名: " << uname << std::endl;
                op_done_ = true;
                break;
            }
            case MSG_LOGIN: {
                uint64_t my_id = data["user_id"].get<uint64_t>();
                std::string my_name = data["username"].get<std::string>();
                known_users_[my_id] = my_name;
                uname_to_id[my_name] = my_id;
                account_.OnLoginResponse(data);

                // 直接在这里填充全量用户和群组映射
                if (data.contains("all_users")) {
                    for (auto& u : data["all_users"]) {
                        uint64_t uid = u["user_id"].get<uint64_t>();
                        std::string uname = u["username"].get<std::string>();
                        known_users_[uid] = uname;
                        uname_to_id[uname] = uid;
                    }
                }
                if (data.contains("all_groups")) {
                    for (auto& g : data["all_groups"]) {
                        uint64_t gid = g["group_id"].get<uint64_t>();
                        std::string gname = g["group_name"].get<std::string>();
                        known_groups_[gid] = gname;
                        gname_to_id[gname] = gid;
                    }
                }
                op_done_ = true;
                break;
            }
            case MSG_LOGOUT:
                account_.OnLogoutResponse(data);
                op_done_ = true;
                break;
            case MSG_GET_CAPTCHA:
                account_.OnCaptchaResponse(data);
                op_done_ = true;
                break;
            case MSG_RESET_PWD:
                std::cout << "密码已重新设置!" << std::endl;
                op_done_ = true;
                break;
            case MSG_DELETE_ACCOUNT:
                account_.OnDeleteResponse(data);
                op_done_ = true;
                break;
            case MSG_SEARCH_USER: {
                auto users = data["users"];
                std::cout << "搜索结果：" << std::endl;
                for (auto& u : users) {
                    uint64_t uid = u["user_id"].get<uint64_t>();
                    std::string uname = u["username"].get<std::string>();
                    known_users_[uid] = uname;
                    uname_to_id[uname] = uid;
                    std::cout
                        << BLUE << "  ID:" << uid << " 用户名:" << uname
                        << " 邮箱:" << u.value("email", "")
                        << " 在线:" << (u["online"].get<bool>() ? "是" : "否")
                        << std::endl;
                }
                op_done_ = true;
                break;
            }
            case MSG_SEARCH_GROUP: {
                auto groups = data["groups"];
                std::cout << "搜索结果：" << std::endl;
                for (auto& g : groups) {
                    uint64_t gid = g["group_id"].get<uint64_t>();
                    std::string gname = g["group_name"].get<std::string>();
                    known_groups_[gid] = gname;
                    gname_to_id[gname] = gid;
                    std::cout << BLUE << "  群ID:" << gid << " 群名:" << gname
                              << " 群主ID:" << g["owner_id"].get<uint64_t>()
                              << " 群介绍:" << g.value("description", "")
                              << std::endl;
                }
                op_done_ = true;
                break;
            }
            case MSG_ADD_FRIEND:
                std::cout << GREEN << "好友申请已发送!" << std::endl;
                op_done_ = true;
                break;
            case MSG_AGREE_FRIEND:
                std::cout << GREEN << "已同意申请!" << std::endl;
                op_done_ = true;
                break;
            case MSG_REFUSE_FRIEND:
                std::cout << GREEN << "已拒绝申请!" << std::endl;
                op_done_ = true;
                break;
            case MSG_DEL_FRIEND:
                std::cout << GREEN << "好友已删除!" << std::endl;
                op_done_ = true;
                break;
            case MSG_GET_FRIEND_LIST: {
                auto friends = data["users"];
                std::cout << BLUE << "好友列表：" << std::endl;
                friend_ids_.clear();
                for (auto& f : friends) {
                    uint64_t uid = f["user_id"].get<uint64_t>();
                    std::string uname = f["username"].get<std::string>();
                    known_users_[uid] = uname;
                    uname_to_id[uname] = uid;
                    friend_ids_.insert(uid);
                    std::cout
                        << BLUE << "  用户名:" << uname
                        << "  备注:" << f.value("remark", "")
                        << "  在线:" << (f["online"].get<bool>() ? "是" : "否")
                        << "  屏蔽:" << (f["blocked"].get<bool>() ? "是" : "否")
                        << std::endl;
                }
                op_done_ = true;
                break;
            }
            case MSG_GET_APPLY_LIST: {
                auto applys = data["applications"];
                std::cout << BLUE << "待处理的好友申请：" << std::endl;
                for (auto& a : applys) {
                    uint64_t apply_id = a["apply_id"].get<uint64_t>();
                    std::string apply_name = known_users_.count(apply_id)
                                                 ? known_users_[apply_id]
                                                 : std::to_string(apply_id);
                    std::cout
                        << BLUE
                        << "  申请ID:" << a["request_id"].get<uint64_t>()
                        << " 来自:" << apply_name << " (用户ID:" << apply_id
                        << ") 消息:" << a.value("message", "")
                        << " 时间:" << a.value("created_at", "") << std::endl;
                }
                op_done_ = true;
                break;
            }
            case MSG_BLACK_USER:
                std::cout << GREEN << "屏蔽状态已更新!" << std::endl;
                op_done_ = true;
                break;
            case MSG_SET_FRIEND_MUTE:
                std::cout << GREEN << "屏蔽状态已更新!" << std::endl;
                op_done_ = true;
                break;
            case MSG_SET_FRIEND_REMARK:
                std::cout << GREEN << "备注已设置!" << std::endl;
                op_done_ = true;
                break;
            case MSG_CREATE_GROUP: {
                uint64_t gid = data["group_id"].get<uint64_t>();
                std::string gname = data["group_name"].get<std::string>();
                known_groups_[gid] = gname;
                gname_to_id[gname] = gid;
                std::cout << GREEN << "群已创建, ID:" << gid << std::endl;
                op_done_ = true;
                break;
            }
            case MSG_DISMISS_GROUP:
                std::cout << GREEN << "群已解散!" << std::endl;
                op_done_ = true;
                break;
            case MSG_KICK_MEMBER:
                std::cout << GREEN << data["msg"].get<std::string>()  << std::endl;
                op_done_ = true;
                break;
            case MSG_QUIT_GROUP:
                std::cout << GREEN << "已退群!" << std::endl;
                op_done_ = true;
                break;
            case MSG_GET_GROUP_LIST: {
                auto groups = data["groups"];
                std::cout << BLUE << "我的群组：" << std::endl;
                my_group_ids_.clear();
                for (auto& g : groups) {
                    uint64_t gid = g["group_id"].get<uint64_t>();
                    std::string gname = g["group_name"].get<std::string>();
                    known_groups_[gid] = gname;
                    gname_to_id[gname] = gid;
                    my_group_ids_.insert(gid);
                    std::cout
                        << BLUE << "  群ID:" << gid << " 群名:" << gname
                        << " 我的身份:" << member_type[g["status"].get<int>()]
                        << std::endl;
                }
                op_done_ = true;
                break;
            }
            case MSG_GET_GROUP_MEMBER: {
                auto members = data["members"];
                std::cout << BLUE << "群成员列表：" << std::endl;
                for (auto& m : members) {
                    uint64_t uid = m["user_id"].get<uint64_t>();
                    std::string uname = m["username"].get<std::string>();
                    known_users_[uid] = uname;
                    uname_to_id[uname] = uid;
                    std::cout
                        << BLUE << "  用户名:" << uname
                        << "  角色:" << member_type[m["status"].get<int>()]
                        << "  在线:" << (m["online"].get<bool>() ? "是" : "否")
                        << std::endl;
                }
                op_done_ = true;
                break;
            }
            case MSG_APPLY_JOIN_GROUP:
                std::cout << GREEN << "入群申请已发送!" << std::endl;
                op_done_ = true;
                break;
            case MSG_GET_GROUP_APPLY: {
                auto apps = data["applies"];
                std::cout << BLUE << "入群申请列表：" << std::endl;
                for (auto& a : apps) {
                    uint64_t apply_id = a["apply_id"].get<uint64_t>();
                    std::string apply_name = known_users_.count(apply_id)
                                                 ? known_users_[apply_id]
                                                 : std::to_string(apply_id);
                    int status = a["status"].get<int>();
                    std::string status_str = status == 0   ? "待处理"
                                             : status == 1 ? "已同意"
                                                           : "已拒绝";
                    std::cout << BLUE << "  申请ID:" << a["id"].get<uint64_t>()
                              << " 申请人:" << apply_name << "(ID:" << apply_id
                              << ")"
                              << " 消息:" << a.value("message", "")
                              << " 状态:" << status_str << std::endl;
                }
                op_done_ = true;
                break;
            }
            case MSG_GROUP_SET_ADMIN:
                std::cout << GREEN << data["msg"].get<std::string>() << std::endl;
                op_done_ = true;
                break;
            case MSG_GROUP_APPLY_REVIEW:
                std::cout << GREEN << "申请已处理!" << std::endl;
                op_done_ = true;
                break;
            case MSG_PRIVATE_CHAT: {
                if (chat_context_type_ == 1 &&
                    data["to_id"].get<uint64_t>() == chat_context_id_) {
                    uint64_t msg_id = data.value("message_id", 0ull);
                    std::string time_str = data.value("time", "");
                    std::string content_str = data.value("content", "");
                    std::string my_name =
                        known_users_.count(Cli_Session::instance().get_id())
                            ? known_users_[Cli_Session::instance().get_id()]
                            : std::to_string(Cli_Session::instance().get_id());
                    std::cout << SKY_BLUE << "[Message_ID:" << msg_id << "]"
                              << "[" << time_str << "] " << my_name << ":"
                              << content_str << std::endl;
                    if (msg_id > last_read_msg_id_) {
                        last_read_msg_id_ = msg_id;
                    }
                    if (!current_prompt_.empty()) {
                        std::cout << SKY_BLUE << current_prompt_ << std::flush;
                    }
                }
                op_done_ = true;
                break;
            }
            case MSG_GROUP_CHAT: {
                if (chat_context_type_ == 2 &&
                    data["to_id"].get<uint64_t>() == chat_context_id_) {
                    uint64_t msg_id = data.value("message_id", 0ull);
                    std::string time_str = data.value("time", "");
                    std::string content_str = data.value("content", "");
                    std::string my_name =
                        known_users_.count(Cli_Session::instance().get_id())
                            ? known_users_[Cli_Session::instance().get_id()]
                            : std::to_string(Cli_Session::instance().get_id());
                    std::cout << SKY_BLUE << "[Message_ID:" << msg_id << "]"
                              << "[" << time_str << "] " << my_name << ":"
                              << content_str << std::endl;
                    if (msg_id > last_read_msg_id_) {
                        last_read_msg_id_ = msg_id;
                    }
                    if (!current_prompt_.empty()) {
                        std::cout << SKY_BLUE << current_prompt_ << std::flush;
                    }
                }
                op_done_ = true;
                break;
            }
            case MSG_FILE_MSG:
                std::cout << GREEN << "文件发送成功,消息ID:" << data["msg_id"].get<uint64_t>() << std::endl;
                op_done_ = true;
                break;

            case MSG_GET_HISTORY_MSG: {
                uint64_t max_msg_id = 0;
                uint64_t min_msg_id = UINT64_MAX;
                for (auto& m : data) {
                    uint64_t msg_id = m["message_id"].get<uint64_t>();
                    if (msg_id > max_msg_id) {
                        max_msg_id = msg_id;
                    }
                    if (msg_id < min_msg_id) {
                        min_msg_id = msg_id;
                    }
                    uint64_t from_id = m["from_id"].get<uint64_t>();
                    std::string from_name = known_users_.count(from_id)
                                                ? known_users_[from_id]
                                                : std::to_string(from_id);
                    std::string time_str = m.value("time", "");
                    std::string content_str = m.value("content", "");
                    std::cout << SKY_BLUE << "[Message_ID:" << msg_id << "]"
                              << "[" << time_str << "] " << from_name << ":"
                              << content_str << std::endl;
                }
                if (max_msg_id > last_read_msg_id_) {
                    last_read_msg_id_ = max_msg_id;
                }
                if (min_msg_id != UINT64_MAX &&
                    (oldest_msg_id_ == 0 || min_msg_id < oldest_msg_id_)) {
                    oldest_msg_id_ = min_msg_id;
                }
                op_done_ = true;
                break;
            }
            case MSG_DOWNLOAD_FILE: {
                chat_.OnDownloadResponse(data);
                op_done_ = true;
                break;
            }
            case PUSH_FRIEND_APPLY: {
                uint64_t apply_id = data["apply_id"].get<uint64_t>();
                uint64_t request_id = data.value("request_id", 0ull);
                std::string apply_name = data.value("apply_name", "");
                if (apply_name.empty()) {
                    apply_name = known_users_.count(apply_id)
                                     ? known_users_[apply_id]
                                     : std::to_string(apply_id);
                }
                known_users_[apply_id] = apply_name;
                PrintPush("[推送] 收到好友申请! 申请ID:" +
                          std::to_string(request_id) + " 来自: " + apply_name +
                          "(ID:" + std::to_string(apply_id) + ") " +
                          " 消息:" + data.value("message", ""));
                break;
            }
            case PUSH_PRIVATE_MSG: {
                uint64_t from = data["from_id"].get<uint64_t>();
                uint64_t msg_id = data.value("message_id", 0ull);
                std::string name = known_users_.count(from)
                                       ? known_users_[from]
                                       : std::to_string(from);
                if (chat_context_type_ == 1 && from == chat_context_id_) {
                    std::string time_str = data.value("time", "");
                    std::string content_str = data.value("content", "");
                    std::cout << MINT << "[Message_ID:" << msg_id << "]"
                              << "[" << time_str << "] " << name << ":"
                              << content_str << std::endl;
                    if (msg_id > last_read_msg_id_) {
                        last_read_msg_id_ = msg_id;
                    }
                    if (!current_prompt_.empty()) {
                        std::cout << SKY_BLUE << current_prompt_ << std::flush;
                    }
                } else if (chat_context_type_ != 0) {
                    other_msg_count_++;
                    push_buffer_.push_back("[私聊] " + name);
                } else {
                    PrintPush("[私聊] " + name, false);
                }
                break;
            }
            case PUSH_GROUP_MSG: {
                uint64_t gid = data["to_id"].get<uint64_t>();
                uint64_t from_id = data["from_id"].get<uint64_t>();
                std::string gname = known_groups_.count(gid)
                                        ? known_groups_[gid]
                                        : ("群ID:" + std::to_string(gid));
                std::string uname = known_users_.count(from_id)
                                        ? known_users_[from_id]
                                        : std::to_string(from_id);
                if (chat_context_type_ == 2 && gid == chat_context_id_) {
                    uint64_t msg_id = data.value("message_id", 0ull);
                    std::string time_str = data.value("time", "");
                    std::string content_str = data.value("content", "");
                    std::cout << MINT << "[Message_ID:" << msg_id << "]"
                              << "[" << time_str << "] " << uname << ":"
                              << content_str << std::endl;
                    if (msg_id > last_read_msg_id_) {
                        last_read_msg_id_ = msg_id;
                    }
                    if (!current_prompt_.empty()) {
                        std::cout << SKY_BLUE << current_prompt_ << std::flush;
                    }
                } else if (chat_context_type_ != 0) {
                    other_msg_count_++;
                    push_buffer_.push_back("[群聊消息]" + gname + ": " + uname);
                } else {
                    PrintPush("[群聊消息]" + gname + ":" + uname, false);
                }
                break;
            }
            case PUSH_OFFLINE_NOTICE: {
                std::string ntype = data.value("type", "");
                std::string notice;
                if (ntype == "new_friend_request") {
                    uint64_t from_id = data["apply_id"].get<uint64_t>();
                    std::string from_name = known_users_.count(from_id)
                                                ? known_users_[from_id]
                                                : std::to_string(from_id);
                    notice = "[离线通知] 收到好友申请 来自: " + from_name +
                             "(ID:" + std::to_string(from_id) + ") " +
                             " 消息: " + data.value("message", "");
                } else if (ntype == "friend_agree") {
                    uint64_t user_id = data["user_id"].get<uint64_t>();
                    std::string user_name = known_users_.count(user_id)
                                                ? known_users_[user_id]
                                                : std::to_string(user_id);
                    notice = "[离线通知] 用户 " + user_name +
                             "(ID:" + std::to_string(user_id) +
                             ") 同意了您的好友申请";
                } else if (ntype == "friend_refuse") {
                    uint64_t user_id = data["user_id"].get<uint64_t>();
                    std::string user_name = known_users_.count(user_id)
                                                ? known_users_[user_id]
                                                : std::to_string(user_id);
                    notice = "[离线通知] 用户 " + user_name +
                             "(ID:" + std::to_string(user_id) +
                             ") 拒绝了您的好友申请";
                } else if (ntype == "new_group_apply") {
                    uint64_t group_id = data["group_id"].get<uint64_t>();
                    uint64_t apply_uid = data["apply_user_id"].get<uint64_t>();
                    std::string gname = known_groups_.count(group_id)
                                            ? known_groups_[group_id]
                                            : std::to_string(group_id);
                    std::string uname = known_users_.count(apply_uid)
                                            ? known_users_[apply_uid]
                                            : std::to_string(apply_uid);
                    notice = "[离线通知] 群 " + gname +
                             "(ID:" + std::to_string(group_id) + ") " +
                             "收到新入群申请，申请人: " + uname +
                             "(ID:" + std::to_string(apply_uid) + ") " +
                             " 消息: " + data.value("message", "");
                } else if (ntype == "group_agree") {
                    uint64_t group_id = data["group_id"].get<uint64_t>();
                    std::string gname = known_groups_.count(group_id)
                                            ? known_groups_[group_id]
                                            : std::to_string(group_id);
                    notice = "[离线通知] 您的入群申请（群 " + gname +
                             " ID:" + std::to_string(group_id) + "）已被通过";
                } else if (ntype == "group_refuse") {
                    uint64_t group_id = data["group_id"].get<uint64_t>();
                    std::string gname = known_groups_.count(group_id)
                                            ? known_groups_[group_id]
                                            : std::to_string(group_id);
                    notice = "[离线通知] 您的入群申请（群 " + gname +
                             " ID:" + std::to_string(group_id) + "）已被拒绝";
                } else if (ntype == "group_apply_result") {
                    uint64_t group_id = data["group_id"].get<uint64_t>();
                    uint64_t handler_id = data["handle_id"].get<uint64_t>();
                    std::string gname = known_groups_.count(group_id)
                                            ? known_groups_[group_id]
                                            : std::to_string(group_id);
                    std::string hname = known_users_.count(handler_id)
                                            ? known_users_[handler_id]
                                            : std::to_string(handler_id);
                    notice = "[离线通知] 群 " + gname +
                             "(ID:" + std::to_string(group_id) +
                             ") 的入群申请已被 " + hname +
                             "(ID:" + std::to_string(handler_id) +
                             ") 处理，结果: " +
                             (data["result"].get<std::string>() == "agreed"
                                  ? "同意"
                                  : "拒绝");
                } else {
                    notice = "[离线通知] 未知类型: " + data.dump();
                }
                PrintPush(notice);
                break;
            }
            case PUSH_GROUP_APPLY: {
                std::string ntype = data.value("type", "");
                if (ntype == "new_group_apply") {
                    uint64_t gid = data["group_id"].get<uint64_t>();
                    uint64_t apply_uid = data["apply_user_id"].get<uint64_t>();
                    uint64_t apply_id = data.value("apply_id", 0ull);
                    std::string gname = known_groups_.count(gid)
                                            ? known_groups_[gid]
                                            : std::to_string(gid);
                    std::string uname = known_users_.count(apply_uid)
                                            ? known_users_[apply_uid]
                                            : std::to_string(apply_uid);
                    PrintPush("[推送] 新的入群申请! 申请ID:" +
                              std::to_string(apply_id) + " [群]:" + gname +
                              "(ID:" + std::to_string(gid) + ") " + "申请人:" +
                              uname + "(ID:" + std::to_string(apply_uid) + ")");
                } else if (ntype == "group_apply_result") {
                    uint64_t gid = data["group_id"].get<uint64_t>();
                    uint64_t handler_id = data.value("handler_id", 0ull);
                    uint64_t apply_id = data.value("apply_id", 0ull);
                    std::string gname = known_groups_.count(gid)
                                            ? known_groups_[gid]
                                            : std::to_string(gid);
                    std::string hname = handler_id > 0
                                            ? (known_users_.count(handler_id)
                                                   ? known_users_[handler_id]
                                                   : std::to_string(handler_id))
                                            : "某管理员";
                    std::string hname_id =
                        handler_id > 0
                            ? "(ID:" + std::to_string(handler_id) + ")"
                            : "";
                    std::string result =
                        data.value("result", "") == "agreed" ? "同意" : "拒绝";
                    PrintPush("[推送] " + hname + hname_id + " 已" + result +
                              "入群申请! 申请ID:" + std::to_string(apply_id) +
                              " [群]:" + gname + "(ID:" + std::to_string(gid) +
                              ")");
                }
                break;
            }
            case PUSH_GROUP_AGREE: {
                std::string ntype = data.value("type", "");
                uint64_t gid = data["group_id"].get<uint64_t>();
                std::string gname = known_groups_.count(gid)
                                        ? known_groups_[gid]
                                        : std::to_string(gid);
                if (ntype == "group_agree") {
                    PrintPush("[推送] 您的入群申请已被同意! [群]:" + gname +
                              "(ID:" + std::to_string(gid) + ")");
                } else {
                    PrintPush("[推送] 您的入群申请已被拒绝! [群]:" + gname +
                              "(ID:" + std::to_string(gid) + ")");
                }
                break;
            }
            case PUSH_FRIEND_AGREE: {
                uint64_t user_id = data["user_id"].get<uint64_t>();
                std::string user_name = data.value("user_name", "");
                if (user_name.empty()) {
                    user_name = known_users_.count(user_id)
                                    ? known_users_[user_id]
                                    : std::to_string(user_id);
                }
                known_users_[user_id] = user_name;
                PrintPush("[推送] 用户 " + user_name + " 同意了您的好友申请");
                break;
            }
            case PUSH_FRIEND_REFUSE: {
                uint64_t user_id = data["user_id"].get<uint64_t>();
                std::string user_name = data.value("user_name", "");
                if (user_name.empty()) {
                    user_name = known_users_.count(user_id)
                                    ? known_users_[user_id]
                                    : std::to_string(user_id);
                }
                known_users_[user_id] = user_name;
                PrintPush("[推送] 用户 " + user_name + " 拒绝了您的好友申请");
                break;
            }
        }
    }
}

// 登录菜单
void Cli_Menu::ShowLoginMenu() {
    std::cout << SKY_BLUE
              << "==================== 您还未登录 ====================\n"
              << "1.注册\n"
              << "2.登陆\n"
              << "3.忘记密码\n"
              << "0.退出\n";
    int choice = read_int("请选择：");

    switch (choice) {
        case 1: {
            push_buffered_ = true;  // 注册期间缓存推送

            std::string uname = readline("用户名: ");
            std::string pwd = read_pwd("密码: ");
            std::string email = readline("邮箱: ");
            std::string phone = readline("手机号: ");

            account_.Register(uname, email, pwd, phone);
            WaitForOperation();  // 同步等待结果

            push_buffered_ = false;
            FlushPushBuffer();
            break;
        }
        case 2: {
            std::string account = readline("用户名/邮箱/手机: ");
            std::string pwd = read_pwd("密码: ");

            account_.Login(account, pwd);
            WaitForOperation(10);  // 同步等待登录结果
            break;
        }
        case 3: {
            push_buffered_ = true;  // 改密码期间缓存推送

            std::string email = readline("邮箱: ");
            account_.GetCaptcha(email, "reset");
            WaitForOperation(15);

            std::string captcha = readline("验证码: ");
            std::string newpwd = read_pwd("新密码: ");
            account_.ResetPassword(email, captcha, newpwd);
            WaitForOperation(15);

            push_buffered_ = false;
            FlushPushBuffer();
            break;
        }
        case 0:
            running_ = false;
            return;
        default:
            std::cout << RED << "无效选择" << std::endl;
    }
}

void Cli_Menu::ShowMainMenu() {
    std::cout << SKY_BLUE
              << "==================== " << Cli_Session::instance().get_name()
              << " ====================\n"
              << "1.搜索用户\n"
              << "2.好友管理\n"
              << "3.群聊管理\n"
              << "4.聊天\n"
              << "5.个人信息与退出登陆\n"
              << "0.退出程序\n";

    int fchoice = read_int("请选择：");
    switch (fchoice) {
        case 1: {
            std::string kw = readline("搜索关键词: ");
            nlohmann::json request;
            request["msg_type"] = MSG_SEARCH_USER;
            request["data"] = {{"keyword", kw}};
            net_.send(request.dump());
            WaitForOperation();
            break;
        }
        case 2: {
            int a_flag = 0;
            while (!a_flag && running_) {
                std::cout
                    << SKY_BLUE
                    << "-------------------- 好友管理 --------------------\n"
                    << "1. 查看好友列表\n"
                    << "2. 查看好友申请\n"
                    << "3. 添加好友\n"
                    << "4. 同意好友申请\n"
                    << "5. 拒绝好友申请\n"
                    << "6. 删除好友\n"
                    << "7. 屏蔽/取消屏蔽好友\n"
                    << "8. 设置好友备注\n"
                    << "0. 取消\n";
                int fchoice2 = read_int("请选择: ");
                if (!running_)
                    break;
                switch (fchoice2) {
                    case 1: {
                        nlohmann::json request;
                        request["msg_type"] = MSG_GET_FRIEND_LIST;
                        net_.send(request.dump());
                        WaitForOperation();
                        break;
                    }
                    case 2: {
                        nlohmann::json request;
                        request["msg_type"] = MSG_GET_APPLY_LIST;
                        net_.send(request.dump());
                        WaitForOperation();
                        break;
                    }
                    case 3: {
                        std::string uname = readline("目标用户名称: ");
                        if (!uname_to_id.count(uname)) {
                            nlohmann::json req;
                            req["msg_type"] = MSG_SEARCH_USER;
                            req["data"] = {{"keyword", uname}};
                            net_.send(req.dump());
                            WaitForOperation();
                        }
                        if (!uname_to_id.count(uname)) {
                            std::cout << RED << "[错误] 未找到该用户" << std::endl;
                            break;
                        }
                        uint64_t uid = uname_to_id[uname];
                        std::string msg = readline("附言: ");
                        friend_.add_friend(uid, Cli_Session::instance().get_id(), msg);
                        WaitForOperation();
                        break;
                    }
                    case 4: {
                        uint64_t req = read_int("申请ID: ");
                        friend_.agree_friend(req, Cli_Session::instance().get_id());
                        WaitForOperation();
                        break;
                    }
                    case 5: {
                        uint64_t req = read_int("申请ID: ");
                        friend_.refuse_friend(req, Cli_Session::instance().get_id());
                        WaitForOperation();
                        break;
                    }
                    case 6: {
                        std::string fname = readline("好友名称: ");
                        if (!uname_to_id.count(fname)) {
                            nlohmann::json req;
                            req["msg_type"] = MSG_SEARCH_USER;
                            req["data"] = {{"keyword", fname}};
                            net_.send(req.dump());
                            WaitForOperation();
                        }
                        if (!uname_to_id.count(fname)) {
                            std::cout << RED << "[错误] 不存在该好友"
                                      << std::endl;
                            break;
                        }
                        uint64_t fid = uname_to_id[fname];
                        friend_.delete_friend(fid);
                        WaitForOperation();
                        break;
                    }
                    case 7: {
                        std::string fname = readline("好友名称: ");
                        if (!uname_to_id.count(fname)) {
                            nlohmann::json req;
                            req["msg_type"] = MSG_SEARCH_USER;
                            req["data"] = {{"keyword", fname}};
                            net_.send(req.dump());
                            WaitForOperation();
                        }
                        if (!uname_to_id.count(fname)) {
                            std::cout << "[错误] 不存在该好友" << std::endl;
                            break;
                        }
                        uint64_t fid = uname_to_id[fname];
                        int block = read_int("1-屏蔽,0-取消: ");
                        friend_.set_friend_mute(fid, block == 1);
                        WaitForOperation();
                        break;
                    }
                    case 8: {
                        std::string fname = readline("好友名称: ");
                        if (!uname_to_id.count(fname)) {
                            nlohmann::json req;
                            req["msg_type"] = MSG_SEARCH_USER;
                            req["data"] = {{"keyword", fname}};
                            net_.send(req.dump());
                            WaitForOperation();
                        }
                        if (!uname_to_id.count(fname)) {
                            std::cout << "[错误] 不存在该好友" << std::endl;
                            break;
                        }
                        uint64_t fid = uname_to_id[fname];
                        std::string remark = readline("备注: ");
                        friend_.set_friend_remark(fid, remark);
                        WaitForOperation();
                        break;
                    }
                    case 0:
                        a_flag = 1;
                        break;
                }
            }
            break;
        }
        case 3: {
            int b_flag = 0;
            while (!b_flag && running_) {
                std::cout
                    << SKY_BLUE
                    << "-------------------- 群聊管理 --------------------\n"
                    << "1. 查看我的群聊\n"
                    << "2. 建立群聊\n"
                    << "3. 申请入群\n"
                    << "4. 查看入群申请\n"
                    << "5. 审批入群申请\n"
                    << "6. 查看群成员\n"
                    << "7. 设置管理员\n"
                    << "8. 踢出群成员\n"
                    << "9. 退出群聊\n"
                    << "10.解散群聊\n"
                    << "11.搜索群聊\n"
                    << "0. 取消\n";
                int gchoice = read_int("请选择: ");
                if (!running_) {
                    break;
                }
                switch (gchoice) {
                    case 1: {
                        nlohmann::json request;
                        request["msg_type"] = MSG_GET_GROUP_LIST;
                        net_.send(request.dump());
                        WaitForOperation();
                        break;
                    }
                    case 2: {
                        std::string gname = readline("群名称: ");
                        std::string desc = readline("群介绍: ");
                        group_.create_group(gname, desc);
                        WaitForOperation();
                        break;
                    }
                    case 3: {
                        std::string gname = readline("群名称: ");
                        if (!gname_to_id.count(gname)) {
                            nlohmann::json req;
                            req["msg_type"] = MSG_SEARCH_GROUP;
                            req["data"] = {{"keyword", gname}};
                            net_.send(req.dump());
                            WaitForOperation();
                        }
                        if (!gname_to_id.count(gname)) {
                            std::cout << RED << "[错误] 未找到该群聊"
                                      << std::endl;
                            break;
                        }
                        uint64_t gid = gname_to_id[gname];
                        std::string msg = readline("附言: ");
                        group_.apply_join_group(gid, msg);
                        WaitForOperation();
                        break;
                    }
                    case 4: {
                        std::string gname = readline("群名称: ");
                        if (!gname_to_id.count(gname)) {
                            nlohmann::json req;
                            req["msg_type"] = MSG_SEARCH_GROUP;
                            req["data"] = {{"keyword", gname}};
                            net_.send(req.dump());
                            WaitForOperation();
                        }
                        if (!gname_to_id.count(gname)) {
                            std::cout << RED << "[错误] 未找到该群聊"
                                      << std::endl;
                            break;
                        }
                        uint64_t gid = gname_to_id[gname];
                        group_.get_group_apply(gid);
                        WaitForOperation();
                        break;
                    }
                    case 5: {
                        std::string gname = readline("群名称: ");
                        if (!gname_to_id.count(gname)) {
                            nlohmann::json req;
                            req["msg_type"] = MSG_SEARCH_GROUP;
                            req["data"] = {{"keyword", gname}};
                            net_.send(req.dump());
                            WaitForOperation();
                        }
                        if (!gname_to_id.count(gname)) {
                            std::cout << RED << "[错误] 未找到该群聊"
                                      << std::endl;
                            break;
                        }
                        uint64_t gid = gname_to_id[gname];
                        uint64_t aid = read_int("申请ID: ");
                        int dec = read_int("1-同意,2-拒绝: ");
                        group_.apply_review(gid, aid, dec);
                        WaitForOperation();
                        break;
                    }
                    case 6: {
                        std::string gname = readline("群名称: ");
                        if (!gname_to_id.count(gname)) {
                            nlohmann::json req;
                            req["msg_type"] = MSG_SEARCH_GROUP;
                            req["data"] = {{"keyword", gname}};
                            net_.send(req.dump());
                            WaitForOperation();
                        }
                        if (!gname_to_id.count(gname)) {
                            std::cout << RED << "[错误] 未找到该群聊"
                                      << std::endl;
                            break;
                        }
                        uint64_t gid = gname_to_id[gname];
                        group_.group_member(gid);
                        WaitForOperation();
                        break;
                    }
                    case 7: {
                        std::string gname = readline("群名称: ");
                        if (!gname_to_id.count(gname)) {
                            nlohmann::json req;
                            req["msg_type"] = MSG_SEARCH_GROUP;
                            req["data"] = {{"keyword", gname}};
                            net_.send(req.dump());
                            WaitForOperation();
                        }
                        if (!gname_to_id.count(gname)) {
                            std::cout << RED << "[错误] 未找到该群聊"
                                      << std::endl;
                            break;
                        }
                        uint64_t gid = gname_to_id[gname];
                        std::string uname = readline("用户名称: ");
                        if (!uname_to_id.count(uname)) {
                            nlohmann::json req2;
                            req2["msg_type"] = MSG_SEARCH_USER;
                            req2["data"] = {{"keyword", uname}};
                            net_.send(req2.dump());
                            WaitForOperation();
                        }
                        if (!uname_to_id.count(uname)) {
                            std::cout << RED << "[错误] 未找到该用户"
                                      << std::endl;
                            break;
                        }
                        uint64_t uid = uname_to_id[uname];
                        int set = read_int("1-设为管理员,0-取消: ");
                        group_.set_admin(gid, uid, set == 1);
                        WaitForOperation();
                        break;
                    }
                    case 8: {
                        std::string gname = readline("群名称: ");
                        if (!gname_to_id.count(gname)) {
                            nlohmann::json req;
                            req["msg_type"] = MSG_SEARCH_GROUP;
                            req["data"] = {{"keyword", gname}};
                            net_.send(req.dump());
                            WaitForOperation();
                        }
                        if (!gname_to_id.count(gname)) {
                            std::cout << RED << "[错误] 未找到该群聊"
                                      << std::endl;
                            break;
                        }
                        uint64_t gid = gname_to_id[gname];
                        std::string uname = readline("用户名称: ");
                        if (!uname_to_id.count(uname)) {
                            nlohmann::json req2;
                            req2["msg_type"] = MSG_SEARCH_USER;
                            req2["data"] = {{"keyword", uname}};
                            net_.send(req2.dump());
                            WaitForOperation();
                        }
                        if (!uname_to_id.count(uname)) {
                            std::cout << RED << "[错误] 未找到该用户"
                                      << std::endl;
                            break;
                        }
                        uint64_t uid = uname_to_id[uname];
                        group_.kick_member(gid, uid);
                        WaitForOperation();
                        break;
                    }
                    case 9: {
                        std::string gname = readline("群名称: ");
                        if (!gname_to_id.count(gname)) {
                            nlohmann::json req;
                            req["msg_type"] = MSG_SEARCH_GROUP;
                            req["data"] = {{"keyword", gname}};
                            net_.send(req.dump());
                            WaitForOperation();
                        }
                        if (!gname_to_id.count(gname)) {
                            std::cout << RED << "[错误] 未找到该群聊"
                                      << std::endl;
                            break;
                        }
                        uint64_t gid = gname_to_id[gname];
                        group_.quit_group(gid);
                        WaitForOperation();
                        break;
                    }
                    case 10: {
                        std::string gname = readline("群名称: ");
                        if (!gname_to_id.count(gname)) {
                            nlohmann::json req;
                            req["msg_type"] = MSG_SEARCH_GROUP;
                            req["data"] = {{"keyword", gname}};
                            net_.send(req.dump());
                            WaitForOperation();
                        }
                        if (!gname_to_id.count(gname)) {
                            std::cout << RED << "[错误] 未找到该群聊" << std::endl;
                            break;
                        }
                        uint64_t gid = gname_to_id[gname];
                        group_.dismiss_group(gid);
                        WaitForOperation();
                        break;
                    }
                    case 11: {
                        std::string kw = readline("搜索群名称: ");
                        group_.search_group(kw);
                        WaitForOperation();
                        break;
                    }
                    case 0:
                        b_flag = 1;
                        break;
                }
                break;
            }
        }
        case 4: {
            chat_show = true;
            FlushPushBuffer();
            push_count_ = 0;
            showed = 0;
            std::cout << SKY_BLUE
                      << "-------------------- 聊天 --------------------\n"
                      << "1. 与好友私聊\n"
                      << "2. 群聊\n"
                      << "0. 取消\n";
            int hc = read_int("请选择: ");
            switch (hc) {
                case 1: {
                    nlohmann::json request;
                    request["msg_type"] = MSG_GET_FRIEND_LIST;
                    net_.send(request.dump());
                    WaitForOperation();

                    std::string uname = readline("好友名称: ");
                    if (!uname_to_id.count(uname)) {
                        std::cout << RED << "[错误] 不存在该用户" << std::endl;
                        break;
                    }
                    uint64_t friend_id = uname_to_id[uname];
                    if (!friend_ids_.count(friend_id)) {
                        std::cout << RED << "[错误] 对方不是好友" << std::endl;
                        break;
                    }
                    ChatMap(1, friend_id);
                    break;
                }
                case 2: {
                    nlohmann::json request;
                    request["msg_type"] = MSG_GET_GROUP_LIST;
                    net_.send(request.dump());
                    WaitForOperation();

                    std::string gname = readline("群名称: ");
                    if (!gname_to_id.count(gname)) {
                        std::cout << RED << "[错误] 不存在该群聊" << std::endl;
                        break;
                    }
                    uint64_t gid = gname_to_id[gname];
                    if (!my_group_ids_.count(gid)) {
                        std::cout << RED << "[错误] 你不在该群中" << std::endl;
                        break;
                    }
                    ChatMap(2, gid);
                    break;
                }
                case 0:
                    break;
            }
            chat_show = false;
            break;
        }
        case 5: {
            std::cout << SKY_BLUE << "1.查看我的ID\n2.登出\n3.注销账号\n";
            int sub = read_int("请选择: ");
            if (sub == 1) {
                std::cout << BLUE
                          << "您的用户ID: " << Cli_Session::instance().get_id()
                          << std::endl;
            } else if (sub == 2) {
                nlohmann::json request;
                request["msg_type"] = MSG_LOGOUT;
                net_.send(request.dump());
                WaitForOperation();
            } else if (sub == 3) {
                nlohmann::json request;
                request["msg_type"] = MSG_DELETE_ACCOUNT;
                net_.send(request.dump());
                WaitForOperation();
            }
            break;
        }
        case 0:
            nlohmann::json request;
            request["msg_type"] = MSG_LOGOUT;
            net_.send(request.dump());
            WaitForOperation();
            running_ = false;
            exit(0);
    }
}

// 工具函数
void Cli_Menu::InputThreadFunc() {
    while (running_) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 100000;
        int ret = select(STDIN_FILENO + 1, &readfds, nullptr, nullptr, &tv);
        if (ret < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (ret == 0)
            continue;
        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            std::string line;
            if (!std::getline(std::cin, line))
                break;
            while (!line.empty() &&
                   (line.back() == '\r' || line.back() == '\n')) {
                line.pop_back();
            }
            {
                std::lock_guard<std::mutex> lock(input_mutex_);
                input_queue_.push(line);
            }
            input_cv_.notify_one();
        }
    }
}

std::string Cli_Menu::readline(const std::string& prompt) {
    current_prompt_ = prompt;
    std::cout << SKY_BLUE << prompt << std::flush;

    while (running_) {
        if (!net_.isConnected()) {
            running_ = false;
            current_prompt_.clear();
            return "";
        }
        std::unique_lock<std::mutex> lock(input_mutex_);
        if (!input_queue_.empty()) {
            std::string line = input_queue_.front();
            input_queue_.pop();
            while (!line.empty() &&
                   (line.back() == '\r' || line.back() == '\n'))
                line.pop_back();

            current_prompt_.clear();
            return line;
        }
        lock.unlock();
        ProcessMessages();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    current_prompt_.clear();
    return "";
}

int Cli_Menu::read_int(const std::string& prompt) {
    while (running_) {
        std::string line = readline(prompt);
        if (!running_)
            return -1;
        if (line.empty()) {
            std::cout << RED << "请输入有效的整数。" << std::endl;
            continue;
        }
        try {
            return std::stoi(line);
        } catch (...) {
            std::cout << RED << "请输入有效的整数。" << std::endl;
        }
    }
    return -1;
}

void Cli_Menu::ChatMap(int chat_type, uint64_t target_id) {
    std::string target_name;
    if (chat_type == 1) {
        target_name = known_users_.count(target_id) ? known_users_[target_id]
                                                    : std::to_string(target_id);
    } else {
        target_name = known_groups_.count(target_id)
                          ? known_groups_[target_id]
                          : ("[群]" + std::to_string(target_id));
    }

    chat_context_type_ = chat_type;
    chat_context_id_ = target_id;
    other_msg_count_ = 0;
    if (last_read_chat_id_ != target_id) {
        last_read_msg_id_ = 0;
        last_read_chat_id_ = target_id;
        oldest_msg_id_ = 0;
    }
    push_buffered_ = true;

    bool exit_chat = false;
    std::cout << SKY_BLUE << "History:" << std::endl;

    chat_.get_history(chat_type, target_id, 20);
    WaitForOperation();

    while (!exit_chat && running_ && net_.isConnected()) {
        std::cout << SKY_BLUE << "\n-------------------- 聊天中 ["
                  << target_name << "] -------------------\n";
        if (other_msg_count_ > 0) {
            std::cout << BLUE << "  [您有 " << other_msg_count_
                      << " 条其他会话的新消息]\n";
        }
        std::cout << SKY_BLUE << "1. 发送消息\n"
                  << "2. 刷新聊天记录\n"
                  << "0. 退出聊天\n";
        int choice = read_int("请选择: ");
        if (!running_ || !net_.isConnected())
            break;
        switch (choice) {
            case 1: {
                while (true) {
                    if (!running_ || !net_.isConnected())
                        break;
                    std::string msg = readline("");
                    if (msg == "exit") {
                        break;
                    } else if (msg == "./file") {
                        int c_flag = 1;
                        while (c_flag) {
                            if (!running_ || !net_.isConnected()) {
                                c_flag = 0;
                                break;
                            }
                            if (!big_file_.IsConnected()) {
                                big_file_.Connect(server_ip_, 2100);
                            }
                            std::cout << SKY_BLUE
                                      << "----------文件操作----------\n";
                            std::cout << SKY_BLUE << "1. 上传文件\n"
                                      << "2. 下载文件\n"
                                      << "0. 返回\n";
                            int sub = read_int("请选择: ");
                            if (!running_ || !net_.isConnected()) {
                                c_flag = 0;
                                break;
                            }
                            switch (sub) {
                                case 1: {
                                    std::string local = readline("本地文件路径: ");
                                    std::string remote = readline("远程保存名称: ");
                                    // 检查本地的文件是否存在
                                    if (!std::filesystem::exists(local)) {
                                        std::cout << RED << "[错误] 本地文件不存在\n";
                                        break;
                                    }
                                    if (!std::filesystem::is_regular_file( local)) {
                                        std::cout << RED << "[错误] 路径不是普通文件（可能是目录）\n";
                                        break;
                                    }
                                    // 先发文件消息通知，确认未被屏蔽后再上传
                                    chat_.send_file(chat_type == 2, target_id, remote);
                                    WaitForOperation();
                                    if (op_success_) {
                                        if (big_file_.Upload(local, remote))
                                            std::cout << GREEN << "文件上传成功!\n";
                                        else
                                            std::cout << RED << "FTP上传失败!\n";
                                    }
                                    break;
                                }
                                case 2: {
                                    uint64_t msgid = read_int("文件消息ID: ");
                                    std::string local = readline("本地保存文件名: ");

                                    chat_.on_download = [this, local](const std::string& fname, uint64_t fsize) {
                                            if (!big_file_.IsConnected()) {
                                                big_file_.Connect(server_ip_, 2100);
                                            }
                                            if (big_file_.Download(fname, local)) {
                                                std::cout << GREEN << "\n下载成功!" << std::endl;
                                            } else {
                                                std::cout << RED << "\n下载失败!" << std::endl;
                                            }
                                            op_done_ = true;
                                        };

                                    chat_.download_file(msgid);
                                    WaitForOperation(120);
                                    break;
                                }
                                case 0:
                                    c_flag = 0;
                                    break;
                            }
                        }
                    } else {
                        // 清除刚刚输入的整行
                        // \r       光标回到当前行开头
                        // \033[K   从光标位置清除到行尾
                        std::cout << "\033[1A\r\033[K" << std::flush;
                        if (chat_type == 1)
                            chat_.private_chat(target_id, msg, 0);
                        else
                            chat_.group_chat(target_id, msg, 0);
                    }
                }
                break;
            }
            case 2: {
                std::cout << SKY_BLUE << "1. 查看最近消息(最多200条)\n"
                          << "2. 查看近25条\n"
                          << "3. 加载更早消息(翻页)\n"
                          << "0. 退出聊天\n";
                int ochoice = read_int("请选择: ");
                if (ochoice == 1) {
                    chat_.get_history(chat_type, target_id, 200);
                } else if (ochoice == 2) {
                    chat_.get_history(chat_type, target_id, 25);
                } else if (ochoice == 3) {
                    if (oldest_msg_id_ <= 1) {
                        std::cout << RED << "已经是最早的消息了，没有更早的记录"
                                  << std::endl;
                    } else {
                        std::cout << SKY_BLUE << "正在加载更早的消息..."
                                  << std::endl;
                        chat_.get_history(chat_type, target_id, 50,
                                          oldest_msg_id_, 0);
                    }
                } else {
                    break;
                }
                WaitForOperation();
                break;
            }
            case 0:
                exit_chat = true;
                break;
        }
    }

    push_buffered_ = false;
    chat_context_type_ = 0;
    chat_context_id_ = 0;

    FlushPushBuffer();
    other_msg_count_ = 0;
}

std::string Cli_Menu::read_pwd(const std::string& prompt) {
    std::cout << prompt;
    std::cout.flush();
    struct termios old_tty, new_tty;
    tcgetattr(STDIN_FILENO, &old_tty);
    new_tty = old_tty;
    new_tty.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tty);
    std::string pwd;
    std::getline(std::cin, pwd);
    tcsetattr(STDIN_FILENO, TCSANOW, &old_tty);
    std::cout << "\n";
    return pwd;
}