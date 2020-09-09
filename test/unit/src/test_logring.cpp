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
	Mnemonic:	test_logring.cpp
	Abstract:	Tests the RFT ring module used to store log entries

	Date:		8 September 2020
	Author:		Alexandre Huff
*/

#include "CppUTest/TestHarness.h"
#include "CppUTest/TestHarness_c.h"

extern "C" {
	#include <string.h>
	#include "types.h"
	#include "../../src/static/logring.c"
}

// generic function to create log entry for testing
log_entry_t *create_log_entry( index_t index ) {
	log_entry_t *entry;
	entry = (log_entry_t *) malloc( sizeof(log_entry_t) );
	assert( entry != NULL );
	entry->index = index;
	entry->data = NULL;
	return entry;
}

TEST_GROUP( NoLogRing ) {
	logring_t *ring = NULL;

	void setup() {

	}

	void teardown() {
		if( ring )
			log_ring_free( ring );
		ring = NULL;
	}

};

TEST( NoLogRing, LogRingCreateInvalidArg ) {
	// creating a ring without size
	ring = log_ring_create( 0 );
	LONGS_EQUAL( EINVAL, errno );
	CHECK_FALSE( ring );

	// creating a ring that is not power of 2
	ring = log_ring_create( 50 );
	LONGS_EQUAL( EINVAL, errno );
	CHECK_FALSE( ring );

	// creating a ring with size < 16
	ring = log_ring_create( 10 );
	CHECK_TRUE( ring );
	CHECK_TRUE( ring->data );
	UNSIGNED_LONGS_EQUAL( 16, ring->size );
	UNSIGNED_LONGS_EQUAL( 15, ring->mask );
	UNSIGNED_LONGS_EQUAL( 0, ring->head );
	UNSIGNED_LONGS_EQUAL( 0, ring->tail );
}

TEST( NoLogRing, LogRingCreateValidArg ) {
	cpputest_malloc_set_out_of_memory();
	ring = log_ring_create( 64 );
	CHECK_FALSE( ring );
	cpputest_malloc_set_not_out_of_memory();

	// creating a ring with size >= 16 and size power of two
	ring = log_ring_create( 64 );
	CHECK_TRUE( ring );
	CHECK_TRUE( ring->data );
	UNSIGNED_LONGS_EQUAL( 64, ring->size );
	UNSIGNED_LONGS_EQUAL( 63, ring->mask );
	UNSIGNED_LONGS_EQUAL( 0, ring->head );
	UNSIGNED_LONGS_EQUAL( 0, ring->tail );
}

TEST_GROUP( EmptyLogRing ) {
	static const u_int32_t size = 16;
	logring_t *ring = NULL;
	log_entry_t *entry;

	void setup() {
		ring = log_ring_create( size );
		CHECK_TRUE( ring );
	}

	void teardown() {
		if( ring ) {
			while( ( entry = log_ring_extract( ring ) ) != NULL )
				free( entry );
			log_ring_free( ring );
			ring = NULL;
		}
	}
};

TEST( EmptyLogRing, LogRingInsert ) {
	int ret;

	// Simple tests here, other tests are in the FullLogRing TEST_GROUP

	entry = create_log_entry( 1 );
	ret = log_ring_insert( ring, entry );
	CHECK_TRUE( ret );
	UNSIGNED_LONGS_EQUAL( 1, ring->head );
	UNSIGNED_LONGS_EQUAL( 0, ring->tail );

	entry = create_log_entry( 2 );
	ret = log_ring_insert( ring, entry );
	CHECK_TRUE( ret );
	UNSIGNED_LONGS_EQUAL( 2, ring->head );
	UNSIGNED_LONGS_EQUAL( 0, ring->tail );
}

TEST( EmptyLogRing, LogRingCount ) {
	// head == tail
	UNSIGNED_LONGS_EQUAL( 0, log_ring_count( ring ) );

	// head > tail
	ring->head = 10;
	ring->tail = 5;
	UNSIGNED_LONGS_EQUAL( (ring->head - ring->tail), log_ring_count( ring ) );

	// ring full but head < tail
	ring->head = 9;
	ring->tail = 10;
	UNSIGNED_LONGS_EQUAL( size-1, log_ring_count( ring ) ); // ring full is size-1
}

TEST( EmptyLogRing, LogRingStats ) {
	DOUBLES_EQUAL( 0.0, log_ring_stats( ring ), 0.000001 );
}

TEST( EmptyLogRing, LogRingGet ) {
	entry = log_ring_get( ring, 0 );	// invalid log entry index (starts from 1)
	CHECK_FALSE( entry );

	// setting head and tail and try getting entries out of bounds
	ring->head = 10;
	ring->tail = 5;

	// upper bound
	entry = log_ring_get( ring, 11 );
	CHECK_FALSE( entry );

	// lower  bound
	entry = log_ring_get( ring, 4 );
	CHECK_FALSE( entry );

	// setting back to avoid breaking teardown
	ring->head = 0;
	ring->tail = 0;
}

TEST( EmptyLogRing, LogRingExtract ) {
	CHECK_FALSE( log_ring_extract( ring) );
}

TEST( EmptyLogRing, LogRingExtractR ) {
	CHECK_FALSE( log_ring_extract_r( ring) );
}

TEST_GROUP( LogRingWithEntries ) {
	static const int num_entries = 3;
	logring_t *ring = NULL;
	log_entry_t *entry;

	void setup() {
		int ret;
		ring = log_ring_create( 16 );
		CHECK_TRUE( ring );
		for( int i = 0; i < num_entries; i++ ) {	// inserting 3 entries
			entry = create_log_entry( i );
			ret = log_ring_insert( ring, entry );
			CHECK_TRUE( ret );
		}
	}

	void teardown() {
		if( ring ) {
			while( ( entry = log_ring_extract( ring ) ) != NULL )
				free( entry );
			log_ring_free( ring );
			ring = NULL;
		}
	}
};

TEST( LogRingWithEntries, LogRingExtract ) {
	// removing all entries
	for( int i = 0; i < num_entries; i++ ) {
		entry = log_ring_extract( ring );
		CHECK_TRUE( entry );
		UNSIGNED_LONGS_EQUAL( i+1, ring->tail );
		free( entry );
	}

	// trying to remove from an empty ring
	entry = log_ring_extract( ring );
	CHECK_FALSE( entry );
	UNSIGNED_LONGS_EQUAL( num_entries, ring->tail );
}

TEST( LogRingWithEntries, LogRingExtractR ) {
	// removing all entries
	for( int i = num_entries; i > 0; i-- ) {
		entry = log_ring_extract_r( ring );
		CHECK_TRUE( entry );
		UNSIGNED_LONGS_EQUAL( i-1, ring->head );
		free( entry );
	}

	// trying to remove from an empty ring
	entry = log_ring_extract_r( ring );
	CHECK_FALSE( entry );
	UNSIGNED_LONGS_EQUAL( 0, ring->head );
}

TEST( LogRingWithEntries, LogRingCount ) {
	UNSIGNED_LONGS_EQUAL( num_entries, log_ring_count( ring) );
}

TEST( LogRingWithEntries, LogRingStats ) {
	DOUBLES_EQUAL( 0.2, log_ring_stats( ring), 0.000001 );
}

TEST_GROUP( FullLogRing ) {
	static const int size = 16;
	logring_t *ring = NULL;
	log_entry_t *entry;

	void setup() {
		int ret;
		ring = log_ring_create( size );
		CHECK_TRUE( ring );
		for( int i = 0; i < size-1; i++ ) {	// the ring stores up to size-1 (mask) elements
			entry = create_log_entry( i );
			ret = log_ring_insert( ring, entry );
			CHECK_TRUE( ret );
		}
	}

	void teardown() {
		if( ring ) {
			while( ( entry = log_ring_extract( ring ) ) != NULL )
				free( entry );
			log_ring_free( ring );
			ring = NULL;
		}
	}
};

TEST( FullLogRing, LogRingInsertOverflow ) {
	int ret;

	// testing head and tail values
	UNSIGNED_LONGS_EQUAL( size-1, ring->head );
	UNSIGNED_LONGS_EQUAL( 0, ring->tail );

	// testing when ring is full
	entry = create_log_entry( size );	// any value would fit here (size is the next index)
	ret = log_ring_insert( ring, entry );
	CHECK_FALSE( ret );
	free( entry );

	// removing one log entry from the tail
	entry = log_ring_extract( ring );
	CHECK_TRUE( entry );
	free( entry );

	// testing if the head jumps to the beginning of the ring
	entry = create_log_entry( size );	// any value would fit here (size is the next index)
	ret = log_ring_insert( ring, entry );
	CHECK_TRUE( ret );
	UNSIGNED_LONGS_EQUAL( 0, ring->head );	// should be 0
	UNSIGNED_LONGS_EQUAL( 1, ring->tail );	// should be 1
}

TEST( FullLogRing, LogRingExtractOverflow ) {
	int ret;

	// moving head to the end
	while( ( entry = log_ring_extract( ring) ) != NULL )
		free( entry );
	UNSIGNED_LONGS_EQUAL( size-1, ring->head );
	UNSIGNED_LONGS_EQUAL( size-1, ring->tail );

	entry = create_log_entry( size ); // size is just the next index
	ret = log_ring_insert( ring, entry );
	CHECK_TRUE( ret );
	UNSIGNED_LONGS_EQUAL( 0, ring->head );
	UNSIGNED_LONGS_EQUAL( size-1, ring->tail );

	entry = log_ring_extract( ring);
	CHECK_TRUE( entry );
	UNSIGNED_LONGS_EQUAL( 0, ring->head );
	UNSIGNED_LONGS_EQUAL( 0, ring->tail );
	free( entry );
}

TEST( FullLogRing, LogRingExtractReverseOverflow ) {
	int ret;
	static const int nempty = 3;

	// removing a few entries from the tail to allow the head overflow to the beginning
	for( int i = 0; i < nempty; i ++ ) {
		entry = log_ring_extract( ring);
		free( entry );
	}
	UNSIGNED_LONGS_EQUAL( size-1, ring->head );
	UNSIGNED_LONGS_EQUAL( nempty, ring->tail );

	// inserting a new entry in the beginning of the ring
	entry = create_log_entry( size ); // size is just the next index
	ret = log_ring_insert( ring, entry );
	CHECK_TRUE( ret );
	UNSIGNED_LONGS_EQUAL( 0, ring->head );
	UNSIGNED_LONGS_EQUAL( nempty, ring->tail );

	// removing the last inserted (reverse) - head should get back to the end of the ring
	entry = log_ring_extract_r( ring );
	CHECK_TRUE( entry );
	UNSIGNED_LONGS_EQUAL( size-1, ring->head );
	UNSIGNED_LONGS_EQUAL( nempty, ring->tail );
	free( entry );
}
