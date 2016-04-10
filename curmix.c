#include <curses.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <pulse/pulseaudio.h>


static void draw_ui(void);
static void stdin_cb(pa_mainloop_api *a, pa_io_event *e, int fd,
	pa_io_event_flags_t f, void *context);
static void signal_cb(pa_mainloop_api *a, pa_signal_event *e, int signal,
	void *userdata);
static void context_state_cb(pa_context *c, void *userdata);
static void input_event_cb(pa_context *c, pa_subscription_event_type_t t,
	uint32_t index, void *userdata);
static void input_info_cb(pa_context *c, const pa_sink_input_info *i, int eol,
	void *userdata);


#define MAX_INPUTS 64
#define MAX_NAME_LEN 32
/* Change volume in increments of 5% */
#define INC PA_VOLUME_NORM/20

struct input_data {
	uint32_t index;
	char name[MAX_NAME_LEN];
	pa_cvolume volume;
	int mute;
};

static int cursor_pos = 0;
static int num_inputs = 0;
static struct input_data inputs[MAX_INPUTS];
static WINDOW *windows[MAX_INPUTS];

/*
 * TODO:
 * - Disambiguate duplicate names
 * - Handle large # of inputs (scrolling, don't segfault on inputs > 64)
 * - Clean up on exit (free main loop, endwin)
 */
int main(void)
{
	pa_mainloop *m;
	pa_mainloop_api *a;
	pa_context *c;

	setlocale(LC_ALL, "");
	initscr();
	noecho();
	curs_set(0);
	cbreak();
	keypad(stdscr, TRUE);

	use_default_colors();
	start_color();
	init_pair(1, COLOR_GREEN, COLOR_GREEN);
	init_pair(2, COLOR_GREEN, COLOR_YELLOW);
	init_pair(3, COLOR_YELLOW, COLOR_YELLOW);
	init_pair(4, COLOR_YELLOW, COLOR_RED);
	init_pair(5, COLOR_RED, COLOR_RED);
	init_pair(6, COLOR_GREEN, -1);
	init_pair(7, COLOR_BLUE, -1);

	m = pa_mainloop_new();
	a = pa_mainloop_get_api(m);

	c = pa_context_new(a, "curmix");
	pa_context_set_state_callback(c, context_state_cb, NULL);
	pa_context_connect(c, NULL, PA_CONTEXT_NOFLAGS, NULL);

	a->io_new(a, STDIN_FILENO, PA_IO_EVENT_INPUT, stdin_cb, c);

	pa_signal_init(a);
	pa_signal_new(SIGWINCH, signal_cb, NULL);

	pa_mainloop_run(m, NULL);

	return EXIT_SUCCESS;
}

static void draw_ui(void)
{
	static const wchar_t medium_shade = L'\u2592';
	struct input_data *input;
	WINDOW *w;
	cchar_t volume_bar;
	int volume, i, j;

	if (num_inputs == 0) {
		mvaddstr(3, 5, "No Inputs found");
		refresh();
	}

	if (cursor_pos >= num_inputs && num_inputs > 0)
		cursor_pos = num_inputs - 1;

	for (i = 0; i < num_inputs; ++i) {
		input = &inputs[i];
		w = windows[i];
		if (w == NULL)
			w = windows[i] = newwin(3, 42, 4*i + 2, 2);

		box(w, 0, 0);
		mvwaddstr(w, 0, 3, input->name);

		if (i == cursor_pos)
			mvwchgat(w, 0, 3, strlen(input->name), A_NORMAL, 7, NULL);

		volume = input->volume.values[0]/1638;
		wmove(w, 1, 1);

		for (j = 0; j < volume; ++j) {
			setcchar(&volume_bar, &medium_shade,
				input->mute ? A_BOLD : A_NORMAL,
				input->mute ? 6 : j/8+1, NULL);
			wadd_wch(w, &volume_bar);
		}

		for (j = volume; j < 40; ++j) {
			waddch(w, ' ');
		}

		wnoutrefresh(w);
	}

	for (i = num_inputs; i < MAX_INPUTS; ++i) {
		w = windows[i];
		if (w == NULL)
			break;

		wclear(w);
		wnoutrefresh(w);
		windows[i] = NULL;
	}

	doupdate();
}

static void stdin_cb(pa_mainloop_api *a, pa_io_event *e, int fd, pa_io_event_flags_t f, void *context)
{
	struct input_data *input;
	int ch;

	switch (ch = getch()) {
	case KEY_UP:
	case 'k':
		if (cursor_pos > 0)
			--cursor_pos;
		break;
	case KEY_DOWN:
	case 'j':
		if (cursor_pos + 1 < num_inputs)
			++cursor_pos;
		break;
	case KEY_LEFT:
	case 'h':
		input = &inputs[cursor_pos];
		pa_cvolume_dec(&input->volume, INC);
		pa_context_set_sink_input_volume(context,
			input->index,
			&input->volume,
			NULL,
			NULL);
		break;
	case KEY_RIGHT:
	case 'l':
		input = &inputs[cursor_pos];
		pa_cvolume_inc_clamp(&input->volume, INC, PA_VOLUME_NORM);
		pa_context_set_sink_input_volume(context,
			input->index,
			&input->volume,
			NULL,
			NULL);
		break;
	case 'm':
		input = &inputs[cursor_pos];
		pa_context_set_sink_input_mute(context,
			input->index,
			!input->mute,
			NULL,
			NULL);
		break;
	}

	draw_ui();
}

static void signal_cb(pa_mainloop_api *a, pa_signal_event *e, int signal, void *userdata)
{
	switch (signal) {
	case SIGWINCH:
		clear();
		refresh();
		draw_ui();
		break;
	}
}

static void context_state_cb(pa_context *c, void *userdata)
{
	switch (pa_context_get_state(c)) {
	case PA_CONTEXT_AUTHORIZING:
	case PA_CONTEXT_CONNECTING:
	case PA_CONTEXT_FAILED:
	case PA_CONTEXT_SETTING_NAME:
	case PA_CONTEXT_TERMINATED:
	case PA_CONTEXT_UNCONNECTED:
		break;
	case PA_CONTEXT_READY:
		//TODO: dec operation?
		pa_context_get_sink_input_info_list(c, input_info_cb, NULL);
		pa_context_set_subscribe_callback(c, input_event_cb, NULL);
		pa_context_subscribe(c, PA_SUBSCRIPTION_MASK_SINK_INPUT, NULL, NULL);
		break;

	}
}

static void input_event_cb(pa_context *c, pa_subscription_event_type_t t, uint32_t index, void *userdata)
{
	num_inputs = 0;
	//Decrement operation?
	pa_context_get_sink_input_info_list(c, input_info_cb, NULL);
}

static void input_info_cb(pa_context *c, const pa_sink_input_info *i, int eol, void *userdata)
{
	struct input_data *input;
	const char *name;

	if (eol) {
		draw_ui();
		return;
	}

	input = &inputs[num_inputs];
	name = pa_proplist_gets(i->proplist, "application.process.binary");

	strncpy(input->name, name, MAX_NAME_LEN);
	memcpy(&input->volume, &i->volume, sizeof(pa_cvolume));
	input->mute = i->mute;
	input->index = i->index;
	++num_inputs;
}
