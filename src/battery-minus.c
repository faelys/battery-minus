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

#include <pebble.h>
#include "storage.h"

static Window *window;
static SimpleMenuLayer *menu_layer;
static SimpleMenuSection menu_section;
static SimpleMenuItem menu_items[PAGE_LENGTH];

static struct event current_page[PAGE_LENGTH];
static const char *titles[PAGE_LENGTH];
static const char *dates[PAGE_LENGTH];

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
	unsigned j;

	for (j = 1;
	    j < PAGE_LENGTH && current_page[j - 1].time < current_page[j].time;
	    j += 1);
	if (j >= PAGE_LENGTH || !current_page[j].time) j = 0;

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

/*********************
 * WINDOW MANAGEMENT *
 *********************/

static void
window_load(Window *window) {
	Layer *window_layer = window_get_root_layer(window);
	GRect bounds = layer_get_bounds(window_layer);
	unsigned i = PAGE_LENGTH;

	menu_section.title = 0;
	menu_section.items = menu_items;
	menu_section.num_items = 0;

	while (i) {
		i -= 1;
		if (!titles[i]) continue;
		menu_items[menu_section.num_items].title = titles[i];
		menu_items[menu_section.num_items].subtitle = dates[i];
		menu_items[menu_section.num_items].icon = 0;
		menu_items[menu_section.num_items].callback = 0;
		menu_section.num_items += 1;
	}

	if (!menu_section.num_items) {
		menu_items[0].title = "No event recorded";
		menu_items[0].subtitle = 0;
		menu_items[0].icon = 0;
		menu_items[0].callback = 0;
		menu_section.num_items = 1;
	}

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
	persist_read_data(1, current_page, sizeof current_page);

	init_strings();

	window = window_create();
	window_set_window_handlers(window, (WindowHandlers) {
	    .load = window_load,
	    .unload = window_unload,
	});
	window_stack_push(window, true);
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
}

int
main(void) {
	init();
	app_event_loop();
	deinit();
}
