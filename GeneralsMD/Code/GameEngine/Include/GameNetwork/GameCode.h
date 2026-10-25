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

// FILE: GameCode.h ///////////////////////////////////////////////////////////////////////////////
// Desc:   A host's internet address as a short code a friend can type.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _GAME_CODE_H_
#define _GAME_CODE_H_

#include "Lib/BaseType.h"

/* "K7QM-2XFA" rather than "86.247.13.201": the host reads it off the Direct Connect screen and the
	 others type it in, or paste it, in place of an address.  Eight characters of Crockford's base32
	 hold the four bytes of the address and a CRC-8 of them, so a mistyped code is refused rather
	 than sending the join to somebody else.  Read without regard to case, dashes or spaces, with O
	 taken for 0 and I and L for 1, the letters people confuse.  The lobby port is always the same,
	 so the code does not carry one. */

enum
{
	GAME_CODE_CHARS = 9,		///< "XXXX-XXXX", without the terminator
};

/** Writes ip's code into out, which holds GAME_CODE_CHARS + 1. */
void gameCodeFromIP( UnsignedInt ip, char out[ GAME_CODE_CHARS + 1 ] );

/** True with *ip set when text is a well-formed code whose check matches. */
Bool gameCodeToIP( const char *text, UnsignedInt *ip );

#endif // _GAME_CODE_H_
