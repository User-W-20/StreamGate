//
// Created by X on 2025/11/18.
//

#ifndef STREAMGATE_CONFIGLOADER_H
#define STREAMGATE_CONFIGLOADER_H

#include <string>
#include <map>
#include <stdexcept>
#include <algorithm>

class ConfigLoader
{
public:
    static ConfigLoader& instance();

    void load(const std::string& filename);

    [[nodiscard]] std::string getString(const std::string& key) const;

    [[nodiscard]] int getInt(const std::string& key) const;

    [[nodiscard]] int getInt(const std::string& key, int defaultValue) const;

    [[nodiscard]] bool has(const std::string& key) const;

    ConfigLoader(const ConfigLoader&) = delete;
    ConfigLoader& operator=(const ConfigLoader&) = delete;

private:
    ConfigLoader();
    ~ConfigLoader() = default;

    std::map<std::string, std::string> _configMap;

    void parseFile(const std::string& filename);
};
#endif //STREAMGATE_CONFIGLOADER_H