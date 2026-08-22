#ifndef GLOBAL_H
#define GLOBAL_H

// 通用基础头文件（网络、字符串、内存、IO、JSON常用）
#include <arpa/inet.h>
#include <curl/curl.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <set>
#include <thread>
#include <unordered_map>
#include <vector>
#include "nlohmann/json.hpp"

#define DEFAULT_SERVER_PORT 8888  // 服务端默认端口
#define PACK_HEAD_LEN 4           // 协议固定：4字节长度头
#define MAX_PACKET_SIZE \
    1024 * 1024 * 10  // 最大包长度10MB，超过这个长度的包直接丢弃
#define MAX_JSON_BODY_LEN \
    (MAX_PACKET_SIZE - PACK_HEAD_LEN)  // JSON消息体最大长度
#define MAX_NAME_LEN 32                // 各种名称最大长度
#define MAX_PWD_LEN 64                 // 密码长度
#define MAX_LIST_COUNT 100             // 单次列表等信息返回的上限（分页）
#define MAX_HISTORY_LIMIT 200  // 单次历史消息拉取上限，防止大数据量导致OOM

// 网络传输的时候发送这个头+JSON字符串
typedef struct PackHead {
    uint32_t body_len;  // 4字节JSON消息体长度（大端序）
} PackHead;

extern std::atomic<bool> shutdown_;

// 错误码（JSON里的code字段）
enum ErrorCode {
    SUCCESS = 0,                               // 成功
    ERR_PARAM = 1,                             // 参数非法
    ERR_ACCOUNT_EXIST = 2,                     // 账号已注册
    ERR_ACCOUNT_NOT_EXIST = 3,                 // 账号不存在
    ERR_PWD_WRONG = 4,                         // 密码错误
    ERR_OFFLINE = 5,                           // 用户离线
    ERR_FRIEND_EXIST = 6,                      // 已是好友
    ERR_FRIEND_NOT_EXIST = 7,                  // 非好友
    ERR_GROUP_EXIST = 8,                       // 群已存在
    ERR_GROUP_NOT_EXIST = 9,                   // 群不存在
    ERR_NOT_IN_GROUP = 10,                     // 不在群内
    ERR_PERMISSION = 11,                       // 权限不足
    ERR_PACK_LEN_OVER = 12,                    // 消息超长
    ERR_JSON_PARSE = 13,                       // JSON解析失败
    ERR_SOCKET = 14,                           // 网络异常
    ERR_SERVER_BUSY = 15,                      // 服务器繁忙
    ERR_CAPTCHA_INVALID = 16,                  // 验证码错误/过期
    ERR_CAPTCHA_FREQUENT = 17,                 // 验证码频繁
    ERR_ALREADY_ONLINE = 18,                   // 账号已登陆
     ERR_BLACKED = 19,                         // 被对方拉黑
    ERR_NO_FRIEND_PERM = 20,                   // 非好友禁止私聊
    ERR_NOT_GROUP_ADMIN = 21,                  // 不是管理员
    ERR_GROUP_OWNER_CANNOT_QUIT = 22,          // 群主不能直接退群
};

// 消息类型(JSON里的msg_type字段)
enum MsgType {
    // ========== 账号模块 10xx ==========
    MSG_REGISTER = 1001,        // 注册
    MSG_LOGIN = 1002,           // 登录
    MSG_LOGOUT = 1003,          // 注销/登出
    MSG_UPDATE_INFO = 1004,     // 修改个人信息
    MSG_GET_CAPTCHA = 1005,     // 获取验证码(邮箱/手机)
    MSG_RESET_PWD = 1006,       // 验证码找回密码
    MSG_MODIFY_PWD = 1007,      // 修改密码
    MSG_DELETE_ACCOUNT = 1008,  // 注销账号

    // ========== 好友模块 20xx ==========
    MSG_SEARCH_USER = 2001,        // 搜索用户
    MSG_ADD_FRIEND = 2002,         // 发起加好友
    MSG_AGREE_FRIEND = 2003,       // 同意好友
    MSG_REFUSE_FRIEND = 2004,      // 拒绝好友
    MSG_DEL_FRIEND = 2005,         // 删除好友
    MSG_GET_FRIEND_LIST = 2006,    // 获取好友列表
    MSG_GET_APPLY_LIST = 2007,     // 获取好友申请列表
    MSG_BLACK_USER = 2008,         // 拉黑用户
    MSG_SET_FRIEND_MUTE = 2009,    // 屏蔽好友消息
    MSG_SET_FRIEND_REMARK = 2010,  // 设置好友备注

    // ========== 群组模块 30xx ==========
    MSG_CREATE_GROUP = 3001,        // 创建群
    MSG_DISMISS_GROUP = 3002,       // 解散群
    MSG_INVITE_MEMBER = 3003,       // 邀请入群
    MSG_KICK_MEMBER = 3004,         // 踢出成员
    MSG_QUIT_GROUP = 3005,          // 退出群
    MSG_GET_GROUP_LIST = 3006,      // 我的群列表
    MSG_GET_GROUP_MEMBER = 3007,    // 群成员列表
    MSG_APPLY_JOIN_GROUP = 3008,    // 主动申请加群
    MSG_GET_GROUP_APPLY = 3009,     // 查看入群申请
    MSG_GROUP_SET_ADMIN = 3010,     // 设置/撤销管理员
    MSG_TRANSFER_OWNER = 3011,      // 转让群主
    MSG_UPDATE_GROUP_INFO = 3012,   // 修改群资料
    MSG_GROUP_MUTE = 3013,          // 群禁言(单人/全员)
    MSG_SET_GROUP_MUTE = 3014,      // 屏蔽群消息
    MSG_GROUP_APPLY_REVIEW = 3015,  // 审批入群申请
    MSG_SEARCH_GROUP = 3016,        // 搜索群聊

    // ========== 聊天模块 40xx ==========
    MSG_PRIVATE_CHAT = 4001,               // 私聊文字
    MSG_GROUP_CHAT = 4002,                 // 群聊文字
    MSG_IMAGE_MSG = 4003,                  // 图片消息
    MSG_FILE_MSG = 4004,                   // 发送文件
    MSG_GET_HISTORY_MSG = 4005,            // 获取历史消息
    MSG_RECALL_MSG = 4006,                 // 撤回消息
    MSG_DOWNLOAD_FILE = 4007,              // 请求下载文件
    MSG_PUSH_OFFLINE_PRIVATE_MSGS = 4008,  // 离线私聊消息拉取
    MSG_PUSH_RECENT_GROUP_MSGS = 4009,     // 拉取最近群消息（上线通知）

    // ========== 服务端推送 50xx ==========
    PUSH_FRIEND_APPLY = 5001,     // 收到好友申请
    PUSH_PRIVATE_MSG = 5002,      // 私聊推送(在线/离线)
    PUSH_GROUP_MSG = 5003,        // 群消息推送
    PUSH_OFFLINE_NOTICE = 5004,   // 上下线通知
    PUSH_FRIEND_ONLINE = 5005,    // 好友上线推送
    PUSH_FRIEND_OFFLINE = 5006,   // 好友离线推送
    PUSH_GROUP_APPLY = 5007,      // 新的加群申请推送
    PUSH_OFFLINE_FILE = 5008,     // 离线文件通知
    PUSH_FRIEND_AGREE = 5009,     // 好友申请被同意
    PUSH_FRIEND_REFUSE = 5010,    // 好友申请被拒绝
    PUSH_GROUP_AGREE = 5011,      // 入群申请被同意
    PUSH_GROUP_REFUSE = 5012,     // 入群申请被拒绝
    PUSH_GROUP_SET_ADMIN = 5013,  // 群管理员变更通知

    MSG_HEARTBEAT = 0  // 心跳消息类型
};

#endif