#include "pch.h"

#include "dr_api.h"
#include "drmgr.h"


#define SHOW_RESULTS
//#define VERBOSE_VERBOSE

#ifdef WINDOWS
# define DISPLAY_STRING(msg) dr_messagebox(msg)
#else
# define DISPLAY_STRING(msg) dr_printf("%s\n", msg);
#endif

#define ALIGN_FORWARD(x, alignment) \
    ((((ptr_uint_t)x) + ((alignment)-1)) & (~((alignment)-1)))

static void *stats_mutex; /* for multithread support */
static int num_bb;
static double ave_size;
static int max_size;

static void event_exit(void);
static dr_emit_flags_t event_bb_analysis(void *drcontext, void *tag, instrlist_t *bb,
	bool for_trace, bool translating,
	OUT void **user_data);

DR_EXPORT void
dr_client_main(client_id_t id, int argc, const char *argv[])
{
	dr_set_client_name("DynamoRIO Sample Client 'bbsize'",
		"http://dynamorio.org/issues");
	num_bb = 0;
	ave_size = 0.;
	max_size = 0;

	drmgr_init();
	stats_mutex = dr_mutex_create();
	drmgr_register_bb_instrumentation_event(event_bb_analysis, NULL, NULL);
	dr_register_exit_event(event_exit);
#ifdef SHOW_RESULTS
	if (dr_is_notify_on()) {
# ifdef WINDOWS
		/* ask for best-effort printing to cmd window.  must be called at init. */
		dr_enable_console_printing();
# endif
		dr_fprintf(STDERR, "Client bbsize is running\n");
	}
#endif
}

static void
event_exit(void)
{
#ifdef SHOW_RESULTS
	char msg[512];
	int len;
	/* Note that using %f with dr_printf or dr_fprintf on Windows will print
	 * garbage as they use ntdll._vsnprintf, so we must use dr_snprintf.
	 */
	len = dr_snprintf(msg, sizeof(msg) / sizeof(msg[0]),
		"Number of basic blocks seen: %d\n"
		"               Maximum size: %d instructions\n"
		"               Average size: %5.1f instructions\n",
		num_bb, max_size, ave_size);
	DR_ASSERT(len > 0);
	msg[sizeof(msg) / sizeof(msg[0]) - 1] = '\0';
	DISPLAY_STRING(msg);
#endif /* SHOW_RESULTS */
	dr_mutex_destroy(stats_mutex);
	drmgr_exit();
}

static dr_emit_flags_t
event_bb_analysis(void *drcontext, void *tag, instrlist_t *bb,
	bool for_trace, bool translating, OUT void **user_data)
{
	instr_t *instr;
	int cur_size = 0;

	/* we use fp ops so we have to save fp state */
	byte fp_raw[DR_FPSTATE_BUF_SIZE + DR_FPSTATE_ALIGN];
	byte *fp_align = (byte *)ALIGN_FORWARD(fp_raw, DR_FPSTATE_ALIGN);

	if (translating)
		return DR_EMIT_DEFAULT;

	proc_save_fpstate(fp_align);

	for (instr = instrlist_first_app(bb);
		instr != NULL;
		instr = instr_get_next_app(instr))
		cur_size++;

	dr_mutex_lock(stats_mutex);
#ifdef VERBOSE_VERBOSE
	dr_fprintf(STDERR,
		"Average: cur=%d, old=%8.1f, num=%d, old*num=%8.1f\n"
		"\told*num+cur=%8.1f, new=%8.1f\n",
		cur_size, ave_size, num_bb, ave_size*num_bb,
		(ave_size * num_bb) + cur_size,
		((ave_size * num_bb) + cur_size) / (double)(num_bb + 1));
#endif
	if (cur_size > max_size)
		max_size = cur_size;
	ave_size = ((ave_size * num_bb) + cur_size) / (double)(num_bb + 1);
	num_bb++;
	dr_mutex_unlock(stats_mutex);

	proc_restore_fpstate(fp_align);

	return DR_EMIT_DEFAULT;
}