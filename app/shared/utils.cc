#include "utils.h"

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <random>
#include <regex>
#include <sstream>
#include <stdexcept>
#include "logger.h"

// Hex编码
std::string bytes_to_hex(const std::vector<unsigned char>& data) {
    std::ostringstream oss;
    for (unsigned char c : data) {
        oss << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(c);
    }
    return oss.str();
}

std::vector<unsigned char> hex_to_bytes(const std::string& hex) {
    if (hex.size() % 2 != 0)
        throw std::runtime_error("invalid hex");
    std::vector<unsigned char> result;
    result.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        std::string byte = hex.substr(i, 2);
        unsigned char value =
            static_cast<unsigned char>(std::stoul(byte, nullptr, 16));
        result.push_back(value);
    }
    return result;
}

// 生成随机盐值
std::vector<unsigned char> generate_salt(size_t len) {
    std::vector<unsigned char> salt(len);
    if (RAND_bytes(salt.data(), len) != 1) {
        throw std::runtime_error("RAND_bytes failed");
    }
    return salt;
}

// PBKDF2，返回加密后的二进制哈希串，不可逆
std::vector<unsigned char> pbkdf2_hash(const std::string& password, const std::vector<unsigned char>& salt) {
    std::vector<unsigned char> hash(HASH_LEN);
    int ok = PKCS5_PBKDF2_HMAC(password.c_str(), password.size(), salt.data(),
                               salt.size(), PBKDF2_ITER, EVP_sha256(), HASH_LEN,
                               hash.data());
    if (!ok) {
        throw std::runtime_error("PBKDF2 failed");
    }
    return hash;
}

// 注册，自动生成随机盐，把盐转十六进制并返回
std::string password_encrypt(const std::string& password, std::string& salt_hex) {
    auto salt = generate_salt();
    auto hash = pbkdf2_hash(password, salt);
    salt_hex = bytes_to_hex(salt);
    return bytes_to_hex(hash);
}

// 恒定时间比较
bool secure_compare(const std::vector<unsigned char>& a, const std::vector<unsigned char>& b) {
    if (a.size() != b.size())
        return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        diff |= a[i] ^ b[i];
    }
    return diff == 0;
}

// 登录验证，参数是密码，数据库存的盐，数据库存的密码hash
bool password_verify(const std::string& password, const std::string& salt_hex, const std::string& hash_hex) {
    auto salt = hex_to_bytes(salt_hex);
    auto db_hash = hex_to_bytes(hash_hex);
    auto calc_hash = pbkdf2_hash(password, salt);
    return secure_compare(calc_hash, db_hash);  // 二进制形式比较
}

// 合法性校检
bool is_valid_username(const std::string& username) {
    if (username.empty()) {
        return false;
    }
    size_t char_count = 0;  // 记录真实的字符个数（不是字节数）
    for (size_t i = 0; i < username.size();) {
        unsigned char c = static_cast<unsigned char>(username[i]);
        size_t bytes = 0;
        // 判断当前字符的 UTF-8 字节长度
        if (c <= 0x7F) {
            bytes = 1;  // 单字节 ASCII 字符（字母、数字、下划线）
        } else if (c <= 0xEF) {
            bytes = 3;  // 三字节字符（绝大多数常用的中文汉字）
        } 
        // 检查
        if (bytes == 1) {
            // 单字节字符：必须符合原来的规则（仅支持字母、数字、下划线）
            if (!std::isalnum(c) && c != '_') {
                return false;
            }
        } else {
            // 多字节字符（中文等）：在这里视为合法字符，直接允许
        }
        // 更新循环索引和字符计数器
        char_count++;
        i += bytes;  // 跳过当前字符的所有字节
    }
    // 使用真实的字符长度做判断
    if (char_count < 3 || char_count > 20) {
        return false;
    }

    return true;
}

bool is_valid_password(const std::string& password) {
    // 检查密码是否为空
    if (password.empty()) {
        return false;
    }
    // 检查密码长度是否在 6 到 32 个字符之间
    if (password.length() < 6 || password.length() > MAX_PWD_LEN) {
        return false;
    }
    // 检查密码是否包含至少一个字母和一个数字
    bool has_zimu = false;
    bool has_shuzi = false;
    for (char c : password) {
        if (std::isalpha(c)) {
            has_zimu = true;
        } else if (std::isdigit(c)) {
            has_shuzi = true;
        }
    }
    return has_zimu && has_shuzi;
}

bool is_valid_email(const std::string& email) {
    // 检查邮箱是否为空
    if (email.empty()) {
        return false;
    }
    // 使用正则表达式检查邮箱格式（xxx@shturl格式）
    const std::regex pattern(R"((\w+)(\.{1}\w+)*@(\w+)(\.\w+)+)");
    return std::regex_match(email, pattern);
}

bool is_valid_phone(const std::string& phone) {
    // 检查手机号是否为空
    if (phone.empty() || phone.size() != 11) {
        return false;
    }
    for (char c : phone)
        if (!isdigit(c))
            return false;
    return true;
}

// 时间
uint64_t get_timestamp_sec() {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch())
        .count();
}
uint64_t get_timestamp_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch())
        .count();
}

// 随机数生成工具
int rand_range(int min, int max) {
    if (min > max)
        std::swap(min, max);
    static thread_local std::mt19937 gen{std::random_device{}()};
    std::uniform_int_distribution<> dis(min, max);
    return dis(gen);
}

// 生成32位大小写字母+数字随机字符串，登陆成功存到redis
std::string generate_session_token() {
    static const char alphanum[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";
    std::string token;
    token.reserve(32);  // 长度32
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, sizeof(alphanum) - 2);
    for (int i = 0; i < 32; ++i) {
        token += alphanum[dis(gen)];
    }
    return token;
}

// libcurl 读取 payload 的回调函数
struct UploadStatus {
    const std::string* data;
    size_t pos;
};

static size_t payload_source(void* ptr, size_t size, size_t nmemb, void* userp) {
    auto* status = static_cast<UploadStatus*>(userp);
    const std::string* data = status->data;
    if (status->pos >= data->size()) {
        return 0;  // 数据已读完
    }
    size_t available = data->size() - status->pos;
    size_t max = size * nmemb;
    size_t copy_len = (available < max) ? available : max;
    memcpy(ptr, data->data() + status->pos, copy_len);
    status->pos += copy_len;
    return copy_len;
}

// 生成当前时间的RFC 2822日期字符串函数，邮件标准
static std::string rfc2822_date() {
    time_t now = time(nullptr);
    struct tm tm_gmt;
    gmtime_r(&now, &tm_gmt);

    const char* day_names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    const char* mon_names[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                               "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    char buf[64];
    // 格式: Thu, 30 Jul 2026 12:00:00 +0000
    snprintf(buf, sizeof(buf), "%s, %02d %s %04d %02d:%02d:%02d +0000",
             day_names[tm_gmt.tm_wday], tm_gmt.tm_mday,
             mon_names[tm_gmt.tm_mon], tm_gmt.tm_year + 1900, tm_gmt.tm_hour,
             tm_gmt.tm_min, tm_gmt.tm_sec);
    return std::string(buf);
}

// 发送邮件
bool sendMail(const std::string& to,
              const std::string& subject,
              const std::string& body,
              const std::string& smtp_server,
              int smtp_port,
              const std::string& username,
              const std::string& password,
              bool use_ssl) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        LOG(ERROR) << "curl_easy_init failed";
        return false;
    }

    // 构建完整邮件内容（RFC 2822 格式）
    std::string mail_content;
    mail_content.reserve(1024);
    mail_content += "To: <" + to + ">\r\n";
    mail_content += "From: <" + username + ">\r\n";
    mail_content += "Subject: " + subject + "\r\n";
    mail_content += "Date: " + rfc2822_date() + "\r\n";
    mail_content += "Content-Type: text/plain; charset=UTF-8\r\n";
    mail_content += "\r\n";
    mail_content += body;

    UploadStatus upload_ctx{&mail_content, 0};

    // 收件人列表
    struct curl_slist* recipients = nullptr;
    std::string full_recipient = "<" + to + ">";
    recipients = curl_slist_append(recipients, full_recipient.c_str());

    // 构造 SMTP URL
    std::string url = (use_ssl ? "smtps://" : "smtp://") + smtp_server + ":" +
                      std::to_string(smtp_port);

    // 设置 cURL 选项 (已去重)
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERNAME, username.c_str());
    curl_easy_setopt(curl, CURLOPT_PASSWORD, password.c_str());
    curl_easy_setopt(curl, CURLOPT_MAIL_FROM, ("<" + username + ">").c_str());
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, payload_source);
    curl_easy_setopt(curl, CURLOPT_READDATA, &upload_ctx);
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    // 1. SSL 设置 (QQ 邮箱必须强制 SSL)
    if (use_ssl) {
        curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_ALL);
    }

    // 2. 禁用代理（防止系统代理干扰）
    curl_easy_setopt(curl, CURLOPT_PROXY, "");
    curl_easy_setopt(curl, CURLOPT_NOPROXY, "*");

    // 3. 强制 AUTH LOGIN 认证（QQ 邮箱专属）
    curl_easy_setopt(curl, CURLOPT_LOGIN_OPTIONS, "AUTH=LOGIN");

    // 4. 允许收件人失败
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT_ALLOWFAILS, 1L);

    CURLcode res = curl_easy_perform(curl);
    bool success = (res == CURLE_OK);

    if (!success) {
        LOG(ERROR) << "sendMail failed: " << curl_easy_strerror(res);
        long resp_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp_code);
        LOG(ERROR) << "SMTP response code: " << resp_code;
    }

    // 清理资源
    curl_slist_free_all(recipients);
    curl_easy_cleanup(curl);
    return success;
}

std::string base64_encode(const std::string& input) {
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO* bmem = BIO_new(BIO_s_mem());
    BIO* bio = BIO_push(b64, bmem);
    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);

    int total = 0;
    int remaining = (int)input.size();
    while (remaining > 0) {
        int n = BIO_write(bio, input.data() + total, remaining);
        if (n <= 0)
            break;
        total += n;
        remaining -= n;
    }
    BIO_flush(bio);

    std::string result;
    char buf[4096];
    int len;
    while ((len = BIO_read(bmem, buf, sizeof(buf))) > 0) {
        result.append(buf, len);
    }

    BIO_free_all(bio);
    return result;
}

std::string base64_decode(const std::string& input) {
    if (input.empty())
        return "";

    BIO* b64 = BIO_new(BIO_f_base64());
    BIO* bmem = BIO_new_mem_buf(input.data(), input.size());
    BIO* bio = BIO_push(b64, bmem);
    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);

    std::vector<char> buf(input.size());
    int len = BIO_read(bio, buf.data(), input.size());

    std::string result;
    if (len > 0) {
        result.assign(buf.data(), len);
    }

    BIO_free_all(bio);
    return result;
}
