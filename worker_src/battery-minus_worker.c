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

#include <pebble_worker.h>

#include "../src/storage.h"

static struct event current_page[PAGE_LENGTH];
static unsigned index;
static BatteryChargeState previous;

/******************************
 * LOW LEVEL EVENT MANAGEMENT *
 ******************************/

static void
append_event(struct event *event) {
	int ret;

	if (index >= PAGE_LENGTH) {
		APP_LOG(APP_LOG_LEVEL_ERROR,
		    "invalid value %u for index", index);
		return;
	}

	current_page[index] = *event;
	index = (index + 1) % PAGE_LENGTH;

	ret = persist_write_data(1, current_page, sizeof current_page);
	if (ret < 0 || (unsigned)ret != sizeof current_page)
		APP_LOG(APP_LOG_LEVEL_ERROR,
		    "unexpected return value %d for persist_wride_data",
		    ret);
}

static uint8_t
convert_state(BatteryChargeState *state) {
	if (state->charge_percent > 100) return ANOMALOUS_VALUE;
	return state->charge_percent | (state->is_charging ? 0x80 : 0);
}

/*********************
 * HIGH LEVEL EVENTS *
 *********************/

static void
new_event(uint8_t before, uint8_t after) {
	struct event event;

	event.time = time(0);
	event.before = before;
	event.after = after;
	append_event(&event);
}

static void
app_started(void) {
	uint8_t current = convert_state(&previous);

	if (current == ANOMALOUS_VALUE) {
		new_event(APP_STARTED, UNKNOWN);
		new_event(ANOMALOUS_VALUE, previous.charge_percent);
	} else
		new_event(APP_STARTED, current);
}

static void
app_stopped(void) {
	uint8_t current = convert_state(&previous);

	new_event(APP_CLOSED, current == ANOMALOUS_VALUE ? UNKNOWN : current);
}

static void
battery_update(BatteryChargeState *before, BatteryChargeState *after) {
	uint8_t i_before = convert_state(before);
	uint8_t i_after = convert_state(after);

	if (i_after == ANOMALOUS_VALUE)
		new_event(ANOMALOUS_VALUE, after->charge_percent);
	else
		new_event(i_before == ANOMALOUS_VALUE ? UNKNOWN : i_before,
		    i_after);
}

/*****************
 * EVENT HANDLER *
 *****************/

static void
battery_handler(BatteryChargeState charge) {
	if (charge.charge_percent == previous.charge_percent
	    && charge.is_charging == previous.is_charging)
		return;

	battery_update(&previous, &charge);
	previous = charge;
}

/***********************************
 * INITIALIZATION AND FINALIZATION *
 ***********************************/

static bool
init(void) {
	int ret = persist_read_data(1, current_page, sizeof current_page);

	if (ret == E_DOES_NOT_EXIST) {
		APP_LOG(APP_LOG_LEVEL_INFO,
		    "no configuration found, initializing to zero");
		memset(current_page, 0, sizeof current_page);
	} else if (ret != sizeof current_page) {
		APP_LOG(APP_LOG_LEVEL_ERROR,
		    "unexpected return value %d for persist_read_data",
		    ret);
		return false;
	} else if (current_page[0].time) {
		for (index = 1;
		    index < PAGE_LENGTH
		    && current_page[index - 1].time < current_page[index].time;
		    index += 1);
	} else
		index = 0;

	previous = battery_state_service_peek();
	app_started();

	battery_state_service_subscribe(&battery_handler);

	return true;
}

static void
deinit(void) {
	battery_state_service_unsubscribe();
	app_stopped();
}

int
main(void) {
	if (!init()) return 1;
	worker_event_loop();
	deinit();
	return 0;
}
