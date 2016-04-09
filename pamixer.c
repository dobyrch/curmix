#include <assert.h>
#include <locale.h>
#include <stdio.h>
#include <curses.h>
/* Remove unneeded headers */
#include <string.h>
#include <unistd.h>

#include <pulse/pulseaudio.h>


#define ONE_SECOND 1000000

#define MAX_INPUTS 64
#define MAX_NAME_LEN 32

struct input_data {
	char name[MAX_NAME_LEN];
	uint32_t volume;
	uint32_t index;
};

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

void draw_ui(void)
{
	static int cursor_pos = 0;
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
		wmove(window, 1, 1);

		volume = inputs[i].volume/1638;
		fprintf(stderr, "Volume is %d\n", volume);
		for (j = 0; j < volume; ++j) {
			//setcchar(&bar, &shade, A_NORMAL, COLOR_PAIR(j/8+1), NULL);
			setcchar(&bar, &shade, A_NORMAL, j/8+1, NULL);
			wadd_wch(window, &bar);
			//waddch(window, '*' | COLOR_PAIR(j/8+1));
		}
		//mvwaddwstr(window, 1, 2, L"\u2592\u2592\u2592\u2592");

		//touchwin(window);
		//wrefresh(window);
		wnoutrefresh(window);
	}

	doupdate();

	//refresh();
}

static void time_event_callback(pa_mainloop_api *m, pa_time_event *e, const struct timeval *tv, void *userdata) {
    struct timeval now;

	//printf("TICK\n");
	//fflush(stdout);

    gettimeofday(&now, NULL);
    pa_timeval_add(&now, ONE_SECOND);
    m->time_restart(e, &now);
}

void context_callback(pa_context *c, pa_subscription_event_type_t t, uint32_t idx, void *userdata)
{
	//printf("index: %d\n", idx);
	//printf("FACILITY_MASK: %d\n", t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK);
	//printf("	TYPE_MASK: %d\n", t & PA_SUBSCRIPTION_EVENT_TYPE_MASK);
	//printf("\n");
}

void success(pa_context *c, int success, void *userdata)
{
	//printf("Success: %d\n", success);
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
		pa_operation *foo = pa_context_subscribe(c, PA_SUBSCRIPTION_MASK_SINK_INPUT, success, NULL);
		//printf("subscribe operation: %ld\n", (long)foo);
		return;
	}

	name = pa_proplist_gets(i->proplist, "application.process.binary");
	if (name == NULL)
		name = "unknown";
	strncpy(inputs[num_inputs].name, name, MAX_NAME_LEN);
	inputs[num_inputs].volume = i->volume.values[0];
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

	assert(c);

	switch (pa_context_get_state(c)) {
	case PA_CONTEXT_TERMINATED:
	case PA_CONTEXT_UNCONNECTED:
	case PA_CONTEXT_FAILED:
	case PA_CONTEXT_CONNECTING:
	case PA_CONTEXT_AUTHORIZING:
	case PA_CONTEXT_SETTING_NAME:
		break;
	case PA_CONTEXT_READY:
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
	pa_time_event *time_event = NULL;
	struct timeval now;
	gettimeofday(&now, NULL);
	pa_timeval_add(&now, ONE_SECOND);

	setlocale(LC_ALL, "");
	initscr();
	start_color();
	init_pair(1, COLOR_GREEN, COLOR_GREEN);
	init_pair(2, COLOR_GREEN, COLOR_YELLOW);
	init_pair(3, COLOR_YELLOW, COLOR_YELLOW);
	init_pair(4, COLOR_YELLOW, COLOR_RED);
	init_pair(5, COLOR_RED, COLOR_RED);

	if (!(m = pa_mainloop_new())) {
		//printf("pa_mainloop_new() failed.\n");
		return 1;
	}

	mapi = pa_mainloop_get_api(m);

	if (!(time_event = mapi->time_new(mapi, &now, time_event_callback, NULL))){
		//fprintf(stderr, "time_new() failed.\n");
		return -1;
	}

	if (!(context = pa_context_new(mapi, "Foo"))) {
		//printf("pa_context_new() failed.\n");
		return -1;
	}

	pa_context_set_state_callback(context, context_state_callback, NULL);

	if (0 > (r = pa_context_connect(context, NULL, PA_CONTEXT_NOFLAGS, NULL))) {
		//printf("pa_context_connect() failed.\n");
		return -1;
	}


	if (pa_mainloop_run(m, &r) < 0) {
		//printf("pa_mainloop_run() failed.\n");
		return -1;
	}




	return 0;
}
