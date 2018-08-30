/**
 * Copyright (c) 2016 - 2017, Nordic Semiconductor ASA
 * 
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form, except as embedded into a Nordic
 *    Semiconductor ASA integrated circuit in a product or a software update for
 *    such product, must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other
 *    materials provided with the distribution.
 * 
 * 3. Neither the name of Nordic Semiconductor ASA nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 * 
 * 4. This software, with or without modification, must only be used with a
 *    Nordic Semiconductor ASA integrated circuit.
 * 
 * 5. Any software provided in binary form under this license must not be reverse
 *    engineered, decompiled, modified and/or disassembled.
 * 
 * THIS SOFTWARE IS PROVIDED BY NORDIC SEMICONDUCTOR ASA "AS IS" AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL NORDIC SEMICONDUCTOR ASA OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 */
/**@cond To Make Doxygen skip documentation generation for this file.
 * @{
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "amt.h"
#include "counter.h"

#include "sdk_config.h"
#include "nrf.h"
#include "ble.h"
#include "ble_hci.h"
#include "nordic_common.h"
#include "nrf_gpio.h"
#include "bsp_btn_ble.h"
#include "ble_advdata.h"
#include "ble_srv_common.h"
#include "nrf_sdh.h"
#include "nrf_sdh_ble.h"
#include "nrf_ble_gatt.h"
#include "app_timer.h"
#include "app_error.h"
#include "nrf_cli.h"
#include "nrf_cli_rtt.h"
#include "nrf_cli_uart.h"
#include "ble_conn_state.h"
#include "peer_manager.h"
#include "nrf_fstorage.h"
#include "fds.h"

#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

#define CONN_INTERVAL_DEFAULT           (uint16_t)(MSEC_TO_UNITS(7.5, UNIT_1_25_MS))    /**< Default connection interval used at connection establishment by central side. */

#define CONN_INTERVAL_MIN               (uint16_t)(MSEC_TO_UNITS(7.5, UNIT_1_25_MS))    /**< Minimum acceptable connection interval, in 1.25 ms units. */
#define CONN_INTERVAL_MAX               (uint16_t)(MSEC_TO_UNITS(500, UNIT_1_25_MS))    /**< Maximum acceptable connection interval, in 1.25 ms units. */
#define CONN_SUP_TIMEOUT                (uint16_t)(MSEC_TO_UNITS(4000,  UNIT_10_MS))    /**< Connection supervisory timeout (4 seconds). */
#define SLAVE_LATENCY                   0                                               /**< Slave latency. */

#define SCAN_ADV_LED                    BSP_BOARD_LED_0
#define READY_LED                       BSP_BOARD_LED_1
#define PROGRESS_LED                    BSP_BOARD_LED_2
#define DONE_LED                        BSP_BOARD_LED_3

#define BOARD_RELAY_BUTTON              BSP_BUTTON_1                                    /**< Button to press at beginning of the test to indicate that this board acts as a relay. */
#define BOARD_SLAVE_BUTTON              BSP_BUTTON_2                                    /**< Button to press at beginning of the test to indicate that this board is a slave. */
#define BOARD_MASTER_BUTTON             BSP_BUTTON_3                                    /**< Button to press at beginning of the test to indicate that this board is a master and connected to the PC and takes input from it via the UART. */
#define BUTTON_DETECTION_DELAY          APP_TIMER_TICKS(50)                             /**< Delay from a GPIOTE event until a button is reported as pushed (in number of timer ticks). */

#define APP_BLE_CONN_CFG_TAG            1                                               /**< A tag that refers to the BLE stack configuration. */
#define APP_BLE_OBSERVER_PRIO           3                                               /**< Application's BLE observer priority. You shouldn't need to modify this value. */
#define L2CAP_HDR_LEN                   4                                               /**< L2CAP header length. */

#define SEC_PARAM_BOND                  1                                           /**< Perform bonding. */
#define SEC_PARAM_MITM                  0                                           /**< Man In The Middle protection not required. */
#define SEC_PARAM_LESC                  0                                           /**< LE Secure Connections not enabled. */
#define SEC_PARAM_KEYPRESS              0                                           /**< Keypress notifications not enabled. */
#define SEC_PARAM_IO_CAPABILITIES       BLE_GAP_IO_CAPS_NONE                        /**< No I/O capabilities. */
#define SEC_PARAM_OOB                   0                                           /**< Out Of Band data not available. */
#define SEC_PARAM_MIN_KEY_SIZE          7                                           /**< Minimum encryption key size in octets. */
#define SEC_PARAM_MAX_KEY_SIZE          16                                          /**< Maximum encryption key size in octets. */

#define APP_FEATURE_NOT_SUPPORTED       BLE_GATT_STATUS_ATTERR_APP_BEGIN + 2        /**< Reply when unsupported features are requested. */

#define DELAY_MS  APP_TIMER_TICKS(30)

typedef struct
{
    uint16_t        att_mtu;                    /**< GATT ATT MTU, in bytes. */
    uint16_t        conn_interval;              /**< Connection interval expressed in units of 1.25 ms. */
    ble_gap_phys_t  phys;                       /**< Preferred PHYs. */
    bool            data_len_ext_enabled;       /**< Data length extension status. */
    bool            conn_evt_len_ext_enabled;   /**< Connection event length extension status. */
} test_params_t;

/**@brief Variable length data encapsulation in terms of length and pointer to data. */
typedef struct
{
    uint8_t  * p_data;      /**< Pointer to data. */
    uint16_t   data_len;    /**< Length of data. */
} data_t;

APP_TIMER_DEF(delay_timer);
NRF_BLE_GATT_DEF(m_gatt);                   /**< GATT module instance. */
BLE_DB_DISCOVERY_ARRAY_DEF(m_ble_db_discovery,NRF_SDH_BLE_CENTRAL_LINK_COUNT);   /**< DB discovery module instance. */
NRF_BLE_AMT_C_ARRAY_DEF(m_amtc, NRF_SDH_BLE_CENTRAL_LINK_COUNT);

//static nrf_ble_amtc_t     m_amtc;
static nrf_ble_amts_t     m_amts;
//NRF_SDH_BLE_OBSERVER(m_amtc_ble_obs, BLE_AMTC_BLE_OBSERVER_PRIO, nrf_ble_amtc_on_ble_evt, &m_amtc);
NRF_SDH_BLE_OBSERVER(m_amts_ble_obs, BLE_AMTS_BLE_OBSERVER_PRIO, nrf_ble_amts_on_ble_evt, &m_amts);

NRF_CLI_UART_DEF(cli_uart, 0, 64, 16);
NRF_CLI_RTT_DEF(cli_rtt);
NRF_CLI_DEF(m_cli_uart, "throughput:~$ ", &cli_uart.transport, '\r', 4);
NRF_CLI_DEF(m_cli_rtt,  "throughput:~$ ", &cli_rtt.transport,  '\n', 4);

//board_role_t volatile m_board_role  = NOT_SELECTED;

//set board role as BOARD_RELAY
board_role_t m_board_role = BOARD_RELAY;

static bool volatile m_run_test;
static bool volatile m_notif_enabled;
static bool volatile m_mtu_exchanged;
static bool volatile m_data_length_updated;
static bool volatile m_phy_updated;
static bool volatile m_conn_interval_configured;

static uint16_t m_conn_handle = BLE_CONN_HANDLE_INVALID;    /**< Handle of the current BLE connection .*/
static uint8_t m_gap_role     = BLE_GAP_ROLE_INVALID;       /**< BLE role for this connection, see @ref BLE_GAP_ROLES */

// Name to use for advertising and connection.
static char const m_target_periph_name[] = DEVICE_NAME1;

// Test parameters.
// Settings like ATT MTU size are set only once on the master board.
// Make sure that defaults are sensible.
static test_params_t m_test_params =
{
    .att_mtu                  = NRF_SDH_BLE_GATT_MAX_MTU_SIZE,
    .conn_interval            = CONN_INTERVAL_DEFAULT,
    .data_len_ext_enabled     = true,
    .conn_evt_len_ext_enabled = false,
    // Only symmetric PHYs are supported.
#if defined(S140)
    .phys.tx_phys             = BLE_GAP_PHY_2MBPS | BLE_GAP_PHY_1MBPS | BLE_GAP_PHY_CODED,
    .phys.rx_phys             = BLE_GAP_PHY_2MBPS | BLE_GAP_PHY_1MBPS | BLE_GAP_PHY_CODED,
#else
    .phys.tx_phys             = BLE_GAP_PHY_2MBPS | BLE_GAP_PHY_1MBPS,
    //.phys.tx_phys             = BLE_GAP_PHY_1MBPS,
    .phys.rx_phys             = BLE_GAP_PHY_2MBPS | BLE_GAP_PHY_1MBPS,
    //.phys.rx_phys             = BLE_GAP_PHY_1MBPS,
    
#endif
};

// Scan parameters requested for scanning and connection.
static ble_gap_scan_params_t const m_scan_param =
{
    .active         = 0x00,
    .interval       = SCAN_INTERVAL,
    .window         = SCAN_WINDOW,
    .use_whitelist  = 0x00,
    .adv_dir_report = 0x00,
    .timeout        = 0x0000, // No timeout.
};

// Connection parameters requested for connection.
static ble_gap_conn_params_t m_conn_param =
{
    .min_conn_interval = CONN_INTERVAL_MIN,   // Minimum connection interval.
    .max_conn_interval = CONN_INTERVAL_MAX,   // Maximum connection interval.
    .slave_latency     = SLAVE_LATENCY,       // Slave latency.
    .conn_sup_timeout  = CONN_SUP_TIMEOUT     // Supervisory timeout.
};


static void test_terminate(void);

/**@brief   Function for sending data received.
 *
 * @details call this function to send received data to another client.
 *
 * @param       p_ctx         Pointer to the AMTS structure.
 * @param[in]   relay_data    data received from.
 */

static void char_notification_relay_send(uint16_t conn_handle, nrf_ble_amtc_evt_t * p_evt, nrf_ble_amtc_t * p_amt_c);

char const * phy_str(ble_gap_phys_t phys)
{
    static char const * str[] =
    {
        "1 Mbps",
        "2 Mbps",
        "Coded",
        "Unknown"
    };

    switch (phys.tx_phys)
    {
        case BLE_GAP_PHY_1MBPS:
            return str[0];

        case BLE_GAP_PHY_2MBPS:
        case BLE_GAP_PHY_2MBPS | BLE_GAP_PHY_1MBPS:
        case BLE_GAP_PHY_2MBPS | BLE_GAP_PHY_1MBPS | BLE_GAP_PHY_CODED:
            return str[1];

        case BLE_GAP_PHY_CODED:
            return str[2];

        default:
            return str[3];
    }
}


static void instructions_print(void)
{
    //NRF_LOG_INFO("Type 'config' to change the configuration parameters.");
    //NRF_LOG_INFO("You can use the TAB key to autocomplete your input.");
    NRF_LOG_INFO("Type 'run' when you are ready to run the test.");
}


/**@brief Parses advertisement data, providing length and location of the field in case
 *        matching data is found.
 *
 * @param[in]  Type of data to be looked for in advertisement data.
 * @param[in]  Advertisement report length and pointer to report.
 * @param[out] If data type requested is found in the data report, type data length and
 *             pointer to data will be populated here.
 *
 * @retval NRF_SUCCESS if the data type is found in the report.
 * @retval NRF_ERROR_NOT_FOUND if the data type could not be found.
 */
static uint32_t adv_report_parse(uint8_t type, data_t * p_advdata, data_t * p_typedata)
{
    uint32_t  index = 0;
    uint8_t * p_data;

    p_data = p_advdata->p_data;

    while (index < p_advdata->data_len)
    {
        uint8_t field_length = p_data[index];
        uint8_t field_type   = p_data[index + 1];

        if (field_type == type)
        {
            p_typedata->p_data   = &p_data[index + 2];
            p_typedata->data_len = field_length - 1;
            return NRF_SUCCESS;
        }
        index += field_length + 1;
    }
    return NRF_ERROR_NOT_FOUND;
}


/**@brief Function for searching a given name in the advertisement packets.
 *
 * @details Use this function to parse received advertising data and to find a given
 * name in them either as 'complete_local_name' or as 'short_local_name'.
 *
 * @param[in]   p_adv_report   advertising data to parse.
 * @param[in]   name_to_find   name to search.
 * @return   true if the given name was found, false otherwise.
 */
static bool find_adv_name(ble_gap_evt_adv_report_t const * p_adv_report, char const * name_to_find)
{
    ret_code_t err_code;
    data_t     adv_data;
    data_t     dev_name;
    bool       found = false;

    // Initialize advertisement report for parsing.
    adv_data.p_data   = (uint8_t *)p_adv_report->data;
    adv_data.data_len = p_adv_report->dlen;

    // Search for matching advertising names.
    err_code = adv_report_parse(BLE_GAP_AD_TYPE_COMPLETE_LOCAL_NAME, &adv_data, &dev_name);

    if (   (err_code == NRF_SUCCESS)
        && (strlen(name_to_find) == dev_name.data_len)
        && (memcmp(name_to_find, dev_name.p_data, dev_name.data_len) == 0))
    {
        found = true;
    }
    else
    {
        // Look for the short local name if the complete name was not found.
        err_code = adv_report_parse(BLE_GAP_AD_TYPE_SHORT_LOCAL_NAME, &adv_data, &dev_name);

        if (   (err_code == NRF_SUCCESS)
            && (strlen(name_to_find) == dev_name.data_len)
            && (memcmp(m_target_periph_name, dev_name.p_data, dev_name.data_len) == 0))
        {
            found = true;
        }
    }

    return found;
}

/**@brief Function for handling Database Discovery events.
 *
 * @details This function is a callback function to handle events from the database discovery module.
 *          Depending on the UUIDs that are discovered, this function should forward the events
 *          to their respective service instances.
 *
 * @param[in] p_evt  Pointer to the database discovery event.
 */
static void db_disc_evt_handler(ble_db_discovery_evt_t * p_evt)
{
    nrf_ble_amtc_on_db_disc_evt(&m_amtc[p_evt->conn_handle], p_evt);
}


/**@brief Function for handling events from the GATT library. */
static void gatt_evt_handler(nrf_ble_gatt_t * p_gatt, nrf_ble_gatt_evt_t const * p_evt)
{
    switch (p_evt->evt_id)
    {
        case NRF_BLE_GATT_EVT_ATT_MTU_UPDATED:
        {
            m_mtu_exchanged = true;
            NRF_LOG_INFO("ATT MTU exchange completed. MTU set to %u bytes.",
                         p_evt->params.att_mtu_effective);
        } break;

        case NRF_BLE_GATT_EVT_DATA_LENGTH_UPDATED:
        {
            m_data_length_updated = true;
            NRF_LOG_INFO("Data length updated to %u bytes.", p_evt->params.data_length);
        } break;
    }

    nrf_ble_amts_on_gatt_evt(&m_amts, p_evt);
}


/**@brief Function for setting up advertising data. */
static void advertising_data_set(void)
{
    ble_advdata_t const adv_data =
    {
        .name_type          = BLE_ADVDATA_FULL_NAME,
        .flags              = BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE,
        .include_appearance = false,
    };

    ret_code_t err_code = ble_advdata_set(&adv_data, NULL);
    APP_ERROR_CHECK(err_code);
}


/**@brief Function for starting advertising. */
static void advertising_start(void)
{
    ble_gap_adv_params_t const adv_params =
    {
        .type        = BLE_GAP_ADV_TYPE_ADV_IND,
        .p_peer_addr = NULL,
        .fp          = BLE_GAP_ADV_FP_ANY,
        .interval    = ADV_INTERVAL,
        .timeout     = 0,
    };

    NRF_LOG_INFO("Starting advertising.");

    bsp_board_led_on(SCAN_ADV_LED);

    ret_code_t err_code = sd_ble_gap_adv_start(&adv_params, APP_BLE_CONN_CFG_TAG);
    APP_ERROR_CHECK(err_code);
}


/**@brief Function to start scanning. */
static void scan_start(void)
{
    NRF_LOG_INFO("Starting scan.");

    bsp_board_led_on(SCAN_ADV_LED);

    ret_code_t err_code = sd_ble_gap_scan_start(&m_scan_param);
    APP_ERROR_CHECK(err_code);
    
}

/**@brief Function for initiating advertising and scanning.
 */
static void adv_scan_start(void)
{
    //check if there are no flash operations in progress
    if (!nrf_fstorage_is_busy(NULL))
    {
        // Start scanning for peripherals and initiate connection to devices which
        // advertise device name "Nordic_ATT_MTU".
        scan_start();
        NRF_LOG_INFO(" Relay scan start.");

        // Start advertising.
        advertising_start();
        NRF_LOG_INFO(" Relay advertising start."); 
    }
}

/**@brief Function for handling Peer Manager events.
 *
 * @param[in] p_evt  Peer Manager event.
 */
static void pm_evt_handler(pm_evt_t const * p_evt)
{
    ret_code_t err_code;

    switch (p_evt->evt_id)
    {
        case PM_EVT_BONDED_PEER_CONNECTED:
        {
            NRF_LOG_INFO("Connected to a previously bonded device.");
        } break;

        case PM_EVT_CONN_SEC_SUCCEEDED:
        {
            NRF_LOG_INFO("Connection secured: role: %d, conn_handle: 0x%x, procedure: %d.",
                         ble_conn_state_role(p_evt->conn_handle),
                         p_evt->conn_handle,
                         p_evt->params.conn_sec_succeeded.procedure);
        } break;

        case PM_EVT_CONN_SEC_FAILED:
        {
            /* Often, when securing fails, it shouldn't be restarted, for security reasons.
             * Other times, it can be restarted directly.
             * Sometimes it can be restarted, but only after changing some Security Parameters.
             * Sometimes, it cannot be restarted until the link is disconnected and reconnected.
             * Sometimes it is impossible, to secure the link, or the peer device does not support it.
             * How to handle this error is highly application dependent. */
        } break;

        case PM_EVT_CONN_SEC_CONFIG_REQ:
        {
            // Reject pairing request from an already bonded peer.
            pm_conn_sec_config_t conn_sec_config = {.allow_repairing = false};
            pm_conn_sec_config_reply(p_evt->conn_handle, &conn_sec_config);
        } break;

        case PM_EVT_STORAGE_FULL:
        {
            // Run garbage collection on the flash.
            err_code = fds_gc();
            if (err_code == FDS_ERR_BUSY || err_code == FDS_ERR_NO_SPACE_IN_QUEUES)
            {
                // Retry.
            }
            else
            {
                APP_ERROR_CHECK(err_code);
            }
        } break;

        case PM_EVT_PEERS_DELETE_SUCCEEDED:
        {
            //adv_scan_start();
        } break;

        case PM_EVT_LOCAL_DB_CACHE_APPLY_FAILED:
        {
            // The local database has likely changed, send service changed indications.
            pm_local_database_has_changed();
        } break;

        case PM_EVT_PEER_DATA_UPDATE_FAILED:
        {
            // Assert.
            APP_ERROR_CHECK(p_evt->params.peer_data_update_failed.error);
        } break;

        case PM_EVT_PEER_DELETE_FAILED:
        {
            // Assert.
            APP_ERROR_CHECK(p_evt->params.peer_delete_failed.error);
        } break;

        case PM_EVT_PEERS_DELETE_FAILED:
        {
            // Assert.
            APP_ERROR_CHECK(p_evt->params.peers_delete_failed_evt.error);
        } break;

        case PM_EVT_ERROR_UNEXPECTED:
        {
            // Assert.
            APP_ERROR_CHECK(p_evt->params.error_unexpected.error);
        } break;

        case PM_EVT_CONN_SEC_START:
        case PM_EVT_PEER_DATA_UPDATE_SUCCEEDED:
        case PM_EVT_PEER_DELETE_SUCCEEDED:
        case PM_EVT_LOCAL_DB_CACHE_APPLIED:
        case PM_EVT_SERVICE_CHANGED_IND_SENT:
        case PM_EVT_SERVICE_CHANGED_IND_CONFIRMED:
        default:
            break;
    }
}

/**@brief Function for handling BLE_GAP_ADV_REPORT events.
 * Search for a peer with matching device name.
 * If found, stop advertising and send a connection request to the peer.
 */
static void on_ble_gap_evt_adv_report(ble_gap_evt_t const * p_gap_evt)
{
    if (!find_adv_name(&p_gap_evt->params.adv_report, m_target_periph_name))
    {
        return;
    }

    NRF_LOG_INFO("Device \"%s\" found, sending a connection request.",
                 (uint32_t) m_target_periph_name);

    // Stop advertising.
    //(void) sd_ble_gap_adv_stop();

    // Initiate connection.
    m_conn_param.min_conn_interval = CONN_INTERVAL_DEFAULT;
    m_conn_param.max_conn_interval = CONN_INTERVAL_DEFAULT;

    ret_code_t err_code;
    err_code = sd_ble_gap_connect(&p_gap_evt->params.adv_report.peer_addr,
                                  &m_scan_param,
                                  &m_conn_param,
                                  APP_BLE_CONN_CFG_TAG);

    if (err_code != NRF_SUCCESS)
    {
        NRF_LOG_ERROR("sd_ble_gap_connect() failed: 0x%x.", err_code);
    }
}


/**@brief Function for handling BLE_GAP_EVT_CONNECTED events.
 * Save the connection handle and GAP role, then discover the peer DB.
 */
static void on_ble_gap_evt_connected(ble_gap_evt_t const * p_gap_evt)
{
    ret_code_t err_code;

    m_conn_handle = p_gap_evt->conn_handle;
    m_gap_role    = p_gap_evt->params.connected.role;
    NRF_LOG_INFO("Conn Handle: 0x%x", p_gap_evt->conn_handle);
    
    if (m_gap_role == BLE_GAP_ROLE_PERIPH)
    {
        NRF_LOG_INFO("Connected as a peripheral.");
    }
    else if (m_gap_role == BLE_GAP_ROLE_CENTRAL)
    {
        NRF_LOG_INFO("Connected as a central.");   
    }
    
    
    if (m_gap_role == BLE_GAP_ROLE_CENTRAL)
    {  
      APP_ERROR_CHECK_BOOL(m_conn_handle < NRF_SDH_BLE_CENTRAL_LINK_COUNT);
      err_code = nrf_ble_amtc_handles_assign(&m_amtc[p_gap_evt->conn_handle],
                                             p_gap_evt->conn_handle,
                                             NULL);
      APP_ERROR_CHECK(err_code); 
    
    // Stop scanning and advertising.
      (void) sd_ble_gap_scan_stop();
      (void) sd_ble_gap_adv_stop();

      bsp_board_leds_off();
      
      err_code  = ble_db_discovery_start(&m_ble_db_discovery[p_gap_evt->conn_handle], p_gap_evt->conn_handle);
      APP_ERROR_CHECK(err_code);
      NRF_LOG_INFO("Discovering GATT database...");
      
      if (ble_conn_state_n_centrals() == NRF_SDH_BLE_CENTRAL_LINK_COUNT)
      {
        bsp_board_led_off(SCAN_ADV_LED);
      }
//      else
//      {
//                // Resume scanning.
//         NRF_LOG_INFO("Resume scan.");
//         bsp_board_led_on(SCAN_ADV_LED);
//         err_code = sd_ble_gap_scan_start(&m_scan_param);
//         APP_ERROR_CHECK(err_code);
//       }
    }

    if (m_gap_role == BLE_GAP_ROLE_PERIPH)
    {
    #if defined(S140)
        err_code = sd_ble_gap_phy_request(p_gap_evt->conn_handle, &m_test_params.phys);
        APP_ERROR_CHECK(err_code);
    #else
        err_code = sd_ble_gap_phy_update(p_gap_evt->conn_handle, &m_test_params.phys);
        APP_ERROR_CHECK(err_code);
    #endif

        m_amts.conn_handle = p_gap_evt->conn_handle;
          
    }
    
    // Relay resume advertising if no central is connected
    if (m_board_role == BOARD_RELAY)
    {
      (void) sd_ble_gap_scan_stop();
      (void) sd_ble_gap_adv_stop();
      if(ble_conn_state_n_peripherals() != NRF_SDH_BLE_PERIPHERAL_LINK_COUNT)
      { 
        NRF_LOG_INFO("Relay still advertises.");
        advertising_start();
      } 
//      if(ble_conn_state_n_centrals() < NRF_SDH_BLE_CENTRAL_LINK_COUNT)
//      {
//        NRF_LOG_INFO("Relay still scans.");
//        scan_start();
//      }

    }
}


/**@brief Function for handling BLE_GAP_EVT_DISCONNECTED events.
 * Unset the connection handle and terminate the test.
 */
static void on_ble_gap_evt_disconnected(ble_gap_evt_t const * p_gap_evt)
{   
    m_conn_handle = BLE_CONN_HANDLE_INVALID;

    NRF_LOG_DEBUG("Disconnected: reason 0x%x.", p_gap_evt->params.disconnected.reason);

    if (m_run_test)
    {
        NRF_LOG_WARNING("GAP disconnection event received while test was running.")
    }

    bsp_board_leds_off();
    
    test_terminate();
}


/**@brief Function for handling BLE Stack events.
 *
 * @param[in] p_ble_evt  Bluetooth stack event.
 */
static void ble_evt_handler(ble_evt_t const * p_ble_evt, void * p_context)
{
    uint32_t              err_code;
    ble_gap_evt_t const * p_gap_evt = &p_ble_evt->evt.gap_evt;
    //uint16_t conn_handle = p_ble_evt->evt.gap_evt.conn_handle;
   // uint16_t role        = ble_conn_state_role(conn_handle);
    //NRF_LOG_INFO("BLE Connection handle in ble evt handler: 0x%x and role: Ox%x ", conn_handle, role);

    switch (p_ble_evt->header.evt_id)
    {
        case BLE_GAP_EVT_ADV_REPORT:           
            on_ble_gap_evt_adv_report(p_gap_evt);
            break;

        case BLE_GAP_EVT_CONNECTED:
            on_ble_gap_evt_connected(p_gap_evt);
            break;

        case BLE_GAP_EVT_DISCONNECTED:
            on_ble_gap_evt_disconnected(p_gap_evt);
            break;

        case BLE_GAP_EVT_CONN_PARAM_UPDATE:
        {
            m_conn_interval_configured = true;
            NRF_LOG_INFO("Connection interval updated: 0x%x, 0x%x.",
                p_gap_evt->params.conn_param_update.conn_params.min_conn_interval,
                p_gap_evt->params.conn_param_update.conn_params.max_conn_interval);
        } break;

        case BLE_GAP_EVT_CONN_PARAM_UPDATE_REQUEST:
        {
            // Accept parameters requested by the peer.
            ble_gap_conn_params_t params;
            params = p_gap_evt->params.conn_param_update_request.conn_params;
            err_code = sd_ble_gap_conn_param_update(p_gap_evt->conn_handle, &params);
            APP_ERROR_CHECK(err_code);

            NRF_LOG_INFO("Connection interval updated (upon request): 0x%x, 0x%x.",
                p_gap_evt->params.conn_param_update_request.conn_params.min_conn_interval,
                p_gap_evt->params.conn_param_update_request.conn_params.max_conn_interval);
        } break;

        case BLE_GATTS_EVT_SYS_ATTR_MISSING:
        {
            err_code = sd_ble_gatts_sys_attr_set(p_gap_evt->conn_handle, NULL, 0, 0);
            APP_ERROR_CHECK(err_code);
        } break;

        case BLE_GATTC_EVT_TIMEOUT: // Fallthrough.
        case BLE_GATTS_EVT_TIMEOUT:
        {
            NRF_LOG_DEBUG("GATT timeout, disconnecting.");
            err_code = sd_ble_gap_disconnect(m_conn_handle,
                                             BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
            APP_ERROR_CHECK(err_code);
        } break;

        case BLE_EVT_USER_MEM_REQUEST:
        {
            err_code = sd_ble_user_mem_reply(p_ble_evt->evt.common_evt.conn_handle, NULL);
            APP_ERROR_CHECK(err_code);
        } break;

        case BLE_GAP_EVT_PHY_UPDATE:
        {
            ble_gap_evt_phy_update_t const * p_phy_evt = &p_ble_evt->evt.gap_evt.params.phy_update;

            if (p_phy_evt->status == BLE_HCI_STATUS_CODE_LMP_ERROR_TRANSACTION_COLLISION)
            {
                // Ignore LL collisions.
                NRF_LOG_DEBUG("LL transaction collision during PHY update.");
                break;
            }

            m_phy_updated = true;

            ble_gap_phys_t phys = {0};
            phys.tx_phys = p_phy_evt->tx_phy;
            phys.rx_phys = p_phy_evt->rx_phy;
            NRF_LOG_INFO("PHY update %s. PHY set to %s.",
                         (p_phy_evt->status == BLE_HCI_STATUS_CODE_SUCCESS) ?
                         "accepted" : "rejected",
                         phy_str(phys));
        } break;

#if defined(S132)
        case BLE_GAP_EVT_PHY_UPDATE_REQUEST:
        {
            err_code = sd_ble_gap_phy_update(p_gap_evt->conn_handle, &m_test_params.phys);
            APP_ERROR_CHECK(err_code);
        } break;
#endif
  
         case BLE_GATTS_EVT_RW_AUTHORIZE_REQUEST:
        {
            ble_gatts_evt_rw_authorize_request_t  req;
            ble_gatts_rw_authorize_reply_params_t auth_reply;

            req = p_ble_evt->evt.gatts_evt.params.authorize_request;

            if (req.type != BLE_GATTS_AUTHORIZE_TYPE_INVALID)
            {
                if ((req.request.write.op == BLE_GATTS_OP_PREP_WRITE_REQ)     ||
                    (req.request.write.op == BLE_GATTS_OP_EXEC_WRITE_REQ_NOW) ||
                    (req.request.write.op == BLE_GATTS_OP_EXEC_WRITE_REQ_CANCEL))
                {
                    if (req.type == BLE_GATTS_AUTHORIZE_TYPE_WRITE)
                    {
                        auth_reply.type = BLE_GATTS_AUTHORIZE_TYPE_WRITE;
                    }
                    else
                    {
                        auth_reply.type = BLE_GATTS_AUTHORIZE_TYPE_READ;
                    }
                    auth_reply.params.write.gatt_status = APP_FEATURE_NOT_SUPPORTED;
                    err_code = sd_ble_gatts_rw_authorize_reply(p_ble_evt->evt.gatts_evt.conn_handle,
                                                               &auth_reply);
                    APP_ERROR_CHECK(err_code);
                }
            }
        } break; // BLE_GATTS_EVT_RW_AUTHORIZE_REQUEST
        
        default:
            // No implementation needed.
            break;
    }
}


/**@brief AMT server event handler. */
static void amts_evt_handler(nrf_ble_amts_evt_t evt)
{
    ret_code_t err_code;

    switch (evt.evt_type)
    {
        case NRF_BLE_AMTS_EVT_NOTIF_ENABLED:
        {
            NRF_LOG_INFO("Notifications enabled.");

            bsp_board_led_on(READY_LED);
            m_notif_enabled = true;
            // Only let GAP peripheral be the server
//            if (m_gap_role!= BLE_GAP_ROLE_PERIPH)
//            {   
//                return;
//            }

            if (m_test_params.conn_interval != CONN_INTERVAL_DEFAULT)
            {
                NRF_LOG_DEBUG("Updating connection parameters..");
                m_conn_param.min_conn_interval = m_test_params.conn_interval;
                m_conn_param.max_conn_interval = m_test_params.conn_interval;
                err_code = sd_ble_gap_conn_param_update(m_conn_handle, &m_conn_param);

                if (err_code != NRF_SUCCESS)
                {
                    NRF_LOG_ERROR("sd_ble_gap_conn_param_update() failed: 0x%x.", err_code);
                }
            }
            else
            {
                m_conn_interval_configured = true;
            }
        } break;
        
        case NRF_BLE_AMTS_EVT_COUNTER_START:
          {  //NRF_LOG_INFO("counter writes to application");
            if (evt.bytes_transfered_cnt==0)
            {
             // counter_start();
              NRF_LOG_INFO("Slave counter start!");
            }
          }break;
      

        case NRF_BLE_AMTS_EVT_NOTIF_DISABLED:
        {
            NRF_LOG_INFO("Notifications disabled.");
            bsp_board_led_off(READY_LED);
        } break;

        case NRF_BLE_AMTS_EVT_TRANSFER_1KB:
        {
            NRF_LOG_INFO("Sent %u KBytes", (evt.bytes_transfered_cnt / 1024));
            bsp_board_led_invert(PROGRESS_LED);
            nrf_ble_amts_rbc_set(&m_amts,(evt.bytes_transfered_cnt / 1024)); //writes attribute value to itself
        } break;

        case NRF_BLE_AMTS_EVT_TRANSFER_FINISHED:
        {
            counter_stop();

            bsp_board_led_off(PROGRESS_LED);
            bsp_board_led_on(DONE_LED);

            uint32_t time_ms      = counter_get();
            uint32_t bit_count    = (evt.bytes_transfered_cnt * 8);
            
            float throughput_kbps = ((bit_count / (time_ms / 1000.f)) / 1000.f);
  
            uint32_t intthroughput_kbps = (bit_count / (time_ms));
            
            nrf_ble_amts_rbc_set(&m_amts, intthroughput_kbps); //writes attribute value to itself

            NRF_LOG_INFO("Done.");
            NRF_LOG_INFO("=============================");
            NRF_LOG_INFO("Time: %u.%.2u seconds elapsed.", (time_ms / 1000), (time_ms % 1000));
            NRF_LOG_INFO("Throughput: " NRF_LOG_FLOAT_MARKER " Kbps.",
                         NRF_LOG_FLOAT(throughput_kbps));
            NRF_LOG_INFO("Sent %u bytes of ATT payload.", evt.bytes_transfered_cnt);
        } break;
    }
}

static uint32_t countlink=0;
static uint32_t relaybytes=0;
static uint32_t relayKbytes=0;

/**@brief AMT Client event handler.  */
static void amtc_evt_handler(nrf_ble_amtc_t * p_amt_c, nrf_ble_amtc_evt_t * p_evt)
{
    ret_code_t err_code;
   
    switch (p_evt->evt_type)
    {
        case NRF_BLE_AMT_C_EVT_DISCOVERY_COMPLETE:
        {
            NRF_LOG_INFO("AMT service discovered at peer.");
            countlink = countlink+1;
           
//            if (countlink == 1)
//            {
//             counter_start();
//              NRF_LOG_INFO("Master counter start!!!");
//            }
           /* err_code = nrf_ble_amtc_handles_assign(&m_amtc[p_amt_c->conn_handle],
                                                   p_evt->conn_handle,
                                                   &p_evt->params.peer_db);
            APP_ERROR_CHECK(err_code);*/
            
            
           //initiate bonding
            if (m_board_role==BOARD_RELAY)
            {
              err_code = pm_conn_secure(p_evt->conn_handle,false);
              if (err_code != NRF_ERROR_INVALID_STATE)
              {
                APP_ERROR_CHECK(err_code);
                NRF_LOG_INFO("Secure complete!");
              }
             }
            
            
           // Enable notifications.
            err_code = nrf_ble_amtc_notif_enable(p_amt_c);
            APP_ERROR_CHECK(err_code);         
        } break;

        case NRF_BLE_AMT_C_EVT_NOTIFICATION:
        {
     
        
            static uint32_t bytes_cnt  = 0;
            static uint32_t kbytes_cnt = 0;
           
            /*if (p_evt->params.hvx.bytes_sent == 0)
            {
                bytes_cnt  = 0;
                kbytes_cnt = 0;
            }*/

            bytes_cnt += p_evt->params.hvx.notif_len;
         
            if (bytes_cnt > 1024)
            {
                bsp_board_led_invert(PROGRESS_LED);

                bytes_cnt -= 1024;
                kbytes_cnt++;

                NRF_LOG_INFO("Received %u kbytes", kbytes_cnt);             
            }

            if ( (1024*kbytes_cnt) >= 3*AMT_BYTE_TRANSFER_CNT)
            {   
                counter_stop();
                bsp_board_led_off(PROGRESS_LED);

                bytes_cnt  = 0;

                NRF_LOG_INFO("Transfer complete, received %u kbytes of ATT payload.",
                             kbytes_cnt);

               // nrf_ble_amts_rbc_set(&m_amts, p_evt->params.hvx.bytes_rcvd);
                uint32_t time_ms      = counter_get();
                uint32_t bit_count    = (kbytes_cnt*8192);
                float throughput_kbps = ((bit_count / (time_ms / 1000.f)) / 1000.f);

                NRF_LOG_INFO("Time: %u.%.2u seconds elapsed.", (time_ms / 1000), (time_ms % 1000));
                NRF_LOG_INFO("Throughput: " NRF_LOG_FLOAT_MARKER " Kbps.",
                             NRF_LOG_FLOAT(throughput_kbps));
              
                NRF_LOG_INFO("====================================================================");
                for (uint32_t i = 0; i < NRF_SDH_BLE_CENTRAL_LINK_COUNT; i++)
                {
                  err_code = nrf_ble_amtc_rcb_read(&m_amtc[i]);
                  if (err_code != NRF_SUCCESS)
                  {
                    NRF_LOG_ERROR("nrf_ble_amtc_rcb_read() failed: 0x%x.", err_code);

                  }             
                }
                kbytes_cnt = 0;
            }
            
                   
            if (m_board_role == BOARD_RELAY)
            {
              sdk_mapped_flags_key_list_t conn_handle = ble_conn_state_periph_handles(); 
              
              char_notification_relay_send(conn_handle.flag_keys[0], p_evt, p_amt_c);           
              
            }                     
        } break;
       
        
        case NRF_BLE_AMT_C_EVT_RBC_READ_RSP:
          {
            
            NRF_LOG_INFO("Slave Throughput of Sending Data: %u Kbps.", p_evt->params.rcv_bytes_cnt);
            //test_terminate();
          } break;

        default:
            break;
    }
}

static void char_notification_relay_send(uint16_t conn_handle, nrf_ble_amtc_evt_t * p_evt, nrf_ble_amtc_t * p_amt_c)
{
          ret_code_t    err_code;
          nrf_ble_amts_t  *p_amts = &m_amts;     
          
          if (conn_handle != BLE_CONN_HANDLE_INVALID)
          {          
           
            uint8_t            data[244];
            
           //add Relay flag to data
            uint8_t    RelayFlag=8;
            data[5]= RelayFlag;
    
            ble_gatts_hvx_params_t const hvx_param =
            {        
              .type   = BLE_GATT_HVX_NOTIFICATION,
              .handle = p_amts->amts_char_handles.value_handle,
              .p_data = data,
              .p_len  = &(p_evt->params.hvx.notif_len),      
             };
             
            
                    (void) uint32_encode(p_evt->params.hvx.bytes_sent, data);                 
                    // NRF_LOG_INFO("hvx conn handle: 0x%x", conn_handle);
                     err_code = sd_ble_gatts_hvx(conn_handle,&hvx_param);
                 
                      if (err_code!= NRF_SUCCESS)
                      {
                        NRF_LOG_ERROR("sd_ble_gatts_hvx() failed: 0x%x", err_code);
                    // APP_ERROR_CHECK(err_code);
                      }
                      if(err_code == NRF_SUCCESS)
                      {                                          
                         relaybytes += *(hvx_param.p_len);
                         if(relayKbytes != relaybytes /1024)
                         {   
                            relayKbytes = relaybytes /1024;
                            NRF_LOG_INFO("relay %d Kbytes data to collector", relayKbytes);                                      
                         }            
                        
                      }
//            }
           }
               else
               {
                  err_code = NRF_ERROR_INVALID_STATE;
                  NRF_LOG_ERROR("hvx conn_handle= invalid");
               }
}

static void log_init(void)
{
    ret_code_t err_code = NRF_LOG_INIT(app_timer_cnt_get);
    APP_ERROR_CHECK(err_code);
}


/**@brief Function for the LEDs initialization.
 *
 * @details Initializes all LEDs used by the application.
 */
static void leds_init(void)
{
    bsp_board_leds_init();
}

static void delay_timer_timeout_handler(void * p_context)
{
   char_notification_send(&m_amts);
}

/**@brief Function for the Timer initialization.
 *
 * @details Initializes the timer module. This creates and starts application timers.
 */
static void timer_init(void)
{
    ret_code_t err_code = app_timer_init();
    APP_ERROR_CHECK(err_code);
    
    err_code = app_timer_create(&delay_timer,APP_TIMER_MODE_REPEATED,delay_timer_timeout_handler);
    APP_ERROR_CHECK(err_code);
}

void application_timers_start(void)
{
   ret_code_t err_code = app_timer_start(delay_timer, DELAY_MS, NULL);
   APP_ERROR_CHECK(err_code); 
}


/**@brief Function for enabling button input.
 */
static void buttons_enable(void)
{
    ret_code_t err_code = app_button_enable();
    APP_ERROR_CHECK(err_code);
}


/**@brief Function for disabling button input. */
static void buttons_disable(void)
{
    ret_code_t err_code = app_button_disable();
    APP_ERROR_CHECK(err_code);
}


/**@brief Function for handling events from the button handler module.
 *
 * @param[in] pin_no        The pin that the event applies to.
 * @param[in] button_action The button action (press/release).
 */
static void button_evt_handler(uint8_t pin_no, uint8_t button_action)
{
    switch (pin_no)
    {
        case BOARD_SLAVE_BUTTON:
        {
            NRF_LOG_INFO("This board will act as a slave.");
            //instructions_print();
            m_board_role = BOARD_SLAVE;
            advertising_start();
        } break;

        case BOARD_MASTER_BUTTON:
        {
            NRF_LOG_INFO("This board will act as a master.");
            instructions_print();
            m_board_role = BOARD_MASTER;
            //advertising_start();
            //scan_start();
        } break;
        
        case BOARD_RELAY_BUTTON:
          {
            NRF_LOG_INFO("This board will act as a relay.");
            m_board_role= BOARD_RELAY;  
            adv_scan_start();
          }
                
        default:
            break;
    }
    buttons_disable();
}

/**@brief Function for initializing the button library.
 */
static void buttons_init(void)
{
   // The array must be static because a pointer to it will be saved in the button library.
    static app_button_cfg_t buttons[] =
    {
         
        {BOARD_RELAY_BUTTON,  false, BUTTON_PULL, button_evt_handler},
        {BOARD_SLAVE_BUTTON, false, BUTTON_PULL, button_evt_handler},
        {BOARD_MASTER_BUTTON,  false, BUTTON_PULL, button_evt_handler}
        
    };

    ret_code_t err_code = app_button_init(buttons, ARRAY_SIZE(buttons), BUTTON_DETECTION_DELAY);
    APP_ERROR_CHECK(err_code);
}


static void client_init(void)
{
    ret_code_t err_code = ble_db_discovery_init(db_disc_evt_handler);
    APP_ERROR_CHECK(err_code);
    for (uint32_t i = 0; i < NRF_SDH_BLE_CENTRAL_LINK_COUNT; i++)
    {
        err_code = nrf_ble_amtc_init(&m_amtc[i], amtc_evt_handler);
        APP_ERROR_CHECK(err_code);
    }
}


static void server_init(void)
{
    nrf_ble_amts_init(&m_amts, amts_evt_handler);
}


/**@brief Function for initializing the BLE stack.
 *
 * @details Initializes the SoftDevice and the BLE event interrupt.
 */
static void ble_stack_init(void)
{
    ret_code_t err_code;

    err_code = nrf_sdh_enable_request();
    APP_ERROR_CHECK(err_code);

    // Configure the BLE stack using the default settings.
    // Fetch the start address of the application RAM.
    uint32_t ram_start = 0;
    err_code = nrf_sdh_ble_default_cfg_set(APP_BLE_CONN_CFG_TAG, &ram_start);
    APP_ERROR_CHECK(err_code);
    
//   /* set HVN queue size */
//   ble_cfg_t cfg;
//   memset(&cfg, 0, sizeof(ble_cfg_t));
//
//   cfg.conn_cfg.conn_cfg_tag = APP_BLE_CONN_CFG_TAG;
//   cfg.conn_cfg.params.gatts_conn_cfg.hvn_tx_queue_size = 8;
//
//   /* application wants to queue 7 HVNs */
//
//   sd_ble_cfg_set(BLE_CONN_CFG_GATTS, &cfg, ram_start);

    // Enable BLE stack.
    err_code = nrf_sdh_ble_enable(&ram_start);
    APP_ERROR_CHECK(err_code);

    // Register a handler for BLE events.
    NRF_SDH_BLE_OBSERVER(m_ble_observer, APP_BLE_OBSERVER_PRIO, ble_evt_handler, NULL);
}

/**@brief Function for the Peer Manager initialization.
 */
static void peer_manager_init(void)
{
    ble_gap_sec_params_t sec_param;
    ret_code_t err_code;

    err_code = pm_init();
    APP_ERROR_CHECK(err_code);

    memset(&sec_param, 0, sizeof(ble_gap_sec_params_t));

    // Security parameters to be used for all security procedures.
    sec_param.bond           = SEC_PARAM_BOND;
    sec_param.mitm           = SEC_PARAM_MITM;
    sec_param.lesc           = SEC_PARAM_LESC;
    sec_param.keypress       = SEC_PARAM_KEYPRESS;
    sec_param.io_caps        = SEC_PARAM_IO_CAPABILITIES;
    sec_param.oob            = SEC_PARAM_OOB;
    sec_param.min_key_size   = SEC_PARAM_MIN_KEY_SIZE;
    sec_param.max_key_size   = SEC_PARAM_MAX_KEY_SIZE;
    sec_param.kdist_own.enc  = 1;
    sec_param.kdist_own.id   = 1;
    sec_param.kdist_peer.enc = 1;
    sec_param.kdist_peer.id  = 1;

    err_code = pm_sec_params_set(&sec_param);
    APP_ERROR_CHECK(err_code);

    err_code = pm_register(pm_evt_handler);
    APP_ERROR_CHECK(err_code);
}

/**@brief Clear bond information from persistent storage.
 */
static void delete_bonds(void)
{
    ret_code_t err_code;

    NRF_LOG_INFO("Erase bonds!");

    err_code = pm_peers_delete();
    APP_ERROR_CHECK(err_code);
}

/**@brief Function for initializing GAP parameters.
 *
 * @details This function sets up all the necessary GAP (Generic Access Profile) parameters of the
 *          device including the device name and the preferred connection parameters.
 */
static void gap_params_init(void)
{
    ret_code_t              err_code;
    ble_gap_conn_sec_mode_t sec_mode;

    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&sec_mode);
    
    err_code = sd_ble_gap_device_name_set(&sec_mode,
                                          (uint8_t const *)DEVICE_NAME,
                                          strlen(DEVICE_NAME));
    APP_ERROR_CHECK(err_code);

    err_code = sd_ble_gap_ppcp_set(&m_conn_param);
    APP_ERROR_CHECK(err_code);
}


/**@brief Function for initializing the GATT library. */
static void gatt_init(void)
{
    ret_code_t err_code = nrf_ble_gatt_init(&m_gatt, gatt_evt_handler);
    APP_ERROR_CHECK(err_code);
}


static void wait_for_event(void)
{
    (void) sd_app_evt_wait();
}


void preferred_phy_set(ble_gap_phys_t * p_phy)
{
#if defined(S140)
    ble_opt_t opts;
    memset(&opts, 0x00, sizeof(ble_opt_t));
    memcpy(&opts.gap_opt.preferred_phys, p_phy, sizeof(ble_gap_phys_t));
    ret_code_t err_code = sd_ble_opt_set(BLE_GAP_OPT_PREFERRED_PHYS_SET, &opts);
    APP_ERROR_CHECK(err_code);
#endif
    memcpy(&m_test_params.phys, p_phy, sizeof(ble_gap_phys_t));
}


void gatt_mtu_set(uint16_t att_mtu)
{
    ret_code_t err_code;

    m_test_params.att_mtu = att_mtu;

    err_code = nrf_ble_gatt_att_mtu_periph_set(&m_gatt, att_mtu);
    APP_ERROR_CHECK(err_code);

    err_code = nrf_ble_gatt_att_mtu_central_set(&m_gatt, att_mtu);
    APP_ERROR_CHECK(err_code);
}


void conn_interval_set(uint16_t value)
{
    m_test_params.conn_interval = value;
}


static void conn_evt_len_ext_set(bool status)
{
    ret_code_t err_code;
    ble_opt_t  opt;

    memset(&opt, 0x00, sizeof(opt));
    opt.common_opt.conn_evt_ext.enable = status ? 1 : 0;

    err_code = sd_ble_opt_set(BLE_COMMON_OPT_CONN_EVT_EXT, &opt);
    APP_ERROR_CHECK(err_code);
}


void data_len_ext_set(bool status)
{
    m_test_params.data_len_ext_enabled = status;

    uint8_t data_length = status ? (247 + L2CAP_HDR_LEN) : (23 + L2CAP_HDR_LEN);
    (void) nrf_ble_gatt_data_length_set(&m_gatt, BLE_CONN_HANDLE_INVALID, data_length);
}


bool is_slave_board(void)
{
    return (m_board_role == BOARD_SLAVE);
}


void current_config_print(nrf_cli_t const * p_cli)
{
    char const * role = (m_board_role == BOARD_SLAVE) ? "slave" :
                        (m_board_role == BOARD_MASTER)  ? "master" : "not selected";

    nrf_cli_fprintf(p_cli, NRF_CLI_NORMAL, "==== Current test configuration ====\r\n");
    nrf_cli_fprintf(p_cli, NRF_CLI_NORMAL,
                    "Board role:\t\t%s\r\n"
                    "ATT MTU size:\t\t%d\r\n"
                    "Connection interval:\t%d units\r\n"
                    "Data length ext:\t%s\r\n"
                    "Connection length ext:\t%s\r\n"
                    "Preferred PHY:\t\t%s\r\n",
                    role,
                    m_test_params.att_mtu,
                    m_test_params.conn_interval,
                    m_test_params.data_len_ext_enabled ? "on" : "off",
                    m_test_params.conn_evt_len_ext_enabled ? "on" : "off",
                    phy_str(m_test_params.phys));
}


void test_begin(void)
{
    NRF_LOG_INFO("Preparing the test.");
    NRF_LOG_FLUSH();

#if defined(S132)
    // PHY does not need to be updated for s132
    if(m_board_role == BOARD_SLAVE||BOARD_RELAY)
    {
      m_phy_updated = true;
    }
#endif
    switch (m_gap_role)
    {
        default:
            // If no connection was established, the role won't be either.
            // In this case, start both advertising and scanning.
            //advertising_start();
          if (m_board_role == BOARD_MASTER)
          {
            scan_start();
          }
            break;

        case BLE_GAP_ROLE_PERIPH:
            advertising_start();
            m_test_params.phys.tx_phys = BLE_GAP_PHY_2MBPS; 
            break;

        case BLE_GAP_ROLE_CENTRAL:
            scan_start();
            break;
    }
}


static void test_run(void)
{
  //  counter_start();
//    if (m_board_role == BOARD_SLAVE)
//    {
//       NRF_LOG_INFO("Slave counter start!");
//    }
//    else if (m_board_role == BOARD_RELAY)
//    {
//      NRF_LOG_INFO( "Relay counter start!");
//    }   
    application_timers_start();
    nrf_ble_amts_notif_spam(&m_amts);
}


static bool is_test_ready()
{
    return (   (m_board_role == BOARD_SLAVE||BOARD_RELAY)
            && m_conn_interval_configured
            && m_notif_enabled
            && m_mtu_exchanged
            && m_data_length_updated
            && m_phy_updated
            && !m_run_test);
}


static void test_terminate(void)
{
    m_run_test                 = false;
    m_notif_enabled            = false;
    m_mtu_exchanged            = false;
    m_data_length_updated      = false;
    m_phy_updated              = false;
    m_conn_interval_configured = false;

    if (m_conn_handle != BLE_CONN_HANDLE_INVALID)
    {
        NRF_LOG_INFO("Disconnecting...");

        ret_code_t err_code;
        err_code = sd_ble_gap_disconnect(m_conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);

        if (err_code != NRF_SUCCESS)
        {
            NRF_LOG_ERROR("sd_ble_gap_disconnect() failed: 0x%0x.", err_code);
        }
        delete_bonds();
    }
    else
    {
        if (m_board_role == BOARD_RELAY)
        {
            if (m_gap_role == BLE_GAP_ROLE_PERIPH)
            {
                advertising_start();
            }
            else
            {
                scan_start();
            }
        }
    }
}


void cli_init(void)
{
    if (CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk)
    {
        ret_code_t err_code = nrf_cli_init(&m_cli_rtt, NULL, true, true, NRF_LOG_SEVERITY_INFO);
        APP_ERROR_CHECK(err_code);
    }

    nrf_drv_uart_config_t uart_config = NRF_DRV_UART_DEFAULT_CONFIG;
    uart_config.pseltxd = TX_PIN_NUMBER;
    uart_config.pselrxd = RX_PIN_NUMBER;
    uart_config.hwfc    = NRF_UART_HWFC_DISABLED;

    ret_code_t err_code = nrf_cli_init(&m_cli_uart, &uart_config, true, true, NRF_LOG_SEVERITY_INFO);
    APP_ERROR_CHECK(err_code);
}


void cli_start(void)
{
    ret_code_t err_code =  nrf_cli_start(&m_cli_uart);
    APP_ERROR_CHECK(err_code);
}


void cli_process(void)
{
    nrf_cli_process(&m_cli_uart);
}



int main(void)
{
    log_init();

    cli_init();
    leds_init();
    timer_init();
    counter_init();
    buttons_init();
    ble_stack_init();
    gap_params_init();
    gatt_init();
    peer_manager_init();
    advertising_data_set();

    server_init();
    client_init();
    
    
    gatt_mtu_set(m_test_params.att_mtu);
    data_len_ext_set(m_test_params.data_len_ext_enabled);
    conn_evt_len_ext_set(m_test_params.conn_evt_len_ext_enabled);
    preferred_phy_set(&m_test_params.phys);

    cli_start();

    buttons_enable();

    NRF_LOG_INFO("ATT MTU measurement started.");
//    NRF_LOG_INFO("Press button 2 on the relay board.");
//    NRF_LOG_INFO("Press button 3 on the slave board.");
//    NRF_LOG_INFO("Press button 4 on the master board.");
    NRF_LOG_INFO("This board will act as a relay.");
    //adv_scan_start();
    advertising_start();

    for (;;)
    {

        cli_process();

        if (is_test_ready())
        {
            NRF_LOG_INFO("Test started");
            m_run_test = true;
            test_run();
        }

        if (NRF_LOG_PROCESS() == false)
        {
            wait_for_event();
        }
    }
}


/**
 * @}
 */
