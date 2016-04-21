#include <curses.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <pulse/pulseaudio.h>


static void draw_ui(int resized);
static void stdin_cb(pa_mainloop_api *a, pa_io_event *e, int fd,
	pa_io_event_flags_t f, void *context);
static void signal_cb(pa_mainloop_api *a, pa_signal_event *e, int signal,
	void *userdata);
static void context_state_cb(pa_context *c, void *userdata);
static void input_event_cb(pa_context *c, pa_subscription_event_type_t t,
	uint32_t index, void *userdata);
static void input_info_cb(pa_context *c, const pa_sink_input_info *i, int eol,
	void *userdata);


#define HPAD 6
#define VPAD 3
#define MAX_INPUTS 64
#define MAX_NAME_LEN 32
/* Adjust volume in increments of 5% */
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
 * - Handle large # of inputs (scrolling)
 */
int main(void)
{
	pa_mainloop *m;
	pa_mainloop_api *a;
	pa_context *c;

	setlocale(LC_ALL, "");
	initscr();
	noecho();
	curs_set(FALSE);
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
	pa_signal_new(SIGINT, signal_cb, NULL);
	pa_signal_new(SIGTERM, signal_cb, NULL);
	pa_signal_new(SIGWINCH, signal_cb, NULL);

	pa_mainloop_run(m, NULL);

	endwin();
	pa_context_disconnect(c);
	pa_mainloop_free(m);

	return EXIT_SUCCESS;
}

/*
 * The UI looks something like this:
 *
 * H ┌─program1─────────────────────────────────┐
 *   │░░░░░░░░░░░░░▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓███████       │
 * P └──────────────────────────────────────────┘
 *                   V  P  A  D
 * A ┌─program2─────────────────────────────────┐
 *   │░░░░░░░░░░░░░▓▓▓▓▓▓▓▓▓▓                   │
 * D └──────────────────────────────────────────┘
 *
 * The definitions of HPAD and VPAD control the spacing between
 * volume bars and the edge of the terminal as shown above.
 */
static void draw_ui(int resized)
{
	static const wchar_t medium_shade = L'\u2592';
	struct input_data *input;
	WINDOW *w;
	cchar_t volume_bar;
	int volume, i, j, width;

	width = getmaxx(stdscr) - HPAD*2;

	if (num_inputs == 0) {
		mvaddstr(VPAD, HPAD, "No Inputs found");
		refresh();
	}

	if (cursor_pos >= num_inputs && num_inputs > 0)
		cursor_pos = num_inputs - 1;

	for (i = 0; i < num_inputs; ++i) {
		input = &inputs[i];
		w = windows[i];
		if (w == NULL)
			w = windows[i] = newwin(3, width, (VPAD + 3)*i + VPAD, HPAD);
		if (resized)
			wresize(w, 3, width);

		box(w, 0, 0);
		mvwaddnstr(w, 0, 2, input->name, MAX_NAME_LEN);

		if (i == cursor_pos)
			mvwchgat(w, 0, 2, strnlen(input->name, MAX_NAME_LEN),
				A_NORMAL, 7, NULL);

		volume = pa_cvolume_avg(&input->volume)*(width - 2)/PA_VOLUME_NORM;
		wmove(w, 1, 1);

		for (j = 0; j < volume; ++j) {
			setcchar(&volume_bar, &medium_shade,
				input->mute ? A_BOLD : A_NORMAL,
				input->mute ? 6 : j*5/(width - 2)+1, NULL);
			wadd_wch(w, &volume_bar);
		}

		for (j = volume; j < width - 2; ++j) {
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
		delwin(w);
		windows[i] = NULL;
	}

	doupdate();
}

static void stdin_cb(pa_mainloop_api *a, pa_io_event *e, int fd, pa_io_event_flags_t f, void *context)
{
	pa_operation *o;
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
		o = pa_context_set_sink_input_volume(context,
			input->index,
			&input->volume,
			NULL,
			NULL);
		pa_operation_unref(o);
		break;
	case KEY_RIGHT:
	case 'l':
		input = &inputs[cursor_pos];
		pa_cvolume_inc_clamp(&input->volume, INC, PA_VOLUME_NORM);
		o = pa_context_set_sink_input_volume(context,
			input->index,
			&input->volume,
			NULL,
			NULL);
		pa_operation_unref(o);
		break;
	case 'm':
		input = &inputs[cursor_pos];
		o = pa_context_set_sink_input_mute(context,
			input->index,
			!input->mute,
			NULL,
			NULL);
		pa_operation_unref(o);
		break;
	case 'q':
		a->quit(a, 0);
		break;
	}

	draw_ui(FALSE);
}

static void signal_cb(pa_mainloop_api *a, pa_signal_event *e, int signal, void *userdata)
{
	switch (signal) {
	case SIGINT:
	case SIGTERM:
		a->quit(a, 0);
		break;
	case SIGWINCH:
		endwin();
		refresh();
		clear();
		draw_ui(TRUE);

		break;
	}
}

static void context_state_cb(pa_context *c, void *userdata)
{
	pa_operation *o;

	switch (pa_context_get_state(c)) {
	case PA_CONTEXT_AUTHORIZING:
	case PA_CONTEXT_CONNECTING:
	case PA_CONTEXT_FAILED:
	case PA_CONTEXT_SETTING_NAME:
	case PA_CONTEXT_TERMINATED:
	case PA_CONTEXT_UNCONNECTED:
		break;
	case PA_CONTEXT_READY:
		o = pa_context_get_sink_input_info_list(c, input_info_cb, NULL);
		pa_operation_unref(o);

		pa_context_set_subscribe_callback(c, input_event_cb, NULL);
		o = pa_context_subscribe(c, PA_SUBSCRIPTION_MASK_SINK_INPUT, NULL, NULL);
		pa_operation_unref(o);
		break;

	}
}

static void input_event_cb(pa_context *c, pa_subscription_event_type_t t, uint32_t index, void *userdata)
{
	pa_operation *o;

	num_inputs = 0;
	o = pa_context_get_sink_input_info_list(c, input_info_cb, NULL);
	pa_operation_unref(o);
}

static void input_info_cb(pa_context *c, const pa_sink_input_info *i, int eol, void *userdata)
{
	struct input_data *input;
	const char *name;

	if (eol || num_inputs >= MAX_INPUTS) {
		draw_ui(FALSE);
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
