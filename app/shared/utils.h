#ifndef UTILS_H
#define UTILS_H

#include <cstdint>
#include <string>
#include <vector>
#include "global.h"

constexpr int SALT_LEN = 16;         // 16字节随机Salt
constexpr int HASH_LEN = 32;         // 256bit
constexpr int PBKDF2_ITER = 100000;  // 10万次迭代


// 生成随机Salt（二进制）
std::vector<unsigned char> generate_salt(size_t len = SALT_LEN);

// PBKDF2加密
std::vector<unsigned char> pbkdf2_hash(const std::string& password, const std::vector<unsigned char>& salt);

// 注册时调用
// 返回：Hex(Hash)
std::string password_encrypt(const std::string& password, std::string& salt_hex);

// 登录时验证
bool password_verify(const std::string& password, const std::string& salt_hex, const std::string& hash_hex);


// Hex转换
std::string bytes_to_hex(const std::vector<unsigned char>& data);

std::vector<unsigned char> hex_to_bytes(const std::string& hex);


// 恒定时间比较
bool secure_compare(const std::vector<unsigned char>& a,const std::vector<unsigned char>& b);

// 工具函数
bool is_valid_username(const std::string& username);
bool is_valid_password(const std::string& password);
bool is_valid_email(const std::string& email);
bool is_valid_phone(const std::string& phone);

uint64_t get_timestamp_sec();

uint64_t get_timestamp_ms();

int rand_range(int min, int max);

std::string generate_session_token();

bool sendMail(const std::string& to,
              const std::string& subject,
              const std::string& body,
              const std::string& smtp_server,
              int smtp_port,
              const std::string& username,
              const std::string& password,
              bool use_ssl = true);


              
// Base64 编解码（使用 OpenSSL）
std::string base64_encode(const std::string& input);
std::string base64_decode(const std::string& input);

#endif