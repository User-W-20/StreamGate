//
// Created by X on 2025/11/18.
//

#ifndef STREAMGATE_CONFIGLOADER_H
#define STREAMGATE_CONFIGLOADER_H

#include <string>
#include <map>
#include <vector>
#include <functional>
#include <stdexcept>
#include <mutex>

class ConfigLoader
{
public:
    struct LoadOptions
    {
        bool allow_missing_ini;
        bool allow_missing_env;
        bool override_from_environment;
        std::vector<std::string> env_keys_to_load;

        LoadOptions()
            : allow_missing_ini(false),
              allow_missing_env(true),
              override_from_environment(true),
              env_keys_to_load({})
        {
        }
    };

    static ConfigLoader& instance()
    {
        static auto* inst = new ConfigLoader();
        return *inst;
    }

    ConfigLoader(const ConfigLoader&) = delete;
    ConfigLoader& operator=(const ConfigLoader&) = delete;

    /**
     * @brief 强一致性加载：要么全部解析成功并验证通过，要么保持旧配置不变。
     * @return true: 加载成功且验证通过。
     * @return false: 加载失败，已保持旧配置不变。
     */
    bool load(const std::string& ini_filename, const std::string& env_filename = "",
              const LoadOptions& options = LoadOptions());

    void reload();

    // 类型安全接口 (带缓存锁)
    [[nodiscard]] std::string getString(const std::string& key) const;
    [[nodiscard]] std::string getString(const std::string& key, const std::string& default_value) const;

    [[nodiscard]] int getInt(const std::string& key) const;
    [[nodiscard]] int getInt(const std::string& key, int default_value) const;

    [[nodiscard]] bool getBool(const std::string& key) const;
    [[nodiscard]] bool getBool(const std::string& key, bool default_value) const;

    [[nodiscard]] double getDouble(const std::string& key) const;
    [[nodiscard]] double getDouble(const std::string& key, double default_value) const;

    [[nodiscard]] bool has(const std::string& key) const;
    void set(const std::string& key, const std::string& value);

    using Validator = std::function<bool(const std::string&)>;
    void addValidator(const std::string& key, Validator validator, const std::string& err_msg);

private:
    ConfigLoader();

    // 内部解析方法：解析到传入的临时 Map 中，不直接动成员变量
    static void parseFileToMap(const std::string& filename, std::map<std::string, std::string>& targetMap,
                               bool optional);
    static void loadEnvToMap(const std::vector<std::string>& keys, std::map<std::string, std::string>& targetMap);
    bool validateMap(const std::map<std::string, std::string>& targetMap) const;

    static std::string trim(const std::string& str);
    static std::string unquote(const std::string& str);
    static bool parseBool(const std::string& str);

    mutable std::mutex _mutex;
    std::map<std::string, std::string> _configMap;

    // 状态记录用于 reload
    std::string _last_ini_file;
    std::string _last_env_file;
    LoadOptions _last_options;

    struct ValidatorInfo
    {
        Validator func;
        std::string error_message;
    };

    std::map<std::string, ValidatorInfo> _validators;
    static const std::vector<std::string>& getDefaultEnvKeys();
};

class ConfigException : public std::runtime_error
{
public:
    explicit ConfigException(const std::string& msg) : std::runtime_error(msg)
    {
    }
};
#endif //STREAMGATE_CONFIGLOADER_H
