#include <iostream>
#include <snl.h>
#include <entt/entt.hpp>

#include "snl.h"

int main()
{
    std::cout << "Hello, World!" << std::endl;

    GameSocket* socket = net_socket_create("127.0.0.1:5000");

    uint8_t readbuffer[1024];
    char sender_adresse[128];


    while (true)
    {
        int32_t bytes_read = net_socket_poll(socket, readbuffer, 1024, sender_adresse, 128);
        if (bytes_read > 0)
        {
            std::cout << "packet received" << std::endl << bytes_read << std::endl;
        }
    }

    return 0;
}
