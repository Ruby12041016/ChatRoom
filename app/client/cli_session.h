#ifndef CLI_SESSION_H
#define CLI_SESSION_H

#include "global.h"

class Cli_Session{
public:
 static Cli_Session& instance() { 
    static Cli_Session cs;
    return cs;
 }

 void set_user(uint64_t id,const std::string& name) { 
    id_ = id;
    name_ = name;
    logged_in = true;
 }

 uint64_t get_id() const{ return id_; }
 std::string get_name() const{ return name_; }
 bool islogin() const{ return logged_in; }
 void logout() { logged_in = false; }

private:
 uint64_t id_ = 0;
 std::string name_;
 bool logged_in = false;
};

#endif