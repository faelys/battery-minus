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

#include <pebble.h>

#include "simple_dialog.h"

#define DIALOG_MESSAGE_WINDOW_MARGIN 10

static void
leave_window(ClickRecognizerRef recognizer, void *context) {
	(void)recognizer;
	(void)context;
	window_stack_pop(true);
}

static void
click_config(void *context) {
	(void)context;
	window_single_click_subscribe(BUTTON_ID_BACK, &leave_window);
	window_single_click_subscribe(BUTTON_ID_UP, &leave_window);
	window_single_click_subscribe(BUTTON_ID_SELECT, &leave_window);
	window_single_click_subscribe(BUTTON_ID_DOWN, &leave_window);
}

static Window *dialog_window;
static TextLayer *message_layer;
static const char *message_text = 0;
static bool static_message_text = true;

static void
window_load(Window *window) {
	Layer *window_layer = window_get_root_layer(window);
	GRect bounds = layer_get_bounds(window_layer);

	message_layer = text_layer_create(GRect(DIALOG_MESSAGE_WINDOW_MARGIN,
	    bounds.size.h / 3,
	    bounds.size.w - (2 * DIALOG_MESSAGE_WINDOW_MARGIN),
	    (bounds.size.h + 2) / 3));
	text_layer_set_text(message_layer, message_text);
	text_layer_set_text_alignment(message_layer, GTextAlignmentCenter);
	text_layer_set_font(message_layer,
	    fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
	layer_add_child(window_layer, text_layer_get_layer(message_layer));
}

static void
window_unload(Window *window) {
	text_layer_destroy(message_layer);
	message_layer = 0;

	if (!static_message_text) free((void *)message_text);
	message_text = 0;
}

static bool
update_text(const char *original_text, bool is_static) {
	const char *new_text = 0;

	if (is_static) {
		new_text = original_text;
	} else {
		size_t text_size = strlen(original_text) + 1;
		char *buffer = malloc(text_size);

		if (!buffer) {
			APP_LOG(APP_LOG_LEVEL_ERROR, "Unable to allocate"
			    " message text for simple dialog");
			return false;
		}

		memcpy(buffer, original_text, text_size);
		new_text = buffer;
	}

	if (message_layer) {
		text_layer_set_text(message_layer, new_text);
	}

	if (!static_message_text) free((void *)message_text);
	message_text = new_text;
	static_message_text = is_static;
	return true;
}

void
push_simple_dialog(const char *message, bool is_static) {
	update_text(message, is_static);

	if (!dialog_window) {
		dialog_window = window_create();
		window_set_window_handlers(dialog_window, (WindowHandlers){
		    .load = &window_load,
		    .unload = &window_unload
		});
		window_set_click_config_provider(dialog_window, &click_config);
	}

	window_stack_push(dialog_window, true);
}
