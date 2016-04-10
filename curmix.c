#include <curses.h>
#include <locale.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <pulse/pulseaudio.h>


#define INC PA_VOLUME_NORM/20

#define MAX_INPUTS 64
#define MAX_NAME_LEN 32

struct input_data {
	char name[MAX_NAME_LEN];
	pa_cvolume volume;
	int mute;
	uint32_t index;
};

static int cursor_pos = 0;
static int num_inputs = 0;
static struct input_data inputs[MAX_INPUTS];
static WINDOW *windows[MAX_INPUTS];

/*
 * Define struct with all necessary fields: name, volume, id
 * Static list of *pointers* to struct
 * Populate initial list, _copying_ into struct
 * Draw screen
 * On updates, add/modify/delete&shift pointers (LOCK)
 * After updates (or on keypresses) redraw screen (LOCK)
 *
 */

void context_callback(pa_context *c, pa_subscription_event_type_t t, uint32_t idx, void *userdata);
void success(pa_context *c, int success, void *userdata);
void callback(pa_context *c, const pa_sink_input_info *i, int eol, void *userdata);
void stdin_callback(pa_mainloop_api*a, pa_io_event *e, int fd, pa_io_event_flags_t f, void *userdata);

void draw_ui(void)
{
	int i, j, volume;
	cchar_t bar;
	WINDOW *window;
	static const wchar_t shade = L'\u2592';

	if (num_inputs == 0) {
		mvaddstr(5, 5, "No Inputs found");
	}

	for (i = 0; i < num_inputs; ++i) {
		window = windows[i];
		if (window == NULL) {
			window = windows[i] = newwin(3, 42, 4*i + 2, 2);
		}

		box(window, 0, 0);

		mvwaddstr(window, 0, 3, inputs[i].name);
		if (i == cursor_pos)
			mvwchgat(window, 0, 3, strlen(inputs[i].name), A_BOLD, 7, NULL);
		wmove(window, 1, 1);

		volume = inputs[i].volume.values[0]/1638;

		/*
		if (inputs[i].mute) {
			setcchar(&muted, &diag_cross, A_BOLD, 6, NULL);
			wadd_wch(window, &muted);
			wnoutrefresh(window);
			break;
		}
		*/

		//fprintf(stderr, "Volume is %d\n", volume);
		for (j = 0; j < volume; ++j) {
			//setcchar(&bar, &shade, A_NORMAL, COLOR_PAIR(j/8+1), NULL);
			setcchar(&bar, &shade, inputs[i].mute ? A_BOLD : A_NORMAL, inputs[i].mute ? 6 : j/8+1, NULL);
			wadd_wch(window, &bar);
			//waddch(window, '*' | COLOR_PAIR(j/8+1));
		}
		for (j = volume; j < 40; ++j) {
			waddch(window, ' ');
		}
		//mvwaddwstr(window, 1, 2, L"\u2592\u2592\u2592\u2592");

		//touchwin(window);
		//wrefresh(window);
		wnoutrefresh(window);
	}
	for (i = num_inputs; i < MAX_INPUTS; ++i) {
		if (windows[i] == NULL)
			break;

		wclear(windows[i]);
	}

	doupdate();

	//refresh();
}

void stdin_callback(pa_mainloop_api *a, pa_io_event *e, int fd, pa_io_event_flags_t f, void *context)
{
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
		pa_cvolume_dec(&inputs[cursor_pos].volume, INC);
		pa_context_set_sink_input_volume(context,
			inputs[cursor_pos].index,
			&inputs[cursor_pos].volume,
			success,
			NULL);
		break;
	case KEY_RIGHT:
	case 'l':
		pa_cvolume_inc_clamp(&inputs[cursor_pos].volume, INC, PA_VOLUME_NORM);
		pa_context_set_sink_input_volume(context,
			inputs[cursor_pos].index,
			&inputs[cursor_pos].volume,
			success,
			NULL);
		break;
	case 'm':
		pa_context_set_sink_input_mute(context,
			inputs[cursor_pos].index,
			!inputs[cursor_pos].mute,
			success,
			NULL);
		break;
	}

	draw_ui();
}

void context_callback(pa_context *c, pa_subscription_event_type_t t, uint32_t idx, void *userdata)
{
	num_inputs = 0;
	//Decrement operation?
	pa_context_get_sink_input_info_list(c, callback, NULL);
	//printf("index: %d\n", idx);
	//printf("FACILITY_MASK: %d\n", t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK);
	//printf("	TYPE_MASK: %d\n", t & PA_SUBSCRIPTION_EVENT_TYPE_MASK);
	//printf("\n");
}

void success(pa_context *c, int success, void *userdata)
{
	int err;

	if (!success) {
		err = pa_context_errno(c);
		//fprintf(stderr, "%s\n", pa_strerror(err));
	}
}

void callback(pa_context *c, const pa_sink_input_info *i, int eol, void *userdata)
{
	/*
	 * Use application.name; fall back to application.process.binary
	 * If one of the above is a dup, also show media.name
	 */
	//void *state = NULL;
	//const char *prop;
	const char *name;

	//printf("EOL: %d\n", eol);
	if (eol) {
		draw_ui();
		pa_context_set_subscribe_callback(c, context_callback, NULL);
		/* unref returned operation? */
		pa_context_subscribe(c, PA_SUBSCRIPTION_MASK_SINK_INPUT, success, NULL);
		//printf("subscribe operation: %ld\n", (long)foo);
		return;
	}

	name = pa_proplist_gets(i->proplist, "application.process.binary");
	if (name == NULL)
		name = "unknown";

	strncpy(inputs[num_inputs].name, name, MAX_NAME_LEN);
	//inputs[num_inputs].volume = i->volume.values[0];
	memcpy(&inputs[num_inputs].volume, &i->volume, sizeof(pa_cvolume));
	inputs[num_inputs].mute = i->mute;
	inputs[num_inputs].index = i->index;
	++num_inputs;
	//printf("Got a sink: %d\n", num_inputs);
	//fflush(stdout);

	//printf("%s\n", i->name);
	//printf("%d\n", ((i->volume).values)[0]);

	/*
	while ((prop = pa_proplist_iterate(i->proplist, &state)) != NULL) {
		printf("%-30s=\t%s\n", prop, pa_proplist_gets(i->proplist, prop));
	}
	*/


	//printf("\n");
}

static void context_state_callback(pa_context *c, void *userdata) {
	pa_operation *operation;

	switch (pa_context_get_state(c)) {
	case PA_CONTEXT_TERMINATED:
	case PA_CONTEXT_UNCONNECTED:
	case PA_CONTEXT_FAILED:
	case PA_CONTEXT_CONNECTING:
	case PA_CONTEXT_AUTHORIZING:
	case PA_CONTEXT_SETTING_NAME:
		break;
	case PA_CONTEXT_READY:
		//TODO: dec operation?
		operation = pa_context_get_sink_input_info_list(c, callback, NULL);
		//printf("context ready\n");
		break;

	}
}

int main(void)
{
	int r;
	pa_mainloop *m = NULL;
	pa_mainloop_api *mapi = NULL;
	pa_context *context;
	pa_io_event *stdin_event = NULL;

	setlocale(LC_ALL, "");
	initscr();
	noecho();
	cbreak();
	keypad(stdscr, TRUE);
	curs_set(0);
	use_default_colors();
	start_color();
	init_pair(1, COLOR_GREEN, COLOR_GREEN);
	init_pair(2, COLOR_GREEN, COLOR_YELLOW);
	init_pair(3, COLOR_YELLOW, COLOR_YELLOW);
	init_pair(4, COLOR_YELLOW, COLOR_RED);
	init_pair(5, COLOR_RED, COLOR_RED);
	init_pair(6, COLOR_GREEN, -1);
	init_pair(7, COLOR_MAGENTA, -1);

	if (!(m = pa_mainloop_new())) {
		//printf("pa_mainloop_new() failed.\n");
		return 1;
	}

	mapi = pa_mainloop_get_api(m);

	if (!(context = pa_context_new(mapi, "curmix"))) {
		//printf("pa_context_new() failed.\n");
		return -1;
	}

	pa_context_set_state_callback(context, context_state_callback, NULL);

	if (0 > (r = pa_context_connect(context, NULL, PA_CONTEXT_NOFLAGS, NULL))) {
		//printf("pa_context_connect() failed.\n");
		return -1;
	}

	if (!(stdin_event = mapi->io_new(mapi, STDIN_FILENO, PA_IO_EVENT_INPUT, stdin_callback, context))) {
		return -1;
	}


	if (pa_mainloop_run(m, &r) < 0) {
		//printf("pa_mainloop_run() failed.\n");
		return -1;
	}




	return 0;
}
