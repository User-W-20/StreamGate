//
// Created by wxx on 2025/12/19.
//

#ifndef STREAMGATE_ENUMTOSTRING_H
#define STREAMGATE_ENUMTOSTRING_H
#include "StreamTask.h"
#include <string>

struct EnumToString
{
    // StreamType
    static std::string to_string(StreamType type);
    static StreamType from_string(StreamType, const std::string& s);

    // StreamState
    static std::string to_string(StreamState state);
    static StreamState from_string(StreamState, const std::string& s);

    // StreamProtocol
    static std::string to_string(StreamProtocol proto);
    static StreamProtocol from_string(StreamProtocol, std::string s);
};
#endif //STREAMGATE_ENUMTOSTRING_H
