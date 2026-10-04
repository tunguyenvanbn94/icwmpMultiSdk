#!/bin/sh
# icwmp_dm.sh — data model driver for libtr098 --with-platform=mtk
#
# Runs the easycwmp shell function library of the product
# (/usr/share/easycwmp/functions/*, the same files cwmpclient/easycwmpd uses)
# as a persistent child of icwmpd (libtr098 platform/script/dmscript.c).
# One JSON request per stdin line, JSON reply lines on stdout, the prompt
# line "icwmp_dm>" closes every reply.  The library itself is NOT modified:
# everything below is the protocol part of easycwmp.sh (json_get_opt +
# handle_action) reduced to the data model commands and made re-entrant
# (globals reset between requests, no exit from the main loop).
#
# Requests (all values are strings):
#   {"cmd":"get_value","param":P}                 GetParameterValues
#   {"cmd":"get_name","param":P,"next_level":"0|1"} GetParameterNames
#   {"cmd":"get_value_list","params":[P,...]}      get_value of each P, one reply
#   {"cmd":"get_name_list","params":[P,...],"next_level":"0|1"}  same for get_name
#   {"cmd":"set_check","param":P,"value":V}       SPV phase 1: validate + queue
#   {"cmd":"set_apply","key":K}                   SPV phase 2: run the queued setters, uci commit
#   {"cmd":"set_abort"}                           drop the queue (another parameter failed)
#   {"cmd":"apply_service"}                       end of session: service restarts
#   {"cmd":"add","param":O,"key":K}               AddObject
#   {"cmd":"delete","param":O,"key":K}            DeleteObject
#   {"cmd":"inform"}                              forced-inform parameters
#   {"cmd":"ping"}                                liveness (replies {"status":"1"})
#   {"cmd":"exit"}
# Replies: the JSON lines of the library (common_json_output_*), i.e.
#   {"parameter":..,"value":..,"type":..}  {"parameter":..,"writable":..}
#   {"parameter":..,"fault_code":"9xxx"}  {"status":"1","instance":..}
#
# Copyright (C) 2012-2014 PIVA Software <www.pivasoftware.com> (easycwmp.sh)
# Copyright (C) 2011-2012 Luka Perkov <freecwmp@lukaperkov.net>

. /lib/functions.sh
. /usr/share/libubox/jshn.sh
[ -f /usr/share/easycwmp/defaults ] && . /usr/share/easycwmp/defaults

PROFILE_CFG=/userfs/profile.cfg
if [ -f $PROFILE_CFG ] ; then
	. $PROFILE_CFG
fi

UCI_GET="/sbin/uci -q ${UCI_CONFIG_DIR:+-c $UCI_CONFIG_DIR} get"
UCI_SET="/sbin/uci -q ${UCI_CONFIG_DIR:+-c $UCI_CONFIG_DIR} set"
UCI_SHOW="/sbin/uci -q ${UCI_CONFIG_DIR:+-c $UCI_CONFIG_DIR} show"
UCI_COMMIT="/sbin/uci -q ${UCI_CONFIG_DIR:+-c $UCI_CONFIG_DIR} commit"
UCI_ADD="/sbin/uci -q ${UCI_CONFIG_DIR:+-c $UCI_CONFIG_DIR} add"
UCI_DELETE="/sbin/uci -q ${UCI_CONFIG_DIR:+-c $UCI_CONFIG_DIR} delete"
UCI_ADD_LIST="/sbin/uci -q ${UCI_CONFIG_DIR:+-c $UCI_CONFIG_DIR} add_list"
UCI_DEL_LIST="/sbin/uci -q ${UCI_CONFIG_DIR:+-c $UCI_CONFIG_DIR} del_list"
UCI_REVERT="/sbin/uci -q ${UCI_CONFIG_DIR:+-c $UCI_CONFIG_DIR} revert"
UCI_CHANGES="/sbin/uci -q ${UCI_CONFIG_DIR:+-c $UCI_CONFIG_DIR} changes"
UCI_BATCH="/sbin/uci -q ${UCI_CONFIG_DIR:+-c $UCI_CONFIG_DIR} batch"
UCI_GET_DEFAULT="/sbin/uci -q -c /rom/etc/config get"

DOWNLOAD_DIR="/tmp/easycwmp_download"
UPLOAD_DIR="/tmp/easycwmp_upload"
EASYCWMP_PROMPT="easycwmp>"
ICWMP_DM_PROMPT="icwmp_dm>"
# same temp files as easycwmp.sh: the library appends the queued setters there
set_fault_tmp_file="/tmp/.easycwmp_set_fault_tmp"
apply_service_tmp_file="/tmp/.easycwmp_apply_service"
set_command_tmp_file="/tmp/.easycwmp_set_command_tmp"
# packages changed by setters/add/delete since the last apply_service (the
# library keeps that in a shell global, we run the setters in subshells)
changed_pkgs_tmp_file="/tmp/.icwmp_dm_changed_pkgs"
FUNCTION_PATH="${ICWMP_DM_FUNCTIONS:-/usr/share/easycwmp/functions}"
NOTIF_PARAM_VALUES="/tmp/.easycwmp_notif_param_value"
easycwmp_config_changed=""
uci_change_packages=""
uci_change_services=""
g_fault_code=""

prefix_list=""
entry_execute_method_list=""
entry_execute_method_list_forcedinform=""
entry_method_root=""

g_entry_param=""
g_entry_method=""
g_entry_arg=""
g_next_level=""
g_entry_done=""
# Fault codes
E_REQUEST_DENIED="1"
E_INTERNAL_ERROR="2"
E_INVALID_ARGUMENTS="3"
E_RESOURCES_EXCEEDED="4"
E_INVALID_PARAMETER_NAME="5"
E_INVALID_PARAMETER_TYPE="6"
E_INVALID_PARAMETER_VALUE="7"
E_NON_WRITABLE_PARAMETER="8"
E_NOTIFICATION_REJECTED="9"
E_DOWNLOAD_FAILURE="10"
E_UPLOAD_FAILURE="11"
E_FILE_TRANSFER_AUTHENTICATION_FAILURE="12"
E_FILE_TRANSFER_UNSUPPORTED_PROTOCOL="13"
E_DOWNLOAD_FAIL_MULTICAST_GROUP="14"
E_DOWNLOAD_FAIL_CONTACT_SERVER="15"
E_DOWNLOAD_FAIL_ACCESS_FILE="16"
E_DOWNLOAD_FAIL_COMPLETE_DOWNLOAD="17"
E_DOWNLOAD_FAIL_FILE_CORRUPTED="18"
E_DOWNLOAD_FAIL_FILE_AUTHENTICATION="19"

# --- load the function library exactly like easycwmp.sh -------------------
if [ ! -f "$FUNCTION_PATH/root" ]; then
	echo '{"fault_code":"9002","error":"function library not found"}'
	echo "$ICWMP_DM_PROMPT"
	exit 1
fi
dmscripts=`ls $FUNCTION_PATH`
. $FUNCTION_PATH/root
if [ "$TCSUPPORT_VOIP" = "" ]; then
	for dms in $dmscripts; do
		[ "$dms" != "root" ] && [ "${dms#x_hni_}" = "$dms" ] && . $FUNCTION_PATH/$dms
	done
else
	. $FUNCTION_PATH/voice_root
	for dms in $dmscripts; do
		[ "$dms" != "root" ] && [ "$dms" != "voice_root" ] && [ "${dms#x_hni_}" = "$dms" ] && . $FUNCTION_PATH/$dms
	done
fi

prefix_list="$DMROOT. $prefix_list"
entry_execute_method_list="$entry_method_root $entry_execute_method_list"

# --- helpers ---------------------------------------------------------------
dm_reset_globals() {
	g_entry_param=""
	g_entry_method=""
	g_entry_arg=""
	g_next_level=""
	g_entry_done=""
	g_fault_code=""
}

dm_status() {
	local status="$1"
	local instance="$2"
	json_init
	json_add_string "status" "$status"
	[ -n "$instance" ] && json_add_string "instance" "$instance"
	json_close_object
	json_dump
}

# uci_change_packages of the library -> file, survives the subshell
dm_save_changed_pkgs() {
	local p
	for p in $uci_change_packages; do
		echo "$p" >> "$changed_pkgs_tmp_file"
	done
}

# revert what the failed setters left uncommitted
dm_revert_pending() {
	local cfg cfg_reverts=`$UCI_CHANGES | cut -d'.' -f1 | sort -u`
	for cfg in $cfg_reverts; do
		cfg=${cfg#[+-]}
		$UCI_REVERT $cfg
	done
}

dm_get_value() {
	local param="$1"
	[ -z "$param" ] && param="$DMROOT."
	dm_reset_globals
	common_entry_get_value "$param"
	local fault="$?"
	if [ "$fault" != "0" ]; then
		common_json_output_fault "$param" "$((fault+9000))"
	fi
}

dm_get_name() {
	local param="$1"
	local nl="$2"
	[ -z "$param" ] && param="$DMROOT."
	case "$nl" in
		[Ff][Aa][Ll][Ss][Ee]|0|"") nl="0" ;;
		[Tt][Rr][Uu][Ee]|1) nl="1" ;;
		*)
			common_json_output_fault "$param" "$((E_INVALID_ARGUMENTS+9000))"
			return
			;;
	esac
	dm_reset_globals
	common_entry_get_name "$param" "$nl"
	local fault="$?"
	if [ "$fault" != "0" ]; then
		common_json_output_fault "$param" "$((fault+9000))"
	fi
}

# SPV phase 1: the library validates the value and appends
# "<param><delim><setcmd><delim><getcmd>" to $set_command_tmp_file
dm_set_check() {
	local param="$1"
	local value="$2"
	dm_reset_globals
	(common_entry_set_value "$param" "$value")
	local fault="$?"
	if [ "$fault" != "0" ]; then
		common_json_output_fault "$param" "$((fault+9000))"
	fi
}

# SPV phase 2 (easycwmp.sh "apply value"): run every queued setter, then
# uci commit.  Any failure reverts the uncommitted changes and reports the
# parameter; libtr098 turns that into the SPV fault.
dm_set_apply() {
	local key="$1"
	local rev="" line param setcmd fault
	if [ ! -f "$set_command_tmp_file" ]; then
		dm_status "1"
		return
	fi
	while read line; do
		[ -z "$line" ] && continue
		param=${line%%<delim>*}
		setcmd=${line#*<delim>}
		setcmd=${setcmd%<delim>*}
		eval "$setcmd"
		fault="$?"
		if [ "$fault" != "0" ]; then
			rev=1
			common_json_output_fault "$param" "$((fault+9000))"
		fi
	done < $set_command_tmp_file
	if [ -n "$rev" ]; then
		dm_revert_pending
		dm_status "0"
	else
		common_uci_change_packages_lookup
		dm_save_changed_pkgs
		$UCI_SET easycwmp.@acs[0].parameter_key="$key"
		$UCI_COMMIT
		dm_status "1"
	fi
	rm -f "$set_fault_tmp_file"
	rm -f "$set_command_tmp_file"
}

dm_set_abort() {
	rm -f "$set_fault_tmp_file"
	rm -f "$set_command_tmp_file"
	dm_status "1"
}

# end of session (easycwmp.sh "apply service"): ucitrack restarts of the
# packages changed by the setters + the delayed commands the setters queued
dm_apply_service() {
	uci_change_packages=""
	if [ -f "$changed_pkgs_tmp_file" ]; then
		uci_change_packages=`sort -u "$changed_pkgs_tmp_file" | tr '\n' ' '`
		rm -f "$changed_pkgs_tmp_file"
	fi
	common_restart_services
	if [ -f "$apply_service_tmp_file" ]; then
		chmod +x "$apply_service_tmp_file"
		# Detached stdio.  This script is icwmpd's coprocess for its whole
		# life: our stdin carries its requests, our stdout its replies.  A
		# queued "... &" (diagnostics launchers, "easycwmpd restart &")
		# would inherit both, and write into -- or read from -- the next
		# request long after this one returned.
		/bin/sh "$apply_service_tmp_file" </dev/null >/dev/null 2>&1
		rm -f "$apply_service_tmp_file"
	fi
	uci_change_packages=""
	uci_change_services=""
	easycwmp_config_changed=""
	dm_status "1"
}

dm_add_object() {
	local param="$1"
	local key="$2"
	dm_reset_globals
	(common_entry_add_object "$param")
	local fault="$?"
	if [ "$fault" != "0" ]; then
		common_json_output_fault "$param" "$((fault+9000))"
		dm_revert_pending
		return
	fi
	common_uci_change_packages_lookup
	dm_save_changed_pkgs
	$UCI_SET easycwmp.@acs[0].parameter_key="$key"
	$UCI_COMMIT
}

dm_delete_object() {
	local param="$1"
	local key="$2"
	dm_reset_globals
	(common_entry_delete_object "$param")
	local fault="$?"
	if [ "$fault" != "0" ]; then
		common_json_output_fault "$param" "$((fault+9000))"
		dm_revert_pending
		return
	fi
	common_uci_change_packages_lookup
	dm_save_changed_pkgs
	$UCI_SET easycwmp.@acs[0].parameter_key="$key"
	$UCI_COMMIT
}

dm_inform() {
	dm_reset_globals
	(common_entry_inform)
}

# --- main loop -------------------------------------------------------------
handle_request() {
	local cmd param value key nl params k p
	json_init
	json_load "$1" || { common_json_output_fault "" "9003"; return; }
	json_get_var cmd cmd
	json_get_var param param
	json_get_var value value
	json_get_var key key
	json_get_var nl next_level
	# the *_list commands: one request, one prompt for many paths (each
	# request costs a json_load, a subshell and a round trip of its own).
	# TR-098 paths hold no white space.
	params=""
	if json_select params 2>/dev/null; then
		json_get_keys k
		for k in $k; do
			json_get_var p "$k"
			[ -n "$p" ] && params="$params $p"
		done
		json_select ..
	fi
	# every handler runs in a subshell: the library calls "exit" from some of
	# its get/set/add paths (e.g. common_get_name_inparam_isparam_check_param)
	# and that must not end this loop
	case "$cmd" in
		get_value)     (dm_get_value "$param") ;;
		get_name)      (dm_get_name "$param" "$nl") ;;
		get_value_list) for p in $params; do (dm_get_value "$p"); done ;;
		get_name_list)  for p in $params; do (dm_get_name "$p" "$nl"); done ;;
		set_check)     (dm_set_check "$param" "$value") ;;
		set_apply)     (dm_set_apply "$key") ;;
		set_abort)     dm_set_abort ;;
		apply_service) (dm_apply_service) ;;
		add)           (dm_add_object "$param" "$key") ;;
		delete)        (dm_delete_object "$param" "$key") ;;
		inform)        (dm_inform) ;;
		ping)          dm_status "1" ;;
		exit)          exit 0 ;;
		*)             common_json_output_fault "" "9000" ;;
	esac
}

# leftovers of a previous icwmpd (killed mid-SPV) must not leak into ours
rm -f "$set_fault_tmp_file" "$set_command_tmp_file" "$changed_pkgs_tmp_file"

if [ "$1" = "--json-input" ]; then
	echo "$ICWMP_DM_PROMPT"
	while read -r CMD; do
		[ -z "$CMD" ] && continue
		handle_request "$CMD"
		echo "$ICWMP_DM_PROMPT"
	done
	exit 0
fi

# one-shot mode for the shell:  icwmp_dm.sh get_value <param> | get_name <param> <0|1> | inform
case "$1" in
	get_value) dm_get_value "$2" ;;
	get_name)  dm_get_name "$2" "$3" ;;
	inform)    dm_inform ;;
	*)
		echo "usage: $0 --json-input | get_value <param> | get_name <param> <0|1> | inform" >&2
		exit 1
		;;
esac
