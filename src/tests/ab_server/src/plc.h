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

#pragma once

#include <stddef.h>
#include <stdint.h>
#include "cip.h"
#include "cpf.h"
#include "eip.h"
#include "pccc.h"
#include "utils/mutex_compat.h"


typedef uint16_t tag_type_t;

/* CIP data types. */
#define TAG_CIP_TYPE_BOOL        ((tag_type_t)0x00C1) /* 8-bit boolean value */
#define TAG_CIP_TYPE_SINT        ((tag_type_t)0x00C2) /* Signed 8–bit integer value */
#define TAG_CIP_TYPE_INT         ((tag_type_t)0x00C3) /* Signed 16–bit integer value */
#define TAG_CIP_TYPE_DINT        ((tag_type_t)0x00C4) /* Signed 32–bit integer value */
#define TAG_CIP_TYPE_LINT        ((tag_type_t)0x00C5) /* Signed 64–bit integer value */
#define TAG_CIP_TYPE_USINT       ((tag_type_t)0x00C6) /* Unsigned 8–bit integer value */
#define TAG_CIP_TYPE_UINT        ((tag_type_t)0x00C7) /* Unsigned 16–bit integer value */
#define TAG_CIP_TYPE_UDINT       ((tag_type_t)0x00C8) /* Unsigned 32–bit integer value */
#define TAG_CIP_TYPE_ULINT       ((tag_type_t)0x00C9) /* Unsigned 64–bit integer value */
#define TAG_CIP_TYPE_REAL        ((tag_type_t)0x00CA) /* 32–bit floating point value, IEEE format */
#define TAG_CIP_TYPE_LREAL       ((tag_type_t)0x00CB) /* 64–bit floating point value, IEEE format */

// FIXME - this is wrong below!
#define TAG_CIP_TYPE_STRING      ((tag_type_t)0x00D0) /* 88-byte string, with 82 bytes of data, 4-byte count and 2 bytes of padding */

/* PCCC data types.   FIXME */
#define TAG_PCCC_TYPE_INT         ((uint8_t)0x89) /* Signed 16–bit integer value */
#define TAG_PCCC_TYPE_DINT        ((uint8_t)0x91) /* Signed 32–bit integer value */
#define TAG_PCCC_TYPE_REAL        ((uint8_t)0x8a) /* 32–bit floating point value, IEEE format */
#define TAG_PCCC_TYPE_STRING      ((uint8_t)0x8d) /* 82-byte string with 2-byte count word. */

struct tag_def_t {
    struct tag_def_t *next_tag;
    char *name;
    mutex_t mutex; /* only one thread at a time can access. */
    tag_type_t tag_type;
    size_t elem_size;
    size_t elem_count;
    size_t data_file_num;
    size_t num_dimensions;
    size_t dimensions[3];
    uint8_t *data;
};

typedef struct tag_def_t tag_def_t;
typedef struct tag_def_t *tag_def_p;

typedef enum plc_type_t {
    PLC_CONTROL_LOGIX,
    PLC_MICRO800,
    PLC_OMRON,
    PLC_PLC5,
    PLC_SLC,
    PLC_MICROLOGIX
} plc_type_t;


const uint32_t MAX_DEVICE_BUFFER_SIZE = (0x10000);

typedef struct {
    struct tcp_connection_t tcp_connection;

    /* protocol-specific connection data */
    eip_connection_t eip_connection;
    cpf_connection_t cpf_connection;
    cip_connection_t cip_connection;
    pccc_connection_t pccc_connection;

    /* PLC info we might need */
    plc_type_t plc_type;
    const char *port_string;
    tag_def_p tags;

    /* a buffer for requests and responses */
    uint8_t buffer_data[MAX_DEVICE_BUFFER_SIZE];
} plc_connection_t;

typedef plc_connection_t *plc_connection_p;

typedef enum {
    /* start off above the TCP connection status */
    PLC_ERR_INSUFFICIENT_RESPONSE_DATA = TCP_CONNECTION_STATUS_LAST,
    PLC_ERR_NOT_FOUND,
} plc_status_t;


#define assert_warn(COND, STATUS, ... ) if(!(COND)) {            \
    debug_impl(__func__, __LINE__, DEBUG_WARN, __VA_ARGS__);    \
    rc = (STATUS);                                              \
    break;                                                      \
} do {} while(0)

#define assert_info(COND, STATUS, ... ) if(!(COND)) {            \
    debug_impl(__func__, __LINE__, DEBUG_INFO, __VA_ARGS__);    \
    rc = (STATUS);                                              \
    break;                                                      \
} do {} while(0)

#define assert_detail(COND, STATUS, ... ) if(!(COND)) {          \
    debug_impl(__func__, __LINE__, DEBUG_DETAIL, __VA_ARGS__);  \
    rc = (STATUS);                                              \
    break;                                                      \
} do {} while(0)

#define assert_flood(COND, STATUS, ... ) if(!(COND)) {          \
    debug_impl(__func__, __LINE__, DEBUG_FLOOD, __VA_ARGS__);   \
    rc = (STATUS);                                              \
    break;                                                      \
} do {} while(0)
