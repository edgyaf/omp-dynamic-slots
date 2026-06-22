#if defined _advanced_query_included
	#endinput
#endif
#define _advanced_query_included

native SetQueryMaxPlayers(slots);
native GetQueryMaxPlayers();
native ResetQueryMaxPlayers();

native SetInitGameHostname(const hostname[]);
native ResetInitGameHostname();
native IsInitGameHostnameSet();
