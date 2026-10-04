/* Unit test of sdk/mtk/dmplatform_mtk.c (tests/host/run.sh unit): the real
 * dmplatform_mtk.c and compat/dmscript.c against fake_dm.py, the engine
 * stubbed below, the module claims of this build (claims.py).  Compares the
 * old request shape (one get_value of the path, one "inform") with what the
 * platform does now: same parameters and values, fewer script getters.
 * A new external symbol of dmplatform_mtk.c needs a stub here. */
#include "sdk/mtk/dmplatform_mtk.c"

#include <stdarg.h>

/* ---- engine stubs ------------------------------------------------------ */
char dmroot[64] = "InternetGatewayDevice";
char *DMT_TYPE[] = {"xsd:string", "xsd:unsignedInt", "xsd:int", "xsd:long", "xsd:boolean",
		    "xsd:dateTime", "xsd:hexBinary", "xsd:base64"};
LIST_HEAD(head_package_change);

#define MAXP 8192
static char *got_name[MAXP], *got_value[MAXP];
static int got_n;

void add_list_paramameter(struct dmctx *ctx, char *n, char *d, char *t, char *v, unsigned int f)
{
	int i;

	for (i = 0; i < got_n; i++)
		if (strcmp(got_name[i], n) == 0)
			return;
	got_name[got_n] = n;
	got_value[got_n++] = d ? d : "";
}
char *__dmstrdup(const char *s) { return strdup(s); }
int __dmasprintf(char **s, const char *fmt, ...)
{
	va_list ap; int r;
	va_start(ap, fmt); r = vasprintf(s, fmt, ap); va_end(ap);
	return r;
}
void add_list_fault_param(struct dmctx *c, char *p, int f) {}
void add_set_list_tmp(struct dmctx *c, char *p, char *v, unsigned int f) {}
int copy_temporary_file_to_original_file(char *a, char *b) { return 0; }
void cwmp_set_end_session(unsigned int f) {}
int dm_entry_get_name(struct dmctx *c) { return 0; }
int dm_entry_get_notification(struct dmctx *c) { return 0; }
int dm_entry_get_value(struct dmctx *c) { return 0; }
int dm_entry_inform(struct dmctx *c) { return 0; }
int dm_entry_set_notification(struct dmctx *c) { return 0; }
char *dm_get_parameter_notification(struct dmctx *c, char *p) { return "0"; }
int dm_set_parameter_notification(struct dmctx *c, char *p, char *v) { return 0; }
void dm_update_enabled_notify_byname(char *n, char *v) {}
int dmcommon_check_notification_value(char *v) { return 0; }
void dmjson_fprintf(FILE *fp, int argc, struct dmjson_arg a[]) {}
void dmjson_get_var(char *k, char **v) { *v = ""; }
void dmjson_parse_fini(void) {}
void dmjson_parse_init(char *m) {}
int dmubus_call_set(char *o, char *m, struct ubus_arg a[], int n) { return 0; }
int dmuci_commit(void) { return 0; }
int dmuci_get_option_value_list(char *p, char *s, char *o, struct uci_list **v) { *v = NULL; return 0; }
int dmuci_get_option_value_string(char *p, char *s, char *o, char **v) { *v = ""; return 0; }
int dmuci_get_varstate_string(char *p, char *s, char *o, char **v) { *v = ""; return 0; }
char *dmuci_set_value(char *p, char *s, char *o, char *v) { return ""; }
void free_all_list_package_change(struct list_head *l) {}
int mtk_input_contract(const char *p, const char *v) { return 0; }
void mtk_run_apply_service(void) {}
int string_to_bool(char *v, bool *b) { *b = (v[0] == '1' || v[0] == 't'); return 0; }

/* ---- registry: the real claims + the real matcher ---------------------- */
static char *claims[512];
static int nclaims;
#include "path_match.inc"

int dm_registry_owns(enum dm_model m, const char *path)
{
	int i;
	if (!path || !*path) return 0;
	for (i = 0; i < nclaims; i++)
		if (path_match(claims[i], path, 0)) return 1;
	return 0;
}
int dm_registry_covers(enum dm_model m, const char *prefix)
{
	int i;
	if (!prefix) return 0;
	if (!*prefix) return nclaims ? 1 : 0;
	for (i = 0; i < nclaims; i++)
		if (path_match(prefix, claims[i], 1)) return 1;
	return 0;
}

/* ---- helpers ----------------------------------------------------------- */
struct stats { int getters, requests; };
static int stats_cb(json_object *l, void *p)
{
	struct stats *s = p; const char *g = jstr(l, "getters"), *r = jstr(l, "requests");
	if (g) s->getters = atoi(g);
	if (r) s->requests = atoi(r);
	return 0;
}
static struct stats take(void)
{
	struct stats s = {0, 0};
	dmscript_request(stats_cb, &s, "stats", NULL);
	dmscript_request(NULL, NULL, "reset", NULL);
	return s;
}
static void clear(void) { got_n = 0; }

struct snap { int n; char **name, **value; };
static struct snap snap(void)
{
	struct snap s = { got_n, malloc(sizeof(char *) * (got_n + 1)), malloc(sizeof(char *) * (got_n + 1)) };
	memcpy(s.name, got_name, sizeof(char *) * got_n);
	memcpy(s.value, got_value, sizeof(char *) * got_n);
	return s;
}
static int same(struct snap a, struct snap b)
{
	int i, j;
	if (a.n != b.n) return 0;
	for (i = 0; i < a.n; i++) {
		for (j = 0; j < b.n; j++)
			if (!strcmp(a.name[i], b.name[j]) && !strcmp(a.value[i], b.value[j])) break;
		if (j == b.n) return 0;
	}
	return 1;
}

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("  FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static int old_get_value(struct dmctx *ctx, const char *path)
{
	struct mtk_reply r;
	mtk_reply_init(&r, ctx, MTK_KIND_VALUE);
	if (dmscript_request(mtk_line_cb, &r, "get_value", "param", mtk_script_path(path), NULL) != 0)
		return FAULT_9002;
	return (r.fault && !r.count) ? r.fault : 0;
}
static int old_inform(struct dmctx *ctx)
{
	struct mtk_reply r;
	mtk_reply_init(&r, ctx, MTK_KIND_VALUE);
	return dmscript_request(mtk_line_cb, &r, "inform", NULL) ? FAULT_9002 : 0;
}

static void gpv(struct dmctx *ctx, const char *path)
{
	struct snap a, b; struct stats so, sn; int fo, fn;

	take(); clear();
	fo = old_get_value(ctx, path); so = take(); a = snap(); clear();
	fn = mtk_get_value(ctx, path); sn = take(); b = snap(); clear();
	printf("GPV %-58s old: %4d getters %3d req | new: %4d getters %3d req | %d params\n",
	       path[0] ? path : "(root)", so.getters, so.requests, sn.getters, sn.requests, b.n);
	CHECK(fo == fn, "fault old %d new %d", fo, fn);
	CHECK(same(a, b), "result differs: old %d new %d params", a.n, b.n);
	CHECK(sn.getters <= so.getters, "more getters than before");
}

static void inform(struct dmctx *ctx, const char *tag, int want_getters, int want_requests, struct snap ref)
{
	struct stats s; struct snap b;
	clear();
	CHECK(mtk_inform(ctx) == 0, "inform fault");
	s = take(); b = snap(); clear();
	printf("Inform %-24s %3d getters %2d req | %d params kept\n", tag, s.getters, s.requests, b.n);
	if (want_getters >= 0) CHECK(s.getters == want_getters, "%s: getters %d want %d", tag, s.getters, want_getters);
	if (want_requests >= 0) CHECK(s.requests == want_requests, "%s: requests %d want %d", tag, s.requests, want_requests);
	CHECK(same(ref, b), "%s: Inform params differ from the full walk", tag);
}

int main(int argc, char **argv)
{
	struct dmctx ctx; struct snap ref; struct stats s;
	char line[512]; FILE *f = fopen(argv[2], "r");

	while (f && fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\n")] = 0;
		if (line[0]) claims[nclaims++] = strdup(line);
	}
	memset(&ctx, 0, sizeof(ctx));
	dmscript_configure(argv[1], "--json-input", 60);

	if (argc > 3 && !strcmp(argv[3], "instance")) {
		clear(); take(); old_inform(&ctx); s = take(); ref = snap(); clear();
		printf("old inform: %d getters, %d kept\n", s.getters, ref.n);
		inform(&ctx, "1st (instance)", s.getters, 1, ref);
		inform(&ctx, "2nd (instance)", s.getters, 1, ref);
		goto out;
	}

	gpv(&ctx, "");
	gpv(&ctx, "InternetGatewayDevice.");
	gpv(&ctx, "InternetGatewayDevice.WANDevice.");
	gpv(&ctx, "InternetGatewayDevice.WANDevice.1.WANConnectionDevice.2.");
	gpv(&ctx, "InternetGatewayDevice.LANDevice.");
	gpv(&ctx, "InternetGatewayDevice.LANDevice.1.");
	gpv(&ctx, "InternetGatewayDevice.Firewall.");
	gpv(&ctx, "InternetGatewayDevice.DeviceSummary");
	gpv(&ctx, "InternetGatewayDevice.Nope.");
	gpv(&ctx, "InternetGatewayDevice.WANDevice.9.");

	clear(); take(); old_inform(&ctx); s = take(); ref = snap(); clear();
	printf("old inform: %d getters, %d kept\n", s.getters, ref.n);
	inform(&ctx, "1st (learn)", s.getters, 1, ref);
	inform(&ctx, "2nd", ref.n, ref.n, ref);
	inform(&ctx, "3rd", ref.n, ref.n, ref);
	/* the script stops answering a remembered name: back to the full walk */
	dmscript_request(NULL, NULL, "forget", "param", "InternetGatewayDevice.DeviceSummary", NULL);
	clear(); take(); old_inform(&ctx); s = take(); ref = snap(); clear();
	inform(&ctx, "after forget", -1, -1, ref);
	inform(&ctx, "nothing left", 0, 0, ref);
out:
	dmscript_shutdown();
	printf("%s (%d failures)\n", fails ? "FAILED" : "PASS", fails);
	return fails != 0;
}
