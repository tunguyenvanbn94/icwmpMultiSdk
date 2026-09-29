#!/bin/sh
# /usr/sbin/icwmp on the MediaTek/Airoha OpenWrt product tree (HNI):
# the external action backend of icwmpd (external.c, "json_continuous_input"
# protocol of scripts/icwmp.sh) with the product's own logic for downloads,
# uploads, firmware apply, reboot and factory reset, lifted from
# cwmpclient/ext/openwrt/scripts/easycwmp.sh (handle_action download /
# upload / apply_download / factory_reset / reboot).  The data model is NOT
# here: libtr098 --with-platform=mtk drives the easycwmp function library
# through /usr/share/icwmp/icwmp_dm.sh.
#
# Replies: {"fault_code":"0"} = ok, {"fault_code":"9xxx"} = CWMP fault.
#
# Copyright (C) 2011-2012 Luka Perkov <freecwmp@lukaperkov.net>
# Copyright (C) 2013-2019 iopsys Software Solutions AB
# Copyright (C) 2012-2016 PIVA Software <www.pivasoftware.com> (easycwmp.sh)

. /usr/share/libubox/jshn.sh

CWMP_PROMPT="icwmp>"
UCI_GET="/sbin/uci -q ${UCI_CONFIG_DIR:+-c $UCI_CONFIG_DIR} get"
UCI_SET="/sbin/uci -q ${UCI_CONFIG_DIR:+-c $UCI_CONFIG_DIR} set"
UCI_COMMIT="/sbin/uci -q ${UCI_CONFIG_DIR:+-c $UCI_CONFIG_DIR} commit"
DOWNLOAD_DIR="/tmp/icwmp_download"
# minimum free memory for a download (easycwmp.sh: 80 MB), KB
DOWNLOAD_MIN_MEM_KB=81920
# backup session file of icwmpd, kept across sysupgrade like easycwmp's
ICWMP_BKP_FILE="/etc/icwmpd/.icwmpd_backup_session.xml"

# Fault codes (CWMP 9000 + n)
FAULT_CPE_NO_FAULT="0"
FAULT_CPE_INTERNAL_ERROR="2"
FAULT_CPE_INVALID_ARGUMENTS="3"
FAULT_CPE_DOWNLOAD_FAILURE="10"
FAULT_CPE_UPLOAD_FAILURE="11"
FAULT_CPE_DOWNLOAD_FAIL_CONTACT_SERVER="15"
FAULT_CPE_DOWNLOAD_FAIL_FILE_CORRUPTED="18"
FAULT_CPE_DOWNLOAD_FAIL_FILE_AUTHENTICATION="19"

icwmp_fault_output() {
	local fault_code="$2"
	json_init
	json_add_string "fault_code" "$fault_code"
	json_close_object
	json_dump
}

icwmp_fault() {
	# $1 = FAULT_CPE_* (n); prints 9000+n, "0" stays "0"
	if [ "$1" = "0" ]; then
		icwmp_fault_output "" "0"
	else
		icwmp_fault_output "" "$((9000 + $1))"
	fi
}

# --- download ---------------------------------------------------------------
# __arg1 url, __arg2 size, __arg3 file type (1|2|3|...), __arg4 user, __arg5 pass
do_download() {
	local url="$__arg1" size="$__arg2" user="$__arg4" pass="$__arg5"
	local mem_available dw_url

	rm -rf "$DOWNLOAD_DIR" 2>/dev/null
	mkdir -p "$DOWNLOAD_DIR"

	mem_available=$(grep MemAvailable /proc/meminfo | awk '{print $2}')
	if [ -z "$mem_available" ] || [ "$mem_available" -lt "$DOWNLOAD_MIN_MEM_KB" ]; then
		echo "icwmp: not enough memory for download: ${mem_available:-0}KB" >&2
		rm -rf "$DOWNLOAD_DIR" 2>/dev/null
		icwmp_fault "$FAULT_CPE_DOWNLOAD_FAILURE"
		return
	fi

	dw_url="$url"
	[ -n "$user" -o -n "$pass" ] && dw_url=`echo "$url" | sed -e "s@://@://$user:$pass\@@g"`

	wget -P "$DOWNLOAD_DIR" "$dw_url"
	if [ "$?" != "0" ]; then
		rm -rf "$DOWNLOAD_DIR" 2>/dev/null
		icwmp_fault "$FAULT_CPE_DOWNLOAD_FAILURE"
		return
	fi
	if [ -z "`ls $DOWNLOAD_DIR 2>/dev/null`" ]; then
		icwmp_fault "$FAULT_CPE_DOWNLOAD_FAILURE"
		return
	fi
	icwmp_fault "$FAULT_CPE_NO_FAULT"
}

# --- apply download (after the ACS got DownloadResponse) --------------------
# __arg1 file type: 1 = firmware, 3 = vendor configuration file
do_apply_download() {
	local dwfile
	case "$__arg1" in
	1*)
		# keep the icwmpd session backup across the upgrade (easycwmp did the
		# same for /etc/easycwmp/.backup.xml)
		grep -q "^$ICWMP_BKP_FILE" /etc/sysupgrade.conf 2>/dev/null || echo "$ICWMP_BKP_FILE" >> /etc/sysupgrade.conf
		dwfile=`ls $DOWNLOAD_DIR 2>/dev/null | head -n 1`
		if [ -z "$dwfile" ]; then
			icwmp_fault "$FAULT_CPE_DOWNLOAD_FAILURE"
			return
		fi
		dwfile="$DOWNLOAD_DIR/$dwfile"
		if [ -x /userfs/bin/hni_validate_image.sh ]; then
			/userfs/bin/hni_validate_image.sh "$dwfile"
			if [ "$?" != "0" ]; then
				rm -rf "$DOWNLOAD_DIR" 2>/dev/null
				icwmp_fault "$FAULT_CPE_DOWNLOAD_FAIL_FILE_CORRUPTED"
				return
			fi
		fi
		$UCI_SET system.@system[0].HumaxUpgradeStatus='START'
		$UCI_SET system.@system[0].next_reboot_reason=SW_UPDATE_BY_ACS
		$UCI_COMMIT system
		/sbin/sysupgrade "$dwfile"
		if [ "$?" != "0" ]; then
			rm -rf "$DOWNLOAD_DIR" 2>/dev/null
			$UCI_SET system.@system[0].next_reboot_reason=POWER_ON_RESET
			$UCI_SET system.@system[0].HumaxUpgradeStatus='FAIL'
			$UCI_COMMIT system
			icwmp_fault "$FAULT_CPE_DOWNLOAD_FAIL_FILE_CORRUPTED"
			return
		fi
		icwmp_fault "$FAULT_CPE_NO_FAULT"
		;;
	3*)
		dwfile=`ls $DOWNLOAD_DIR 2>/dev/null | head -n 1`
		if [ -z "$dwfile" ]; then
			icwmp_fault "$FAULT_CPE_DOWNLOAD_FAILURE"
			return
		fi
		dwfile="$DOWNLOAD_DIR/$dwfile"
		rm -rf /tmp/icwmp_gz
		mkdir -p /tmp/icwmp_gz
		if tar -zxf "$dwfile" -C /tmp/icwmp_gz 2>/dev/null || tar -jxf "$dwfile" -C /tmp/icwmp_gz 2>/dev/null; then
			sysupgrade --restore-backup "$dwfile"
			if [ "$?" != "0" ]; then
				rm -rf "$DOWNLOAD_DIR" /tmp/icwmp_gz 2>/dev/null
				icwmp_fault "$FAULT_CPE_DOWNLOAD_FAIL_FILE_CORRUPTED"
				return
			fi
			rm -rf "$DOWNLOAD_DIR" /tmp/icwmp_gz 2>/dev/null
			icwmp_fault "$FAULT_CPE_NO_FAULT"
			sync
			reboot -b SW_UPDATE_BY_ACS
		else
			echo "icwmp: vendor configuration file is not a tar.gz/tar.bz2" >&2
			rm -rf "$DOWNLOAD_DIR" /tmp/icwmp_gz 2>/dev/null
			icwmp_fault "$FAULT_CPE_DOWNLOAD_FAIL_FILE_CORRUPTED"
		fi
		;;
	*)
		rm -rf "$DOWNLOAD_DIR" 2>/dev/null
		icwmp_fault "$FAULT_CPE_INVALID_ARGUMENTS"
		;;
	esac
}

# --- upload -------------------------------------------------------------------
# __arg1 url, __arg2 file type, __arg3 user, __arg4 pass, __arg5 name
do_upload() {
	local up_url="$__arg1" user="$__arg3" pass="$__arg4" rc=1
	local sn=`$UCI_GET easycwmp.@device[0].serial_number`
	local f

	[ -n "$user" -o -n "$pass" ] && up_url=`echo "$__arg1" | sed -e "s@://@://$user:$pass\@@g"`
	case "$__arg2" in
	*"Vendor Log File"*)
		f="/tmp/log$sn.log"
		tail -n 100 /var/log/currLogFile > "$f" 2>/dev/null
		;;
	*"Vendor Configuration File"*)
		f="/tmp/config$sn.tar.gz"
		sysupgrade --create-backup "$f"
		;;
	*)
		icwmp_fault "$FAULT_CPE_INVALID_ARGUMENTS"
		return
		;;
	esac
	if [ -n "$user" ]; then
		curl --anyauth --user "$user:$pass" --connect-timeout 30 --upload-file "$f" "$__arg1"
	else
		curl --anyauth --connect-timeout 30 --upload-file "$f" "$__arg1"
	fi
	rc="$?"
	rm -f "$f"
	if [ "$rc" != "0" ]; then
		icwmp_fault "$FAULT_CPE_UPLOAD_FAILURE"
	else
		icwmp_fault "$FAULT_CPE_NO_FAULT"
	fi
}

do_factory_reset() {
	if [ "`which jffs2_mark_erase`" != "" ]; then
		jffs2_mark_erase "rootfs_data"
	else
		/sbin/jffs2mark -y -b RESTORE_DEFAULT_ACS
	fi
	sync
	reboot -b nochange
}

do_reboot() {
	sync
	[ -n "$commandKey" ] && $UCI_SET cwmp.acs.ParameterKey="$commandKey" && $UCI_COMMIT cwmp
	reboot -b KERNEL_RESET_ACS
}

handle_action() {
	case "$action" in
		download)           do_download ;;
		apply_download)     do_apply_download ;;
		upload)             do_upload ;;
		factory_reset)      do_factory_reset ;;
		factory_reset_soft) do_factory_reset ;;
		reboot)             do_reboot ;;
		# the CR port is opened on the WAN by easycwmp_firewall.sh (init):
		# nothing per ACS address
		allow_cr_ip)        ;;
		end_session)        ;;
		apply_value|apply_notification) ;;
		du_install|du_update|du_uninstall)
			icwmp_fault_output "" "9000" ;;
	esac
}

if [ "$1" != "json_continuous_input" ]; then
	echo "usage: $0 json_continuous_input (driven by icwmpd)" >&2
	exit 1
fi

echo "$CWMP_PROMPT"
while read CMD; do
	[ -z "$CMD" ] && continue
	action=""
	__arg1=""; __arg2=""; __arg3=""; __arg4=""; __arg5=""; __arg6=""; __arg7=""
	json_init
	json_load "$CMD" || continue
	json_get_var command command
	json_get_var arg arg
	case "$command" in
		download)
			json_get_var __arg1 url
			json_get_var __arg2 size
			json_get_var __arg3 type
			json_get_var __arg4 user
			json_get_var __arg5 pass
			json_get_var __arg6 ids
			json_get_var __arg7 cert_path
			action="download"
			;;
		upload)
			json_get_var __arg1 url
			json_get_var __arg2 type
			json_get_var __arg3 user
			json_get_var __arg4 pass
			json_get_var __arg5 name
			action="upload"
			;;
		du_install|du_update|du_uninstall)
			action="$command"
			;;
		factory_reset|factory_reset_soft|end_session)
			action="$command"
			;;
		reboot)
			action="reboot"
			commandKey="$arg"
			;;
		apply)
			json_get_var action action
			if [ "$action" = "download" ]; then
				json_get_var __arg1 arg
				json_get_var __arg2 ids
				action="apply_download"
			elif [ "$action" = "notification" ]; then
				action="apply_notification"
			else
				json_get_var __arg1 arg
				action="apply_value"
			fi
			;;
		allow_cr_ip)
			action="allow_cr_ip"
			json_get_var __arg1 arg
			json_get_var __arg2 ipv6
			;;
		end)
			echo "$CWMP_PROMPT"
			continue
			;;
		exit)
			exit 0
			;;
		*)
			continue
			;;
	esac
	handle_action 2>/dev/null
done
exit 0
