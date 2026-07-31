1. `UNIQUE KEYuk_user_friend(user_id,friend_id)`
- 含义：`user_id` 和 `friend_id` 的组合不能重复。
- 为什么要有这个约束：防止用户 A 重复添加用户 B 为好友。

2. `KEYidx_friend_id(friend_id)`
- 含义：给 `friend_id` 单独建一个索引。
- 为什么需要：查询“谁添加了用户 5 为好友”时，需要根据 `friend_id` 查找，建索引后查询快。
- `UNIQUE KEY (user_id, friend_id)` 的索引在查询单独 `friend_id` 时用不上（最左前缀原则），所以需要独立索引。

3. `CONSTRAINTfk_friend_userFOREIGN KEY (user_id) REFERENCESusers(id) ON DELETE CASCADE`
拆开看：
- `CONSTRAINTfk_friend_user`：给这个约束起个名字，方便报错时定位。
- `OREIGN KEY (user_id)`：`user_id` 是外键，它的值必须来自另一张表。
- `REFERENCESusers(id)`：引用的是 `users` 表的 `id` 字段。
- `ON DELETE CASCADE`：如果 `users` 表中的某个用户被删除了，那么 `friends` 表中所有 `user_id` 等于该用户 ID 的记录也会自动删除。
- 举例：
用户 3 被注销/删除 → `friends` 表中所有 `user_id = 3` 的好友关系自动消失。这样就不会出现“好友关系指向一个不存在的用户”的垃圾数据。

4. `CONSTRAINTfk_friend_friendFOREIGN KEY (friend_id) REFERENCESusers(id) ON DELETE CASCADE`
- 同理：如果被添加的好友（`friend_id`）这个用户被删了，相关的所有好友关系也会自动删除。
- 举例：
用户 5 被注销 → 所有 `friend_id = 5` 的记录（即别人加他为好友的记录）也会自动删除。