#ifndef CLI_BIG_FILE_H
#define CLI_BIG_FILE_H

#include <fcntl.h>
#include <sys/sendfile.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include "global.h"

class Cli_BigFile {
   public:
    Cli_BigFile() = default;

    bool Connect(const std::string& ip, int port = 2100);
    void Disconnect();
    bool IsConnected() const { return ctrlfd_ >= 0; }

    bool Upload(const std::string& local_path, const std::string& remote_name);
    bool Download(const std::string& remote_name, const std::string& local_path);

    using Download_Progress = std::function<void(long long current, long long total)>;
    Download_Progress on_progress;

   private:
    int connect_ser(const std::string& ip, int port);
    void send_cmd(const std::string& command);
    std::string recv_ans();
    std::pair<std::string, int> do_pasv(const std::string& answer);
    int pasv_conn();
    std::string file_name(const std::string& file);

    long long get_remote_size(const std::string& filename);  // 查询远端文件大小
    bool set_restart(long long offset);                      // 设置断点续传偏移
    bool FindResumeFile(const std::string& save_path, std::string& resume_path, std::string& resume_meta_path);

    int ctrlfd_ = -1;
    static constexpr int BUF_SIZE = 8*1024*1024;
};

#endif