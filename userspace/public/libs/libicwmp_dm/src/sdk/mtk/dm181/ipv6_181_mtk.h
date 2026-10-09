/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	TR-181 IPv6Address / IPv6Prefix rows of an IP.Interface (ipv6_181_mtk.c).
 */
#ifndef __IPV6_181_MTK_H
#define __IPV6_181_MTK_H

/* how many IPv6Address (prefixes 0) or IPv6Prefix (1) rows the interface on
 * network section <sec> has: what its NumberOfEntries leaves read */
int ipv6181_count(const char *sec, int prefixes);

#endif
