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
	Mnemonic:	utils.h
	Abstract:	Implements general functions used by the RFT library

	Date:		12 November 2019
	Author:		Alexandre Huff
*/

#ifndef _RFT_UTILS_H
#define _RFT_UTILS_H

/*
	Macro to comprare two timespecs
*/
#define timespec_cmp(a, b, CMP)				\
	(((a).tv_sec == (b).tv_sec) ?			\
		((a).tv_nsec CMP (b).tv_nsec) :		\
		((a).tv_sec CMP (b).tv_sec) ? 1 : 0 )


/*
	Macro to add milliseconds in a timespec
*/
#define timespec_add_ms(a, ms)					\
	do {										\
		(a).tv_sec += ms / 1000;				\
		(a).tv_nsec += ( ms % 1000 ) * 1000000;	\
		if ( (a).tv_nsec >= 1000000000 ) {		\
			(a).tv_sec++;						\
			(a).tv_nsec -= 1000000000;			\
		}										\
	} while (0)

/*
	Returns the maximum value of two numbers
*/
# define MAX(val1, val2) ( ( val1 > val2 ) ? val1 : val2 )

/*
	Returns the minimum value of two numbers
*/
# define MIN(val1, val2) ( ( val1 < val2 ) ? val1 : val2 )


int randomize_election_timeout( );
int parse_int( char *str );
unsigned int parse_uint( char *str );
long parse_long( char *str );
unsigned long parse_ulong( char *str );


#endif