//
// Created by wxx on 12/16/25.
//

#ifndef STREAMGATE_STREAMTASKSERIALIZER_H
#define STREAMGATE_STREAMTASKSERIALIZER_H
#include "StreamTask.h"

#include <map>
#include <string>
#include <optional>

class StreamTaskSerializer
{
public:
    /**
     * @brief 将 StreamTask 序列化为 Redis Hash 支持的字段映射
     * @param task
     */
    static std::map<std::string, std::string> serialize(const StreamTask& task);

    /**
     *@brief 从 Redis Hash 字段反序列化为 StreamTask
     * @param fields
     * @return 成功返回任务对象，失败或字段缺失返回 std::nullopt
     */
    static std::optional<StreamTask> deserialize(const std::map<std::string, std::string>& fields);

private:
    //辅助函数
    static std::string time_point_to_string(const std::chrono::system_clock::time_point& tp);
    static std::chrono::system_clock::time_point string_to_time_point(const std::string& s);

    //安全获取字段工具
    static std::string get_field(const std::map<std::string, std::string>& fields,
                                 const std::string& key,
                                 const std::string& default_val = "");
};
#endif //STREAMGATE_STREAMTASKSERIALIZER_H
