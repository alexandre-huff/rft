// :vim ts=4 sw=4 noet:
/*
==================================================================================
	Copyright (c) 2019-2020 AT&T Intellectual Property.

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
	Mnemonic:	utils_test.c
	Abstract:	Implements unit tests for the utils module of the RFT library.
				NOTE: not all functions have their respective unit coverage
				tests implemented so far.

	Date:		17 November 2019
	Author:		Alexandre Huff
*/

#include <stdio.h>
#include <time.h>
#include <assert.h>

#include <rmr/rmr.h>

#include "rft.h"
#include "utils.h"


void test_timespec_cmp( ) {

	struct timespec t1;
	struct timespec t2;

	t1.tv_sec = 10;
	t1.tv_nsec = 150;
	t2.tv_sec = 10;
	t2.tv_nsec = 150;

	// == <= >=
	assert( timespec_cmp( t1, t2, == ) == 1 ) ;
	assert( timespec_cmp( t1, t2, > ) == 0 ) ;
	assert( timespec_cmp( t1, t2, < ) == 0 ) ;
	assert( timespec_cmp( t1, t2, >= ) == 1 ) ;
	assert( timespec_cmp( t1, t2, <= ) == 1 ) ;

	t2.tv_nsec = 151; // t1 < and <= t2
	assert( timespec_cmp( t1, t2, == ) == 0 ) ;
	assert( timespec_cmp( t1, t2, > ) == 0 ) ;
	assert( timespec_cmp( t1, t2, < ) == 1 ) ;
	assert( timespec_cmp( t1, t2, >= ) == 0 ) ;
	assert( timespec_cmp( t1, t2, <= ) == 1 ) ;

	t2.tv_sec = 11; // t1 < and <= t2
	assert( timespec_cmp( t1, t2, == ) == 0 ) ;
	assert( timespec_cmp( t1, t2, > ) == 0 ) ;
	assert( timespec_cmp( t1, t2, < ) == 1 ) ;
	assert( timespec_cmp( t1, t2, >= ) == 0 ) ;
	assert( timespec_cmp( t1, t2, <= ) == 1 ) ;

	t2.tv_sec = 10; // t1 > and >= t2
	t1.tv_nsec = 300;
	assert( timespec_cmp( t1, t2, == ) == 0 ) ;
	assert( timespec_cmp( t1, t2, > ) == 1 ) ;
	assert( timespec_cmp( t1, t2, < ) == 0 ) ;
	assert( timespec_cmp( t1, t2, >= ) == 1 ) ;
	assert( timespec_cmp( t1, t2, <= ) == 0 ) ;

	t1.tv_sec = 12; // t1 > and >= t2
	assert( timespec_cmp( t1, t2, == ) == 0 ) ;
	assert( timespec_cmp( t1, t2, > ) == 1 ) ;
	assert( timespec_cmp( t1, t2, < ) == 0 ) ;
	assert( timespec_cmp( t1, t2, >= ) == 1 ) ;
	assert( timespec_cmp( t1, t2, <= ) == 0 ) ;

	// when secs < and nsec ==
	t1.tv_sec = 10; // t1 < and <= t2
	t2.tv_sec = 11;
	t1.tv_nsec = 150;
	t2.tv_nsec = 150;
	assert( timespec_cmp( t1, t2, == ) == 0 ) ;
	assert( timespec_cmp( t1, t2, > ) == 0 ) ;
	assert( timespec_cmp( t1, t2, < ) == 1 ) ;
	assert( timespec_cmp( t1, t2, >= ) == 0 ) ;
	assert( timespec_cmp( t1, t2, <= ) == 1 ) ;

}

void test_timespec_add_ms( ) {
	struct timespec t1;

	t1.tv_sec = 10;
	t1.tv_nsec = 0;

	timespec_add_ms( t1, 300 );
	assert( t1.tv_sec == 10 );
	assert( t1.tv_nsec == 300 * 1000000 );

	t1.tv_nsec = 0;
	timespec_add_ms( t1, 2250 );
	assert( t1.tv_sec == 12 );
	assert( t1.tv_nsec == 250 * 1000000 );

	t1.tv_sec = 10;
	t1.tv_nsec = 999000000;
	timespec_add_ms( t1, 1 );
	assert( t1.tv_sec == 11 );
	assert( t1.tv_nsec == 0 );

}

void test_randomize_election_timeout( ) {
	int i, t;
	for ( i = 0; i < 1000000; i++ ) {
		t = randomize_election_timeout( );
		assert( t >= ELECTION_TIMEOUT && t <= ELECTION_TIMEOUT * 2 );
	}

}

int main(int argc, char const *argv[]) {

	test_timespec_cmp( );
	test_timespec_add_ms( );

	test_randomize_election_timeout( );

	printf("Tests OK!\n");

	return 0;
}
