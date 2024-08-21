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

#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "debug.h"
#include "slice.h"
#include "util.h"


static status_t fix_up_alignment(char alignment_char, slice_p full_data_slice, slice_p unprocessed_data_slice);
static status_t decode_signed_int(const char int_size_char, bool little_endian, bool word_swap, slice_p unprocessed_data, void *dest);
static status_t encode_signed_int(const char int_size_char, bool little_endian, bool word_swap, va_list va, slice_p unprocessed_data);
static status_t decode_unsigned_int(const char int_size_char, bool little_endian, bool word_swap, slice_p unprocessed_data, void *data);
static status_t encode_unsigned_int(const char int_size_char, bool little_endian, bool word_swap, va_list va, slice_p unprocessed_data);
static status_t encode_unsigned_int_impl(const char int_size_char, bool little_endian, bool word_swap, slice_p unprocessed_data, uint64_t val_arg);
static status_t decode_float(const char int_size_char, bool little_endian, bool word_swap, slice_p unprocessed_data, void *data);
static status_t encode_float(const char int_size_char, bool little_endian, bool word_swap, va_list va, slice_p unprocessed_data);
static status_t decode_counted_byte_string(const char int_size_char, bool little_endian, bool word_swap, uint32_t multiplier, slice_p unprocessed_data, slice_p dest);
static status_t encode_counted_byte_string(const char int_size_char, bool little_endian, bool word_swap, bool byte_swap, uint32_t multiplier, slice_p unused_data_slice, slice_p src);
static status_t decode_terminated_byte_string(slice_p fmt_slice, slice_p unprocessed_data, slice_p dest);
static status_t encode_terminated_byte_string(slice_p fmt_slice, bool byte_swap, slice_p unused_data_slice, slice_p src);
static status_t check_or_allocate(slice_p slice, uint32_t required_size);
uint32_t *get_byte_order(size_t size, bool little_endian, bool word_swap);


status_t slice_split_at_ptr(slice_p source, uint8_t *cut_ptr, slice_p first_part, slice_p second_part)
{
    status_t rc = STATUS_OK;

    do {
        if(!source || !cut_ptr) {
            rc = STATUS_NULL_PTR;
            break;;
        }

        if(!first_part && !second_part) {
            rc = STATUS_NULL_PTR;
            break;
        }

        if(!slice_contains_ptr(source, cut_ptr)) {
            rc = STATUS_OUT_OF_BOUNDS;
            break;
        }

        if(first_part) {
            first_part->start = source->start;
            first_part->end = cut_ptr;
        }

        if(second_part) {
            second_part->start = cut_ptr;
            second_part->end = source->end;
        }
    } while(0);

    return rc;
}

status_t slice_split_at_offset(slice_p source, uint32_t offset, slice_p first_part, slice_p second_part)
{
    status_t rc = STATUS_OK;

    do {
        if(!source) {
            rc = STATUS_NULL_PTR;
            break;
        }

        if(!slice_contains_offset(source, offset)) {
            rc = STATUS_OUT_OF_BOUNDS;
            break;
        }

        rc = slice_split_at_ptr(source, source->start + offset, first_part, second_part);
    } while(0);

    return rc;
}



status_t slice_truncate_to_ptr(slice_p slice, uint8_t *ptr)
{
    status_t rc = STATUS_OK;

    do {
        if(!slice || !ptr) {
            rc = STATUS_NULL_PTR;
            break;
        }

        if(!slice_contains_ptr(slice, ptr)) {
            rc = STATUS_OUT_OF_BOUNDS;
            break;
        }

        slice->end = ptr;
    } while(0);

    return rc;
}


status_t slice_truncate_to_slice_end(slice_p slice, slice_p end_slice)
{
    status_t rc = STATUS_OK;

    if(slice && end_slice) {
        if(slice_contains_ptr(slice, end_slice->end)) {
            slice->end = end_slice->end;
        } else {
            rc = STATUS_OUT_OF_BOUNDS;
        }
    } else {
        rc = STATUS_NULL_PTR;
    }

    return rc;
}


status_t slice_to_string(slice_p slice, char *result, uint32_t result_size, bool byte_swap)
{
    status_t rc = STATUS_OK;
    uint32_t slice_length = 0;
    uint32_t str_length_required = 0;

    do {
        if(!slice) {
            warn("Slice pointer must not be NULL!");
            rc =  STATUS_NULL_PTR;
            break;
        }

        if(!result) {
            warn("Result pointer must not be NULL!");
            rc =  STATUS_NULL_PTR;
            break;
        }

        slice_length = slice_get_len(slice);

        /* if the raw data is byteswapped, we must have an even number of bytes. */
        if(byte_swap && (slice_length & 0x01)) {
            warn("Byteswapped string data must be even in length!");
            rc = STATUS_BAD_INPUT;
            break;
        }

        str_length_required += slice_length + 1; /* +1 for the nul terminator */

        if(str_length_required > result_size) {
            warn("Slice contains more data than can fit in the string buffer!");
            rc = STATUS_OUT_OF_BOUNDS;
            break;
        }

        for(uint32_t i=0; i < result_size; i++) {
            uint32_t index = i;

            if(! byte_swap) {
                index = i;
            } else {
                index = (i & 0x01) ? (i - 1) : (i + 1);
            }

            if(i < slice_length) {
                result[i] = *(char *)(slice->start + index);
            } else {
                result[i] = 0;
            }
        }
    } while(0);

    return rc;
}


status_t string_to_slice(const char *source, slice_p dest, bool byte_swap)
{
    status_t rc = STATUS_OK;
    uint32_t slice_length = 0;
    uint32_t slice_length_required = 0;
    uint32_t str_length = 0;

    do {
        if(!source) {
            warn("Source pointer must not be NULL!");
            rc =  STATUS_NULL_PTR;
            break;
        }

        str_length = strlen(source);

        if(!dest) {
            warn("Slice pointer must not be NULL!");
            rc =  STATUS_NULL_PTR;
            break;
        }

        slice_length = slice_get_len(dest);

        slice_length_required = str_length;

        /* if we are byte swapping we need to have an even number of bytes */
        if(byte_swap && (slice_length_required & 0x01)) {
            slice_length_required += 1;
        }

        if(slice_length_required > slice_get_len(dest)) {
            warn("Insufficient space in the destination slice!");
            rc = STATUS_NO_RESOURCE;
            break;
        }

        for(uint32_t i=0; i < slice_length_required; i++) {
            uint32_t index = i;

            if(! byte_swap) {
                index = i;
            } else {
                index = (i & 0x01) ? (i - 1) : (i + 1);
            }

            if(i < str_length) {
                *(char *)(dest->start + index) = source[i];
            } else {
                /* zero pad if we need to */
                *(char *)(dest->start + index) = 0;
            }
        }

        rc = slice_truncate_to_offset(dest, slice_length_required);
    } while(0);

    return rc;
}




status_t slice_from_slice(slice_p parent, slice_p new_slice, uint8_t *start, uint8_t *end)
{
    intptr_t parent_start = 0;
    intptr_t parent_end = 0;
    intptr_t new_slice_start = 0;
    intptr_t new_slice_end = 0;

    if(!parent) {
        warn("Parent pointer cannot be NULL!");
        return STATUS_NULL_PTR;
    }

    if(!new_slice) {
        warn("New slice pointer cannot be NULL!");
        return STATUS_NULL_PTR;
    }

    if(!start) {
        warn("Start pointer cannot be NULL!");
        return STATUS_NULL_PTR;
    }

    if(!end) {
        warn("End pointer cannot be NULL!");
        return STATUS_NULL_PTR;
    }

    parent_start = (intptr_t)(parent->start);
    parent_end = (intptr_t)(parent->end);
    new_slice_start = (intptr_t)(start);
    new_slice_end = (intptr_t)(end);

    if(parent_start > new_slice_start || new_slice_start > parent_end) {
        warn("Start must be inside the parent slice bounds!");
        return STATUS_OUT_OF_BOUNDS;
    }

    if(parent_start > new_slice_end || new_slice_end > parent_end) {
        warn("End must be inside the parent slice bounds!");
        return STATUS_OUT_OF_BOUNDS;
    }

    if(new_slice_start > new_slice_end) {
        warn("Start must be a lower address than end!");
        return STATUS_OUT_OF_BOUNDS;
    }

    new_slice->start = start;
    new_slice->end = end;

    return STATUS_OK;
}



status_t slice_split_middle_at_offsets(slice_p slice, uint32_t start_offset, uint32_t end_offset, slice_p first, slice_p second, slice_p third)
{
    status_t rc = STATUS_OK;

    if(slice && (first || second || third)) {
        if(slice_contains_offset(slice, start_offset) && slice_contains_offset(slice, end_offset)) {
            uint8_t *first_cut = slice->start + start_offset;
            uint8_t *second_cut = slice->start + end_offset;

            if(first) {
                first->start = slice->start;
                first->end = first_cut;
            }

            if(second) {
                second->start = first_cut;
                second->end = second_cut;
            }

            if(third) {
                third->start = second_cut;
                third->end = slice->end;
            }
        } else {
            rc = STATUS_OUT_OF_BOUNDS;
        }
    } else {
        rc = STATUS_NULL_PTR;
    }

    return rc;
}


status_t slice_get_u8_ptr(slice_p slice, uint8_t *ptr, uint8_t *val)
{
    if(!slice || !ptr || !val) {
        return STATUS_NULL_PTR;
    }

    if(!slice_contains_ptr(slice, ptr)) {
        return STATUS_OUT_OF_BOUNDS;
    }

    *val = *ptr;

    return STATUS_OK;
}

status_t slice_set_u8_ptr(slice_p slice, uint8_t *ptr, uint8_t val)
{
    if(!slice || !ptr) {
        return STATUS_NULL_PTR;
    }

    if(!slice_contains_ptr(slice, ptr)) {
        return STATUS_OUT_OF_BOUNDS;
    }

    *ptr = val;

    return STATUS_OK;
}

status_t slice_get_u8_offset(slice_p slice, uint32_t offset, uint8_t *val)
{
    if(!slice) {
        return STATUS_NULL_PTR;
    }

    return slice_get_u8_ptr(slice, slice->start + offset, val);
}

status_t slice_set_u8_offset(slice_p slice, uint32_t offset, uint8_t val)
{
    if(!slice) {
        return STATUS_NULL_PTR;
    }

    return slice_set_u8_ptr(slice, slice->start + offset, val);
}



status_t slice_get_u16_le_at_ptr(slice_p slice, uint8_t *ptr, uint16_t *val)
{
    if(!slice || !ptr || !val) {
        return STATUS_NULL_PTR;
    }

    if(!slice_contains_ptr(slice, ptr) || (slice_get_len_from_ptr(slice, ptr) < sizeof(*val))) {
        return STATUS_OUT_OF_BOUNDS;
    }

    *val = 0;

    *val |= (uint16_t)*(ptr);
    *val |= ((uint16_t)(*(ptr + 1)) << 8);

    return STATUS_OK;
}


status_t slice_set_u16_le_at_ptr(slice_p slice, uint8_t *ptr, uint16_t val)
{
    if(!slice || !ptr) {
        return STATUS_NULL_PTR;
    }

    if(!slice_contains_ptr(slice, ptr) || (slice_get_len_from_ptr(slice, ptr) < sizeof(val))) {
        return STATUS_OUT_OF_BOUNDS;
    }

    *ptr  = (uint8_t)(val & 0xFF);
    *(ptr + 1) = (uint8_t)((val >> 8) & 0xFF);

    return STATUS_OK;
}


status_t slice_get_u32_le_at_ptr(slice_p slice, uint8_t *ptr, uint32_t *val)
{
    if(!slice || !ptr || !val) {
        return STATUS_NULL_PTR;
    }

    if(!slice_contains_ptr(slice, ptr) || (slice_get_len_from_ptr(slice, ptr) < sizeof(*val))) {
        return STATUS_OUT_OF_BOUNDS;
    }

    *val = 0;

    *val |= (uint16_t)*(ptr);
    *val |= ((uint16_t)(*(ptr + 1)) << 8);
    *val |= ((uint32_t)(*(ptr + 2)) << 16);
    *val |= ((uint32_t)(*(ptr + 3)) << 24);

    return STATUS_OK;
}


status_t slice_set_u32_le_at_ptr(slice_p slice, uint8_t *ptr, uint32_t val)
{
    if(slice && ptr && val && slice_contains_ptr(slice, ptr) && slice_get_len_from_ptr(slice, ptr) >= sizeof(val)) {
        *ptr  = (uint8_t)(val & 0xFF);
        *(ptr + 1) = (uint8_t)((val >> 8) & 0xFF);
        *(ptr + 2) = (uint8_t)((val >> 16) & 0xFF);
        *(ptr + 3) = (uint8_t)((val >> 24) & 0xFF);

        return true;
    }

    return false;
}


bool slice_get_u64_le_at_ptr(slice_p slice, uint8_t *ptr, uint64_t *val)
{
    if(slice && ptr && val && slice_contains_ptr(slice, ptr) && slice_get_len_from_ptr(slice, ptr) >= sizeof(*val)) {
        *val = 0;

        *val |= (uint64_t)*(ptr);
        *val |= ((uint64_t)(*(ptr + 1)) << 8);
        *val |= ((uint64_t)(*(ptr + 2)) << 16);
        *val |= ((uint64_t)(*(ptr + 3)) << 24);
        *val |= ((uint64_t)(*(ptr + 4)) << 32);
        *val |= ((uint64_t)(*(ptr + 5)) << 40);
        *val |= ((uint64_t)(*(ptr + 6)) << 48);
        *val |= ((uint64_t)(*(ptr + 7)) << 56);

        return true;
    }

    return false;
}

bool slice_set_u64_le_at_ptr(slice_p slice, uint8_t *ptr, uint64_t val)
{
    if(slice && ptr && val && slice_contains_ptr(slice, ptr) && slice_get_len_from_ptr(slice, ptr) >= sizeof(val)) {
        *ptr  = (uint8_t)(val & 0xFF);
        *(ptr + 1) = (uint8_t)((val >> 8) & 0xFF);
        *(ptr + 2) = (uint8_t)((val >> 16) & 0xFF);
        *(ptr + 3) = (uint8_t)((val >> 24) & 0xFF);
        *(ptr + 4) = (uint8_t)((val >> 32) & 0xFF);
        *(ptr + 5) = (uint8_t)((val >> 40) & 0xFF);
        *(ptr + 6) = (uint8_t)((val >> 48) & 0xFF);
        *(ptr + 7) = (uint8_t)((val >> 56) & 0xFF);

        return true;
    }

    return false;
}



status_t slice_get_f32_le_at_ptr(slice_p slice, uint8_t *ptr, float *val)
{
    status_t rc = STATUS_OK;
    uint32_t u_val = 0;

    if(!val) {
        return STATUS_NULL_PTR;
    }

    rc = slice_get_u32_le_at_ptr(slice, ptr, &u_val);

    if(rc == STATUS_OK) {
        memcpy(val, &u_val, sizeof(*val));
    }

    return rc;
}

status_t slice_set_f32_le_at_ptr(slice_p slice, uint8_t *ptr, float val)
{
    status_t rc = STATUS_OK;
    uint32_t u_val = 0;

    memcpy(&u_val, &val, sizeof(u_val));

    return slice_set_u32_le_at_ptr(slice, ptr, u_val);
}



status_t slice_get_f64_le_at_ptr(slice_p slice, uint8_t *ptr, double *val)
{
    status_t rc = STATUS_OK;
    uint64_t u_val = 0;

    if(!val) {
        return STATUS_NULL_PTR;
    }

    if(slice_get_u64_le_at_ptr(slice, ptr, &u_val)) {
        memcpy(val, &u_val, sizeof(*val));
        return true;
    }

    return false;
}

status_t slice_set_f64_le_at_ptr(slice_p slice, uint8_t *ptr, double val)
{
    uint64_t u_val = 0;

    memcpy(&u_val, &val, sizeof(u_val));

    return slice_set_u64_le_at_ptr(slice, ptr, u_val);
}