/**
 * @file main.cpp
 * @author Mit Bailey (mitbailey99@gmail.com)
 * @brief 
 * @version See Git tags for version information.
 * @date 2021.08.04
 * 
 * @copyright Copyright (c) 2021
 * 
 */

#include <pthread.h>
#include <unistd.h>
#include "txmodem.h"
#include "rxmodem.h"
#include "gs_xband.hpp"
#include "meb_debug.hpp"

// TODO: libiio.h has configuration functions

int main(int argc, char **argv)
{

    // Set up global data.
    global_data_t global_data[1] = {0};
    // global_data->network_data = new NetworkData();
    network_data_init(global_data->network_data);
    global_data->network_data->rx_active = true;

    // 1 = All good, 0 = recoverable failure, -1 = fatal failure (close program)
    global_data->thread_status = 1;

    // Create Ground Station Network thread IDs.
    pthread_t net_polling_tid, net_rx_tid;

    // Start the RX threads, and restart them should it be necessary.
    // Only gets-out if a thread declares an unrecoverable emergency and sets its status to -1.
    while (global_data->thread_status > -1)
    {
        // Initialize and begin socket communication to the server.
        if (!global_data->network_data->connection_ready)
        {
            // TODO: Check if this is the desired functionality.
            // Currently, the program will not proceed past this point if it cannot connect to the server.
            // TODO: This probably means that losing connection to the server is a recoverable error (threads should set their statuses to 0).
            while (gs_connect_to_server(global_data) != 1)
            {
                dbprintlf(RED_FG "Failed to establish connection to the server.");
                usleep(5 SEC);
            }
        }

        // Start the threads.
        pthread_create(&net_polling_tid, NULL, gs_polling_thread, global_data);
        pthread_create(&net_rx_tid, NULL, gs_network_rx_thread, global_data);

        // Initialize txmodem ID.
        int txmodem_id = 0;
        int txdma_id = 0;

        while (!global_data->tx_ready)
        {
            if (txmodem_init(global_data->tx_modem, txmodem_id, txdma_id) > 0)
            {
                global_data->tx_ready = true;
            }
            else
            {
                dbprintlf(RED_FG "X-Band radio initialization failed!");
                usleep(5 SEC);
            }
        }

        void *thread_return;
        pthread_join(net_polling_tid, &thread_return);
        pthread_join(net_rx_tid, &thread_return);

        // Loop will begin, restarting the threads.
    }

    // Finished.
    // delete global_data->network_data;

    return 1;
}