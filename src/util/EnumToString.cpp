//
// Created by wxx on 2025/12/19.
//
#include "EnumToString.h"

std::string EnumToString::to_string(StreamType type)
{
    switch (type)
    {
    case StreamType::PUBLISHER:
        return "PUBLISHER";
    case StreamType::PLAYER:
    default:
        return "PLAYER";
    }
}

StreamType EnumToString::from_string(StreamType, const std::string& s)
{
    if (s == "PUBLISHER") return StreamType::PUBLISHER;
    return StreamType::PLAYER;
}

std::string EnumToString::to_string(StreamState state)
{
    switch (state)
    {
    case StreamState::INITIALIZING: return "INITIALIZING";
    case StreamState::ACTIVE: return "ACTIVE";
    case StreamState::INACTIVE: return "INACTIVE";
    case StreamState::ERROR: return "ERROR";
    case StreamState::CLOSED: return "CLOSED";
    default: return "UNKNOWN";
    }
}

StreamState EnumToString::from_string(StreamState, const std::string& s)
{
    if (s == "ACTIVE") return StreamState::ACTIVE;
    if (s == "INACTIVE") return StreamState::INACTIVE;
    if (s == "ERROR") return StreamState::ERROR;
    if (s == "CLOSED") return StreamState::CLOSED;
    return StreamState::INITIALIZING;
}

std::string EnumToString::to_string(StreamProtocol proto)
{
    switch (proto)
    {
    case StreamProtocol::RTMP: return "RTMP";
    case StreamProtocol::RTSP: return "RTSP";
    case StreamProtocol::HLS: return "HLS";
    case StreamProtocol::HTTP_FLV: return "HTTP_FLV";
    case StreamProtocol::HTTP_TS: return "HTTP_TS";
    case StreamProtocol::HTTP_FMP4: return "HTTP_FMP4";
    case StreamProtocol::WebRTC: return "WEBRTC";
    case StreamProtocol::SRT: return "SRT";
    case StreamProtocol::Unknown:
    default: return "UNKNOWN";
    }
}

StreamProtocol EnumToString::from_string(StreamProtocol, std::string s)
{
    std::ranges::transform(s, s.begin(), ::toupper);

    if (s == "RTMP") return StreamProtocol::RTMP;
    if (s == "RTSP") return StreamProtocol::RTSP;
    if (s == "HLS") return StreamProtocol::HLS;
    if (s == "HTTP_FLV") return StreamProtocol::HTTP_FLV;
    if (s == "HTTP_TS" || s == "HTTP-TS") return StreamProtocol::HTTP_TS;
    if (s == "HTTP_FMP4" || s == "HTTP-FMP4") return StreamProtocol::HTTP_FMP4;
    if (s == "WEBRTC") return StreamProtocol::WebRTC;
    if (s == "SRT") return StreamProtocol::SRT;
    return StreamProtocol::Unknown;
}
