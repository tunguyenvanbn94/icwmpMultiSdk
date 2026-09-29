/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	Script data model provider — transport only.
 *
 *	A persistent child shell runs a driver script (scripts/mtk/icwmp_dm.sh
 *	for the easycwmp function library) and answers one request per line:
 *
 *	  libtr098 -> child   {"cmd":"get_value","param":"InternetGatewayDevice.LANDevice."}\n
 *	  child -> libtr098   {"parameter":"...","value":"...","type":"xsd:string"}\n   (0..n lines)
 *	                      {"parameter":"...","fault_code":"9005"}\n               (or faults)
 *	                      icwmp_dm>\n                                             (prompt = end)
 *
 *	Once its library is loaded the child prints one prompt before it reads
 *	the first request; the spawn consumes it, so each reply belongs to the
 *	request that was just written.
 *
 *	Every reply line is a JSON object handed to the caller's callback; the
 *	platform (platform/mtk/dmplatform_mtk.c) turns them into engine lists.
 *	The child is spawned on first use and respawned when it died; a request
 *	that gets no prompt within the timeout kills the child and fails with
 *	FAULT_9002 so a hung shell cannot hang the CWMP session forever.
 *
 *	All calls are serialised by a mutex: icwmpd uses the data model from the
 *	session thread and from the value-change (ubus "tr069 notify") thread.
 *
 *	No dmmem allocation in here: the callbacks decide (dmstrdup for engine
 *	lists, plain buffers for the value-change thread).
 */
#ifndef __DMSCRIPT_H__
#define __DMSCRIPT_H__

#include <json-c/json.h>

/* driver script and its argument (the child is "/bin/sh <script> <arg>") */
#ifndef DMSCRIPT_PATH
#define DMSCRIPT_PATH   "/usr/share/icwmp/icwmp_dm.sh"
#endif
#ifndef DMSCRIPT_ARG
#define DMSCRIPT_ARG    "--json-input"
#endif
#ifndef DMSCRIPT_PROMPT
#define DMSCRIPT_PROMPT "icwmp_dm>"
#endif
/* seconds to wait for the prompt after a request; a GetParameterValues of
 * the whole tree forks a few thousand "uci get" in the shell */
#ifndef DMSCRIPT_TIMEOUT_SEC
#define DMSCRIPT_TIMEOUT_SEC 240
#endif

/* reply line callback: line is owned by dmscript (json_object_put after the
 * call), priv is the caller's cookie.  Return value ignored. */
typedef int (*dmscript_line_cb)(json_object *line, void *priv);

/* Send req (a JSON object, not consumed) and collect the reply lines until
 * the prompt.  0 on success, -1 when the child could not be started, died or
 * timed out (child is killed and will be respawned on the next call). */
int dmscript_call(json_object *req, dmscript_line_cb cb, void *priv);

/* Convenience: build {"cmd":cmd, k1:v1, k2:v2, ...} (NULL-terminated pairs,
 * NULL values are skipped) and call.  Same return as dmscript_call(). */
int dmscript_request(dmscript_line_cb cb, void *priv, const char *cmd, ...);

/* Stop the child (exit request, then SIGTERM after a short grace). Safe to
 * call when no child runs. */
void dmscript_shutdown(void);

/* Override the defaults before the first call (icwmpd may pass a UCI
 * configured path); NULL keeps the current value. */
void dmscript_configure(const char *script, const char *arg, int timeout_sec);

/* 1 when a child is currently alive */
int dmscript_alive(void);

#endif
