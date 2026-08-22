#include "logger.h"
#include <sys/stat.h>
#include <string>

void InitLogger(const char* program_name, const char* log_dir) {
    // 创建日志目录，不存在则生成
    mkdir(log_dir, 0755);
    // 设置日志输出目录
    FLAGS_log_dir = log_dir;
    // 日志文件名前缀
    google::InitGoogleLogging(program_name);

    // 配置日志参数
    FLAGS_stderrthreshold = 0;               // 所有级别日志同时输出到控制台
    FLAGS_minloglevel = 0;                   // 最低打印INFO级别
    FLAGS_max_log_size = 100;                // 单日志文件最大100MB
    FLAGS_stop_logging_if_full_disk = true;  // 磁盘满停止打日志
}

void CloseLogger() {
    google::ShutdownGoogleLogging();
}
