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

    update_input_state();
    send_input_packet();

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

        UtilityFunctions::print("[CLIENT] Packet of type ", (uint8_t)packet_type);
        if (packet_type == PacketType::ASSIGN_ID)
        {
            AssignIDPacket* assign_packet = reinterpret_cast<AssignIDPacket*>(read_buffer);
            if (bytes_read >= sizeof(AssignIDPacket))
            {
                client_id = assign_packet->client_id;
                UtilityFunctions::print("[CLIENT] Assigned client ID: ", client_id);
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
                // Find the node with this NetID and update its position
                Node* node_to_update = linking_context.get_node(pos_update->netID);
                if (node_to_update)
                {
                    Node2D* node_2d = dynamic_cast<Node2D*>(node_to_update);
                    if (node_2d)
                    {
                        Vector2 server_pos = Vector2(pos_update->x, pos_update->y);

                        // Check if this is the local player (our client_id matches the NetID)
                        if (pos_update->netID == client_id) {
                            // This is our local player - reconcile prediction
                            if (has_predicted_position) {
                                Vector2 prediction_error = server_pos - predicted_position;
                                float error_distance = prediction_error.length();

                                if (error_distance > 0.1f) {  // Significant error
                                    // Correct the position
                                    node_2d->set_position(server_pos);
                                    predicted_position = server_pos;

                                    UtilityFunctions::print("[CLIENT] Corrected local prediction error: ", error_distance,
                                                           " pixels. Server pos: (", server_pos.x, ", ", server_pos.y, ")");
                                } else {
                                    // Prediction was good, just update our reference
                                    predicted_position = server_pos;
                                }
                            } else {
                                // No prediction yet, just set position
                                node_2d->set_position(server_pos);
                                predicted_position = server_pos;
                                has_predicted_position = true;
                            }
                        } else {
                            // This is another player - just update position directly
                            node_2d->set_position(server_pos);
                            UtilityFunctions::print("[CLIENT] Updated remote NetID ", pos_update->netID,
                                                   " position to: (", pos_update->x, ", ", pos_update->y, ")");
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

    // Apply client-side prediction for local player movement
    if (client_id != 0) {  // Only if we have a client ID
        Node* local_player = linking_context.get_node(client_id);
        if (local_player) {
            Node2D* local_player_2d = dynamic_cast<Node2D*>(local_player);
            if (local_player_2d) {
                // Get current position (either predicted or server position)
                Vector2 current_pos = has_predicted_position ? predicted_position : local_player_2d->get_position();

                // Apply movement prediction
                const int16_t MOVE_SPEED = 5;  // Must match server speed
                Vector2 new_pos = current_pos;

                if (keys & (1 << 0)) new_pos.y -= MOVE_SPEED;  // UP
                if (keys & (1 << 1)) new_pos.y += MOVE_SPEED;  // DOWN
                if (keys & (1 << 2)) new_pos.x -= MOVE_SPEED;  // LEFT
                if (keys & (1 << 3)) new_pos.x += MOVE_SPEED;  // RIGHT

                // Only update if position actually changed
                if (new_pos != current_pos) {
                    local_player_2d->set_position(new_pos);
                    predicted_position = new_pos;
                    has_predicted_position = true;

                    UtilityFunctions::print("[CLIENT] Predicted local movement to: (", new_pos.x, ", ", new_pos.y, ")");
                }
            }
        }
    }

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
    uint64_t t2 = get_time_ms();  // Current time (reception)

    // Calculate RTT: t2 (reception) - t0 (transmission)
    latency_ms = t2 - response->t0;

    UtilityFunctions::print("[CLIENT] Ping #", response->ping_id, " - RTT = ", latency_ms, " ms");
}
