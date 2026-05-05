#include <iostream>
#include <snl.h>
#include <vector>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <thread>

#include "../../shared/include/net_protocol.h"


static uint64_t get_time_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

struct Client
{
    char address[128];
    uint32_t id;
};

struct PlayerObject
{
    NetID netID;
    int16_t x;
    int16_t y;
    uint32_t client_id;           // Client ID
    uint32_t last_input_sequence; // Last received input sequence number
    float aim_x;  // Mouse aim position
    float aim_y;  // Mouse aim position
};

struct PendingInput {
    InputPacket packet;
    uint64_t receive_time;
    char sender_address[128];
};

int main() {
    std::cout << "Init server" << std::endl;

    GameSocket* socket = net_socket_create("0.0.0.0:5000");
    std::cout << "Listening to port 5000" << std::endl;

    std::vector<Client> clients;
    uint32_t next_userID = 101;

    std::vector<PlayerObject> player_objects;
    std::vector<PendingInput> pending_inputs;
    uint32_t next_netID = 101;

    uint32_t position_update_sequence = 0;

    uint8_t read_buffer[2048];
    char sender_address[128];

    // Frame rate limiting (** FPS)
    const uint64_t FRAME_TIME_MS = 1000 / 30;
    const uint64_t INPUT_DELAY_MS = 100;  // Delay for input
    auto frame_start = std::chrono::high_resolution_clock::now();

    while (true) {
        // Process all available packets
        while (true) {
            int32_t bytes_read = net_socket_poll(socket, read_buffer, 1024, sender_address, 128);
            if (bytes_read <= 0)
            {
                break;
            }

            PacketType packet_type = (PacketType)read_buffer[0];
            HelloPacket* hello_packet = reinterpret_cast<HelloPacket*>(read_buffer);

            if (packet_type == PacketType::HELLO) {
                auto it = std::find_if(clients.begin(), clients.end(), [&](const Client& c)
                {
                   if (std::strncmp(sender_address, c.address, 128) == 0)
                   {
                       return true;
                   } else return false;
                });

                bool is_new_client = (it == clients.end());

                if (is_new_client)
                {
                    std::cout << "[SERVER] New connection from " << sender_address << std::endl;

                    Client new_client;
                    memcpy(new_client.address, sender_address, sizeof(new_client.address));
                    new_client.id = next_userID;
                    clients.push_back(new_client);

                    std::cout << "[SERVER] Spawn Node ID " << next_netID << " at " << hello_packet->x << " " << hello_packet->y << std::endl;

                    PlayerObject new_player_object;
                    new_player_object.x = hello_packet->x;
                    new_player_object.y = hello_packet->y;
                    new_player_object.netID = next_netID;
                    new_player_object.client_id = next_userID;
                    new_player_object.last_input_sequence = 0;
                    player_objects.push_back(new_player_object);


                    AssignIDPacket assign_packet;
                    assign_packet.type = PacketType::ASSIGN_ID;
                    assign_packet.client_id = next_userID;
                    assign_packet.net_id = next_netID;
                    net_socket_send(socket, sender_address, (uint8_t*)&assign_packet, sizeof(AssignIDPacket));

                    SpawnPacket packet;
                    packet.type = PacketType::SPAWN;
                    packet.netID = next_netID;
                    packet.typeID = 1;
                    packet.x = hello_packet->x;
                    packet.y = hello_packet->y;

                    for (const auto& client : clients)
                    {
                        net_socket_send(socket, client.address, (uint8_t*)&packet, sizeof(SpawnPacket));
                    }

                    ++next_userID;
                    ++next_netID;

                    for (const auto& player_object : player_objects)
                    {
                        if (player_object.netID != packet.netID)
                        {
                            std::cout << "[SERVER] Sending previously connected client to new client" << std::endl;

                            SpawnPacket new_packet;
                            new_packet.type = PacketType::SPAWN;
                            new_packet.netID = player_object.netID;
                            new_packet.typeID = 1;
                            new_packet.x = player_object.x;
                            new_packet.y = player_object.y;

                            net_socket_send(socket, sender_address, (uint8_t*)&new_packet, sizeof(SpawnPacket));
                        } else std::cout << "[SERVER] This netID is the new client player" << std::endl;
                    }
                }
                else
                {
                    std::cout << "[SERVER] Old connection from " << sender_address << std::endl;
                }
            }
            else if (packet_type == PacketType::INPUT)
            {
                if (bytes_read >= sizeof(InputPacket))
                {
                    PendingInput pi;
                    memcpy(&pi.packet, read_buffer, sizeof(InputPacket));
                    pi.receive_time = get_time_ms();
                    memcpy(pi.sender_address, sender_address, 128);
                    pending_inputs.push_back(pi);
                }
            }
            else if (packet_type == PacketType::PING)
            {
                if (bytes_read >= sizeof(PingRequest))
                {
                    PingRequest* ping_req = reinterpret_cast<PingRequest*>(read_buffer);

                    PingResponse pong;
                    pong.type = PacketType::PONG;
                    pong.ping_id = ping_req->ping_id;
                    pong.t0 = ping_req->t0;
                    pong.t1 = get_time_ms();

                    net_socket_send(socket, sender_address, (uint8_t*)&pong, sizeof(PingResponse));

                    std::cout << "[SERVER] Ping #" << ping_req->ping_id << " received from " << sender_address
                              << " - Responding with server timestamp" << std::endl;
                }
            }
        }

        uint64_t curr_time_ms = get_time_ms();
        for (auto it = pending_inputs.begin(); it != pending_inputs.end(); ) {
            if (curr_time_ms - it->receive_time >= INPUT_DELAY_MS) {
                InputPacket* input_packet = &it->packet;

                auto player_it = std::find_if(player_objects.begin(), player_objects.end(),
                    [&](const PlayerObject& p) { return p.client_id == input_packet->client_id; });

                if (player_it != player_objects.end()) {
                    bool position_changed = false;

                    for (int i = 19; i >= 0; --i) {
                        InputFrame& frame = input_packet->input_history[i];

                        if (frame.sequence > player_it->last_input_sequence) {
                            player_it->last_input_sequence = frame.sequence;

                            player_it->aim_x = frame.aim_x;
                            player_it->aim_y = frame.aim_y;

                            uint8_t keys = frame.keys;
                            int16_t old_x = player_it->x;
                            int16_t old_y = player_it->y;

                            const int16_t MOVE_SPEED = 5;

                            if (keys & (1 << 0)) player_it->y -= MOVE_SPEED;
                            if (keys & (1 << 1)) player_it->y += MOVE_SPEED;
                            if (keys & (1 << 2)) player_it->x -= MOVE_SPEED;
                            if (keys & (1 << 3)) player_it->x += MOVE_SPEED;

                            if (player_it->x != old_x || player_it->y != old_y) {
                                position_changed = true;
                            }
                        }
                    }

                    if (position_changed) {
                        PositionUpdatePacket pos_update;
                        pos_update.type = PacketType::POSITION_UPDATE;
                        pos_update.netID = player_it->netID;
                        pos_update.sequence = position_update_sequence++;
                        pos_update.x = player_it->x;
                        pos_update.y = player_it->y;
                        pos_update.aim_x = player_it->aim_x;
                        pos_update.aim_y = player_it->aim_y;

                        for (const auto& client : clients) {
                            net_socket_send(socket, client.address, (uint8_t*)&pos_update, sizeof(PositionUpdatePacket));
                        }
                    }
                }

                it = pending_inputs.erase(it);
            } else {
                ++it;
            }
        }

        // Frame rate limiting (60 FPS)
        auto current_time = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - frame_start).count();

        if (elapsed < FRAME_TIME_MS) {
            std::this_thread::sleep_for(std::chrono::milliseconds(FRAME_TIME_MS - elapsed));
            frame_start = std::chrono::high_resolution_clock::now();
        } else {
            frame_start = current_time;
        }
    }

    return 0;
}
