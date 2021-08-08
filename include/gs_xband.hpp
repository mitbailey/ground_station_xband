/**
 * @file gs_xband.hpp
 * @author Mit Bailey (mitbailey99@gmail.com)
 * @brief 
 * @version See Git tags for version information.
 * @date 2021.08.04
 * 
 * @copyright Copyright (c) 2021
 * 
 */

#ifndef GS_XBAND_HPP
#define GS_XBAND_HPP

#include <stdint.h>
#include "txmodem.h"
#include "adf4355.h"
#include "network.hpp"
#include "libiio.h"

#define SEC *1000000
#define SERVER_PORT 54220

/**
 * @brief X-Band configuration status information to be filled by the radios, sent to the server, and then to the client.
 * 
 */
typedef struct
{
    int mode;               // 0:SLEEP, 1:FDD, 2:TDD
    int pll_freq;           // PLL Frequency
    uint64_t LO;            // LO freq
    uint64_t samp;          // sampling rate
    uint64_t bw;            // bandwidth
    char ftr_name[64];      // filter name
    int temp;               // temperature
    double rssi;            // RSSI
    double gain;            // TX Gain
    char curr_gainmode[16]; // fast_attack or slow_attack
    bool pll_lock;
} phy_config_t;

typedef struct
{
    txmodem tx_modem[1];
    adf4355 ADF[1];
    adradio_t radio[1];

    network_data_t network_data[1];
    bool tx_ready;
    uint8_t netstat;
} global_data_t;

/**
 * @brief X-Band data structure.
 * 
 * From line 113 of https://github.com/SPACE-HAUC/shflight/blob/flight_test/src/cmd_parser.c
 * Used for:
 *  XBAND_SET_TX
 *  XBAND_SET_RX
 * 
 * FOR SPACE-HAUC USE ONLY
 * 
 */
typedef struct __attribute__((packed))
{
    float LO;
    float bw;
    uint16_t samp;
    uint8_t phy_gain;
    uint8_t adar_gain;
    uint8_t ftr;
    short phase[16];
} xband_set_data_t;

/**
 * @brief Listens for NetworkFrames from the Ground Station Network.
 * 
 * @param args 
 * @return void* 
 */
void *gs_network_rx_thread(void *args);

#endif // GS_XBAND_HPP