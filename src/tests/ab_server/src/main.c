/***************************************************************************
 *   Copyright (C) 2024 by Kyle Hayes                                      *
 *   Author Kyle Hayes  kyle.hayes@gmail.com                               *
 *                                                                         *
 * This software is available under either the Mozilla Public License      *
 * version 2.0 or the GNU LGPL version 2 (or later) license, whichever     *
 * you choose.                                                             *
 *                                                                         *
 * MPL 2.0:                                                                *
 *                                                                         *
 *   This Source Code Form is subject to the terms of the Mozilla Public   *
 *   License, v. 2.0. If a copy of the MPL was not distributed with this   *
 *   file, You can obtain one at http://mozilla.org/MPL/2.0/.              *
 *                                                                         *
 *                                                                         *
 * LGPL 2:                                                                 *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library General Public License as       *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this program; if not, write to the                 *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "eip.h"
#include "plc.h"
#include "arg_parser.h"
#include "utils/compat.h"
#include "utils/debug.h"
#include "utils/net_event.h"
#include "utils/slice.h"
#include "utils/status.h"
// #include "utils/tcp_server.h"
#include "utils/time_utils.h"


static void usage(void);
// static tcp_connection_p allocate_client(void *template_connection_arg);


#ifdef IS_WINDOWS

typedef volatile int sig_flag_t;

sig_flag_t done = 0;

/* straight from MS' web site :-) */
int WINAPI CtrlHandler(DWORD fdwCtrlType)
{
    switch (fdwCtrlType)
    {
        // Handle the CTRL-C signal.
    case CTRL_C_EVENT:
        info("^C event");
        done = 1;
        return TRUE;

        // CTRL-CLOSE: confirm that the user wants to exit.
    case CTRL_CLOSE_EVENT:
        info("Close event");
        done = 1;
        return TRUE;

        // Pass other signals to the next handler.
    case CTRL_BREAK_EVENT:
        info("^Break event");
        done = 1;
        return TRUE;

    case CTRL_LOGOFF_EVENT:
        info("Logoff event");
        done = 1;
        return TRUE;

    case CTRL_SHUTDOWN_EVENT:
        info("Shutdown event");
        done = 1;
        return TRUE;

    default:
        info("Default Event: %d", fdwCtrlType);
        return FALSE;
    }
}


void setup_break_handler(void)
{
    if (!SetConsoleCtrlHandler(CtrlHandler, TRUE))
    {
        printf("\nERROR: Could not set control handler!\n");
        usage();
    }
}

#else

typedef volatile sig_atomic_t sig_flag_t;

sig_flag_t done = 0;

void SIGINT_handler(int not_used)
{
    (void)not_used;

    done = 1;
}

void setup_break_handler(void)
{
    struct sigaction act;

    /* set up signal handler. */
    memset(&act, 0, sizeof(act));
    act.sa_handler = SIGINT_handler;
    sigaction(SIGINT, &act, NULL);
}

#endif


bool program_terminating(app_data_p app_data);
void terminate_program(app_data_p app_data);

static net_event_callback_result_t event_manager_on_dispose_cb(struct net_event_manager_t *event_manager, void *app_data);
static net_event_callback_result_t event_manager_on_start_cb(struct net_event_manager_t *event_manager, void *app_data);
static net_event_callback_result_t event_manager_on_stop_cb(struct net_event_manager_t *event_manager, void *app_data);
static net_event_callback_result_t event_manager_on_tick_cb(struct net_event_manager_t *event_manager, void *app_data);
static net_event_callback_result_t event_manager_on_wake_cb(struct net_event_manager_t *event_manager, void *app_data);


static net_event_callback_result_t listener_socket_on_accepted_cb(struct net_event_socket_t *socket, struct net_event_socket_t *client_socket, status_t status, void *app_data, void *socket_data);
static net_event_callback_result_t listener_socket_on_close_cb(struct net_event_socket_t *socket, status_t status, void *app_data, void *socket_data);
static net_event_callback_result_t client_socket_on_close_cb(struct net_event_socket_t *socket, status_t status, void *app_data, void *socket_data);
static net_event_callback_result_t client_socket_on_received_cb(struct net_event_socket_t *socket, const char *sender_ip, uint16_t sender_port, struct buf_t *buffer, status_t status, void *app_data, void *socket_data);
static net_event_callback_result_t client_socket_on_sent_cb(struct net_event_socket_t *socket, struct buf_t *buffer, status_t status, void *app_data, void *socket_data);
static net_event_callback_result_t socket_on_tick_cb(struct net_event_socket_t *socket, status_t status, void *app_data, void *socket_data);
static net_event_callback_result_t socket_on_wake_cb(struct net_event_socket_t *socket, status_t status, void *app_data, void *socket_data);



int main(int argc, const char **argv)
{
    plc_t plc = {0};
    struct net_event_manager_cb_t event_manager_cb;
    struct net_event_socket_cb_t listener_cb = {0};
    struct net_event_socket_cb_t client_cb = {0};
    struct net_event_manager_t *event_manager = NULL;
    struct net_event_socket_t *listener_socket = NULL;

    /* set up handler for ^C etc. */
    setup_break_handler();

    debug_set_level(DEBUG_INFO);

    /* set the random seed. */
    srand((unsigned int)util_time_ms());

    if(!process_args(argc, argv, &plc)) {
        usage();
    }

    event_manager_cb.on_dispose_cb = event_manager_on_dispose_cb;
    event_manager_cb.on_start_cb = event_manager_on_start_cb;
    event_manager_cb.on_stop_cb = event_manager_on_stop_cb;
    event_manager_cb.on_tick_cb = event_manager_on_tick_cb;
    event_manager_cb.on_wake_cb = event_manager_on_wake_cb;

    event_manager = net_event_manager_create(100, &plc, &event_manager_cb);
    if(!event_manager) {
        warn("Unable to allocate memory for new event manager!");
        usage();
    }

    net_event_manager_start(event_manager);
    // FIXME - check status!

    listener_cb.on_accepted_cb = listener_socket_on_accepted_cb;
    listener_cb.on_close_cb = listener_socket_on_close_cb;
    listener_cb.on_tick_cb = socket_on_tick_cb;
    listener_cb.on_wake_cb = socket_on_wake_cb;

    client_cb.on_close_cb = client_socket_on_close_cb;
    client_cb.on_received_cb = client_socket_on_received_cb;
    client_cb.on_sent_cb = client_socket_on_sent_cb;
    client_cb.on_tick_cb = socket_on_tick_cb;
    client_cb.on_wake_cb = socket_on_wake_cb;

    listener_socket = net_event_socket_open(event_manager, NET_EVENT_SOCKET_TYPE_TCP_LISTENER, "0.0.0.0", 44818, &client_cb, &listener_cb);
    if(!event_manager) {
        warn("Unable to allocate memory for new listener socket!");
        usage();
    }



    // server_config.host = "0.0.0.0";
    // server_config.port = (plc.port_string ? plc.port_string : "44818");

    // server_config.app_connection_data_size = sizeof(plc_t);
    // server_config.app_data = (app_data_p)&plc;
    // server_config.buffer_size = MAX_DEVICE_BUFFER_SIZE;
    // server_config.clean_up_app_connection_data = clean_up_plc_connection_data;
    // server_config.init_app_connection_data = init_plc_connection_data;
    // server_config.process_request = eip_process_pdu;
    // server_config.program_terminating = program_terminating;
    // server_config.terminate_program = terminate_program;

    // /* open a server connection and listen on the right port. */
    // tcp_server_run(&server_config);

    return 0;
}


void usage(void)
{
    fprintf(stderr, "Usage: ab_server --plc=<plc_type> [--path=<path>] [--port=<port>] --tag=<tag>\n"
                    "   <plc type> = one of the CIP PLCs: \"ControlLogix\", \"Micro800\" or \"Omron\",\n"
                    "                or one of the PCCC PLCs: \"PLC/5\", \"SLC500\" or \"Micrologix\".\n"
                    "\n"
                    "   <path> = (required for ControlLogix) internal path to CPU in PLC.  E.g. \"1,0\".\n"
                    "\n"
                    "   <port> = (required for ControlLogix) internal path to CPU in PLC.  E.g. \"1,0\".\n"
                    "            Defaults to 44818.\n"
                    "\n"
                    "    PCCC-based PLC tags are in the format: <file>[<size>] where:\n"
                    "        <file> is the data file, only the following are supported:\n"
                    "            N7   - 2-byte signed integer.\n"
                    "            F8   - 4-byte floating point number.\n"
                    "            ST18 - 82-byte ASCII string.\n"
                    "            L19  - 4-byte signed integer.\n"
                    "\n"
                    "        <size> field is the length of the data file.\n"
                    "\n"
                    "    CIP-based PLC tags are in the format: <name>:<type>[<sizes>] where:\n"
                    "        <name> is alphanumeric, starting with an alpha character.\n"
                    "        <type> is one of:\n"
                    "            SINT   - 1-byte signed integer.  Requires array size(s).\n"
                    "            INT    - 2-byte signed integer.  Requires array size(s).\n"
                    "            DINT   - 4-byte signed integer.  Requires array size(s).\n"
                    "            LINT   - 8-byte signed integer.  Requires array size(s).\n"
                    "            REAL   - 4-byte floating point number.  Requires array size(s).\n"
                    "            LREAL  - 8-byte floating point number.  Requires array size(s).\n"
                    "            STRING - 82-byte string.  Requires array size(s).\n"
                    "            BOOL   - 1-byte boolean value.  Requires array size(s).\n"
                    "\n"
                    "        <sizes> field is one or more (up to 3) numbers separated by commas.\n"
                    "\n"
                    "Example: ab_server --plc=ControlLogix --path=1,0 --tag=MyTag:DINT[10,10]\n");

    exit(1);
}



status_t init_plc_connection_data(app_connection_data_p app_connection_data, app_data_p app_data)
{
    plc_connection_p connection = (plc_connection_p)app_connection_data;
    plc_connection_p template_connection = (plc_connection_p)app_data;

    /* copy the template data */
    *connection = *template_connection;

    /* fill in anything that changes */
    // connection->tcp_connection.request_buffer.start = &(connection->pdu_data_buffer);
    // connection->tcp_connection.request_buffer.end = &(connection->pdu_data_buffer) + MAX_DEVICE_BUFFER_SIZE;
    // connection->tcp_connection.response_buffer.start = &(connection->pdu_data_buffer);
    // connection->tcp_connection.response_buffer.end = &(connection->pdu_data_buffer) + MAX_DEVICE_BUFFER_SIZE;
    // connection->tcp_connection.handler = eip_process_pdu;

    return STATUS_OK;
}


bool program_terminating(app_data_p app_data)
{
    (void)app_data;

    return (done ? true : false);
}

void terminate_program(app_data_p app_data)
{
    (void)app_data;
}



net_event_callback_result_t event_manager_on_dispose_cb(struct net_event_manager_t *event_manager, void *app_data)
{
    net_event_callback_result_t rc = NET_EVENT_CALLBACK_RESULT_RESET;

    info("Starting.");


    info("Done with status %s.", rc == NET_EVENT_CALLBACK_RESULT_RESET ? "Reset event" : "Clear event";

    return rc;
}


net_event_callback_result_t event_manager_on_start_cb(struct net_event_manager_t *event_manager, void *app_data)
{
    net_event_callback_result_t rc = NET_EVENT_CALLBACK_RESULT_RESET;

    info("Starting.");


    info("Done with status %s.", rc == NET_EVENT_CALLBACK_RESULT_RESET ? "Reset event" : "Clear event";

    return rc;
}



net_event_callback_result_t event_manager_on_stop_cb(struct net_event_manager_t *event_manager, void *app_data)
{
    net_event_callback_result_t rc = NET_EVENT_CALLBACK_RESULT_RESET;

    info("Starting.");


    info("Done with status %s.", rc == NET_EVENT_CALLBACK_RESULT_RESET ? "Reset event" : "Clear event";

    return rc;
}



net_event_callback_result_t event_manager_on_tick_cb(struct net_event_manager_t *event_manager, void *app_data)
{
    net_event_callback_result_t rc = NET_EVENT_CALLBACK_RESULT_RESET;

    info("Starting.");


    info("Done with status %s.", rc == NET_EVENT_CALLBACK_RESULT_RESET ? "Reset event" : "Clear event";

    return rc;
}



net_event_callback_result_t event_manager_on_wake_cb(struct net_event_manager_t *event_manager, void *app_data)
{
    net_event_callback_result_t rc = NET_EVENT_CALLBACK_RESULT_RESET;

    info("Starting.");


    info("Done with status %s.", rc == NET_EVENT_CALLBACK_RESULT_RESET ? "Reset event" : "Clear event";

    return rc;
}





net_event_callback_result_t listener_socket_on_accepted_cb(struct net_event_socket_t *socket, struct net_event_socket_t *client_socket, status_t status, void *app_data, void *socket_data)
{
    net_event_callback_result_t rc = NET_EVENT_CALLBACK_RESULT_RESET;
    struct net_event_socket_cb_config_t *client_cb = (struct net_event_socket_cb_config_t *)socket_data;

    info("Starting.");

    if(status != STATUS_OK) {
        warn("Status %s received!", status_to_str(status));

         /* shutdown everything */
    }

    detail("Setting client socket callbacks.")
    net_event_socket_set_cb_config(client_socket, client_cb);




    info("Done with status %s.", rc == NET_EVENT_CALLBACK_RESULT_RESET ? "Reset event" : "Clear event";

    return rc;
}



net_event_callback_result_t listener_socket_on_close_cb(struct net_event_socket_t *socket, status_t status, void *app_data, void *socket_data)
{
    net_event_callback_result_t rc = NET_EVENT_CALLBACK_RESULT_RESET;

    info("Starting.");


    info("Done with status %s.", rc == NET_EVENT_CALLBACK_RESULT_RESET ? "Reset event" : "Clear event";

    return rc;
}



net_event_callback_result_t client_socket_on_close_cb(struct net_event_socket_t *socket, status_t status, void *app_data, void *socket_data)
{
    net_event_callback_result_t rc = NET_EVENT_CALLBACK_RESULT_RESET;

    info("Starting.");


    info("Done with status %s.", rc == NET_EVENT_CALLBACK_RESULT_RESET ? "Reset event" : "Clear event";

    return rc;
}



net_event_callback_result_t client_socket_on_received_cb(struct net_event_socket_t *socket, const char *sender_ip, uint16_t sender_port, struct buf_t *buffer, status_t status, void *app_data, void *socket_data)
{
    net_event_callback_result_t rc = NET_EVENT_CALLBACK_RESULT_RESET;

    info("Starting.");


    info("Done with status %s.", rc == NET_EVENT_CALLBACK_RESULT_RESET ? "Reset event" : "Clear event";

    return rc;
}



net_event_callback_result_t client_socket_on_sent_cb(struct net_event_socket_t *socket, struct buf_t *buffer, status_t status, void *app_data, void *socket_data)
{
    net_event_callback_result_t rc = NET_EVENT_CALLBACK_RESULT_RESET;

    info("Starting.");


    info("Done with status %s.", rc == NET_EVENT_CALLBACK_RESULT_RESET ? "Reset event" : "Clear event";

    return rc;
}



net_event_callback_result_t socket_on_tick_cb(struct net_event_socket_t *socket, status_t status, void *app_data, void *socket_data)
{
    net_event_callback_result_t rc = NET_EVENT_CALLBACK_RESULT_RESET;

    info("Starting.");


    info("Done with status %s.", rc == NET_EVENT_CALLBACK_RESULT_RESET ? "Reset event" : "Clear event";

    return rc;
}



net_event_callback_result_t socket_on_wake_cb(struct net_event_socket_t *socket, status_t status, void *app_data, void *socket_data)
{
    net_event_callback_result_t rc = NET_EVENT_CALLBACK_RESULT_RESET;

    info("Starting.");


    info("Done with status %s.", rc == NET_EVENT_CALLBACK_RESULT_RESET ? "Reset event" : "Clear event";

    return rc;
}
