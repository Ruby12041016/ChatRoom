#ifndef LOGGER_H
#define LOGGER_H

// 引入glog核心头文件
#include <glog/logging.h>

// 日志级别宏封装，统一业务使用
#define LOG_INFO LOG(INFO)
#define LOG_WARN LOG(WARNING)
#define LOG_ERROR LOG(ERROR)

// 自定义日志初始化函数声明
void InitLogger(const char* program_name, const char* log_dir = "./log");

// 日志关闭函数
void CloseLogger();

#endif  