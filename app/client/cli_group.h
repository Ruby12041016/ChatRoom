#ifndef CLI_GROUP_H
#define CLI_GROUP_H

#include "cli_network.h"
#include "global.h"

class Cli_Group {
   public:
    Cli_Group(Cli_Network* net) : net_(net) {}
    void create_group(const std::string& group_name, const std::string& group_desc);
    void dismiss_group(uint64_t group_id);
    void apply_join_group(uint64_t group_id, const std::string& message);
    void get_group_apply(uint64_t group_id);
    void apply_review(uint64_t group_id, uint64_t apply_id, int decision);
    void quit_group(uint64_t group_id);
    void group_member(uint64_t group_id);
    void set_admin(uint64_t group_id, uint64_t user_id, bool set_admin);
    void kick_member(uint64_t group_id, uint64_t user_id);
    void search_group(const std::string& keyword);

   private:
    Cli_Network* net_;
};

#endif