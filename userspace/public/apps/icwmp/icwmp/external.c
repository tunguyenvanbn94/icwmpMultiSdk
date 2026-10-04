/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	Copyright (C) 2013-2019  iopsys Software Solutions AB
 *	  Author Mohamed Kallel <mohamed.kallel@pivasoftware.com>
 *	  Author Ahmed Zribi <ahmed.zribi@pivasoftware.com>
 *	Copyright (C) 2011 Luka Perkov <freecwmp@lukaperkov.net>
 */

#include <errno.h>
#include <malloc.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <libubox/uloop.h>
#include <json-c/json.h>

#include "external.h"
#include "cwmp.h"
#include "xml.h"
#include "log.h"

static int pid;
static json_object *json_obj_in;
static int pfds_in[2], pfds_out[2];
static FILE *fpipe;
char *external_MethodFault = NULL;
char *external_MethodName = NULL;
char *external_MethodVersion = NULL;
char *external_MethodUUID = NULL;
char *external_MethodENV = NULL;
// extern char *ns;

#define ICWMP_PROMPT "icwmp>"

void external_downloadFaultResp (char *fault_code)
{
	FREE(external_MethodFault);
	external_MethodFault = fault_code ? strdup(fault_code) : NULL;
}

void external_fetch_downloadFaultResp (char **fault)
{
	*fault = external_MethodFault;
	external_MethodFault = NULL;
}

void external_uploadFaultResp (char *fault_code)
{
	FREE(external_MethodFault);
	external_MethodFault = fault_code ? strdup(fault_code) : NULL;
}

void external_fetch_uploadFaultResp (char **fault)
{
	*fault = external_MethodFault;
	external_MethodFault = NULL;
}

void external_uninstallFaultResp (char *fault_code)
{
	FREE(external_MethodFault);
	external_MethodFault = fault_code ? strdup(fault_code) : NULL;
}

void external_fetch_uninstallFaultResp (char **fault)
{
	*fault = external_MethodFault;
	external_MethodFault = NULL;
}

void external_du_change_stateFaultResp (char *fault_code, char *version, char *name, char *uuid, char *env)
{
	FREE(external_MethodFault);
	external_MethodFault = fault_code ? strdup(fault_code) : NULL;
	FREE(external_MethodVersion);
	external_MethodVersion = version ? strdup(version) : NULL;
	FREE(external_MethodName);
	external_MethodName = name ? strdup(name) : NULL;
	FREE(external_MethodUUID);
	external_MethodUUID = uuid ? strdup(uuid) : NULL;
	FREE(external_MethodENV);
	external_MethodENV = env ? strdup(env) : NULL;
}

void external_fetch_du_change_stateFaultResp(char **fault, char **version, char **name, char **uuid, char **env)
{
	*fault = external_MethodFault;
	external_MethodFault = NULL;
	*version = external_MethodVersion;
	external_MethodVersion = NULL;
	*name = external_MethodName;
	external_MethodName = NULL;
	*uuid = external_MethodUUID;
	external_MethodUUID = NULL;
	*env = external_MethodENV;
	external_MethodENV = NULL;
}
/* bytes the script sent after the prompt that ended the last reply */
static char *ext_pending;
static size_t ext_pending_len;

/* Hand every line of the script's reply to external_handler, up to the
 * prompt line.  It used to read one byte per read() and asprintf() the
 * whole line again for every byte: quadratic in the line length, a
 * syscall and an allocation per byte. */
static void external_read_pipe_input(int (*external_handler)(char *msg))
{
	char chunk[1024];
	char *line = NULL, *data;
	size_t len = 0, cap = 0, n, i;
	ssize_t r;
	int owned, done = 0;
	struct pollfd fd = {
		.fd	= pfds_in[0],
		.events	= POLLIN
	};

	while (!done) {
		if (ext_pending_len) {
			data = ext_pending;
			n = ext_pending_len;
			ext_pending = NULL;
			ext_pending_len = 0;
			owned = 1;
		} else {
			poll(&fd, 1, 500000);
			if (!(fd.revents & POLLIN))
				break;
			r = read(pfds_in[0], chunk, sizeof(chunk));
			if (r <= 0)
				break;
			data = chunk;
			n = (size_t)r;
			owned = 0;
		}
		for (i = 0; i < n; i++) {
			if (data[i] != '\n') {
				if (len + 2 > cap) {
					size_t ncap = cap ? cap * 2 : 256;
					char *nl = realloc(line, ncap);

					if (!nl)
						continue;	/* drop the byte, keep the line */
					line = nl;
					cap = ncap;
				}
				line[len++] = data[i];
				continue;
			}
			if (!len)
				continue;
			line[len] = '\0';
			len = 0;
			if (strcmp(line, ICWMP_PROMPT) == 0) {
				/* keep what followed the prompt for the next reply */
				if (i + 1 < n) {
					ext_pending = malloc(n - i - 1);
					if (ext_pending) {
						memcpy(ext_pending, data + i + 1, n - i - 1);
						ext_pending_len = n - i - 1;
					}
				}
				done = 1;
				break;
			}
			if (external_handler)
				external_handler(line);
		}
		if (owned)
			free(data);
	}
	free(line);
}

static void external_write_pipe_output(const char *msg)
{
    char *value = NULL;
    int i=0, len;

    asprintf(&value, "%s\n", msg);
    if (write(pfds_out[1], value, strlen(value)) == -1) {
    	CWMP_LOG(ERROR,"Error occured when trying to write to the pipe");
	}
    free(value);
}

static void json_obj_out_add(json_object *json_obj_out, char *name, char *val)
{
	json_object *json_obj_tmp;

	json_obj_tmp = json_object_new_string(val);
	json_object_object_add(json_obj_out, name, json_obj_tmp);
	}

void external_init()
{
	/* a new script: nothing pending from the previous one */
	FREE(ext_pending);
	ext_pending_len = 0;

	if (pipe(pfds_in) < 0)
			return;

	if (pipe(pfds_out) < 0)
		return;

	if ((pid = fork()) == -1)
		goto error;

	if (pid == 0) {
		/* child */

		close(pfds_out[1]);
		close(pfds_in[0]);

		dup2(pfds_out[0], STDIN_FILENO);
		dup2(pfds_in[1], STDOUT_FILENO);

		const char *argv[5];
		int i = 0;
		argv[i++] = "/bin/sh";
	 	argv[i++] = fc_script;
	 	argv[i++] = "json_continuous_input";
		argv[i++] = NULL;
		execvp(argv[0], (char **) argv);

		close(pfds_out[0]);
		close(pfds_in[1]);

		exit(ESRCH);
	}

	close(pfds_in[1]);
    close(pfds_out[0]);

    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
    {
    	DD(ERROR, "icwmp script intialization: signal ignoring error");
    }
	external_read_pipe_input(NULL);

	DD(INFO, "icwmp script is listening");
	return;

error:
	CWMP_LOG(ERROR,"icwmp script intialization failed");
	exit(EXIT_FAILURE);
}

void external_exit()
{
    int status;

	json_object *json_obj_out;

	json_obj_out = json_object_new_object();

	json_obj_out_add(json_obj_out, "command", "exit");

	external_write_pipe_output(json_object_to_json_string(json_obj_out));

	json_object_put(json_obj_out);

	/* this child only: wait() took any child and looped until it saw this
	 * one, but the uloop thread (libubox SIGCHLD handling) reaps every child
	 * of the process with waitpid(-1, WNOHANG) as soon as it wakes up.  When
	 * it got there first, wait() then blocked on the data model shell, which
	 * never exits, or returned -1 forever without one: the session thread
	 * hung, or spun at 100% CPU.  ECHILD = already reaped, nothing to wait for. */
	while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
		;

	close(pfds_in[0]);
    close(pfds_out[1]);
}

int external_handle_action(int (*external_handler)(char *msg))
{
	json_object *json_obj_out;

	json_obj_out = json_object_new_object();
	json_obj_out_add(json_obj_out, "command", "end");
	external_write_pipe_output(json_object_to_json_string(json_obj_out));
	json_object_put(json_obj_out);
	external_read_pipe_input(external_handler);
	return 0;
}

int external_simple(char *command, char *arg, int c)
{
	DD(INFO,"executing %s request", command);

	json_object *json_obj_out;

	/* send data to the script */
	json_obj_out = json_object_new_object();

	json_obj_out_add(json_obj_out, "command", command);
	if (arg) json_obj_out_add(json_obj_out, "arg", arg);

	if (c)  json_obj_out_add(json_obj_out, "ipv6", "1");
	external_write_pipe_output(json_object_to_json_string(json_obj_out));

	json_object_put(json_obj_out);

	return 0;
}

int external_download(char *url, char *size, char *type, char *user, char *pass, time_t c)
{
	DD(INFO,"executing download url '%s'", url);
	char *id = NULL;
	char *cert_path = NULL;
	struct config *conf;
	json_object *json_obj_out;
	struct cwmp   *cwmp = &cwmp_main;
	
	conf = &(cwmp->conf);
	if (strncmp(url,DOWNLOAD_PROTOCOL_HTTPS,strlen(DOWNLOAD_PROTOCOL_HTTPS)) == 0)
	{
		if(conf->https_ssl_capath)
			cert_path = strdup(conf->https_ssl_capath);
		else
			cert_path = NULL;
	}
	if(cert_path)
		CWMP_LOG(DEBUG,"https certif path %s", cert_path);
	if (c) asprintf(&id, "%ld", c);
	/* send data to the script */
	json_obj_out = json_object_new_object();

	json_obj_out_add(json_obj_out, "command", "download");
	json_obj_out_add(json_obj_out, "url", url);
	json_obj_out_add(json_obj_out, "size", size);
	json_obj_out_add(json_obj_out, "type", type);
	if(user) json_obj_out_add(json_obj_out, "user", user);
	if(pass) json_obj_out_add(json_obj_out, "pass", pass);
	if(id) json_obj_out_add(json_obj_out, "ids", id);
	if(cert_path) json_obj_out_add(json_obj_out, "cert_path", cert_path);
	external_write_pipe_output(json_object_to_json_string(json_obj_out));

	json_object_put(json_obj_out);

	if(cert_path)
		free(cert_path);
	if(id)
		free(id);
	return 0;
}

int external_upload(char *url, char *type, char *user, char *pass, char *name)
{
	DD(INFO,"executing download url '%s'", url);

	json_object *json_obj_out;

	/* send data to the script */
	json_obj_out = json_object_new_object();

	json_obj_out_add(json_obj_out, "command", "upload");
	json_obj_out_add(json_obj_out, "url", url);
	json_obj_out_add(json_obj_out, "type", type);
	json_obj_out_add(json_obj_out, "name", name);
	if(user) json_obj_out_add(json_obj_out, "user", user);
	if(pass) json_obj_out_add(json_obj_out, "pass", pass);

	external_write_pipe_output(json_object_to_json_string(json_obj_out));

	json_object_put(json_obj_out);

	return 0;
}

int external_change_du_state_install(char *url, char *uuid, char *user, char *pass, char *env)
{
	DD(INFO,"executing DU install");
	json_object *json_obj_out;

	/* send data to the script */
	json_obj_out = json_object_new_object();

	json_obj_out_add(json_obj_out, "command", "du_install");
	json_obj_out_add(json_obj_out, "url", url);
	if (uuid) json_obj_out_add(json_obj_out, "uuid", uuid);
	if (user) json_obj_out_add(json_obj_out, "user", user);
	if (pass) json_obj_out_add(json_obj_out, "pass", pass);
	if (env) json_obj_out_add(json_obj_out, "env", env);

	external_write_pipe_output(json_object_to_json_string(json_obj_out));

	json_object_put(json_obj_out);

	return 0;
}

int external_change_du_state_update(char *uuid, char *url, char *version, char *user, char *pass)
{
	DD(INFO,"executing DU update");
	json_object *json_obj_out;

	/* send data to the script */
	json_obj_out = json_object_new_object();

	json_obj_out_add(json_obj_out, "command", "du_update");
	json_obj_out_add(json_obj_out, "uuid", uuid);
	json_obj_out_add(json_obj_out, "url", url);
	if (version) json_obj_out_add(json_obj_out, "version", version);
	if (user) json_obj_out_add(json_obj_out, "user", user);
	if (pass) json_obj_out_add(json_obj_out, "pass", pass);

	external_write_pipe_output(json_object_to_json_string(json_obj_out));

	json_object_put(json_obj_out);

	return 0;
}

int external_change_du_state_uninstall(char *name, char *env)
{
	DD(INFO,"executing DU uninstall");
	json_object *json_obj_out;

	/* send data to the script */
	json_obj_out = json_object_new_object();

	json_obj_out_add(json_obj_out, "command", "du_uninstall");
	json_obj_out_add(json_obj_out, "name", name);
	if(env) json_obj_out_add(json_obj_out, "env", env);

	external_write_pipe_output(json_object_to_json_string(json_obj_out));

	json_object_put(json_obj_out);

	return 0;
}

int external_apply(char *action, char *arg, time_t c)
{
	DD(INFO,"executing apply %s", action);

	json_object *json_obj_out;
	char *id = NULL;

	if (c) asprintf(&id, "%ld", c);

	/* send data to the script */
	json_obj_out = json_object_new_object();

	json_obj_out_add(json_obj_out, "command", "apply");
	json_obj_out_add(json_obj_out, "action", action);
	if (arg) json_obj_out_add(json_obj_out, "arg", arg);

	if(id) json_obj_out_add(json_obj_out, "ids", id);
	external_write_pipe_output(json_object_to_json_string(json_obj_out));

	json_object_put(json_obj_out);

	if(id) {
		free(id);
		id= NULL;
	}
	return 0;
}

