/*
 * Copyright (c) 2015, Natacha Porté
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

#ifndef BATTERY_STORAGE_H
#define BATTERY_STORAGE_H

/* a single entry in the event log */
struct __attribute__((__packed__)) event {
	time_t time;
	uint8_t before;
	uint8_t after;
};

/*
 * after field has the following format:
 *  - the most significant is 1 when charging, 0 when discharging
 *  - the remaining 7 bits hold the battery percentage value
 *
 * before either the value before the event, using the same format as after,
 * or one of the following special value:
 *  - UNKNOWN (update from anomalous to normal value)
 *  - APP_STARTED
 *  - APP_CLOSED
 *  - ANOMALOUS_VALUE  (then after has the whole 8-bit value)
 */

#define UNKNOWN         0xF0
#define APP_STARTED     0xF1
#define APP_CLOSED      0xF2
#define ANOMALOUS_VALUE 0xF3

#define PAGE_LENGTH (PERSIST_DATA_MAX_LENGTH / sizeof(struct event))

#endif /* defined BATTERY_STORAGE_H */
