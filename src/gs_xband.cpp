/**
 * @file gs_xband.cpp
 * @author Mit Bailey (mitbailey99@gmail.com)
 * @brief 
 * @version See Git tags for version information.
 * @date 2021.08.04
 * 
 * @copyright Copyright (c) 2021
 * 
 */

#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
// #include "libiio.h"
#include "gs_xband.hpp"
#include "meb_debug.hpp"

int gs_xband_init(global_data_t *global)
{
    if (global->tx_modem_ready && global->radio_ready)
    {
        dbprintlf(YELLOW_FG "TX modem and radio marked as ready, but gs_xband_init(...) was called anyway. Canceling redundant initialization.");
        return -1;
    }

    if (!global->tx_modem_ready)
    {
        if (txmodem_init(global->tx_modem, uio_get_id("tx_ipcore"), uio_get_id("rx_dma")) < 0)
        {
            dbprintlf(RED_FG "TX modem initialization failure.");
            return -1;
        }
        dbprintlf(GREEN_FG "TX modem initialized.");
        global->tx_modem_ready = true;
    }

    if (!global->radio_ready)
    {
        if (adradio_init(global->radio) < 0)
        {
            dbprintlf(RED_FG "Radio initialization failure.");
            return -3;
        }
        dbprintlf(GREEN_FG "Radio initialized.");
        global->radio_ready = true;
    }

    dbprintlf(GREEN_FG "Automatic initialization complete.");
    return 1;
}

int gs_xband_transmit(global_data_t *global, txmodem *dev, uint8_t *buf, ssize_t size)
{
    if (!global->tx_modem_ready || !global->radio_ready)
    {
        if (gs_xband_init(global) < 0)
        {
            dbprintlf(RED_FG "Transmission aborted, radio not initialized.");
            return -1;
        }
    }

    if (!global->PLL_ready)
    {
        dbprintlf(RED_FG "Transmission aborted, PLL not initialized by GUI client operator.");
        return -2;
    }

    dbprintlf(GREEN_FG "Transmitting to SPACE-HAUC...");
    return txmodem_write(dev, buf, size);
}

void *gs_network_rx_thread(void *args)
{
    global_data_t *global = (global_data_t *)args;
    NetDataClient *network_data = global->network_data;
    global->PLL->spi_bus = 0;
    global->PLL->spi_cs = 1;
    global->PLL->spi_cs_internal = 1;
    global->PLL->cs_gpio = -1;
    global->PLL->single = 1;
    global->PLL->muxval = 6;

    // Roof XBAND is a network client to the GS Server, and so should be very similar in socketry to ground_station.

    while (network_data->recv_active)
    {
        if (!network_data->connection_ready)
        {
            usleep(5 SEC);
            continue;
        }

        int read_size = 0;

        while (read_size >= 0 && network_data->recv_active)
        {
            char buffer[sizeof(NetFrame) * 2];
            memset(buffer, 0x0, sizeof(buffer));

            dbprintlf(BLUE_BG "Waiting to receive...");
            read_size = recv(network_data->socket, buffer, sizeof(buffer), 0);
            dbprintlf("Read %d bytes.", read_size);

            if (read_size > 0)
            {
                dbprintf("RECEIVED (hex): ");
                for (int i = 0; i < read_size; i++)
                {
                    printf("%02x", buffer[i]);
                }
                printf("(END)\n");

                // Parse the data by mapping it to a NetworkFrame.
                NetFrame *network_frame = (NetFrame *)buffer;

                // Check if we've received data in the form of a NetworkFrame.
                if (network_frame->validate() < 0)
                {
                    dbprintlf("Integrity check failed (%d).", network_frame->validate());
                    continue;
                }
                dbprintlf("Integrity check successful.");

                global->netstat = network_frame->getNetstat();

                // For now, just print the Netstat.
                uint8_t netstat = network_frame->getNetstat();
                dbprintlf(BLUE_FG "NETWORK STATUS (%d)", netstat);
                dbprintf("GUI Client ----- ");
                ((netstat & 0x80) == 0x80) ? printf(GREEN_FG "ONLINE" RESET_ALL "\n") : printf(RED_FG "OFFLINE" RESET_ALL "\n");
                dbprintf("Roof UHF ------- ");
                ((netstat & 0x40) == 0x40) ? printf(GREEN_FG "ONLINE" RESET_ALL "\n") : printf(RED_FG "OFFLINE" RESET_ALL "\n");
                dbprintf("Roof X-Band ---- ");
                ((netstat & 0x20) == 0x20) ? printf(GREEN_FG "ONLINE" RESET_ALL "\n") : printf(RED_FG "OFFLINE" RESET_ALL "\n");
                dbprintf("Haystack ------- ");
                ((netstat & 0x10) == 0x10) ? printf(GREEN_FG "ONLINE" RESET_ALL "\n") : printf(RED_FG "OFFLINE" RESET_ALL "\n");
                dbprintf("Track ---------- ");
                ((netstat & 0x8) == 0x8) ? printf(GREEN_FG "ONLINE" RESET_ALL "\n") : printf(RED_FG "OFFLINE" RESET_ALL "\n");

                // Extract the payload into a buffer.
                int payload_size = network_frame->getPayloadSize();
                unsigned char *payload = (unsigned char *)malloc(payload_size);
                if (network_frame->retrievePayload(payload, payload_size) < 0)
                {
                    dbprintlf(RED_FG "Error retrieving data.");
                    continue;
                }

                switch (network_frame->getType())
                {
                case NetType::XBAND_CONFIG:
                {
                    dbprintlf(BLUE_FG "Received an X-Band CONFIG frame!");
                    if (!global->radio_ready)
                    {
                        // TODO: Send a packet indicating this.
                        dbprintlf(RED_FG "Cannot configure radio: radio not ready, does not exist, or failed to initialize.");
                        break;
                    }
                    if (network_frame->getDestination() == NetVertex::ROOFXBAND)
                    {
                        phy_config_t *config = (phy_config_t *)payload;

                        // RECONFIGURE XBAND
                        adradio_set_ensm_mode(global->radio, (ensm_mode)config->mode);
                        adradio_set_tx_lo(global->radio, config->LO);
                        adradio_set_samp(global->radio, config->samp);
                        adradio_set_tx_bw(global->radio, config->bw);
                        char filter_name[256];
                        // TODO: Keep track of the return value of the load filter thing in the status.
                        snprintf(filter_name, sizeof(filter_name), "/home/sunip/%s.ftr", config->ftr_name);
                        adradio_load_fir(global->radio, filter_name);
                        adradio_set_tx_hardwaregain(global->radio, config->gain);
                    }
                    else
                    {
                        dbprintlf(YELLOW_FG "Incorrectly received a configuration for Haystack.");
                    }
                    break;
                }
                case NetType::XBAND_COMMAND:
                {
                    dbprintlf(BLUE_FG "Received an X-Band Radio command frame!");
                    XBAND_COMMAND *command = (XBAND_COMMAND *)payload;

                    switch (*command)
                    {
                    case XBC_INIT_PLL:
                    {
                        dbprintlf("Received PLL Initialize command.");
                        if (global->PLL_ready)
                        {
                            dbprintlf(YELLOW_FG "PLL already initialized, canceling.");
                            break;
                        }

                        if (adf4355_init(global->PLL) < 0)
                        {
                            dbprintlf(RED_FG "PLL initialization failure.");
                        }
                        else if (adf4355_set_tx(global->PLL) < 0)
                        {
                            dbprintlf(RED_FG "PLL set TX failure.");
                        }
                        else
                        {
                            dbprintlf(GREEN_FG "PLL initialization success.");
                            global->PLL_ready = true;
                        }
                        break;
                    }
                    case XBC_DISABLE_PLL:
                    {
                        dbprintlf("Received Disable PLL command.");
                        if (!global->PLL_ready)
                        {
                            dbprintlf(YELLOW_FG "PLL already disabled, canceling.");
                            break;
                        }

                        if (adf4355_pw_down(global->PLL) < 0)
                        {
                            dbprintlf(RED_FG "PLL shutdown failure.");
                        }
                        else
                        {
                            dbprintlf(GREEN_FG "PLL shutdown success.");
                            global->PLL_ready = false;
                        }
                        break;
                    }
                    case XBC_ARM_RX:
                    {
                        dbprintlf("Received Arm RX command.");
                        // Do nothing, not for us.
                        break;
                    }
                    case XBC_DISARM_RX:
                    {
                        dbprintlf("Received Disarm RX command.");
                        // Do nothing, not for us.
                        break;
                    }
                    }
                    break;
                }
                case NetType::DATA:
                {
                    dbprintlf(BLUE_FG "Received a DATA frame!");
                    int retval = gs_xband_transmit(global, global->tx_modem, payload, payload_size);
                    if (retval < 0)
                    {
                        // TODO: Send a packet notifying GUI client of a failed transmission. Status packet??
                        dbprintlf(RED_FG "Failed to transmit.");
                    }
                    break;
                }
                case NetType::ACK:
                {
                    dbprintlf(BLUE_FG "Received an ACK frame!");
                    break;
                }
                case NetType::NACK:
                {
                    dbprintlf(BLUE_FG "Received a NACK frame!");
                    break;
                }
                default:
                {
                    break;
                }
                }
                free(payload);
            }
            else
            {
                break;
            }
        }
        if (read_size == 0)
        {
            dbprintlf(RED_BG "Connection forcibly closed by the server.");
            strcpy(network_data->disconnect_reason, "SERVER-FORCED");
            network_data->connection_ready = false;
            continue;
        }
        else if (errno == EAGAIN)
        {
            dbprintlf(YELLOW_BG "Active connection timed-out (%d).", read_size);
            strcpy(network_data->disconnect_reason, "TIMED-OUT");
            network_data->connection_ready = false;
            continue;
        }
        else if (errno == ETIMEDOUT)
        {
            dbprintlf(FATAL "ETIMEDOUT");
            strcpy(network_data->disconnect_reason, "TIMED-OUT");
            network_data->connection_ready = false;
        }
        erprintlf(errno);
    }

    network_data->recv_active = false;
    dbprintlf(FATAL "DANGER! NETWORK RECEIVE THREAD IS RETURNING!");

    if (global->network_data->thread_status > 0)
    {
        global->network_data->thread_status = 0;
    }
    return NULL;
}

void *xband_status_thread(void *args)
{
    global_data_t *global = (global_data_t *)args;
    NetDataClient *network_data = global->network_data;

    while (network_data->recv_active && network_data->thread_status > 0)
    {
        if (!global->radio_ready)
        {
            dbprintlf(RED_FG "Cannot send radio config: radio not ready, does not exist, or failed to initialize.");
            continue;
        }

        if (network_data->connection_ready)
        {
            phy_status_t status[1];
            memset(status, 0x0, sizeof(phy_status_t));

            adradio_get_tx_bw(global->radio, (long long *)&status->bw);
            adradio_get_tx_hardwaregain(global->radio, &status->gain);
            adradio_get_tx_lo(global->radio, (long long *)&status->LO);
            adradio_get_rssi(global->radio, &status->rssi);
            adradio_get_samp(global->radio, (long long *)&status->samp);
            adradio_get_temp(global->radio, (long long *)&status->temp);

            char buf[32];
            memset(buf, 0x0, 32);
            adradio_get_ensm_mode(global->radio, buf, sizeof(buf));
            dbprintlf("Got ensm mode: %s", buf);
            if (strcmp(buf, "sleep") == 0)
            {
                status->mode = 0;
            }
            else if (strcmp(buf, "fdd") == 0)
            {
                status->mode = 1;
            }
            else if (strcmp(buf, "tdd") == 0)
            {
                status->mode = 2;
            }
            else
            {
                status->mode = -1;
            }
            status->modem_ready = global->tx_modem_ready;
            status->PLL_ready = global->PLL_ready;
            status->radio_ready = global->radio_ready;

            dbprintlf(GREEN_FG "Sending the following X-Band status data:");
            dbprintlf(GREEN_FG "mode %d", status->mode);
            dbprintlf(GREEN_FG "pll_freq %d", status->pll_freq);
            dbprintlf(GREEN_FG "LO %lld", status->LO);
            dbprintlf(GREEN_FG "samp %lld", status->samp);
            dbprintlf(GREEN_FG "bw %lld", status->bw);
            dbprintlf(GREEN_FG "ftr_name %s", status->ftr_name);
            dbprintlf(GREEN_FG "temp %lld", status->temp);
            dbprintlf(GREEN_FG "rssi %f", status->rssi);
            dbprintlf(GREEN_FG "gain %f", status->gain);
            // dbprintlf(GREEN_FG "curr_gainmode %s", status->curr_gainmode);
            dbprintlf(GREEN_FG "pll_lock %d", status->pll_lock);
            dbprintlf(GREEN_FG "modem_ready %d", status->modem_ready);
            dbprintlf(GREEN_FG "PLL_ready %d", status->PLL_ready);
            dbprintlf(GREEN_FG "radio_ready %d", status->radio_ready);
            dbprintlf(GREEN_FG "rx_armed %d", status->rx_armed);
            dbprintlf(GREEN_FG "MTU %d", status->MTU);

            NetFrame *status_frame = new NetFrame((unsigned char *)status, sizeof(phy_status_t), NetType::XBAND_DATA, NetVertex::CLIENT);
            status_frame->sendFrame(network_data);
            delete status_frame;
        }

        usleep(network_data->polling_rate SEC);
    }

    dbprintlf(FATAL "XBAND_STATUS_THREAD IS EXITING!");
    if (network_data->thread_status > 0)
    {
        network_data->thread_status = 0;
    }
    return NULL;
}