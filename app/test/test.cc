// #include <iostream>
// #include "global.h"
// #include "logger.h"
// #include "utils.h"

// int main() {
//     // ========== 1. 测试全局常量 ==========
//     std::cout << "=== 1. 全局常量测试 ===" << std::endl;
//     std::cout << "协议头长度: " << PACK_HEAD_LEN << " (预期: 4)" <<
//     std::endl; std::cout << "注册请求消息号: " << MSG_REGISTER << " (预期:
//     1001)"
//               << std::endl;
//     std::cout << "登录请求消息号: " << MSG_LOGIN << " (预期: 1002)"
//               << std::endl;
//     std::cout << "成功错误码: " << SUCCESS << " (预期: 0)" << std::endl;
//     std::cout << "全局常量测试通过" << std::endl << std::endl;

//     // ========== 2. 测试日志 ==========
//     std::cout << "=== 2. 日志测试 ===" << std::endl;
//     InitLogger("test_base");
//     LOG_INFO << "这是一条 INFO 日志";
//     LOG_WARN << "这是一条 WARN 日志";
//     LOG_ERROR << "这是一条 ERROR 日志";
//     std::cout << "日志测试完成，请查看终端输出是否有带时间戳的日志内容"
//               << std::endl
//               << std::endl;

//     // ========== 3. 测试密码加密 ==========
//     std::cout << "=== 3. 密码加密测试 ===" << std::endl;

//     // 3.1 生成两次盐值，验证不同
//     std::string salt1 = generate_salt(16);
//     std::string salt2 = generate_salt(16);
//     std::cout << "盐值1长度: " << salt1.size() << " (预期: 16)" << std::endl;
//     std::cout << "两次盐值是否不同: " << (salt1 != salt2 ? "是 " : "否 ")
//               << std::endl;

//     // 3.2 相同密码 + 不同盐，哈希不同
//     std::string pwd = "123456";
//     std::string hash1 = sha256_encrypt(pwd, salt1);
//     std::string hash2 = sha256_encrypt(pwd, salt2);
//     std::cout << "哈希1长度: " << hash1.size() << " (预期: 64位十六进制)"
//               << std::endl;
//     std::cout << "不同盐的哈希是否不同: "
//               << (hash1 != hash2 ? "是 " : "否 ") << std::endl;

//     // 3.3 相同密码 + 相同盐，哈希一致（验证登录比对逻辑）
//     std::string hash1_repeat = sha256_encrypt(pwd, salt1);
//     std::cout << "相同盐相同密码哈希是否一致: "
//               << (hash1 == hash1_repeat ? "是" : "否") << std::endl;

//     // 3.4 错误密码比对不通过
//     std::string wrong_hash = sha256_encrypt("654321", salt1);
//     std::cout << "错误密码哈希是否不同: "
//               << (hash1 != wrong_hash ? "是" : "否") << std::endl;
//     std::cout << "密码加密测试完成" << std::endl << std::endl;

//     // ========== 4. 测试字符串合法性校验 ==========
//     std::cout << "=== 4. 字符串校验测试 ===" << std::endl;

//     std::string valid_str = "test_user123567";
//     std::string empty_str = "";
//     std::string long_str(1024, 'a');
//     std::string control_str = "hello\x01world";  // 含控制字符

//     std::cout << "正常字符串校验: "
//               << (check_str_valid(valid_str, 15, 20) ? "通过" : "不通过")
//               << std::endl;
//     std::cout << "空字符串校验: "
//               << (!check_str_valid(empty_str, 15, 20) ? "拦截" : "未拦截")
//               << std::endl;
//     std::cout << "超长字符串校验: "
//               << (!check_str_valid(long_str, 15, 20) ? "拦截" : "未拦截")
//               << std::endl;
//     std::cout << "含控制字符校验: "
//               << (!check_str_valid(control_str, 15, 20) ? "拦截" : "未拦截")
//               << std::endl;
//     std::cout << "字符串校验测试完成" << std::endl << std::endl;

//     std::cout << "========== 所有基础测试结束 ==========" << std::endl;
//     return 0;
// }

// #include <chrono>
// #include <iostream>
// #include <thread>
// #include <vector>
// #include "logger.h"
// #include "mysql_pool.h"
// #include "redis_pool.h"

// // MySQL 测试线程
// void MysqlTestThread(int thread_id) {
//     for (int i = 0; i < 20; ++i) {
//         MysqlConnGuard guard;
//         if (!guard.IsValid()) {
//             LOG(WARNING) << "[" << thread_id << "] 获取MySQL连接失败";
//             std::this_thread::sleep_for(std::chrono::milliseconds(100));
//             continue;
//         }

//         // 使用 MysqlResultGuard 自动管理结果集，避免手动释放
//         MysqlResultGuard res(
//             MysqlPool::GetInstance().Query("SELECT 1+1 AS res;"));
//         if (res.IsValid()) {
//             MYSQL_ROW row = mysql_fetch_row(res.Get());
//             if (row) {
//                 LOG(INFO) << "Mysql Thread[" << thread_id
//                           << "] query result:" << row[0];
//             }
//         }
//         std::this_thread::sleep_for(std::chrono::milliseconds(50));
//     }
//     LOG(INFO) << "Mysql Thread[" << thread_id << "] 测试完成";
// }

// // Redis 测试线程
// void RedisTestThread(int thread_id) {
//     std::string key = "test_key_" + std::to_string(thread_id);
//     std::string list_key = "test_list_" + std::to_string(thread_id);

//     for (int i = 0; i < 20; ++i) {
//         RedisConnGuard guard;
//         if (!guard.IsValid()) {
//             LOG(WARNING) << "[" << thread_id << "] 获取Redis连接失败";
//             std::this_thread::sleep_for(std::chrono::milliseconds(100));
//             continue;
//         }
//         auto& pool = RedisPool::GetInstance();
//         std::string val = "data_" + std::to_string(i);

//         // string
//         pool.string_set(key, val);
//         std::string get_val = pool.string_get(key);
//         LOG(INFO) << "Redis Thread[" << thread_id << "] Get:" << get_val;

//         // list
//         pool.tail_push(list_key, val);
//         int len = pool.list_len(list_key);
//         LOG(INFO) << "Redis Thread[" << thread_id << "] list_len=" << len;

//         // hash 和 set 也可以加一点测试
//         pool.hash_set("htest", "field1", "hello");
//         pool.set_add("stest", "member1");

//         std::this_thread::sleep_for(std::chrono::milliseconds(50));
//     }
//     LOG(INFO) << "Redis Thread[" << thread_id << "] 测试完成";
// }

// int main() {
//     // 初始化（新接口）
//     // MySQL: host, port, user, pwd, db, min_size, max_size
//     bool mysql_init = MysqlPool::GetInstance().Init(
//         "127.0.0.1", 3306, "root", "123456", "chat_room", 8, 32);
//     if (!mysql_init) {
//         LOG(ERROR) << "MySQL连接池初始化失败！";
//         return -1;
//     }

//     // Redis: host, port, pwd, db, min_size, max_size
//     bool redis_init =
//         RedisPool::GetInstance().Init("127.0.0.1", 6379, "1204", 0, 8, 32);
//     if (!redis_init) {
//         LOG(ERROR) << "Redis连接池初始化失败！";
//         MysqlPool::GetInstance().Destroy();
//         return -1;
//     }

//     LOG(INFO) << "连接池初始化成功，开始并发测试";

//     // 输出初始状态
//     LOG(INFO) << "MySQL 连接数: " << MysqlPool::GetInstance().Size()
//               << ", 空闲: " << MysqlPool::GetInstance().Idle();
//     LOG(INFO) << "Redis 连接数: " << RedisPool::GetInstance().Size()
//               << ", 空闲: " << RedisPool::GetInstance().Idle();

//     std::vector<std::thread> threads;
//     int thread_num = 8;

//     // MySQL 线程
//     for (int i = 0; i < thread_num; ++i) {
//         threads.emplace_back(MysqlTestThread, i);
//     }
//     // Redis 线程
//     for (int i = 0; i < thread_num; ++i) {
//         threads.emplace_back(RedisTestThread, i + 100);
//     }

//     for (auto& t : threads) {
//         if (t.joinable())
//             t.join();
//     }

//     LOG(INFO) << "所有并发测试执行完毕";
//     LOG(INFO) << "MySQL 最终连接数: " << MysqlPool::GetInstance().Size()
//               << ", 空闲: " << MysqlPool::GetInstance().Idle();
//     LOG(INFO) << "Redis 最终连接数: " << RedisPool::GetInstance().Size()
//               << ", 空闲: " << RedisPool::GetInstance().Idle();

//     RedisPool::GetInstance().Destroy();
//     MysqlPool::GetInstance().Destroy();
//     LOG(INFO) << "销毁完成，程序正常退出";
//     return 0;
// }

// #include <chrono>
// #include <iostream>
// #include <thread>
// #include "client.h"
// #include "global.h"

// // 全局接收回调，打印所有服务端发来的消息
// void onMessage(const std::string& json_str) {
//     std::cout << "[收到] " << json_str << std::endl;
// }

// int main() {
//     // 测试用账号（为了避免与历史数据冲突，建议每次改一下用户名）
//     std::string test_user = "cppclient";
//     std::string test_email = "2650598968@qq.com";  
//     std::string test_phone = "13800001111";

//     // 1. 连接服务器
//     Client client("127.0.0.1", 8888);
//     client.set_MassageHandle(onMessage);
//     std::cout << "正在连接服务器..." << std::endl;
//     if (!client.connectServer()) {
//         std::cerr << "连接失败" << std::endl;
//         return 1;
//     }
//     std::cout << "连接成功！\n" << std::endl;

//     // 辅助宏：发送 JSON（自动添加 msg_type）
//     auto send = [&](uint32_t type, const nlohmann::json& data) {
//         nlohmann::json j;
//         j["msg_type"] = type;
//         j["data"] = data;
//         client.send_json(j.dump());
//     };

//     // ========== 测试 1：注册 ==========
//     std::cout << "[1] 注册..." << std::endl;
//     send(1001, {{"username", test_user},
//                 {"password", "Test@123"},
//                 {"email", test_email},
//                 {"phone", test_phone}});
//     std::this_thread::sleep_for(std::chrono::seconds(1));

//     // ========== 测试 2：重复注册（应拒绝）==========
//     std::cout << "\n[2] 重复注册..." << std::endl;
//     send(1001, {{"username", test_user},
//                 {"password", "Test@123"},
//                 {"email", test_email},
//                 {"phone", test_phone}});
//     std::this_thread::sleep_for(std::chrono::seconds(1));

//     // ========== 测试 3：正确登录 ==========
//     std::cout << "\n[3] 登录（正确密码）..." << std::endl;
//     send(1002, {{"username", test_user}, {"password", "Test@123"}});
//     std::this_thread::sleep_for(std::chrono::seconds(1));

//     // ========== 测试 4：错误密码登录 ==========
//     std::cout << "\n[4] 登录（错误密码）..." << std::endl;
//     send(1002, {{"username", test_user}, {"password", "wrong"}});
//     std::this_thread::sleep_for(std::chrono::seconds(1));

//     // ========== 测试 5：登出（需知道 user_id，可从登录响应拿到）==========
//     std::cout << "\n[5] 登出...（请查看之前登录成功消息中的 user_id，手动填入）"
//               << std::endl;
//     int uid;
//     std::cout << "请输入 user_id: ";
//     std::cin >> uid;
//     send(1003, {{"user_id", uid}, {"username", test_user}});
//     std::this_thread::sleep_for(std::chrono::seconds(1));

//     // ========== 测试 6：获取验证码（找回密码）==========
//     std::cout << "\n[6] 获取验证码（reset）..." << std::endl;
//     send(1006, {{"email", test_email}, {"type", "reset"}});
//     std::this_thread::sleep_for(std::chrono::seconds(3));  // 等 SMTP 发送

//     // ========== 测试 7：重置密码 ==========
//     std::cout << "\n[7] 重置密码..." << std::endl;
//     std::string captcha;
//     std::cout << "请输入邮箱收到的验证码: ";
//     std::cin >> captcha;
//     send(1007, {{"email", test_email},
//                 {"captcha", captcha},
//                 {"new_password", "NewPass@456"}});
//     std::this_thread::sleep_for(std::chrono::seconds(1));

//     // ========== 测试 8：用新密码登录 ==========
//     std::cout << "\n[8] 新密码登录..." << std::endl;
//     send(1002, {{"username", test_user}, {"password", "NewPass@456"}});
//     std::this_thread::sleep_for(std::chrono::seconds(1));

//     std::cout << "\n===== 所有测试完成 =====" << std::endl;
//     std::this_thread::sleep_for(std::chrono::seconds(2));
//     client.close_conn();
//     return 0;
// }

// #include <chrono>
// #include <iostream>
// #include <thread>
// #include "client.h"
// #include "global.h"

// // 全局回调：打印收到的所有消息
// void onMessage(const std::string& json_str) {
//     std::cout << "[收到] " << json_str << std::endl;
// }

// int main() {
//     // 测试账号信息（确保与之前不冲突）
//     std::string userA = "friendA01";
//     std::string userB = "friendB01";
//     std::string emailA =
//         "3046921379@qq.com";  // 改成真实邮箱或直接写死（不影响好友功能测试）
//     std::string emailB = "3794475902@qq.com";
//     std::string phoneA = "13800001112";
//     std::string phoneB = "13800002222";
//     std::string passwd = "Test@123";

//     // 连接服务器
//     Client client("127.0.0.1", 8888);
//     client.set_MassageHandle(onMessage);
//     if (!client.connectServer()) {
//         std::cerr << "连接服务器失败" << std::endl;
//         return 1;
//     }
//     std::cout << "===== 连接成功 =====" << std::endl;

//     // 辅助 lambda：发送 JSON 消息
//     auto send = [&](uint32_t type, const nlohmann::json& data) {
//         nlohmann::json j;
//         j["msg_type"] = type;
//         j["data"] = data;
//         client.send_json(j.dump());
//     };

//     std::this_thread::sleep_for(std::chrono::seconds(1));

//     // ========== 1. 注册两个账号 ==========
//     std::cout << "\n[1] 注册账号 " << userA << " 和 " << userB << std::endl;
//     send(1001, {
//         {"username", userA},
//         {"password", "Test@123"},
//         {"email", emailA},
//         {"phone", phoneA}});
//     std::this_thread::sleep_for(std::chrono::seconds(1));
//     send(1001, {
//         {"username", userB},
//         {"password", passwd},
//         {"email", emailB},
//         {"phone", phoneB}});
//         std::this_thread::sleep_for(std::chrono::seconds(1));

//         // 如果已注册会返回重复错误，可忽略，不影响后续

//         // ========== 2. 分别登录 ==========
//         std::cout << "\n[2] 登录 " << userA << std::endl;
//         send(MSG_LOGIN, {{"username", userA}, {"password", passwd}});
//         std::this_thread::sleep_for(std::chrono::seconds(1));
//         // 记录 userA 的 ID（从收到的 JSON
//         // 里手动查看，或后续解析，这里为了简单不自动提取）
//         std::cout << "请从上面收到的 LOGIN 响应中记录 " << userA
//                   << " 的 user_id，然后按回车继续...";
//         std::cin.ignore();  // 等待回车

//         std::cout << "登录 " << userB << std::endl;
//         // 注意：一个客户端只能维护一个连接，无法同时模拟两个用户在线。
//         // 测试中我们用 A 登录后操作，然后登出 A，再登录 B 操作。
//         // 你也可以开两个客户端实例，这里为了简单采用顺序登录方式。

//         // 登出 A（因为我们要登录 B 来操作）
//         std::cout << "登出 A...（B 登录后才能处理申请）" << std::endl;
//         std::cout << "请输入 A 的 user_id: ";
//         uint64_t uidA;
//         std::cin >> uidA;
//         send(MSG_LOGOUT, {{"user_id", uidA}, {"username", userA}});
//         std::this_thread::sleep_for(std::chrono::seconds(1));

//         // 登录 B
//         send(MSG_LOGIN, {{"username", userB}, {"password", passwd}});
//         std::this_thread::sleep_for(std::chrono::seconds(1));
//         std::cout << "请记录 B 的 user_id，然后按回车...";
//         std::cin.ignore();
//         std::cin.get();

//         uint64_t uidB;
//         std::cout << "请输入 B 的 user_id: ";
//         std::cin >> uidB;

//         // ========== 3. A 搜索 B（需要 A 登录） ==========
//         std::cout << "\n[3] 登出 B，重新登录 A..." << std::endl;
//         send(MSG_LOGOUT, {{"user_id", uidB}, {"username", userB}});
//         std::this_thread::sleep_for(std::chrono::seconds(1));
//         send(MSG_LOGIN, {{"username", userA}, {"password", passwd}});
//         std::this_thread::sleep_for(std::chrono::seconds(1));

//         std::cout << "搜索用户 " << userB << std::endl;
//         send(MSG_SEARCH_USER, {{"keyword", userB}});
//         std::this_thread::sleep_for(std::chrono::seconds(1));

//         // ========== 4. A 向 B 发送好友申请 ==========
//         std::cout << "\n[4] A 向 B 发送好友申请..." << std::endl;
//         send(MSG_ADD_FRIEND, {{"user_id", uidB}, {"message", "hello"}});
//         std::this_thread::sleep_for(std::chrono::seconds(1));

//         // ========== 5. 登出 A，登录 B 查看申请列表并同意 ==========
//         std::cout << "\n[5] 登出 A，登录 B 处理申请..." << std::endl;
//         send(MSG_LOGOUT, {{"user_id", uidA}, {"username", userA}});
//         std::this_thread::sleep_for(std::chrono::seconds(1));
//         send(MSG_LOGIN, {{"username", userB}, {"password", passwd}});
//         std::this_thread::sleep_for(std::chrono::seconds(1));

//         // 查看申请列表
//         std::cout << "B 获取好友申请列表" << std::endl;
//         send(MSG_GET_APPLY_LIST, {});
//         std::this_thread::sleep_for(std::chrono::seconds(1));

//         // 同意申请（需要 request_id，从上一条响应中查看）
//         uint64_t request_id;
//         std::cout << "请输入 B 收到的申请 request_id: ";
//         std::cin >> request_id;
//         send(MSG_AGREE_FRIEND,
//              {{"user_id",
//                request_id}});  // 注意：你的 friend.cc 中用的字段是
//                                // "user_id" 不是 "request_id"，保持一致
//         std::this_thread::sleep_for(std::chrono::seconds(1));

//         // ========== 6. 查看双方好友列表 ==========
//         std::cout << "\n[6] 查看 B 的好友列表" << std::endl;
//         send(MSG_GET_FRIEND_LIST, {});
//         std::this_thread::sleep_for(std::chrono::seconds(1));

//         // 切回 A 查看列表
//         std::cout << "登出 B，登录 A 查看好友列表" << std::endl;
//         send(MSG_LOGOUT, {{"user_id", uidB}, {"username", userB}});
//         std::this_thread::sleep_for(std::chrono::seconds(1));
//         send(MSG_LOGIN, {{"username", userA}, {"password", passwd}});
//         std::this_thread::sleep_for(std::chrono::seconds(1));
//         send(MSG_GET_FRIEND_LIST, {});
//         std::this_thread::sleep_for(std::chrono::seconds(1));

//         // ========== 7. 设置备注和屏蔽 ==========
//         std::cout << "\n[7] A 设置 B 的备注为 'best friend'" << std::endl;
//         send(MSG_SET_FRIEND_REMARK,
//              {{"friend_id", uidB}, {"remark", "best friend"}});
//         std::this_thread::sleep_for(std::chrono::seconds(1));

//         std::cout << "A 屏蔽 B 的消息" << std::endl;
//         send(MSG_SET_FRIEND_MUTE, {{"friend_id", uidB}, {"blocked", true}});
//         std::this_thread::sleep_for(std::chrono::seconds(1));

//         // 查看好友列表确认备注和屏蔽状态
//         send(MSG_GET_FRIEND_LIST, {});
//         std::this_thread::sleep_for(std::chrono::seconds(1));

//         // ========== 8. 取消屏蔽并删除好友 ==========
//         std::cout << "\n[8] 取消屏蔽" << std::endl;
//         send(MSG_SET_FRIEND_MUTE, {{"friend_id", uidB}, {"blocked", false}});
//         std::this_thread::sleep_for(std::chrono::seconds(1));

//         std::cout << "A 删除好友 B" << std::endl;
//         send(MSG_DEL_FRIEND, {{"user_id", uidB}});
//         std::this_thread::sleep_for(std::chrono::seconds(1));

//         // 再次查看好友列表，应为空
//         send(MSG_GET_FRIEND_LIST, {});
//         std::this_thread::sleep_for(std::chrono::seconds(1));

//         std::cout << "\n===== 测试结束 =====" << std::endl;
//         client.close_conn();
//         return 0;
// }

#include <chrono>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>

#include "client.h"
#include "global.h"

using json = nlohmann::json;

// 全局回调：打印收到的所有消息
void onMessage(const std::string& json_str) {
    std::cout << "[收到] " << json_str << std::endl;
}

int main(int argc, char* argv[]) {
    // 解析命令行参数：IP 和端口
    std::string server_ip = "127.0.0.1";
    int server_port = 8888;
    if (argc >= 2)
        server_ip = argv[1];
    if (argc >= 3)
        server_port = std::stoi(argv[2]);

    // 测试账号信息（确保不与之前数据冲突）
    std::string userA = "testuserA";
    std::string userB = "testuserB";
    std::string emailA = "3046921379@qq.com";
    std::string emailB = "3330573423@qq.com";
    std::string phoneA = "13800001111";
    std::string phoneB = "13800002222";
    std::string passwd = "Test@123";

    // 连接服务器
    Client client(server_ip, server_port);
    client.set_MassageHandle(onMessage);
    std::cout << "正在连接服务器 " << server_ip << ":" << server_port << " ..."
              << std::endl;
    if (!client.connectServer()) {
        std::cerr << "连接失败，请检查服务端是否启动" << std::endl;
        return 1;
    }
    std::cout << "===== 连接成功 =====" << std::endl;

    // 辅助 lambda：发送 JSON 消息
    auto send = [&](uint32_t type, const json& data) {
        json j;
        j["msg_type"] = type;
        j["data"] = data;
        client.send_json(j.dump());
    };

    // 等待响应的延迟（简单起见，实际可根据需要调整）
    auto wait = []() { std::this_thread::sleep_for(std::chrono::seconds(1)); };

    // ========== 第一部分：账号模块测试 ==========
    std::cout << "\n========== 账号模块测试 ==========\n";

    // 1. 注册用户 A 和 B（如果已存在会返回错误，不影响后续）
    std::cout << "[1] 注册用户 A..." << std::endl;
    send(MSG_REGISTER, {{"username", userA},
                        {"password", passwd},
                        {"email", emailA},
                        {"phone", phoneA}});
    wait();
    std::cout << "[2] 注册用户 B..." << std::endl;
    send(MSG_REGISTER, {{"username", userB},
                        {"password", passwd},
                        {"email", emailB},
                        {"phone", phoneB}});
    wait();

    // 记录用户ID（从登录响应中手动获取，这里我们通过登录获得）
    uint64_t uidA = 0, uidB = 0;

    // 登录 A 获取 ID
    std::cout << "[3] 登录用户 A..." << std::endl;
    send(MSG_LOGIN, {{"username", userA}, {"password", passwd}});
    wait();
    // 无法自动提取 ID，提示用户输入（从上面收到的响应中查看）
    std::cout << "请从上一条收到的 LOGIN 响应中查看 A 的 user_id，输入后回车：";
    std::cin >> uidA;

    // 登出 A
    std::cout << "[4] 登出 A..." << std::endl;
    send(MSG_LOGOUT, {{"user_id", uidA}, {"username", userA}});
    wait();

    // 登录 B 获取 ID
    std::cout << "[5] 登录用户 B..." << std::endl;
    send(MSG_LOGIN, {{"username", userB}, {"password", passwd}});
    wait();
    std::cout << "请从上一条收到的 LOGIN 响应中查看 B 的 user_id，输入后回车：";
    std::cin >> uidB;

    // 登出 B
    std::cout << "[6] 登出 B..." << std::endl;
    send(MSG_LOGOUT, {{"user_id", uidB}, {"username", userB}});
    wait();

    // 测试验证码发送（需要真实邮箱）
    std::cout << "[7] 获取验证码（测试邮箱：" << emailB << ")..." << std::endl;
    send(MSG_GET_CAPTCHA, {{"email", emailB}, {"type", "register"}});
    wait();
    // 实际验证码会发送到邮箱，此处仅测试流程，不测重置密码（可手动完成）

    // ========== 第二部分：好友模块测试 ==========
    std::cout << "\n========== 好友模块测试 ==========\n";

    // A 登录并搜索 B
    std::cout << "[8] A 登录..." << std::endl;
    send(MSG_LOGIN, {{"username", userA}, {"password", passwd}});
    wait();

    std::cout << "[9] A 搜索用户 " << userB << std::endl;
    send(MSG_SEARCH_USER, {{"keyword", userB}});
    wait();

    // A 向 B 发送好友申请
    std::cout << "[10] A 向 B 发送好友申请..." << std::endl;
    send(MSG_ADD_FRIEND, {{"user_id", uidB}, {"message", "hello"}});
    wait();

    // 登出 A
    std::cout << "[11] A 登出..." << std::endl;
    send(MSG_LOGOUT, {{"user_id", uidA}, {"username", userA}});
    wait();

    // B 登录并查看申请列表
    std::cout << "[12] B 登录..." << std::endl;
    send(MSG_LOGIN, {{"username", userB}, {"password", passwd}});
    wait();

    std::cout << "[13] B 查看好友申请列表..." << std::endl;
    send(MSG_GET_APPLY_LIST, {});
    wait();

    // 获取申请 ID（需要手动输入）
    uint64_t request_id = 0;
    std::cout << "请输入 B 收到的申请 request_id（从申请列表响应中查看）：";
    std::cin >> request_id;

    // B 同意申请
    std::cout << "[14] B 同意好友申请..." << std::endl;
    send(MSG_AGREE_FRIEND,
         {{"user_id", request_id}});  // 注意：你的代码中字段是 user_id
    wait();

    // 查看 B 的好友列表
    std::cout << "[15] B 查看好友列表..." << std::endl;
    send(MSG_GET_FRIEND_LIST, {});
    wait();

    // B 设置对 A 的备注和屏蔽测试
    std::cout << "[16] B 设置 A 的备注为 'BestFriend' ..." << std::endl;
    send(MSG_SET_FRIEND_REMARK,
         {{"friend_id", uidA}, {"remark", "BestFriend"}});
    wait();

    std::cout << "[17] B 屏蔽 A 的消息..." << std::endl;
    send(MSG_SET_FRIEND_MUTE, {{"friend_id", uidA}, {"blocked", true}});
    wait();

    // 解除屏蔽
    std::cout << "[18] B 取消屏蔽 A..." << std::endl;
    send(MSG_SET_FRIEND_MUTE, {{"friend_id", uidA}, {"blocked", false}});
    wait();

    // B 登出
    std::cout << "[19] B 登出..." << std::endl;
    send(MSG_LOGOUT, {{"user_id", uidB}, {"username", userB}});
    wait();

    // ========== 第三部分：群组模块测试 ==========
    std::cout << "\n========== 群组模块测试 ==========\n";

    // A 登录并创建群
    std::cout << "[20] A 登录..." << std::endl;
    send(MSG_LOGIN, {{"username", userA}, {"password", passwd}});
    wait();

    std::cout << "[21] A 创建群 'TestGroup'..." << std::endl;
    send(MSG_CREATE_GROUP,
         {{"group_name", "TestGroup"}, {"group_desc", "测试群"}});
    wait();
    uint64_t group_id = 0;
    std::cout << "请输入创建的群 group_id（从响应中查看）：";
    std::cin >> group_id;

    // B 登录并申请加入群
    std::cout << "[22] A 登出, B 登录..." << std::endl;
    send(MSG_LOGOUT, {{"user_id", uidA}, {"username", userA}});
    wait();
    send(MSG_LOGIN, {{"username", userB}, {"password", passwd}});
    wait();

    std::cout << "[23] B 申请加入群 " << group_id << std::endl;
    send(MSG_APPLY_JOIN_GROUP,
         {{"group_id", group_id}, {"message", "我想加入"}});
    wait();

    // B 登出，A 登录并审批
    std::cout << "[24] B 登出, A 登录..." << std::endl;
    send(MSG_LOGOUT, {{"user_id", uidB}, {"username", userB}});
    wait();
    send(MSG_LOGIN, {{"username", userA}, {"password", passwd}});
    wait();

    // 查看入群申请
    std::cout << "[25] A 查看入群申请..." << std::endl;
    send(MSG_GET_GROUP_APPLY, {{"group_id", group_id}});
    wait();
    uint64_t apply_id = 0;
    std::cout << "请输入入群申请的 id（从响应中查看）：";
    std::cin >> apply_id;

    // 同意申请
    std::cout << "[26] A 同意 B 的入群申请..." << std::endl;
    send(MSG_GROUP_APPLY_REVIEW,
         {{"group_id", group_id}, {"apply_id", apply_id}, {"decision", 1}});
    wait();

    // A 查看群成员
    std::cout << "[27] A 查看群成员列表..." << std::endl;
    send(MSG_GET_GROUP_MEMBER, {{"group_id", group_id}});
    wait();

    // A 设置 B 为管理员
    std::cout << "[28] A 设置 B 为管理员..." << std::endl;
    send(MSG_GROUP_SET_ADMIN,
         {{"group_id", group_id}, {"user_id", uidB}, {"set_admin", true}});
    wait();

    // A 踢出 B（先取消管理员才能踢？不，管理员也可以被群主踢，代码已支持）
    std::cout << "[29] A 将 B 踢出群..." << std::endl;
    send(MSG_KICK_MEMBER, {{"group_id", group_id}, {"user_id", uidB}});
    wait();

    // A 解散群
    std::cout << "[30] A 解散群..." << std::endl;
    send(MSG_DISMISS_GROUP, {{"group_id", group_id}});
    wait();

    // ========== 第四部分：聊天功能测试 ==========
    std::cout << "\n========== 聊天功能测试 ==========\n";

    // 重新建立好友关系和群组（因为之前解散了群）
    // 快速重建：A 和 B 已成为好友（之前同意了），只需重新创建群并加入
    // B 加入群需要再次申请和同意，省略繁琐，直接使用已有好友关系测试私聊
    std::cout << "[31] A 和 B 重新登录准备私聊..." << std::endl;
    send(MSG_LOGOUT, {{"user_id", uidA}, {"username", userA}});
    wait();
    // B 登录
    send(MSG_LOGIN, {{"username", userB}, {"password", passwd}});
    wait();
    // B 向 A 发送私聊（A 此时离线，消息将离线存储）
    std::cout << "[32] B 向 A 发送私聊消息（A 离线）..." << std::endl;
    send(MSG_PRIVATE_CHAT,
         {{"receiver_id", uidA}, {"message", "Hello A!"}, {"message_type", 0}});
    wait();

    // B 登出，A 登录
    std::cout << "[33] B 登出, A 登录..." << std::endl;
    send(MSG_LOGOUT, {{"user_id", uidB}, {"username", userB}});
    wait();
    send(MSG_LOGIN, {{"username", userA}, {"password", passwd}});
    wait();
    // A 会收到离线消息（pushOfflinePrivateMsgs 在登录时被调用），可观察输出

    // A 回复 B（B 离线）
    std::cout << "[34] A 向 B 发送私聊消息（B 离线）..." << std::endl;
    send(MSG_PRIVATE_CHAT,
         {{"receiver_id", uidB}, {"message", "Hi B!"}, {"message_type", 0}});
    wait();

    // 查询历史记录
    std::cout << "[35] A 查询与 B 的私聊历史..." << std::endl;
    send(MSG_GET_HISTORY_MSG,
         {{"chat_type", 1}, {"target_id", uidB}, {"limit", 20}});
    wait();

    // ========== 第五部分：文件传输测试 ==========
    std::cout << "\n========== 文件传输测试 ==========\n";
    // A 向 B 发送文件（base64 编码的小文件内容）
    std::string file_content =
        "VGhpcyBpcyBhIHRlc3QgZmlsZSBjb250ZW50Lg==";  // "This is a test file
                                                     // content." 的 base64
    std::cout << "[36] A 向 B 发送文件..." << std::endl;
    send(MSG_FILE_MSG, {{"receiver_id", uidB},
                        {"file_name", "test.txt"},
                        {"file_size", file_content.size()},
                        {"file_content", file_content}});
    wait();

    // B
    // 登录接收离线文件（登录时会推送离线消息，但文件消息和普通消息一样推送，这里我们主动下载）
    std::cout << "[37] A 登出, B 登录..." << std::endl;
    send(MSG_LOGOUT, {{"user_id", uidA}, {"username", userA}});
    wait();
    send(MSG_LOGIN, {{"username", userB}, {"password", passwd}});
    wait();
    // 获取最近的消息 ID（假设文件消息 ID 从响应中获取，这里简单起见，手动输入）
    uint64_t file_msg_id = 0;
    std::cout
        << "请输入文件消息的 message_id（从离线消息推送或历史记录中查看）：";
    std::cin >> file_msg_id;

    // 下载文件
    std::cout << "[38] B 下载文件..." << std::endl;
    send(MSG_DOWNLOAD_FILE, {{"message_id", file_msg_id}});
    wait();

    // 清理并退出
    std::cout << "\n===== 所有测试完成 =====" << std::endl;
    client.close_conn();
    return 0;
}
