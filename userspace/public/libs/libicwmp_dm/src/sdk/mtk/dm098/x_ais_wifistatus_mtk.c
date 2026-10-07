/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	InternetGatewayDevice.X_AIS_WiFiStatus. -- two on-demand reports of the
 *	operator, ported from functions/tr098/X_AIS_WiFiStatus:
 *	  X_AIS_NeighborAP          state 0..3 in /tmp/ais_neighborap_state
 *	                            ("0" when absent); 1 = scan now
 *	  X_AIS_NeighborAPResponse  /tmp/ais_neighborap_response.json, "{}" when
 *	                            absent
 *	  X_AIS_WiFiClient          state 0..3 in /tmp/ais_wificlient_state;
 *	                            1 = list the stations now
 *	  X_AIS_WiFiClientResponse  /tmp/ais_wificlient_response.json
 *	A set takes 0, 1, 2 or 3 exactly and writes it to the state file; 1 runs
 *	the report, which ends in 2 (or 3 when "iw" fails).
 *
 *	Neighbour scan, as the shell did it, inside the setter: per band (ra0,
 *	then rai0 three seconds later) "mwctl <if> scan clear", two seconds,
 *	"mwctl <if> scan type=partial dump" to /tmp/ais_scan_{24g,5g}.tmp, up to
 *	three times while the dump says "No BssInfo".  Every dump line after the
 *	first two that has a BSSID gives rssi|ssid|bssid|channel: the channel is
 *	field 2, the SSID the field before the BSSID ("<hidden>" when there is
 *	none), the RSSI the first "-<digits>" field from the BSSID on ("NO"
 *	without one).  The ten first of "sort -nr" (busybox: numeric, then the
 *	whole line, both descending) per band make the JSON.
 *
 *	Station list: "iw dev ra0/rai0 station dump" to /tmp/ais_sta_{24g,5g}.tmp;
 *	per "Station <mac>" block the last "signal:" value that is "-<digits>",
 *	"NO" without one.
 *
 *	Differences from the shell, on purpose:
 *	  - the station list ran in the background ("&"); it takes a few
 *	    milliseconds and runs in the setter here, so the state reads 2 (or 3)
 *	    as soon as the SPV is answered;
 *	  - Hostname and IP of a station come from /tmp/dhcp.leases (the lease
 *	    whose MAC matches: IP field 3, host name field 4).  The shell meant
 *	    that but split the lease lines on "|" -- the field separator of the
 *	    awk around it -- so it never matched and sent both empty;
 *	  - a '"' or '\' in an SSID is escaped: the shell printed it raw, which
 *	    made the whole report invalid JSON.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>

#include "dmtr098.h"
#include "dmmem.h"
#include "dm_registry.h"
#include "dmmtk.h"

#define NEIGHBOR_STATE	"/tmp/ais_neighborap_state"
#define NEIGHBOR_RESP	"/tmp/ais_neighborap_response.json"
#define CLIENT_STATE	"/tmp/ais_wificlient_state"
#define CLIENT_RESP	"/tmp/ais_wificlient_response.json"
#define DHCP_LEASES	"/tmp/dhcp.leases"

#define WS_MAX_FIELDS	64
#define WS_TOP		10

/* ------------------------------------------------------------------ */
/* small helpers                                                       */
/* ------------------------------------------------------------------ */

static void ws_write(const char *path, const char *text)
{
	FILE *f = fopen(path, "w");

	if (!f)
		return;
	fputs(text, f);
	fclose(f);
}

/* "cat <file>" in a command substitution: all of it, trailing newlines off */
static char *ws_cat(const char *path, char *absent)
{
	FILE *f = fopen(path, "r");
	char *out = NULL, buf[1024];
	size_t len = 0, n;

	if (!f)
		return absent;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0 && len < 256 * 1024) {
		char *grown = dmrealloc(out, len + n + 1);

		if (!grown)
			break;
		out = grown;
		memcpy(out + len, buf, n);
		len += n;
		out[len] = '\0';
	}
	fclose(f);
	if (!out)
		return "";
	while (len && out[len - 1] == '\n')
		out[--len] = '\0';
	return out;
}

/* awk's default field split: runs of blanks, none leading or trailing */
static int ws_fields(char *line, char *f[], int max)
{
	int n = 0;
	char *p = line;

	while (*p && n < max) {
		while (*p == ' ' || *p == '\t')
			p++;
		if (!*p)
			break;
		f[n++] = p;
		while (*p && *p != ' ' && *p != '\t')
			p++;
		if (*p)
			*p++ = '\0';
	}
	return n;
}

/* /^[0-9a-fA-F]{2}(:[0-9a-fA-F]{2}){5}$/ */
static int ws_is_mac(const char *s)
{
	int i;

	for (i = 0; i < 17; i++) {
		if (i % 3 == 2) {
			if (s[i] != ':')
				return 0;
		} else if (!isxdigit((unsigned char)s[i])) {
			return 0;
		}
	}
	return s[17] == '\0';
}

/* /^-[0-9]+$/ */
static int ws_is_rssi(const char *s)
{
	if (*s++ != '-' || !*s)
		return 0;
	for (; *s; s++) {
		if (!isdigit((unsigned char)*s))
			return 0;
	}
	return 1;
}

/* printf "%s" into a JSON string, '"', '\' and controls escaped */
static void ws_json_str(FILE *f, const char *s)
{
	for (; *s; s++) {
		if (*s == '"' || *s == '\\')
			fprintf(f, "\\%c", *s);
		else if ((unsigned char)*s < 0x20)
			fprintf(f, "\\u%04x", (unsigned char)*s);
		else
			fputc(*s, f);
	}
}

/* awk -F'|': the fields of a line split on every "|", in place */
static int ws_bar_split(char *line, char *f[], int max)
{
	int n = 0;
	char *p = line;

	f[n++] = p;
	for (; *p && n < max; p++) {
		if (*p == '|') {
			*p = '\0';
			f[n++] = p + 1;
		}
	}
	return n;
}

/* ------------------------------------------------------------------ */
/* neighbour scan                                                      */
/* ------------------------------------------------------------------ */

/* scan_with_retry <iface> <outfile> */
static void ws_scan(const char *iface, const char *out)
{
	char *clear_argv[] = { "mwctl", (char *)iface, "scan", "clear", NULL };
	char *dump_argv[] = { "/bin/sh", "-c", "mwctl \"$0\" scan type=partial dump > \"$1\"",
			      (char *)iface, (char *)out, NULL };
	int retry;

	for (retry = 3; retry > 0; retry--) {
		mtk_run(clear_argv);
		sleep(2);
		mtk_run(dump_argv);
		if (!strstr(ws_cat(out, ""), "No BssInfo"))
			return;
		sleep(1);
	}
}

static int ws_cmp_desc(const void *a, const void *b)
{
	const char *x = *(const char *const *)a, *y = *(const char *const *)b;
	double dx = strtod(x, NULL), dy = strtod(y, NULL);

	if (dx != dy)
		return dx < dy ? 1 : -1;
	return -strcmp(x, y);
}

/* one band of ais_build_neighborap_json: the awk, sort -nr, head -n 10 and
 * the printf of the second awk */
static void ws_neighbor_band(FILE *o, const char *scan_file)
{
	FILE *f = fopen(scan_file, "r");
	char line[1024], *rows[256];
	int nrows = 0, nr = 0, i, first = 1;

	while (f && fgets(line, sizeof(line), f) && nrows < (int)(sizeof(rows) / sizeof(rows[0]))) {
		char copy[sizeof(line)], *fld[WS_MAX_FIELDS];
		int nf, k;

		line[strcspn(line, "\n")] = '\0';
		if (++nr <= 2)
			continue;
		strcpy(copy, line);
		nf = ws_fields(copy, fld, WS_MAX_FIELDS);
		for (k = 0; k < nf; k++) {
			const char *ssid, *rssi = "NO";
			int j;

			if (!ws_is_mac(fld[k]))
				continue;
			ssid = k > 0 ? fld[k - 1] : line;	/* $(i-1), $0 for i == 1 */
			if (!*ssid || strcmp(ssid, fld[k]) == 0)
				ssid = "<hidden>";
			for (j = k; j < nf; j++) {
				if (ws_is_rssi(fld[j])) {
					rssi = fld[j];
					break;
				}
			}
			dmasprintf(&rows[nrows], "%s|%s|%s|%s", rssi, ssid, fld[k], nf > 1 ? fld[1] : "");
			if (rows[nrows])
				nrows++;
			break;
		}
	}
	if (f)
		fclose(f);
	qsort(rows, nrows, sizeof(rows[0]), ws_cmp_desc);
	for (i = 0; i < nrows && i < WS_TOP; i++) {
		char *bar[8];
		int nb = ws_bar_split(rows[i], bar, 8);

		if (!first)
			fputs(",\n", o);
		first = 0;
		/* printf $2 SSID, $3 BSSID, $4 Ch, $1 Signal */
		fputs("      {\"SSID\":\"", o);
		ws_json_str(o, nb > 1 ? bar[1] : "");
		fputs("\",\"BSSID\":\"", o);
		ws_json_str(o, nb > 2 ? bar[2] : "");
		fputs("\",\"Ch\":\"", o);
		ws_json_str(o, nb > 3 ? bar[3] : "");
		fputs("\",\"Signal\":\"", o);
		ws_json_str(o, bar[0]);
		fputs("\"}", o);
	}
	fputc('\n', o);
}

static void ws_neighbor_scan(void)
{
	FILE *o;

	ws_write(NEIGHBOR_STATE, "1\n");
	ws_scan("ra0", "/tmp/ais_scan_24g.tmp");
	sleep(3);
	ws_scan("rai0", "/tmp/ais_scan_5g.tmp");
	o = fopen(NEIGHBOR_RESP, "w");
	if (o) {
		fputs("{\n  \"WiFi_Neighbor\": {\n    \"2.4GHz\": [\n", o);
		ws_neighbor_band(o, "/tmp/ais_scan_24g.tmp");
		fputs("    ],\n    \"5GHz\": [\n", o);
		ws_neighbor_band(o, "/tmp/ais_scan_5g.tmp");
		fputs("    ]\n  }\n}\n", o);
		fclose(o);
	}
	ws_write(NEIGHBOR_STATE, "2\n");
}

/* ------------------------------------------------------------------ */
/* station list                                                        */
/* ------------------------------------------------------------------ */

/* the lease of <mac>: IP and host name, "" when there is none */
static void ws_lease(const char *mac, char *ip, size_t ipsz, char *host, size_t hostsz)
{
	FILE *f = fopen(DHCP_LEASES, "r");
	char line[512];

	ip[0] = host[0] = '\0';
	while (f && fgets(line, sizeof(line), f)) {
		char *fld[8];
		int nf;

		line[strcspn(line, "\n")] = '\0';
		nf = ws_fields(line, fld, 8);
		if (nf >= 2 && strcmp(fld[1], mac) == 0) {
			snprintf(ip, ipsz, "%s", nf > 2 ? fld[2] : "");
			snprintf(host, hostsz, "%s", nf > 3 ? fld[3] : "");
			break;
		}
	}
	if (f)
		fclose(f);
}

static void ws_station_entry(FILE *o, int *first, const char *mac, const char *rssi)
{
	char ip[64], host[128];

	ws_lease(mac, ip, sizeof(ip), host, sizeof(host));
	if (!*first)
		fputs(",\n", o);
	*first = 0;
	fputs("      {\"Hostname\":\"", o);
	ws_json_str(o, host);
	fputs("\",\"MAC\":\"", o);
	ws_json_str(o, mac);
	fputs("\",\"IP\":\"", o);
	ws_json_str(o, ip);
	fputs("\",\"RSSI\":\"", o);
	ws_json_str(o, rssi);
	fputs("\"}", o);
}

static void ws_station_band(FILE *o, const char *sta_file)
{
	FILE *f = fopen(sta_file, "r");
	char line[512], mac[64] = "", rssi[32] = "NO";
	int first = 1;

	while (f && fgets(line, sizeof(line), f)) {
		char copy[sizeof(line)], *fld[WS_MAX_FIELDS];
		int nf;

		line[strcspn(line, "\n")] = '\0';
		strcpy(copy, line);
		nf = ws_fields(copy, fld, WS_MAX_FIELDS);
		if (strncmp(line, "Station", 7) == 0) {
			if (*mac)
				ws_station_entry(o, &first, mac, rssi);
			snprintf(mac, sizeof(mac), "%s", nf > 1 ? fld[1] : "");
			snprintf(rssi, sizeof(rssi), "NO");
		}
		if (strstr(line, "signal:") && nf > 1 && ws_is_rssi(fld[1]))
			snprintf(rssi, sizeof(rssi), "%s", fld[1]);
	}
	if (*mac)
		ws_station_entry(o, &first, mac, rssi);
	if (f)
		fclose(f);
	fputc('\n', o);
}

static int ws_station_dump(const char *iface, const char *out)
{
	char *argv[] = { "/bin/sh", "-c", "iw dev \"$0\" station dump > \"$1\" 2>/dev/null",
			 (char *)iface, (char *)out, NULL };

	return mtk_run(argv);
}

static void ws_client_dump(void)
{
	FILE *o;

	ws_write(CLIENT_STATE, "1\n");
	if (ws_station_dump("ra0", "/tmp/ais_sta_24g.tmp") != 0 ||
	    ws_station_dump("rai0", "/tmp/ais_sta_5g.tmp") != 0) {
		ws_write(CLIENT_STATE, "3\n");
		ws_write(CLIENT_RESP, "{}\n");
		return;
	}
	o = fopen(CLIENT_RESP, "w");
	if (o) {
		fputs("{\n  \"WiFi_Client\": {\n    \"2.4GHz\": [\n", o);
		ws_station_band(o, "/tmp/ais_sta_24g.tmp");
		fputs("    ],\n    \"5GHz\": [\n", o);
		ws_station_band(o, "/tmp/ais_sta_5g.tmp");
		fputs("    ]\n  }\n}\n", o);
		fclose(o);
	}
	ws_write(CLIENT_STATE, "2\n");
}

/* ------------------------------------------------------------------ */
/* parameters                                                          */
/* ------------------------------------------------------------------ */

static int ws_state_ok(const char *v)
{
	return v && v[0] >= '0' && v[0] <= '3' && !v[1];
}

static int ws_set_state(const char *state_file, const char *value, int action, void (*run)(void))
{
	char line[4];

	if (!ws_state_ok(value))
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	snprintf(line, sizeof(line), "%s\n", value);
	ws_write(state_file, line);
	if (value[0] == '1')
		run();
	return 0;
}

static int get_ws_neighbor_state(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = ws_cat(NEIGHBOR_STATE, "0");
	return 0;
}

static int set_ws_neighbor_state(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return ws_set_state(NEIGHBOR_STATE, value, action, ws_neighbor_scan);
}

static int get_ws_neighbor_resp(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = ws_cat(NEIGHBOR_RESP, "{}");
	return 0;
}

static int get_ws_client_state(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = ws_cat(CLIENT_STATE, "0");
	return 0;
}

static int set_ws_client_state(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return ws_set_state(CLIENT_STATE, value, action, ws_client_dump);
}

static int get_ws_client_resp(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = ws_cat(CLIENT_RESP, "{}");
	return 0;
}

/* ------------------------------------------------------------------ */
/* tables                                                              */
/* ------------------------------------------------------------------ */

static DMLEAF tWiFiStatusParams[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"X_AIS_NeighborAP", &DMWRITE, DMT_UNINT, get_ws_neighbor_state, set_ws_neighbor_state, NULL, NULL},
{"X_AIS_NeighborAPResponse", &DMREAD, DMT_STRING, get_ws_neighbor_resp, NULL, NULL, NULL},
{"X_AIS_WiFiClient", &DMWRITE, DMT_UNINT, get_ws_client_state, set_ws_client_state, NULL, NULL},
{"X_AIS_WiFiClientResponse", &DMREAD, DMT_STRING, get_ws_client_resp, NULL, NULL, NULL},
{0}
};

static DMOBJ tWiFiStatusRoot[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"X_AIS_WiFiStatus", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tWiFiStatusParams, NULL},
{0}
};

static const char *const wifistatus_mtk_paths[] = {
	"InternetGatewayDevice.X_AIS_WiFiStatus.",
	NULL
};

static const struct dm_module wifistatus_mtk_module = {
	.name  = "mtk-x-ais-wifistatus",
	.model = DM_MODEL_TR098,
	.order = DM_ORDER_SDK,
	.objs  = tWiFiStatusRoot,
	.paths = wifistatus_mtk_paths,
};
DM_MODULE_REGISTER(wifistatus_mtk_module);
