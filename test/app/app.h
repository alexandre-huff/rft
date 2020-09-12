// :vim ts=4 sw=4 noet:
/*
==================================================================================
	Copyright (c) 2020 AT&T Intellectual Property.
	Copyright (c) 2020 Alexandre Huff.

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
	Mnemonic:	app.h
	Abstract:	Header file used by RFT examples
	Date:		24 March 2020
	Author:		Alexandre Huff
*/

#ifndef _EXAMPLES_H
#define _EXAMPLES_H

#include <time.h>

#define RAN1_MSG	100
#define RAN2_MSG	101
#define RAN3_MSG	102

/*
	Macro to add nanoseconds in a timespec
*/
#define timespec_add_ns(a, ns)				\
	do {									\
		(a).tv_sec += ns / 1000000000;		\
		(a).tv_nsec += ( ns % 1000000000 );	\
		if ( (a).tv_nsec >= 1000000000 ) {	\
			(a).tv_sec++;					\
			(a).tv_nsec -= 1000000000;		\
		}									\
	} while (0)

/*
	Examples' message payload
*/
typedef struct mpl {
	long num_msgs;
	long seq;
	struct timespec out_ts;
} mpl_t;

#endif
