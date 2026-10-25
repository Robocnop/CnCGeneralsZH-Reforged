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

// FILE: RelayEnvelope.h //////////////////////////////////////////////////////////////////////////
// Desc:   Datagrams one player sends another through the packet router, when the two cannot
//         reach each other over the internet.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _RELAY_ENVELOPE_H_
#define _RELAY_ENVELOPE_H_

#include "Lib/BaseType.h"

/* A game over the internet (-publicip) runs as a star for most of its traffic: every command goes
	 to the packet router, which hands it on.  Some of it does not - disconnect votes, the load
	 screen's "done", frame resends and map transfers go from player to player - and two players
	 who are each behind a router may have no way to reach each other at all, while both reach the
	 host, whose ports UPnP or a hand-made rule opened.

	 So the transport sends such a datagram to the packet router in an envelope that names where it
	 is going, and the router sends it on in one that names where it came from; the receiver takes
	 it out and hands it up as though it had come straight from its author.  Nothing above the
	 transport knows.

	 Whether a pair needs it is found out, not assumed: the two send each other a small probe every
	 half second, saying whether they hear the other one directly.  A player goes through the router
	 to a peer that has not said so for RELAY_SILENCE_MS, and straight again as soon as it does.

	 Three kinds of datagram, each opened by a four-byte tag in plain text, ahead of what the
	 transport encrypts:
		 "ZHRF" ip port datagram - to the router: please send datagram to ip:port
		 "ZHRD" ip port datagram - from the router: datagram was sent by ip:port
		 "ZHRP" heard 0 0 0      - a probe; heard is 1 when the sender hears us directly
	 ip and port in network byte order; here, as in the rest of the network code, they are in host
	 order.  The functions below are pure, so a test drives them with made-up bytes. */

enum RelayKind
{
	RELAY_NONE,
	RELAY_FORWARD,		///< "ZHRF": a player asks the router to send the datagram on
	RELAY_DELIVER,		///< "ZHRD": the router delivers a datagram from another player
};

enum
{
	RELAY_ENVELOPE_BYTES = 10,		///< tag, address and port in front of the datagram
	RELAY_PROBE_BYTES = 8,
	RELAY_PROBE_INTERVAL_MS = 500,
	RELAY_SILENCE_MS = 3000,			///< no word from a peer for this long, and the router carries its traffic
};

/** Puts datagram in an envelope of the given kind naming ip:port, in out.  Returns the envelope's
		length, or 0 when kind is RELAY_NONE or out has no room. */
Int relayWrap( RelayKind kind, UnsignedInt ip, UnsignedShort port, const UnsignedByte *datagram, Int length,
	UnsignedByte *out, Int outSize );

/** The kind of envelope data is, with the address it names and the datagram inside.  RELAY_NONE,
		and nothing set, for anything else: a game datagram, a probe, or an envelope with nothing in it. */
RelayKind relayUnwrap( const UnsignedByte *data, Int length, UnsignedInt *ip, UnsignedShort *port,
	const UnsignedByte **datagram, Int *datagramLength );

/** Writes a probe into out, which holds RELAY_PROBE_BYTES.  Returns RELAY_PROBE_BYTES. */
Int relayBuildProbe( Bool heardYou, UnsignedByte out[ RELAY_PROBE_BYTES ] );

/** True when data is a probe, with *heardYou set from it. */
Bool relayParseProbe( const UnsignedByte *data, Int length, Bool *heardYou );

/** Whether a datagram for a peer goes through the router.  since is when the peer was first seen,
		confirmed when it last said it heard us directly (0 if never), now the time; all in ms.  A peer
		gets RELAY_SILENCE_MS from its arrival before the router is called in. */
Bool relayNeedsRouter( UnsignedInt now, UnsignedInt since, UnsignedInt confirmed );

#endif // _RELAY_ENVELOPE_H_
