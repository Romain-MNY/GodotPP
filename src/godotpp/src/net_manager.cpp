#include "net_manager.h"

#include <random>
#include <cstring>
#include <chrono>
#include <algorithm>
#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/viewport.hpp>

static uint64_t get_time_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

godot::NetworkManager::NetworkManager() {}

godot::NetworkManager::~NetworkManager() {}

void godot::NetworkManager::_ready()
{
    Node::_ready();

    socket = net_socket_create("127.0.0.1:0");

    if (socket) {
        // Send hello packet with x y
        HelloPacket packet;
        packet.type = PacketType::HELLO;
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> distrib(-512, 512);
        packet.x = distrib(gen);
        packet.y = distrib(gen) / 2;

        UtilityFunctions::print("[CLIENT] Send hello at: ", packet.x, ", ", packet.y);
        net_socket_send(socket, server_address, (uint8_t*)&packet, sizeof(HelloPacket));
    } else UtilityFunctions::print("[CLIENT] Socket could not be created");

    linking_context = LinkingContext();
    linking_context.register_type(1, []() -> Node*
    {
        Ref<PackedScene> player_scene = ResourceLoader::get_singleton()->load("res://player.tscn");
        return player_scene->instantiate();
    });
}

void godot::NetworkManager::_process(double delta)
{
    Node::_process(delta);

    time_since_last_ping += delta;
    if (time_since_last_ping >= ping_interval)
    {
        send_ping_request();
        time_since_last_ping = 0.0;
    }

    // Process all available packets
    while (true) {
        int32_t bytes_read = net_socket_poll(socket, read_buffer, 1024, sender_address, 128);
        if (bytes_read <= 0)
        {
            break;  // No more packets available
        }

        PacketType packet_type = (PacketType)read_buffer[0];

        if (packet_type == PacketType::ASSIGN_ID)
        {
            AssignIDPacket* assign_packet = reinterpret_cast<AssignIDPacket*>(read_buffer);
            if (bytes_read >= sizeof(AssignIDPacket))
            {
                client_id = assign_packet->client_id;
                local_net_id = assign_packet->net_id;
                UtilityFunctions::print("[CLIENT] Assigned client ID: ", client_id, ", local NetID: ", local_net_id);
            }
        }
        else if (packet_type == PacketType::PONG)
        {
            PingResponse* pong = reinterpret_cast<PingResponse*>(read_buffer);
            if (bytes_read >= sizeof(PingResponse))
            {
                handle_ping_response(pong);
            }
        }
        else if (packet_type == PacketType::SPAWN)
        {
            SpawnPacket* packet = reinterpret_cast<SpawnPacket*>(read_buffer);
            if (bytes_read >= sizeof(SpawnPacket))
            {
                Node* spawned_node = linking_context.spawn_network_object(packet->netID, packet->typeID);
                if (spawned_node)
                {
                    add_child(spawned_node);
                    Node2D* spawned_node_2d = dynamic_cast<Node2D*>(spawned_node);
                    if (spawned_node_2d != nullptr) {
                        spawned_node_2d->set_position(Vector2(packet->x, packet->y));
                    }
                    UtilityFunctions::print("[CLIENT] Spawned ID: ", packet->netID, " at: ", packet->x, ", ", packet->y);
                }
            }
        }
        else if (packet_type == PacketType::POSITION_UPDATE)
        {
            PositionUpdatePacket* pos_update = reinterpret_cast<PositionUpdatePacket*>(read_buffer);
            if (bytes_read >= sizeof(PositionUpdatePacket))
            {
                Node* node_to_update = linking_context.get_node(pos_update->netID);
                if (node_to_update)
                {
                    Node2D* node_2d = dynamic_cast<Node2D*>(node_to_update);
                    if (node_2d)
                    {
                        Vector2 server_pos = Vector2(pos_update->x, pos_update->y);
                        UtilityFunctions::print("[CLIENT] Received position update for NetID ", pos_update->netID, ", local_net_id is ", local_net_id);
                        if (pos_update->netID == local_net_id) {
                            node_2d->set_position(server_pos);
                            UtilityFunctions::print("[CLIENT] Local player moved to: (", pos_update->x, ", ", pos_update->y, ")");
                        } else {
                            PositionSnapshot snapshot;
                            snapshot.sequence = pos_update->sequence;
                            snapshot.x = pos_update->x;
                            snapshot.y = pos_update->y;
                            snapshot.aim_x = pos_update->aim_x;
                            snapshot.aim_y = pos_update->aim_y;
                            snapshot.timestamp = get_time_ms();

                            auto& history = position_history[pos_update->netID];

                            if (!history.empty() && snapshot.sequence > history.back().sequence + 1) {
                                uint32_t missing_start = history.back().sequence + 1;
                                uint32_t missing_end = snapshot.sequence - 1;
                                for (uint32_t seq = missing_start; seq <= missing_end; ++seq) {
                                    float t = static_cast<float>(seq - history.back().sequence) / (snapshot.sequence - history.back().sequence);
                                    PositionSnapshot interp;
                                    interp.sequence = seq;
                                    interp.x = history.back().x + (snapshot.x - history.back().x) * t;
                                    interp.y = history.back().y + (snapshot.y - history.back().y) * t;
                                    interp.aim_x = history.back().aim_x + (snapshot.aim_x - history.back().aim_x) * t;
                                    interp.aim_y = history.back().aim_y + (snapshot.aim_y - history.back().aim_y) * t;
                                    interp.timestamp = history.back().timestamp + (snapshot.timestamp - history.back().timestamp) * t;
                                    history.push_back(interp);
                                    if (history.size() > 10) history.pop_front();
                                }
                            }

                            history.push_back(snapshot);
                            if (history.size() > 10) {
                                history.pop_front();
                            }

                            // Start interpolation
                            auto& state = interp_states[pos_update->netID];
                            Vector2 current_pos = node_2d->get_position();
                            state.start_pos = current_pos;
                            state.target_pos = Vector2(pos_update->x, pos_update->y);
                            state.start_time = get_time_ms();
                            state.duration = 100; // Fixed duration for interpolation
                            state.active = true;
                        }
                    }
                }
                else
                {
                    UtilityFunctions::print("[CLIENT] Could not find node with NetID ", pos_update->netID);
                }
            }
        }
        else
        {
            UtilityFunctions::print("[CLIENT] Packet not of type SPAWN ", (uint8_t)packet_type);
        }
    }

    // Interpolate positions for remote players
    uint64_t current_time = get_time_ms();
    for (auto& pair : interp_states) {
        NetID netID = pair.first;
        auto& state = pair.second;
        if (!state.active) continue;

        Node* node = linking_context.get_node(netID);
        if (!node) continue;
        Node2D* node2d = dynamic_cast<Node2D*>(node);
        if (!node2d) continue;

        uint64_t elapsed = current_time - state.start_time;
        if (elapsed >= state.duration) {
            node2d->set_position(state.target_pos);
            state.active = false;
        } else {
            float factor = static_cast<float>(elapsed) / state.duration;
            Vector2 pos = state.start_pos + (state.target_pos - state.start_pos) * factor;
            node2d->set_position(pos);
        }
    }

    // Update RTT label
    Label* rtt_label = nullptr;
    if (has_node("RTTLabel")) {
        rtt_label = get_node<Label>("RTTLabel");
    }
    if (rtt_label) {
        rtt_label->set_text(String("RTT: ") + String::num(average_rtt) + " ms");
    }
}

void godot::NetworkManager::_physics_process(double delta)
{
    Node::_physics_process(delta);

    update_input_state();
    send_input_packet();
}

void godot::NetworkManager::_bind_methods() {}

void godot::NetworkManager::update_input_state()
{
    // Capture current input state
    Input* input = Input::get_singleton();

    uint8_t keys = 0;
    if (input->is_action_pressed("ui_up")) keys |= (1 << 0);      // bit0 = up
    if (input->is_action_pressed("ui_down")) keys |= (1 << 1);    // bit1 = down
    if (input->is_action_pressed("ui_left")) keys |= (1 << 2);    // bit2 = left
    if (input->is_action_pressed("ui_right")) keys |= (1 << 3);   // bit3 = right
    if (input->is_action_pressed("ui_accept")) keys |= (1 << 4);  // bit4 = action

    // Shift history to make room for new input
    for (int i = 19; i > 0; --i) {
        input_history[i] = input_history[i - 1];
    }

    // Get mouse position for aiming
    Viewport* viewport = get_viewport();
    Vector2 mouse_pos = viewport->get_mouse_position();

    // Add new input at the front
    input_history[0].sequence = input_sequence;
    input_history[0].keys = keys;
    input_history[0].aim_x = mouse_pos.x;
    input_history[0].aim_y = mouse_pos.y;

    ++input_sequence;
}

void godot::NetworkManager::send_input_packet()
{
    if (!socket) return;

    // Create and send input packet
    InputPacket packet;
    packet.type = PacketType::INPUT;
    packet.client_id = client_id;

    // Copy input history
    std::memcpy(packet.input_history, input_history, sizeof(input_history));

    net_socket_send(socket, server_address, (uint8_t*)&packet, sizeof(InputPacket));
}

void godot::NetworkManager::send_ping_request()
{
    if (!socket) return;

    PingRequest ping;
    ping.type = PacketType::PING;
    ping.ping_id = ping_id++;
    ping.t0 = get_time_ms();

    net_socket_send(socket, server_address, (uint8_t*)&ping, sizeof(PingRequest));
}

void godot::NetworkManager::handle_ping_response(PingResponse* response)
{
    uint64_t t2 = get_time_ms();

    latency_ms = t2 - response->t0;

    latencies.push_back(latency_ms);
    if (latencies.size() > MAX_LATENCIES) {
        latencies.pop_front();
    }

    uint64_t sum = 0;
    for (auto l : latencies) {
        sum += l;
    }
    average_rtt = latencies.empty() ? 0 : sum / latencies.size();

    UtilityFunctions::print("[CLIENT] Client ", client_id, " Ping #", response->ping_id, " - RTT = ", latency_ms, " ms, Average = ", average_rtt, " ms");
}
