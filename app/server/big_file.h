#ifndef BIG_FILE_H
#define BIG_FILE_H

#include <atomic>
#include <string>
#include <thread>
#include "global.h"

class BigFileServer {
   public:
    BigFileServer(int port = 2100);
    ~BigFileServer();

    void Start();
    void Stop();

   private:
    void Run();

    int port_;
    std::atomic<bool> running_;
    std::thread worker_;
};

#endif