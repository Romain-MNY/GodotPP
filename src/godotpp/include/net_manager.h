#ifndef GODOTPP_NET_MANAGER_H
#define GODOTPP_NET_MANAGER_H

#include <linking_context.h>

#include "snl.h"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/packed_scene.hpp>

namespace godot {
    class NetworkManager : public Node {
        GDCLASS(NetworkManager, Node)

    protected:
        const char* server_address = "127.0.0.1:5000";
        GameSocket *socket;

        LinkingContext linking_context;

        uint8_t read_buffer[1024];
        char sender_address[128];

        uint32_t input_sequence = 0;
        InputFrame input_history[20] = {};
        uint32_t client_id = 0;
        uint32_t ping_id = 0;
        uint64_t latency_ms = 0;
        double ping_interval = 1.0;
        double time_since_last_ping = 0.0;

        Vector2 predicted_position;
        bool has_predicted_position = false;

    public:
        NetworkManager();
        ~NetworkManager();

        void _ready() override;

        void _process(double delta) override;

        void send_input_packet();
        void update_input_state();
        void send_ping_request();
        void handle_ping_response(PingResponse* response);

    protected:
        static void _bind_methods();
    };
}

#endif //GODOTPP_NET_MANAGER_H