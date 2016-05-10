/*
 * Copyright (c) 2016, Natacha Porté
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <inttypes.h>

#include "dict_tools.h"

static bool
check_length(Tuple *tuple, const char *context) {
	if (tuple->length == 1 || tuple->length == 2 || tuple->length == 4) {
		return true;
	}

	APP_LOG(APP_LOG_LEVEL_ERROR,
	    "Unexpected length %" PRIu16 " in %s dictionary entry",
	    tuple->length, context);
	return false;
}

static int32_t
raw_read_int(Tuple *tuple) {
	switch (tuple->length) {
	    case 1:
		return tuple->value->int8;
	    case 2:
		return tuple->value->int16;
	    case 4:
		return tuple->value->int32;
	    default:
		return 0;
	}
}

static uint32_t
raw_read_uint(Tuple *tuple) {
	switch (tuple->length) {
	    case 1:
		return tuple->value->uint8;
	    case 2:
		return tuple->value->uint16;
	    case 4:
		return tuple->value->uint32;
	    default:
		return 0;
	}
}

int32_t
tuple_int(Tuple *tuple) {
	if (!tuple) return 0;
	switch (tuple->type) {
	    case TUPLE_INT:
		if (!check_length(tuple, "integer")) return 0;
		return raw_read_int(tuple);
	    case TUPLE_UINT:
		if (!check_length(tuple, "integer")) return 0;
		uint32_t u = raw_read_uint(tuple);
		if (u > 2147483647) {
			APP_LOG(APP_LOG_LEVEL_ERROR,
			    "Integer overflow in signed dictionary entry %"
			    PRIu32, u);
			return 0;
		}
		return u;
	    default:
		APP_LOG(APP_LOG_LEVEL_ERROR,
		    "Unexpected type %d for integer dictionary entry",
		    (int)tuple->type);
		return 0;
	}
}

uint32_t
tuple_uint(Tuple *tuple) {
	if (!tuple) return 0;
	switch (tuple->type) {
	    case TUPLE_UINT:
		if (!check_length(tuple, "unsigned")) return 0;
		return raw_read_uint(tuple);
	    case TUPLE_INT:
		if (!check_length(tuple, "unsigned")) return 0;
		int32_t i = raw_read_int(tuple);
		if (i < 0) {
			APP_LOG(APP_LOG_LEVEL_ERROR,
			    "Integer underflow in unsigned dictionary entry %"
			    PRIi32, i);
			return 0;
		}
		return i;
	    default:
		APP_LOG(APP_LOG_LEVEL_ERROR,
		    "Unexpected type %d for unsigned dictionary entry",
		    (int)tuple->type);
		return 0;
	}
}
