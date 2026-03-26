#ifndef GODOTPP_NET_PROTOCOL_H
#define GODOTPP_NET_PROTOCOL_H

#include <cstdint>

using NetID = uint32_t;
using TypeID = uint32_t;

enum class PacketType : uint8_t
{
    SPAWN = 1,
    HELLO = 2,
    INPUT = 3,
    ASSIGN_ID = 4,
    PING = 5,
    PONG = 6,
    POSITION_UPDATE = 7
};


#pragma pack(push, 1)
struct InputFrame
{
    uint32_t sequence;
    uint8_t keys;       // Bitfield: bit0=up, bit1=down, bit2=left, bit3=right, bit4=action
    float aim_x;
    float aim_y;
};
#pragma pack(pop)


#pragma pack(push, 1)
struct InputPacket
{
    PacketType type;
    uint32_t client_id;
    InputFrame input_history[20];
};
#pragma pack(pop)

#pragma pack(push, 1)
struct SpawnPacket
{
    PacketType type; // 8 bits
    NetID netID; // 32 bits
    TypeID typeID; // 32 bits
    int16_t x; // 16 bits
    int16_t y; // 16 bits
};
#pragma pack(pop)

#pragma pack(push, 1)
struct HelloPacket
{
    PacketType type; // 8 bits
    int16_t x; // 16 bits
    int16_t y; // 16 bits
};
#pragma pack(pop)

#pragma pack(push, 1)
struct AssignIDPacket
{
    PacketType type;    // 8 bits
    uint32_t client_id; // 32 bits
};
#pragma pack(pop)

#pragma pack(push, 1)
struct PingRequest
{
    PacketType type;  // PING = 5
    uint32_t ping_id;
    uint64_t t0;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct PingResponse
{
    PacketType type;  // PONG = 6
    uint32_t ping_id;
    uint64_t t0;
    uint64_t t1;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct PositionUpdatePacket
{
    PacketType type;  // POSITION_UPDATE = 7
    NetID netID;
    int16_t x;
    int16_t y;
    float aim_x;
    float aim_y;
};
#pragma pack(pop)

#endif //GODOTPP_NET_PROTOCOL_H