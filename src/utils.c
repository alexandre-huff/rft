// :vim ts=4 sw=4 noet:
/*
==================================================================================
	Copyright (c) 2019 AT&T Intellectual Property.

	Licensed under the Apache License, Version 2.0 (the "License");
	you may not use this file except in compliance with the License.
	You may obtain a copy of the License at

		http://www.apache.org/licenses/LICENSE-2.0

	Unless required by applicable law or agreed to in writing, software
	distributed under the License is distributed on an "AS IS" BASIS,
	WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
	See the License for the specific language governing permissions and
	limitations under the License.
==================================================================================
*/

/*
	Mnemonic:	utils.c
	Abstract:	Implements general functions used by the RFT library

	Date:		12 November 2019
	Author:		Alexandre Huff
*/

#include <stdlib.h>
#include <string.h>
#include <rmr/rmr.h>
#include <errno.h>
#include <limits.h>

#include "rft.h"
#include "logger.h"
#include "utils.h"


/*
	randomize election timeout, tipically values between 150 and 300 ms (min: ELECTION_TIMEOUT, max: ELECTION_TIMEOUT * 2)
*/
int randomize_election_timeout( ) {
	return ELECTION_TIMEOUT + rand() % (ELECTION_TIMEOUT + 1);
}

/*
	parses a string to an integer
*/
int parse_int( char *str ) {
	long value;
	char *endptr;

	endptr = str;
	errno = 0;
	value = strtol( str, &endptr, 10 );

	if( errno == EINVAL ) {
		logger_fatal( "unable to parse %s to integer", str );
		exit( 1 );
	}

	if( ( errno == ERANGE ) || ( ( value < INT_MIN ) || ( value > INT_MAX ) ) ) { // checking for long and int overflows
		logger_fatal( "%s is out of the integer range", str );
		exit( 1 );
	}

	if( *endptr != '\0' ) {
		logger_fatal( "invalid characters after the number: %s", str );
		exit( 1 );
	}

	return (int) value;
}

/*
	parses a string to an unsigned integer
*/
unsigned int parse_uint( char *str ) {
	unsigned long value;
	char *endptr;

	endptr = str;
	errno = 0;
	value = strtoul( str, &endptr, 10 );

	if( errno == EINVAL ) {
		logger_fatal( "unable to parse %s to unsigned integer", str );
		exit( 1 );
	}

	if( ( errno == ERANGE ) || ( value > UINT_MAX ) ) { // checking for long and int overflows
		logger_fatal( "%s is out of the unsigned integer range", str );
		exit( 1 );
	}

	if( *endptr != '\0' ) {
		logger_fatal( "invalid characters after the number: %s", str );
		exit( 1 );
	}

	return (unsigned int) value;
}

/*
	parses a string to a long
*/
long parse_long( char *str ) {
	long value;
	char *endptr;

	endptr = str;
	errno = 0;
	value = strtol( str, &endptr, 10 );

	if( errno == EINVAL ) {
		logger_fatal( "unable to parse %s to long", str );
		exit( 1 );
	}

	if( errno == ERANGE ) {
		logger_fatal( "%s is out of the long range", str );
		exit( 1 );
	}

	if( *endptr != '\0' ) {
		logger_fatal( "invalid characters after the number: %s", str );
		exit( 1 );
	}

	return value;
}

/*
	parses a string to an unsigned long
*/
unsigned long parse_ulong( char *str ) {
	unsigned long value;
	char *endptr;

	endptr = str;
	errno = 0;
	value = strtoul( str, &endptr, 10 );

	if( errno == EINVAL ) {
		logger_fatal( "unable to parse %s to unsigned long", str );
		exit( 1 );
	}

	if( errno == ERANGE ) {
		logger_fatal( "%s is out of the unsigned long range", str );
		exit( 1 );
	}

	if( *endptr != '\0' ) {
		logger_fatal( "invalid characters after the number: %s", str );
		exit( 1 );
	}

	return value;
}
