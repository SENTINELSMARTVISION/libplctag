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

#include <stdbool.h>

typedef enum {
    STATUS_OK = 0,
    STATUS_PENDING,
    STATUS_TERMINATE,
    STATUS_WOULD_BLOCK,
    STATUS_NOT_FOUND,
    STATUS_NOT_RECOGNIZED,
    STATUS_NOT_SUPPORTED,
    STATUS_BAD_INPUT,
    STATUS_ABORTED,
    STATUS_BUSY,
    STATUS_PARTIAL,
    STATUS_OUT_OF_BOUNDS,
    STATUS_TIMEOUT,
    STATUS_NULL_PTR,
    STATUS_NO_RESOURCE,
    STATUS_SETUP_FAILURE,
    STATUS_INTERNAL_FAILURE,
    STATUS_EXTERNAL_FAILURE,
    STATUS_NOT_ALLOWED,
} status_t;


static inline bool status_is_warning(status_t status)
{
    return (status >= 1000 && status < 2000 ? true : false);
}

static inline bool status_is_error(status_t status)
{
    return (status >= 2000 ? true : false);
}

extern const char *status_to_str(status_t status);
