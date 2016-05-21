/*
 * Copyright (c) 2015-2016, Natacha Porté
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
#include <pebble.h>
#include "dict_tools.h"
#include "simple_dialog.h"
#include "storage.h"

#undef DISPLAY_TEST_DATA

static Window *window;
static SimpleMenuLayer *menu_layer;
static SimpleMenuSection menu_section;
static SimpleMenuItem menu_items[PAGE_LENGTH];

static struct event current_page[PAGE_LENGTH];
static const char *titles[PAGE_LENGTH];
static const char *dates[PAGE_LENGTH];
static int cfg_wakeup_time = -1;

static void
do_start_worker(int index, void *context);

static void
do_stop_worker(int index, void *context);

/*************
 * UTILITIES *
 *************/

static void
close_app(void) {
	window_stack_pop_all(true);
}

static unsigned
first_index(struct event *page, size_t page_length) {
	unsigned j;

	for (j = 1;
	    j < page_length && page[j - 1].time < page[j].time;
	    j += 1);
	if (j >= page_length || !page[j].time) j = 0;

	return j;
}

/**********************
 * DATA UPLOAD TO WEB *
 **********************/

#define MSG_KEY_LAST_SENT	110
#define MSG_KEY_DATA_TIME	210
#define MSG_KEY_DATA_LINE	220
#define MSG_KEY_CFG_WAKEUP_TIME	320

static unsigned upload_index;

static const char keyword_anomalous[] = "error";
static const char keyword_charge_start[] = "charge";
static const char keyword_charge_stop[] = "dischg";
static const char keyword_charging[] = "+";
static const char keyword_discharging[] = "-";
static const char keyword_unknown[] = "unknown";
static const char keyword_start[] = "start";
static const char keyword_start_charging[] = "start+";
static const char keyword_stop[] = "stop";
static const char keyword_stop_charging[] = "stop+";

static bool
event_csv_image(char *buffer, size_t size, struct event *event) {
	struct tm *tm;
	size_t ret;
	int i;
	const char *keyword;
	uint8_t int_1, int_2;
	bool has_int_2;

	if (!buffer || !event) return false;

	tm = gmtime(&event->time);
	if (!tm) {
		APP_LOG(APP_LOG_LEVEL_ERROR, "event_csv_image: "
		    "Unable to get UTC time for %" PRIi32, event->time);
		return false;
	}

	ret = strftime(buffer, size, "%FT%TZ", tm);
	if (!ret) {
		APP_LOG(APP_LOG_LEVEL_ERROR, "event_csv_image: "
		    "Unable to build RFC-3339 representation of %" PRIi32,
		    event->time);
		return false;
	}

	if (ret >= size) {
		APP_LOG(APP_LOG_LEVEL_ERROR, "event_csv_image: "
		    "Unexpected returned value %zu of strftime on buffer %zu",
		    ret, size);
		return false;
	}

	switch (event->before) {
	    case UNKNOWN:
		keyword = keyword_unknown;
		int_1 = event->after;
		has_int_2 = false;
		break;

	    case APP_STARTED:
		keyword = (event->after & 0x80)
		    ? keyword_start_charging : keyword_start;
		int_1 = event->after & 0x7f;
		has_int_2 = false;
		break;

	    case APP_CLOSED:
		keyword = (event->after & 0x80)
		    ? keyword_stop_charging : keyword_stop;
		int_1 = event->after & 0x7f;
		has_int_2 = false;
		break;

	    case ANOMALOUS_VALUE:
		keyword = keyword_anomalous;
		int_1 = event->after;
		has_int_2 = false;
		break;

	    default:
		keyword = (event->before & 0x80)
		    ? ((event->after & 0x80)
		      ? keyword_charging : keyword_charge_stop)
		    : ((event->after & 0x80)
		      ? keyword_charge_start : keyword_discharging);
		int_1 = event->after & 0x7f;
		int_2 = event->before & 0x7f;
		has_int_2 = true;
		break;
	}

	if (has_int_2)
		i = snprintf(buffer + ret, size - ret,
		    ",%s,%" PRIu8 ",%" PRIu8, keyword, int_1, int_2);
	else
		i = snprintf(buffer + ret, size - ret,
		    ",%s,%" PRIu8, keyword, int_1);

	if (i <= 0) {
		APP_LOG(APP_LOG_LEVEL_ERROR, "event_csv_image: "
		    "Unexpected return value %d from snprintf", i);
		return false;
	}

	return true;
}

static bool
send_event(struct event *event) {
	AppMessageResult msg_result;
	DictionaryIterator *iter;
	DictionaryResult dict_result;
	char buffer[64];
	bool result = true;

	if (!event) return false;

	if (!event_csv_image(buffer, sizeof buffer, event))
		return false;

	msg_result = app_message_outbox_begin(&iter);
	if (msg_result) {
		APP_LOG(APP_LOG_LEVEL_ERROR,
		    "send_event: app_message_outbox_begin returned %d",
		    (int)msg_result);
		return false;
	}

	dict_result = dict_write_int(iter, MSG_KEY_DATA_TIME,
	    &event->time, sizeof event->time, true);
	if (dict_result != DICT_OK) {
		APP_LOG(APP_LOG_LEVEL_ERROR,
		    "send_event: [%d] unable to add data time %" PRIi32,
		    (int)dict_result, event->time);
		result = false;
	}

	dict_result = dict_write_cstring(iter, MSG_KEY_DATA_LINE, buffer);
	if (dict_result != DICT_OK) {
		APP_LOG(APP_LOG_LEVEL_ERROR,
		    "send_event: [%d] unable to add data line \"%s\"",
		    (int)dict_result, buffer);
		result = false;
	}

	msg_result = app_message_outbox_send();
	if (msg_result) {
		APP_LOG(APP_LOG_LEVEL_ERROR,
		    "send_event: app_mesage_outbox_send returned %d",
		    (int)msg_result);
		result = false;
	}

	return result;
}

static void
handle_last_sent(Tuple *tuple) {
	time_t t = tuple_int(tuple);

	upload_index = first_index(current_page, PAGE_LENGTH);

	if (!current_page[upload_index].time) {
		/* empty page */
		if (launch_reason() == APP_LAUNCH_WAKEUP) close_app();
		return;
	}

	if (t)
		while (current_page[upload_index].time <= t) {
			unsigned next_index = (upload_index + 1) % PAGE_LENGTH;
			if (current_page[upload_index].time
			     > current_page[next_index].time) {
				/* end of page reached without match */
				if (launch_reason() == APP_LAUNCH_WAKEUP)
					close_app();
				return;
			}
			upload_index = next_index;
		}

	send_event(current_page + upload_index);
}

static void
inbox_received_handler(DictionaryIterator *iterator, void *context) {
	Tuple *tuple;
	(void)context;

	for (tuple = dict_read_first(iterator);
	    tuple;
	    tuple = dict_read_next(iterator)) {
		switch (tuple->key) {
		    case MSG_KEY_LAST_SENT:
			handle_last_sent(tuple);
			break;

		    case MSG_KEY_CFG_WAKEUP_TIME:
			cfg_wakeup_time = tuple_int(tuple);
			persist_write_int(MSG_KEY_CFG_WAKEUP_TIME,
			    cfg_wakeup_time + 1);
			break;

		    default:
			APP_LOG(APP_LOG_LEVEL_ERROR,
			    "Unknown key %" PRIu32 " in received message",
			    tuple->key);
			break;
		}
	}
}

static void
outbox_sent_handler(DictionaryIterator *iterator, void *context) {
	unsigned next_index = (upload_index + 1) % PAGE_LENGTH;
	(void)iterator;
	(void)context;

	if (current_page[upload_index].time <= current_page[next_index].time) {
		upload_index = next_index;
		send_event(current_page + next_index);
	} else if (launch_reason() == APP_LAUNCH_WAKEUP) {
		close_app();
	}
}

static void
outbox_failed_handler(DictionaryIterator *iterator, AppMessageResult reason,
    void *context) {
	(void)iterator;
	(void)context;
	APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox failed: 0x%x", (unsigned)reason);
	if (launch_reason() == APP_LAUNCH_WAKEUP)
		close_app();
}

/*************
 * MENU ITEM *
 *************/

#define SET_BUF(dest, src) (strcpy(dest, src ""), (int)(sizeof src) - 1)

static const char *
strdup(char *buffer) {
	size_t length = strlen(buffer);
	char *result = malloc(length + 1);
	if (result) memcpy(result, buffer, length + 1);
	return result;
}

static void
init_strings(void) {
	char buffer[256];
	int ret;
	struct tm *tm;
	unsigned j = first_index(current_page, PAGE_LENGTH);

	for (unsigned i = 0; i < PAGE_LENGTH; i += 1) {
		if (!current_page[j].time) {
			titles[i] = dates[i] = 0;
			continue;
		}

		tm = localtime(&current_page[j].time);
		ret = strftime(buffer, sizeof buffer, "%Y-%m-%d %H:%M:%S", tm);
		dates[i] = ret ? strdup(buffer) : 0;

		switch (current_page[j].before) {
		    case UNKNOWN:
			snprintf(buffer, sizeof buffer,
			    "%u%%%c",
			    (unsigned)(current_page[j].after & 0x7f),
			    (current_page[j].after & 0x80) ? '+' : '-');
			break;

		    case APP_STARTED:
			snprintf(buffer, sizeof buffer,
			    "Start %u%%%c",
			    (unsigned)(current_page[j].after & 0x7f),
			    (current_page[j].after & 0x80) ? '+' : '-');
			break;

		    case APP_CLOSED:
			snprintf(buffer, sizeof buffer,
			    "Close %u%%%c",
			    (unsigned)(current_page[j].after & 0x7f),
			    (current_page[j].after & 0x80) ? '+' : '-');
			break;

		    case ANOMALOUS_VALUE:
			snprintf(buffer, sizeof buffer,
			    "Anomalous %u",
			    (unsigned)(current_page[j].after));
			break;

		    default:
			if ((current_page[j].before & 0x80)
			    == (current_page[j].after & 0x80)) {
				snprintf(buffer, sizeof buffer,
				    "%u%% %c> %u%%",
				    (unsigned)(current_page[j].before & 0x7f),
				    (current_page[j].after & 0x80) ? '+' : '-',
				    (unsigned)(current_page[j].after & 0x7f));
				break;
			}

			if ((current_page[j].before & 0x7f)
			    == (current_page[j].after & 0x7f))
				snprintf (buffer, sizeof buffer,
				    "%s %u%%",
				    (current_page[j].after & 0x80)
				    ? "Charge" : "Discharge",
				    (unsigned)(current_page[j].after & 0x7f));
			else
				snprintf (buffer, sizeof buffer,
				    "%s %u%% -> %u%%",
				    (current_page[j].after & 0x80)
				    ? "Chg" : "Disch",
				    (unsigned)(current_page[j].before & 0x7f),
				    (unsigned)(current_page[j].after & 0x7f));
			break;
		}

		titles[i] = strdup(buffer);

		j = (j + 1) % PAGE_LENGTH;
	}
}

static time_t
latest_event(void) {
	time_t result = current_page[0].time;
	for (unsigned j = 1; j < PAGE_LENGTH; j += 1) {
		if (result < current_page[j].time) {
			result = current_page[j].time;
		}
	}
	return result;
}

static void
rebuild_menu(void) {
	unsigned i = PAGE_LENGTH;
	bool is_empty = true;
	time_t old_latest = latest_event();

	persist_read_data(1, current_page, sizeof current_page);

	if (old_latest != latest_event()) {
		for (unsigned i = 0; i < PAGE_LENGTH; i += 1) {
			free((void *)dates[i]);
			free((void *)titles[i]);
		}
		init_strings();
	}

	menu_section.title = 0;
	menu_section.items = menu_items;
	menu_section.num_items = 0;

	if (app_worker_is_running()) {
		menu_items[0] = (SimpleMenuItem){
		    .title = "Stop worker",
		    .callback = &do_stop_worker
		};
		menu_section.num_items += 1;
	} else {
		menu_items[0] = (SimpleMenuItem){
		    .title = "Start worker",
		    .callback = &do_start_worker
		};
		menu_section.num_items += 1;
	}

	while (i) {
		i -= 1;
		if (!titles[i]) continue;
		menu_items[menu_section.num_items].title = titles[i];
		menu_items[menu_section.num_items].subtitle = dates[i];
		menu_items[menu_section.num_items].icon = 0;
		menu_items[menu_section.num_items].callback = 0;
		menu_section.num_items += 1;
		is_empty = false;
	}

	if (is_empty) {
		menu_items[menu_section.num_items].title = "No event recorded";
		menu_items[menu_section.num_items].subtitle = 0;
		menu_items[menu_section.num_items].icon = 0;
		menu_items[menu_section.num_items].callback = 0;
		menu_section.num_items += 1;
	}
}

/****************
 * MENU ACTIONS *
 ****************/

static void
do_start_worker(int index, void *context) {
	(void)index;
	(void)context;
	char buffer[256];
	AppWorkerResult result = app_worker_launch();

	switch (result) {
	    case APP_WORKER_RESULT_SUCCESS:
		push_simple_dialog("Worker start requested.", true);
		break;
	    case APP_WORKER_RESULT_ALREADY_RUNNING:
		push_simple_dialog("Worker is already running.", true);
		break;
	    case APP_WORKER_RESULT_ASKING_CONFIRMATION:
		APP_LOG(APP_LOG_LEVEL_INFO,
		    "Frirmware requesting confirmation, skipping UI");
		break;
	    default:
		snprintf(buffer, sizeof buffer,
		   "Unexpected result %d",
		   (int)result);
		push_simple_dialog(buffer, false);
	}
}

static void
do_stop_worker(int index, void *context) {
	(void)index;
	(void)context;
	char buffer[256];
	AppWorkerResult result = app_worker_kill();

	switch (result) {
	    case APP_WORKER_RESULT_SUCCESS:
		push_simple_dialog("Worker stop requested.", true);
		break;
	    case APP_WORKER_RESULT_NOT_RUNNING:
		push_simple_dialog("Worker is already stopped.", true);
		break;
	    case APP_WORKER_RESULT_DIFFERENT_APP:
		push_simple_dialog("A different worker is running.", true);
		break;
	    case APP_WORKER_RESULT_ASKING_CONFIRMATION:
		APP_LOG(APP_LOG_LEVEL_INFO,
		    "Frirmware requesting confirmation, skipping UI");
		break;
	    default:
		snprintf(buffer, sizeof buffer,
		   "Unexpected result %d",
		   (int)result);
		push_simple_dialog(buffer, false);
	}
}

/*********************
 * WINDOW MANAGEMENT *
 *********************/

static void
window_appear(Window *window) {
	rebuild_menu();
}

static void
window_load(Window *window) {
	Layer *window_layer = window_get_root_layer(window);
	GRect bounds = layer_get_bounds(window_layer);

	rebuild_menu();

	menu_layer = simple_menu_layer_create(bounds, window,
	    &menu_section, 1, 0);

	layer_add_child(window_layer, simple_menu_layer_get_layer(menu_layer));
}

static void
window_unload(Window *window) {
	simple_menu_layer_destroy(menu_layer);
}

/**********************************
 * INTIALIZATION AND FINALIZATION *
 **********************************/

static void
init(void) {
	cfg_wakeup_time = persist_read_int(MSG_KEY_CFG_WAKEUP_TIME) - 1;

	persist_read_data(1, current_page, sizeof current_page);

#ifdef DISPLAY_TEST_DATA
	current_page[0].time = 1449738000; /* 2015-12-10T10:00:00 */
	current_page[0].before = APP_STARTED;
	current_page[0].after = 90;
	current_page[1].time = 1449741600; /* 2015-12-10T11:00:00 */
	current_page[1].before = 90;
	current_page[1].after = 80;
	current_page[2].time = 1449742980; /* 2015-12-10T11:23:00 */
	current_page[2].before = ANOMALOUS_VALUE;
	current_page[2].after = 131;
	current_page[3].time = 1449743160; /* 2015-12-10T11:26:00 */
	current_page[3].before = UNKNOWN;
	current_page[3].after = 70;
	current_page[4].time = 1449743460; /* 2015-12-10T11:31:00 */
	current_page[4].before = 70;
	current_page[4].after = 128 | 70;
	current_page[5].time = 1449743940; /* 2015-12-10T11:39:00 */
	current_page[5].before = 128 | 70;
	current_page[5].after = 128 | 80;
	current_page[6].time = 1449744000; /* 2015-12-10T11:40:00 */
	current_page[6].before = 128 | 80;
	current_page[6].after = 80;
	current_page[7].time = 1449744420; /* 2015-12-10T11:47:00 */
	current_page[7].before = 80;
	current_page[7].after = 128 | 70;
	current_page[8].time = 1449744660; /* 2015-12-10T11:51:00 */
	current_page[8].before = 128 | 70;
	current_page[8].after = 80;
	current_page[9].time = 1449744660; /* 2015-12-10T11:51:00 */
	current_page[9].before = 80;
	current_page[9].after = 90;
	current_page[10].time = 1449745140; /* 2015-12-10T11:59:00 */
	current_page[10].before = 90;
	current_page[10].after = 128 | 100;
	current_page[11].time = 1449745260; /* 2015-12-10T12:01:00 */
	current_page[11].before = 128 | 100;
	current_page[11].after = 128 | 80;
	current_page[12].time = 1449745620; /* 2015-12-10T12:07:00 */
	current_page[12].before = 128 | 80;
	current_page[12].after = 60;
	current_page[13].time = 1449745800; /* 2015-12-10T12:10:00 */
	current_page[13].before = APP_CLOSED;
	current_page[13].after = 60;
	current_page[14].time = 1449846060; /* 2015-12-11T16:01:00 */
	current_page[14].before = APP_STARTED;
	current_page[14].after = 128 | 40;
	current_page[15].time = 1449846480; /* 2015-12-11T16:08:00 */
	current_page[15].before = 128 | 40;
	current_page[15].after = 128 | 40;
	current_page[16].time = 1449846660; /* 2015-12-11T16:11:00 */
	current_page[16].before = UNKNOWN;
	current_page[16].after = 128 | 60;
	current_page[17].time = 1449846780; /* 2015-12-11T16:13:00 */
	current_page[17].before = APP_CLOSED;
	current_page[17].after = 128 | 60;
	for (unsigned i = 18; i < PAGE_LENGTH; i += 1) {
		current_page[i].time = 0;
		current_page[i].before = 0;
		current_page[i].after = 0;
	}
#else
	if (launch_reason() == APP_LAUNCH_WAKEUP) {
		push_simple_dialog("Battery- Auto Sync", true);
		app_message_register_inbox_received(inbox_received_handler);
		app_message_register_outbox_failed(outbox_failed_handler);
		app_message_register_outbox_sent(outbox_sent_handler);
		app_message_open(256, 2048);
		return;
	}
#endif

	init_strings();

	window = window_create();
	window_set_window_handlers(window, (WindowHandlers) {
	    .load = window_load,
	    .unload = window_unload,
	    .appear = window_appear,
	});
	window_stack_push(window, true);

	app_message_register_inbox_received(inbox_received_handler);
	app_message_register_outbox_failed(outbox_failed_handler);
	app_message_register_outbox_sent(outbox_sent_handler);
	app_message_open(256, 2048);
}

static void
deinit(void) {
	window_destroy(window);

	for (unsigned i = 0; i < PAGE_LENGTH; i += 1) {
		if (titles[i]) free((void *)titles[i]);
		titles[i] = 0;

		if (dates[i]) free((void *)dates[i]);
		dates[i] = 0;
	}

	if (cfg_wakeup_time >= 0) {
		WakeupId res;
		time_t now = time(0);
		time_t t = clock_to_timestamp(TODAY,
		    cfg_wakeup_time / 60, cfg_wakeup_time % 60);

		if (t - now > 6 * 86400)
			t -= 6 * 86400;
		else if (t - now <= 120)
			t += 86400;

		res = wakeup_schedule(t, 0, true);

		if (res < 0)
			APP_LOG(APP_LOG_LEVEL_ERROR,
			    "wakeup_schedule(%" PRIi32 ", 0, true)"
			    " returned %" PRIi32,
			    t, res);
	}
}

int
main(void) {
	init();
	app_event_loop();
	deinit();
}
