/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// FILE: StunClient.h /////////////////////////////////////////////////////////////////////////////
// Desc:   The address the internet sees us at, asked of a public STUN server.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _STUN_CLIENT_H_
#define _STUN_CLIENT_H_

#include "Lib/BaseType.h"

/* A LAN game knows every player by the address his own machine has.  Over the internet that address
	 is the one behind his router, 192.168.x.y, which nobody else can reach and which two players may
	 well share; the address that counts is the router's, the one the other machines' packets arrive
	 from.  A machine cannot see that address itself, so it asks a STUN server (RFC 5389): one 20-byte
	 Binding request, and the reply carries the address and port the request came from.

	 The request builder and the reply parser are kept apart from the socket so a test can drive them
	 with made-up bytes.  Every address and port here is in host byte order, like the rest of the
	 network code. */

enum
{
	STUN_HEADER_BYTES = 20,
	STUN_TRANSACTION_ID_BYTES = 12,
	STUN_DEFAULT_TIMEOUT_MS = 1500,
};

/** Writes a Binding request with the given transaction ID into out.  Returns its length, always
		STUN_HEADER_BYTES: the request carries no attributes. */
Int stunBuildBindingRequest( UnsignedByte out[ STUN_HEADER_BYTES ], const UnsignedByte transactionID[ STUN_TRANSACTION_ID_BYTES ] );

/** Reads a Binding success response.  True, with *ip and *port set, when data is a response to the
		request with this transaction ID and carries an XOR-MAPPED-ADDRESS or, from a server that only
		speaks RFC 3489, a MAPPED-ADDRESS for IPv4.  False for anything else, a truncated or foreign
		packet included, and then *ip and *port are left alone. */
Bool stunParseBindingResponse( const UnsignedByte *data, Int length,
	const UnsignedByte transactionID[ STUN_TRANSACTION_ID_BYTES ], UnsignedInt *ip, UnsignedShort *port );

/** Asks the public STUN servers, one after the other, which address a socket bound to bindIP is
		seen at, waiting up to timeoutMS for each.  True with *publicIP set on the first answer. */
Bool stunQueryPublicIP( UnsignedInt bindIP, UnsignedInt *publicIP, Int timeoutMS = STUN_DEFAULT_TIMEOUT_MS );

/** The servers stunQueryPublicIP asks, "host:port" each, ended by NULL. */
extern const char *const TheStunServers[];

#endif // _STUN_CLIENT_H_
