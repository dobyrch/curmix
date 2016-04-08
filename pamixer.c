#include <assert.h>
#include <stdio.h>

#include <pulse/context.h>
#include <pulse/introspect.h>
#include <pulse/mainloop.h>


void callback(pa_context *c, const pa_sink_info *i, int eol, void *userdata)
{
	printf("Doing stuff\n");
}

static void context_state_callback(pa_context *c, void *userdata) {
    assert(c);

    switch (pa_context_get_state(c)) {
        case PA_CONTEXT_TERMINATED:
		printf("context terminated\n");
		break;
        case PA_CONTEXT_UNCONNECTED:
		printf("context unconnected\n");
		break;
        case PA_CONTEXT_FAILED:
		printf("context failed\n");
		break;
        case PA_CONTEXT_CONNECTING:
		printf("context connecting\n");
		break;
        case PA_CONTEXT_AUTHORIZING:
		printf("context authorizing\n");
		break;
        case PA_CONTEXT_SETTING_NAME:
		printf("context setting name\n");
		    break;
        case PA_CONTEXT_READY:
		pa_context_get_sink_input_info_list(c, (pa_sink_input_info_cb_t)callback, NULL);
		printf("context ready\n");
		break;

    }
}

int main(void)
{
	int r;
	pa_mainloop *m = NULL;
	pa_mainloop_api *mapi = NULL;
	pa_context *context;

	if (!(m = pa_mainloop_new())) {
		printf("pa_mainloop_new() failed.\n");
		return 1;
	}

	mapi = pa_mainloop_get_api(m);

	if (!(context = pa_context_new(mapi, "Foo"))) {
		printf("pa_context_new() failed.\n");
		return -1;
	}

	pa_context_set_state_callback(context, context_state_callback, NULL);

	if (0 > (r = pa_context_connect(context, NULL, 0, NULL))) {
		printf("pa_context_connect() failed.\n");
		return -1;
	}

	printf("%d\n", r);

	if (pa_mainloop_run(m, &r) < 0) {
		printf("pa_mainloop_run() failed.\n");
		return -1;
	}

	



	return 0;
}
