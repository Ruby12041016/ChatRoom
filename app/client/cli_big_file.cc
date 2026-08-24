#include "cli_big_file.h"
#include <netdb.h>
#include <sys/time.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>
#include "logger.h"

bool Cli_BigFile::Connect(const std::string& ip, int port) {
    if (ctrlfd_ >= 0)
        Disconnect();
    ctrlfd_ = connect_ser(ip, port);
    if (ctrlfd_ < 0) {
        LOG(ERROR) << "无法连接到FTP服务器 " << ip << ":" << port << std::endl;
        return false;
    }
    std::string ans = recv_ans();
    if (ctrlfd_ < 0)
        return false;
    return true;
}

void Cli_BigFile::Disconnect() {
    if (ctrlfd_ < 0)
        return;
    send_cmd("QUIT");
    if (ctrlfd_ >= 0) {
        recv_ans();
        close(ctrlfd_);
        ctrlfd_ = -1;
    }
}

int Cli_BigFile::connect_ser(const std::string& ip, int port) {
    int data_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (data_fd < 0)
        return -1;
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    std::string port_str = std::to_string(port);
    if (getaddrinfo(ip.c_str(), port_str.c_str(), &hints, &res) != 0) {
        close(data_fd);
        return -1;
    }
    if (connect(data_fd, res->ai_addr, res->ai_addrlen) < 0) {
        freeaddrinfo(res);
        close(data_fd);
        return -1;
    }
    freeaddrinfo(res);
    return data_fd;
}

void Cli_BigFile::send_cmd(const std::string& command) {
    std::string cmd = command + "\r\n";
    int ret = send(ctrlfd_, cmd.c_str(), cmd.size(), 0);
    if (ret <= 0) {
        close(ctrlfd_);
        ctrlfd_ = -1;
    }
}

std::string Cli_BigFile::recv_ans() {
    // 超时
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(ctrlfd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // 逐字节读取直到 \n，只消费一行FTP响应，绝不触碰后续数据
    std::string answer;
    char c;
    while (true) {
        int n = recv(ctrlfd_, &c, 1, 0);
        if (n <= 0) {
            close(ctrlfd_);
            ctrlfd_ = -1;
            return "";
        }
        if (c == '\n')
            break;
        answer += c;
    }
    while (!answer.empty() && answer.back() == '\r')
        answer.pop_back();
    return answer;
}

std::string Cli_BigFile::file_name(const std::string& file) {
    if (!std::filesystem::exists(file)) {
        return file;
    }
    std::filesystem::path path(file);
    std::string parent = path.parent_path().string();
    std::string name = path.stem().string();
    std::string hou = path.extension().string();

    int index = 1;
    while (true) {
        std::string new_name = name + "(" + std::to_string(index) + ")" + hou;
        std::string full = parent.empty() ? new_name : parent + "/" + new_name;
        if (!std::filesystem::exists(full)) {
            return full;
        }
        index++;
    }
}

static bool SaveDownloadMeta(const std::string& meta_path, long long downloaded) {
    std::string tmp_path = meta_path + ".tmp";
    int fd = open(tmp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return false;
    std::string data = std::to_string(downloaded) + "\n";
    const char* p = data.data();
    size_t left = data.size();
    while (left > 0) {
        ssize_t n = write(fd, p, left);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            close(fd);
            unlink(tmp_path.c_str());
            return false;
        }
        if (n == 0) {
            close(fd);
            unlink(tmp_path.c_str());
            return false;
        }
        p += n;
        left -= n;
    }
    fsync(fd);
    close(fd);
    if (rename(tmp_path.c_str(), meta_path.c_str()) != 0) {
        unlink(tmp_path.c_str());
        return false;
    }
    return true;
}

static bool LoadDownloadMeta(const std::string& meta_path, long long& downloaded) {
    downloaded = 0;
    std::ifstream fp(meta_path);
    if (!fp.is_open())
        return false;
    fp >> downloaded;
    if (fp.fail() || downloaded < 0)
        return false;
    return true;
}

bool Cli_BigFile::Upload(const std::string& local_path, const std::string& remote_name) {
    if (ctrlfd_ < 0) {
        std::cerr << "未连接到FTP服务器" << std::endl;
        return false;
    }

    // 前置校验本地文件
    if (!std::filesystem::exists(local_path)) {
        std::cerr << "本地文件不存在" << std::endl;
        return false;
    }
    long long local_total = std::filesystem::file_size(local_path);
    long long remote_size = get_remote_size(remote_name);
    long long start_offset = 0;
    std::string final_name = remote_name;

    // 断点续传逻辑：远端已有部分文件，设置续传点
    if (remote_size > 0) {
        if (remote_size == local_total) {
            // 向服务端查询可用文件名，避免覆盖
            std::filesystem::path p(final_name);
            std::string stem = p.stem().string();
            std::string ext = p.extension().string();
            int idx = 1;
            do {
                final_name = stem + "(" + std::to_string(idx) + ")" + ext;
                idx++;
            } while (get_remote_size(final_name) >= 0);
            std::cout << "文件已存在，更名为: " << final_name << std::endl;
            remote_size = 0;
        } else if (remote_size < local_total) {
            if (!set_restart(remote_size)) {
                LOG(ERROR) << "设置续传点失败" << std::endl;
                return false;
            }
            start_offset = remote_size;
            std::cout << "断点续传：已上传 " << remote_size
                      << " 字节，继续传输剩余部分" << std::endl;
        }
    }

    send_cmd("STOR " + final_name);
    std::string ans = recv_ans();
    if (ctrlfd_ < 0)
        return false;
    if (ans.find("550") != std::string::npos) {
        LOG(ERROR) << "服务端无法创建文件" << std::endl;
        return false;
    }
    int fd = open(local_path.c_str(), O_RDONLY);
    if (fd < 0) {
        LOG(ERROR) << "无法打开本地文件: " << local_path << std::endl;
        return false;
    }

    // 定位到断点
    off_t offset = start_offset;
    // 定位到续传起始位置
    if (start_offset > 0) {
        if (lseek(fd, offset, SEEK_SET) == (off_t)-1) {
            LOG(ERROR) << "本地文件定位失败" << std::endl;
            close(fd);
            return false;
        }
    }

    // 告诉内核这是顺序读取
    if (local_total > start_offset) {
        posix_fadvise(fd, start_offset, local_total - start_offset, POSIX_FADV_SEQUENTIAL);
    }

    off_t remain = local_total - start_offset;
    while (remain > 0) {
        ssize_t n = sendfile(ctrlfd_, fd, &offset, remain);
        if (n > 0) {
            remain -= n;
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        LOG(ERROR) << "sendfile上传失败: " << strerror(errno) << std::endl;
        close(fd);
        close(ctrlfd_);
        ctrlfd_ = -1;
        return false;
    }
    close(fd);
    shutdown(ctrlfd_, SHUT_WR);
    recv_ans();
    close(ctrlfd_);
    ctrlfd_ = -1;
    return true;
}

bool Cli_BigFile::Download(const std::string& remote_name, const std::string& local_path) {
    if (ctrlfd_ < 0) {
        LOG(ERROR) << "未连接到FTP服务器" << std::endl;
        return false;
    }

    std::filesystem::create_directories("./downloads");
    std::string save_path = "./downloads/" + local_path;
    std::string real_name = save_path;

    std::string meta_path = real_name + ".meta";

    // 查询远端文件总大小
    long long remote_size = get_remote_size(remote_name);
    if (remote_size < 0) {
        LOG(ERROR) << "服务端文件不存在" << std::endl;
        return false;
    }

    std::string resume_path;
    std::string resume_meta_path;
    long long local_size = 0;

    if (FindResumeFile(save_path, resume_path, resume_meta_path)) {
        // 找到了断点文件
        real_name = resume_path;
        meta_path = resume_meta_path;
        std::cout << "发现未完成的断点文件: "
                  << std::filesystem::path(real_name).filename().string()
                  << std::endl;
        if (!LoadDownloadMeta(meta_path, local_size)) {
            LOG(WARNING) << "断点文件损坏: " << meta_path;
            // meta 坏了，就不要继续覆盖这个文件
            std::filesystem::remove(meta_path);
            // 当成普通已有文件，重新命名
            real_name = file_name(save_path);
            meta_path = real_name + ".meta";
            local_size = 0;
            std::cout << "断点无效，更名为: "
                      << std::filesystem::path(real_name).filename().string()
                      << std::endl;
        } else {
            // 检查断点是否合法
            if (local_size < 0 || local_size > remote_size) {
                LOG(WARNING) << "断点无效，local_size=" << local_size << ", remote_size=" << remote_size;
                std::filesystem::remove(meta_path);
                // 原断点文件保留，不覆盖
                real_name = file_name(save_path);
                meta_path = real_name + ".meta";
                local_size = 0;
                std::cout
                    << "断点无效，更名为: "
                    << std::filesystem::path(real_name).filename().string()
                    << std::endl;
            } else {
                // 这里最重要
                // 找到断点后，不再调用 file_name()
                std::cout
                    << "断点续传："
                    << std::filesystem::path(real_name).filename().string()
                    << "，已下载 " << local_size << " / " << remote_size
                    << " 字节" << std::endl;
            }
        }

    } else {
        if (std::filesystem::exists(save_path)) {
            real_name = file_name(save_path);
            meta_path = real_name + ".meta";
            local_size = 0;
            std::cout << "文件已存在，更名为: "
                      << std::filesystem::path(real_name).filename().string()
                      << std::endl;
        }
    }
    if (local_size == remote_size && remote_size > 0) {
        real_name = file_name(save_path);
        meta_path = real_name + ".meta";
        local_size = 0;
        std::cout << "文件已存在，更名为: "
                  << std::filesystem::path(real_name).filename().string()
                  << std::endl;
    }
    // 设置断点续传偏移
    if (local_size > 0) {
        if (!set_restart(local_size)) {
            LOG(ERROR) << "设置续传点失败" << std::endl;
            return false;
        } else {
            std::cout << "断点续传：已下载 " << local_size << " 字节，继续传输剩余部分" << std::endl;
        }
    }

    send_cmd("RETR " + remote_name);
    std::string ans = recv_ans();
    if (ctrlfd_ < 0)
        return false;
    if (ans.find("550") != std::string::npos) {
        LOG(ERROR) << "服务端文件不存在" << std::endl;
        return false;
    }

    // 文件传输阶段不要使用 recv_ans() 设置的 5 秒超时
    struct timeval tv{};
    setsockopt(ctrlfd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // 用原生IO替换ofstream，避免ofstream默认8KB小缓冲的频繁写盘
    int open_flags = O_WRONLY | O_CREAT;
    // 新下载时从头写，截断旧内容
    if (local_size == 0) {
        open_flags |= O_TRUNC;
    }
    int fd = open(real_name.c_str(), open_flags, 0644);
    if (fd < 0) {
        LOG(ERROR) << "无法打开本地文件：" << real_name << std::endl;
        return false;
    }

    // 预分配文件空间，让文件系统一次性分配连续块，避免逐块分配导致碎片化
    if (local_size == 0 && remote_size > 0) {
        int ret = posix_fallocate(fd, 0, remote_size);
        if (ret != 0) {
            LOG(WARNING) << "posix_fallocate失败: " << strerror(ret) << std::endl;
            // 预分配失败不影响下载
            if (ftruncate(fd, 0) != 0) {
                close(fd);
                return false;
            }
        }
    }

    if (local_size == 0 && remote_size > 0 &&
        !std::filesystem::exists(meta_path)) {
        if (!SaveDownloadMeta(meta_path, 0)) {
            LOG(WARNING) << "初始化下载断点失败" << std::endl;
        }
    }

    if (lseek(fd, local_size, SEEK_SET) == (off_t)-1) {
        LOG(ERROR) << "设置本地写入位置失败" << std::endl;
        close(fd);
        return false;
    }

    std::cout << "保存为: "
              << std::filesystem::path(real_name).filename().string()
              << std::endl;

    std::vector<char> buf(BUF_SIZE);
    int n;
    long long downloaded = local_size;
    long long last_checkpoint = local_size;
    auto last_progress_time = std::chrono::steady_clock::now();
    constexpr long long CHECKPOINT_SIZE = 256LL * 1024 * 1024;

    while (downloaded < remote_size) {
        long long remain_bytes = remote_size - downloaded;
        int recv_size =
            static_cast<int>(std::min<long long>(BUF_SIZE, remain_bytes));
        n = recv(ctrlfd_, buf.data(), recv_size, 0);
        if (n > 0) {
            int written = 0;
            while (written < n) {
                ssize_t m = write(fd, buf.data() + written, n - written);
                if (m < 0) {
                    if (errno == EINTR)
                        continue;
                    LOG(ERROR) << "写文件失败: " << strerror(errno) << std::endl;
                    close(fd);
                    close(ctrlfd_);
                    ctrlfd_ = -1;
                    return false;
                }
                if (m == 0) {
                    LOG(ERROR) << "write返回0" << std::endl;
                    close(fd);
                    close(ctrlfd_);
                    ctrlfd_ = -1;
                    return false;
                }
                written += static_cast<int>(m);
            }
            downloaded += n;
            // 100ms刷新一次进度
            auto now = std::chrono::steady_clock::now();
            if (on_progress && remote_size > 0 && now - last_progress_time >= std::chrono::milliseconds(100)) {
                on_progress(downloaded, remote_size);
                last_progress_time = now;
            }

            continue;
        }
        if (n == 0) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        LOG(ERROR) << "recv失败: " << strerror(errno) << std::endl;
        break;
    }

    fsync(fd);
    close(fd);
    close(ctrlfd_);
    ctrlfd_ = -1;

    // 判断是否真的完整
    if (downloaded == remote_size) {
        std::filesystem::remove(meta_path);
        if (on_progress && remote_size > 0) {
            on_progress(remote_size, remote_size);
        }
        std::cout << "\n下载完成" << std::endl;
        return true;
    }
    // 中断：保存最终断点
    SaveDownloadMeta(meta_path, downloaded);
    std::cout << "\n下载中断，已保存 " << downloaded << " / " << remote_size << " 字节" << std::endl;
    std::cout << "下次下载将自动断点续传" << std::endl;
    return false;
}

// 查询远端文件大小，失败返回-1
long long Cli_BigFile::get_remote_size(const std::string& filename) {
    if (ctrlfd_ < 0)
        return -1;
    send_cmd("SIZE " + filename);
    std::string ans = recv_ans();
    if (ctrlfd_ < 0 || ans.empty())
        return -1;

    // 解析FTP标准响应：213 <文件大小>
    if (ans.substr(0, 3) == "213") {
        size_t pos = ans.find(' ', 4);
        std::string num_str = ans.substr(4, pos - 4);
        return std::stoll(num_str);
    }
    return -1;
}

// 设置续传断点，成功返回true
bool Cli_BigFile::set_restart(long long offset) {
    if (ctrlfd_ < 0)
        return false;
    send_cmd("REST " + std::to_string(offset));
    std::string ans = recv_ans();
    if (ctrlfd_ < 0 || ans.empty())
        return false;
    // 350状态码表示设置成功
    return ans.substr(0, 3) == "350";
}

bool Cli_BigFile::FindResumeFile(const std::string& save_path, std::string& resume_path, std::string& resume_meta_path) {
    namespace fs = std::filesystem;
    fs::path path(save_path);
    fs::path parent = path.parent_path();
    if (parent.empty())
        parent = ".";
    const std::string stem = path.stem().string();
    const std::string ext = path.extension().string();
    // 先检查原始文件：
    {
        std::string meta = save_path + ".meta";

        if (fs::exists(save_path) && fs::exists(meta)) {
            resume_path = save_path;
            resume_meta_path = meta;
            return true;
        }
    }
    // 再检查自动重命名的文件：
    int best_index = -1;
    fs::path best_file;
    fs::path best_meta;
    std::string prefix = stem + "(";
    std::string suffix = ")" + ext;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(parent, ec)) {
        if (ec)
            break;
        if (!entry.is_regular_file())
            continue;
        std::string filename = entry.path().filename().string();
        if (filename.rfind(prefix, 0) != 0)
            continue;
        if (filename.size() <= prefix.size() + suffix.size())
            continue;
        if (filename.substr(filename.size() - suffix.size()) != suffix)
            continue;
        // 提取中间的 index
        size_t index_begin = prefix.size();
        size_t index_end = filename.size() - suffix.size();
        std::string index_str =
            filename.substr(index_begin, index_end - index_begin);
        if (index_str.empty())
            continue;
        bool all_digit = true;
        for (char c : index_str) {
            if (!std::isdigit(static_cast<unsigned char>(c))) {
                all_digit = false;
                break;
            }
        }
        if (!all_digit)
            continue;
        int index = 0;
        try {
            index = std::stoi(index_str);
        } catch (...) {
            continue;
        }
        if (index <= 0)
            continue;
        fs::path file_path = entry.path();
        fs::path meta_path = fs::path(file_path.string() + ".meta");
        // 必须文件和 .meta 都存在
        if (!fs::exists(meta_path))
            continue;
        // 找编号最大的断点文件
        if (index > best_index) {
            best_index = index;
            best_file = file_path;
            best_meta = meta_path;
        }
    }
    if (best_index >= 0) {
        resume_path = best_file.string();
        resume_meta_path = best_meta.string();
        return true;
    }
    return false;
}