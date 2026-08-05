#ifndef CLI_FRIEND_H
#define CLI_FRIEND_H

#include "global.h"
#include "cli_network.h"

class Cli_Friend {
public:
 Cli_Friend(Cli_Network* net):net_(net){}

 void seack_user(const std::string& account);
 void add_friend(uint64_t friend_id,uint64_t user_id);
 void agree_friend(uint64_t apply_id,uint64_t agree_id);
 void refuse_friend(uint64_t apply_id, uint64_t agree_id);
 void delete_friend(uint64_t user_id);
 void set_friend_mute(uint64_t friend_id,bool blocked);
 void set_friend_remark(uint64_t friend_id, const std::string& remark);

private :
 Cli_Network* net_;
};

#endif
