#include <assert.h>
#include <stdio.h>

#include <pulse/context.h>
#include <pulse/introspect.h>
#include <pulse/mainloop.h>
#include <pulse/subscribe.h>

void context_callback(pa_context *c, pa_subscription_event_type_t t, uint32_t idx, void *userdata)
{
	printf("Got event:\n");
	printf("\n");
}

void success(pa_context *c, int success, void *userdata)
{
	printf("Success: %d\n", success);
	pa_context_set_subscribe_callback(c, context_callback, NULL);
}


void callback(pa_context *c, const pa_sink_input_info *i, int eol, void *userdata)
{
	/*
	 * Use application.name; fall back to application.process.binary
	 * If one of the above is a dup, also show media.name
	 */
	void *state = NULL;
	const char *prop;

	//printf("EOL: %d\n", eol);
	if (eol)
		return;

	//printf("%s\n", i->name);
	//printf("%d\n", ((i->volume).values)[0]);

	while ((prop = pa_proplist_iterate(i->proplist, &state)) != NULL) {
		//printf("%-30s=\t%s\n", prop, pa_proplist_gets(i->proplist, prop));
	}


	//printf("\n");
}

static void context_state_callback(pa_context *c, void *userdata) {
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
		;
		pa_operation *foo = pa_context_subscribe(c, PA_SUBSCRIPTION_MASK_SINK_INPUT, success, NULL);
		printf("subscribe operation: %ld\n", (long)foo);
		pa_context_get_sink_input_info_list(c, callback, NULL);
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


	if (pa_mainloop_run(m, &r) < 0) {
		printf("pa_mainloop_run() failed.\n");
		return -1;
	}


	return 0;
}
