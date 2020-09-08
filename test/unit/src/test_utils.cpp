// :vim ts=4 sw=4 noet:
/*
==================================================================================
	Copyright (c) 2020 AT&T Intellectual Property.

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
	Mnemonic:	test_utils.cpp
	Abstract:	Tests the RFT utils module

	Date:		2 September 2020
	Author:		Alexandre Huff
*/

#include "CppUTest/TestHarness.h"

extern "C" {
	#include "utils.h"
	#include "stubs/stub_rft.h"
	#include "stubs/stub_logger.h"
	#include "stubs/stub_utils.h"
}

TEST_GROUP( TestUtils ) {
	void setup() {

	}

	void teardown() {

	}
};

TEST( TestUtils, TimespecCompare ) {
	struct timespec t1 = { .tv_sec = 10, .tv_nsec = 150 };
	struct timespec t2 = { .tv_sec = 10, .tv_nsec = 150 };

	// == <= >=
	LONGS_EQUAL( 1, timespec_cmp( t1, t2, == ) );
	LONGS_EQUAL( 0, timespec_cmp( t1, t2, > ) );
	LONGS_EQUAL( 0, timespec_cmp( t1, t2, < ) );
	LONGS_EQUAL( 1, timespec_cmp( t1, t2, >= ) );
	LONGS_EQUAL( 1, timespec_cmp( t1, t2, <= ) );

	t2.tv_nsec = 151; // t1 < and <= t2
	LONGS_EQUAL( 0, timespec_cmp( t1, t2, == ) );
	LONGS_EQUAL( 0, timespec_cmp( t1, t2, > ) );
	LONGS_EQUAL( 1, timespec_cmp( t1, t2, < ) );
	LONGS_EQUAL( 0, timespec_cmp( t1, t2, >= ) );
	LONGS_EQUAL( 1, timespec_cmp( t1, t2, <= ) );

	t2.tv_sec = 11; // t1 < and <= t2
	LONGS_EQUAL( 0, timespec_cmp( t1, t2, == ) );
	LONGS_EQUAL( 0, timespec_cmp( t1, t2, > ) );
	LONGS_EQUAL( 1, timespec_cmp( t1, t2, < ) );
	LONGS_EQUAL( 0, timespec_cmp( t1, t2, >= ) );
	LONGS_EQUAL( 1, timespec_cmp( t1, t2, <= ) );

	t2.tv_sec = 10; // t1 > and >= t2
	t1.tv_nsec = 300;
	LONGS_EQUAL( 0, timespec_cmp( t1, t2, == ) );
	LONGS_EQUAL( 1, timespec_cmp( t1, t2, > ) );
	LONGS_EQUAL( 0, timespec_cmp( t1, t2, < ) );
	LONGS_EQUAL( 1, timespec_cmp( t1, t2, >= ) );
	LONGS_EQUAL( 0, timespec_cmp( t1, t2, <= ) );

	t1.tv_sec = 12; // t1 > and >= t2
	LONGS_EQUAL( 0, timespec_cmp( t1, t2, == ) );
	LONGS_EQUAL( 1, timespec_cmp( t1, t2, > ) );
	LONGS_EQUAL( 0, timespec_cmp( t1, t2, < ) );
	LONGS_EQUAL( 1, timespec_cmp( t1, t2, >= ) );
	LONGS_EQUAL( 0, timespec_cmp( t1, t2, <= ) );

	// when secs < and nsec ==
	t1.tv_sec = 10; // t1 < and <= t2
	t2.tv_sec = 11;
	t1.tv_nsec = 150;
	t2.tv_nsec = 150;
	LONGS_EQUAL( 0, timespec_cmp( t1, t2, == ) );
	LONGS_EQUAL( 0, timespec_cmp( t1, t2, > ) );
	LONGS_EQUAL( 1, timespec_cmp( t1, t2, < ) );
	LONGS_EQUAL( 0, timespec_cmp( t1, t2, >= ) );
	LONGS_EQUAL( 1, timespec_cmp( t1, t2, <= ) );
}

TEST( TestUtils, TimespecAddMilliseconds ) {
	struct timespec t1 = { .tv_sec = 10, .tv_nsec = 0 };

	timespec_add_ms( t1, 300 );
	LONGS_EQUAL( 10, t1.tv_sec );
	LONGS_EQUAL( 300 * 1000000, t1.tv_nsec );

	t1.tv_nsec = 0;
	timespec_add_ms( t1, 2250 );
	LONGS_EQUAL( 12, t1.tv_sec );
	LONGS_EQUAL( 250 * 1000000, t1.tv_nsec );

	t1.tv_sec = 10;
	t1.tv_nsec = 999000000;
	timespec_add_ms( t1, 1 );
	LONGS_EQUAL( 11, t1.tv_sec );
	LONGS_EQUAL( 0, t1.tv_nsec );
}

TEST( TestUtils, RandomizeElectionTimeout ) {
	int t;
	t = randomize_election_timeout( );
	/*
		should return ELECTION_TIMEOUT with rand returning 0 in stub_utils.h
		t = ELECTION_TIMEOUT + rand() % (ELECTION_TIMEOUT + 1);
	*/
	CHECK_EQUAL( ELECTION_TIMEOUT, t );
}
