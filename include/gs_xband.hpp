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
 * @brief Sent to Roof X-Band / Haystack for configurations.
 * 
 */
typedef struct
{
    // libiio.h: ensm_mode
    int mode;               // SLEEP, FDD, TDD 
    int pll_freq;           // PLL Frequency
    int64_t LO;            // LO freq
    int64_t samp;          // sampling rate
    int64_t bw;            // bandwidth
    char ftr_name[64];      // filter name
    int temp;               // temperature
    double rssi;            // RSSI
    double gain;            // TX Gain
    char curr_gainmode[16]; // fast_attack or slow_attack
    bool pll_lock;
    uint32_t MTU;
} phy_config_t;

/**
 * @brief Sent to GUI client for status updates.
 * 
 */
typedef struct
{
    // libiio.h: ensm_mode
    int mode;               // SLEEP, FDD, TDD 
    int pll_freq;           // PLL Frequency
    int64_t LO;            // LO freq
    int64_t samp;          // sampling rate
    int64_t bw;            // bandwidth
    char ftr_name[64];      // filter name
    int64_t temp;               // temperature
    double rssi;            // RSSI
    double gain;            // TX Gain
    char curr_gainmode[16]; // fast_attack or slow_attack
    bool pll_lock;
    bool modem_ready;
    bool PLL_ready;
    bool radio_ready;
    bool rx_armed;
    uint32_t MTU;
} phy_status_t;

typedef struct
{
    txmodem tx_modem[1];
    adf4355 PLL[1];
    adradio_t radio[1];

    bool tx_modem_ready;
    bool PLL_ready;
    bool radio_ready;

    NetDataClient *network_data;
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

enum XBAND_COMMAND
{
    XBC_INIT_PLL = 0,
    XBC_DISABLE_PLL = 1,
    XBC_ARM_RX = 2,
    XBC_DISARM_RX = 3,
};

/**
 * @brief Initializes radio.
 * 
 * Called from transmit function if necessary.
 * 
 * @param global_data 
 * @return int 
 */
int gs_xband_init(global_data_t *global);

/**
 * @brief Transmits over X-Band to SPACE-HAUC.
 * 
 * @param global_data 
 * @param dev 
 * @param buf 
 * @param size 
 * @return int 
 */
int gs_xband_transmit(global_data_t *global, txmodem *dev, uint8_t *buf, ssize_t size);

/**
 * @brief Listens for NetworkFrames from the Ground Station Network.
 * 
 * @param args 
 * @return void* 
 */
void *gs_network_rx_thread(void *args);

/**
 * @brief Periodically sends X-Band status updates.
 * 
 */
void *xband_status_thread(void *args);

#endif // GS_XBAND_HPP