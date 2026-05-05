#ifndef GODOTPP_NET_MANAGER_H
#define GODOTPP_NET_MANAGER_H

#include <linking_context.h>

#include "snl.h"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/label.hpp>

#include <deque>
#include <map>

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

        uint32_t local_net_id = 0; // The NetID of the local player


        struct PositionSnapshot {
            uint32_t sequence;
            int16_t x, y;
            float aim_x, aim_y;
            uint64_t timestamp;
        };

        std::map<NetID, std::deque<PositionSnapshot>> position_history;

        struct InterpolationState {
            Vector2 start_pos;
            Vector2 target_pos;
            uint64_t start_time;
            uint64_t duration = 100; // ms
            bool active = false;
        };

        std::map<NetID, InterpolationState> interp_states;

        std::deque<uint64_t> latencies;
        uint64_t average_rtt = 0;
        const int MAX_LATENCIES = 10;

        struct LocalPositionSnapshot {
            uint32_t sequence;
            float x, y;
        };

        std::deque<LocalPositionSnapshot> local_position_history;

        struct CorrectionState {
            Vector2 target;
            Vector2 velocity;
            bool active = false;
        };

        CorrectionState correction_state;

    public:
        NetworkManager();
        ~NetworkManager();

        void _ready() override;

        void _process(double delta) override;

        void _physics_process(double delta) override;

        void send_input_packet();
        void update_input_state();
        void send_ping_request();
        void handle_ping_response(PingResponse* response);

    protected:
        static void _bind_methods();
    };
}

#endif //GODOTPP_NET_MANAGER_H