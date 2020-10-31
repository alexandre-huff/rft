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
	Mnemonic:	test_log.cpp
	Abstract:	Tests the RFT log module

	Date:		2 September 2020
	Author:		Alexandre Huff
*/

#include "CppUTest/TestHarness.h"
#include "CppUTest/TestHarness_c.h"
#include "CppUTestExt/MockSupport.h"
// #include "CppUTest/MemoryLeakWarningPlugin.h"


extern "C" {
	#include <string.h>
	#include "types.h"
	#include "log.h"
	#include "mtl.h"
	#include "../../src/static/logring.c"
	#include "stubs/stub_logger.h"
	#include "stubs/stub_snapshot.h"
	#include "stubs/stub_rft.h"
}

TEST_GROUP( TestInitLog ) {
	log_entries_t *raft_log = get_raft_log( );
	log_entries_t *server_log = get_server_log( );
	int log_size = 32;

	void setup() {

	}

	void teardown() {
		if( raft_log->entries ) {
			log_ring_free( raft_log->entries );
			raft_log->entries = NULL;
		}

		if( server_log->entries ) {
			log_ring_free( server_log->entries );
			server_log->entries = NULL;
		}
	}
};

TEST( TestInitLog, InitRaftLog ) {
	int ret;
	int ring_mask = log_size - 1;

	// invalid size
	ret = init_log( RAFT_LOG, 0, 10 );
	LONGS_EQUAL( EINVAL, errno );
	CHECK_FALSE( ret );

	// invalid memory threshold size
	ret = init_log( RAFT_LOG, log_size, 0 );
	LONGS_EQUAL( EINVAL, errno );
	CHECK_FALSE( ret );

	// valid arguments
	ret = init_log( RAFT_LOG, log_size, 10 );
	CHECK_TRUE( ret );
	UNSIGNED_LONGS_EQUAL( 0, raft_log->index_offset );
	UNSIGNED_LONGS_EQUAL( 0, raft_log->last_log_index );
	UNSIGNED_LONGS_EQUAL( 0, raft_log->memsize );
	UNSIGNED_LONGS_EQUAL( 10 * 1048576, raft_log->mthresh );	// convert Mbytes to bytes
	UNSIGNED_LONGS_EQUAL( (index_t)( ring_mask * LOG_COUNT_RATIO ), raft_log->cthresh );
}

TEST( TestInitLog, InitServerLog ) {
	int ret;
	int ring_mask = log_size - 1;

	// invalid size
	ret = init_log( SERVER_LOG, 0, 10 );
	LONGS_EQUAL( EINVAL, errno );
	CHECK_FALSE( ret );

	// invalid memory threshold size
	ret = init_log( SERVER_LOG, log_size, 0 );
	LONGS_EQUAL( EINVAL, errno );
	CHECK_FALSE( ret );

	// valid arguments
	ret = init_log( SERVER_LOG, log_size, 10 );
	CHECK_TRUE( ret );
	UNSIGNED_LONGS_EQUAL( 0, server_log->index_offset );
	UNSIGNED_LONGS_EQUAL( 0, server_log->last_log_index );
	UNSIGNED_LONGS_EQUAL( 0, server_log->memsize );
	UNSIGNED_LONGS_EQUAL( 10 * 1048576, server_log->mthresh );	// convert Mbytes to bytes
	UNSIGNED_LONGS_EQUAL( (index_t)( ring_mask * LOG_COUNT_RATIO ), server_log->cthresh );
}

TEST_GROUP( TestLog ) {
	log_entry_t *entry;     // generic pointer of a log entry
	const char *server = "server1";
	const char *target = "127.0.0.1:4560";
	server_conf_cmd_data_t config_data;	// RAFT_CONFIG data
	log_entries_t *raft_log = get_raft_log( );
	log_entries_t *server_log = get_server_log( );
	int size = 64;

	void setup() {
		entry = NULL;

		// initilizing raft log entries for using on almost all tests
		snprintf( config_data.server_id, sizeof(server_id_t), "%s", server );
		snprintf( config_data.target, sizeof(server_id_t), "%s", target );

		init_log( RAFT_LOG, size, 10 );
		init_log( SERVER_LOG, size, 10 );
	}

	void teardown() {
		mock().clear();

		// removing all raft log entries
		if( raft_log->last_log_index > 0 ) {
			mock().disable();	// disabling mock checks (but it still returns the default mock value)
			remove_raft_conflicting_entries( 0, 0, 0 );
			mock().enable();
		}

		if( raft_log->entries ) {
			free( raft_log->entries->data );
			free( raft_log->entries );
			raft_log->entries = NULL;
		}

		if( server_log->entries ) {
			// removing all xapp log entries
			while( ( entry = log_ring_extract( server_log->entries ) ) != NULL )
				free_log_entry( entry );

			free( server_log->entries->data );
			free( server_log->entries );
			server_log->entries = NULL;
		}
	}
};

/*
	Tests if all log entries are removed from the logring and
	also tests if the corresponding log structure is setup accordingly
	Works for both Raft and xApp logrings
*/
TEST( TestLog, FreeAllLogEntries ) {
	raft_log->memsize = 10;
	raft_log->index_offset = 2;
	raft_log->last_log_index = 5;
	entry = new_raft_log_entry( 1, RAFT_CONFIG, ADD_MEMBER, &config_data, sizeof(server_conf_cmd_data_t) );
	log_ring_insert( raft_log->entries, entry );

	free_all_log_entries( raft_log, 8 );

	UNSIGNED_LONGS_EQUAL( 0, log_ring_count( raft_log->entries ) );
	UNSIGNED_LONGS_EQUAL( 0, raft_log->memsize );
	UNSIGNED_LONGS_EQUAL( 8, raft_log->index_offset );
	UNSIGNED_LONGS_EQUAL( 8, raft_log->last_log_index );

}

TEST( TestLog, NewRaftConfigLogEntry ) {
	server_conf_cmd_data_t entry_data;	// RAFT_CONFIG data from entry

	cpputest_malloc_set_out_of_memory();
	entry = new_raft_log_entry( 1, RAFT_CONFIG, ADD_MEMBER, &config_data, sizeof(server_conf_cmd_data_t) );
	CHECK( entry == NULL );
	cpputest_malloc_set_not_out_of_memory();

	entry = new_raft_log_entry( 1, RAFT_CONFIG, ADD_MEMBER, &config_data, sizeof(server_conf_cmd_data_t) );
	CHECK( entry != NULL );

	UNSIGNED_LONGS_EQUAL( 1, entry->term );
	LONGS_EQUAL( RAFT_CONFIG, entry->type );
	LONGS_EQUAL( ADD_MEMBER, entry->command );
	UNSIGNED_LONGS_EQUAL( sizeof(server_conf_cmd_data_t), entry->dlen );

	memcpy( &entry_data, entry->data, entry->dlen );
	STRCMP_EQUAL( server, entry_data.server_id );
	STRCMP_EQUAL( target, entry_data.target );

	POINTERS_EQUAL( NULL, entry->context );
	POINTERS_EQUAL( NULL, entry->key );

	free_log_entry( entry );
}

TEST( TestLog, NewRaftCommandLogEntry ) {
	int command = 0;
	int cmd_data = 1;
	int entry_data;

	cpputest_malloc_set_out_of_memory();
	entry = new_raft_log_entry( 1, RAFT_COMMAND, command, &cmd_data, sizeof(cmd_data) );
	CHECK( entry == NULL );
	cpputest_malloc_set_not_out_of_memory();

	entry = new_raft_log_entry( 1, RAFT_COMMAND, command, &cmd_data, sizeof(cmd_data) );
	CHECK( entry != NULL );

	UNSIGNED_LONGS_EQUAL( 1, entry->term );
	LONGS_EQUAL( RAFT_COMMAND, entry->type );
	LONGS_EQUAL( command, entry->command );
	UNSIGNED_LONGS_EQUAL( sizeof(cmd_data), entry->dlen );

	memcpy( &entry_data, entry->data, entry->dlen );
	LONGS_EQUAL( cmd_data, entry_data );

	POINTERS_EQUAL( NULL, entry->context );
	POINTERS_EQUAL( NULL, entry->key );

	free_log_entry( entry );
}

TEST( TestLog, NewServerLogEntry ) {
	int command = 1;
	int cmd_data = 50;
	int entry_data;
	char context[16];
	char key[16];

	snprintf( context, sizeof(context), "%s", "mycontext" );
	snprintf( key, sizeof(key), "%s", "mykey" );

	cpputest_malloc_set_out_of_memory();
	entry = new_server_log_entry( context, key, command, &cmd_data, sizeof(cmd_data) );
	CHECK( entry == NULL );
	cpputest_malloc_set_not_out_of_memory();

	entry = new_server_log_entry( context, key, command, &cmd_data, sizeof(cmd_data) );
	CHECK( entry != NULL );

	// LONGS_EQUAL( 0, entry->term );   // not used by server log entries
	LONGS_EQUAL( SERVER_COMMAND, entry->type );
	LONGS_EQUAL( command, entry->command );
	UNSIGNED_LONGS_EQUAL( sizeof(cmd_data), entry->dlen );

	memcpy( &entry_data, entry->data, entry->dlen );
	LONGS_EQUAL( cmd_data, entry_data );

	STRCMP_EQUAL( context, entry->context );
	STRCMP_EQUAL( key, entry->key );

	free_log_entry( entry );
}

TEST( TestLog, NewServerLogEntry_NoKey_NoContext ) {
	int command = 1;
	int cmd_data = 50;
	char context[8];
	char key[8];

	memset(context, 0, 8 );
	memset(key, 0, 8 );

	// testing with NULL context and NULL key
	entry = new_server_log_entry( NULL, NULL, command, &cmd_data, sizeof(cmd_data) );
	CHECK( entry == NULL );

	// testing with empty context and key
	entry = new_server_log_entry( context, key, command, &cmd_data, sizeof(cmd_data) );
	CHECK( entry == NULL );

	snprintf( context, sizeof(context), "ctx" );	// setting a context

	// testing with NULL key
	entry = new_server_log_entry( context, NULL, command, &cmd_data, sizeof(cmd_data) );
	CHECK( entry == NULL );

	// testing with empty key
	entry = new_server_log_entry( context, key, command, &cmd_data, sizeof(cmd_data) );
	CHECK( entry == NULL );
}

TEST( TestLog, GetRaftLastLogIndex_EmptyLog ) {
	UNSIGNED_LONGS_EQUAL( 0, get_raft_last_log_index( ) );
}

TEST( TestLog, GetRaftLogEntry_EmptyLog ) {
	entry = get_raft_log_entry( 0 );
	CHECK( entry == NULL );
}

TEST( TestLog, GetRaftLastLogTerm_EmptyLog ) {
	mock()
		.expectOneCall( "get_raft_current_term" )	// should return the current term of the raft state
		.andReturnValue( 0 );

	UNSIGNED_LONGS_EQUAL( 0, get_raft_last_log_term( ) );
}

TEST( TestLog, GetServerLastLogIndex_EmptyLog ) {
	UNSIGNED_LONGS_EQUAL( 0, get_server_last_log_index( ) );
}

TEST( TestLog, GetServerLogEntry_EmptyLog ) {
	entry = get_server_log_entry( 0 );
	CHECK( entry == NULL );
}

TEST( TestLog, AppendRaftLogEntry ) {
	mock()
		.expectOneCall( "raft_config_get_server" )
		.withParameter( "server_id", config_data.server_id )
		.andReturnValue( (void *)NULL );
	mock()
		.expectOneCall( "raft_config_add_server" )
		.withParameter( "server_id", config_data.server_id )
		.withParameter( "target", config_data.target )
		.withParameter( "last_log_index", 0 )
		.andReturnValue( 1 );
	mock()
		.expectOneCall( "raft_config_set_server_status" )
		.withParameter( "server_id", config_data.server_id )
		.withParameter( "status", VOTING_MEMBER )
		.andReturnValue( 1 );

	entry = new_raft_log_entry( 1, RAFT_CONFIG, ADD_MEMBER, &config_data, sizeof(server_conf_cmd_data_t) );
	CHECK( entry != NULL );
	append_raft_log_entry( entry );

	mock().checkExpectations();

	UNSIGNED_LONGS_EQUAL( 1, raft_log->last_log_index );	// we added the first log entry
	UNSIGNED_LONGS_EQUAL( sizeof(log_entry_t) + entry->dlen, raft_log->memsize );
}

TEST( TestLog, AppendRaftLogEntry_TakeSnapshotLogMemory ) {
	raft_log->memsize = 1025;
	raft_log->mthresh = 1024;

	mock()
		.expectOneCall( "take_raft_snapshot" );

	mock()
		.expectOneCall( "raft_config_get_server" )
		.withParameter( "server_id", config_data.server_id )
		.andReturnValue( (void *)NULL );
	mock()
		.expectOneCall( "raft_config_add_server" )
		.withParameter( "server_id", config_data.server_id )
		.withParameter( "target", config_data.target )
		.withParameter( "last_log_index", 0 )
		.andReturnValue( 1 );
	mock()
		.expectOneCall( "raft_config_set_server_status" )
		.withParameter( "server_id", config_data.server_id )
		.withParameter( "status", VOTING_MEMBER )
		.andReturnValue( 1 );

	entry = new_raft_log_entry( 1, RAFT_CONFIG, ADD_MEMBER, &config_data, sizeof(server_conf_cmd_data_t) );
	CHECK( entry != NULL );
	append_raft_log_entry( entry );

	mock().checkExpectations();

	UNSIGNED_LONGS_EQUAL( 1, raft_log->last_log_index );	// we added the first log entry
}

TEST( TestLog, AppendRaftLogEntry_TakeSnapshotLogCount ) {
	raft_log->cthresh = 1;

	mock()
		.expectOneCall( "take_raft_snapshot" );

	mock()
		.expectOneCall( "raft_config_get_server" )
		.withParameter( "server_id", config_data.server_id )
		.andReturnValue( (void *)NULL );
	mock()
		.expectOneCall( "raft_config_add_server" )
		.withParameter( "server_id", config_data.server_id )
		.withParameter( "target", config_data.target )
		.withParameter( "last_log_index", 0 )
		.andReturnValue( 1 );
	mock()
		.expectOneCall( "raft_config_set_server_status" )
		.withParameter( "server_id", config_data.server_id )
		.withParameter( "status", VOTING_MEMBER )
		.andReturnValue( 1 );

	entry = new_raft_log_entry( 1, RAFT_CONFIG, ADD_MEMBER, &config_data, sizeof(server_conf_cmd_data_t) );
	CHECK( entry != NULL );

	log_ring_insert( raft_log->entries, entry );	// added first
	log_ring_insert( raft_log->entries, entry );	// added second (is the same, no problem)

	append_raft_log_entry( entry );	// now testing the count threshold to take a snapshot (is the same, no problem)

	mock().checkExpectations();

	UNSIGNED_LONGS_EQUAL( 1, raft_log->last_log_index );	// we added the first log entry
}

TEST( TestLog, AppendServerLogEntry ) {
	int command = 1;
	int cmd_data = 50;

	entry = new_server_log_entry( "myctx", "mykey", command, &cmd_data, sizeof(cmd_data) );
	CHECK( entry != NULL );

	mock()
		.expectNoCall( "take_xapp_snapshot" );

	append_server_log_entry( entry, NULL, NULL );

	mock().checkExpectations();

	UNSIGNED_LONGS_EQUAL( 1, server_log->last_log_index );	// we added the first log entry
	UNSIGNED_LONGS_EQUAL( sizeof(log_entry_t) + entry->dlen + entry->clen + entry->klen, server_log->memsize );
}

TEST( TestLog, AppendServerLogEntry_TakeSnapshotLogMemory ) {
	int command = 1;
	int cmd_data = 50;
	hashtable_t htable;

	entry = new_server_log_entry( "myctx", "mykey", command, &cmd_data, sizeof(cmd_data) );
	CHECK( entry != NULL );

	server_log->memsize = 1025;
	server_log->mthresh = 1024;

	mock()
		.expectOneCall( "take_xapp_snapshot" )
		.withPointerParameter( "ctxtable", &htable )
		.withPointerParameter( "take_snapshot_cb", (take_snapshot_cb_t *) take_snapshot );

	append_server_log_entry( entry, &htable, take_snapshot );

	mock().checkExpectations();

	UNSIGNED_LONGS_EQUAL( 1, server_log->last_log_index );	// we added the first log entry
}

TEST( TestLog, AppendServerLogEntry_TakeSnapshotLogCount ) {
	int command = 1;
	int cmd_data = 50;
	hashtable_t htable;

	server_log->cthresh = 1;

	mock()
		.expectOneCall( "take_xapp_snapshot" )
		.withPointerParameter( "ctxtable", &htable )
		.withPointerParameter( "take_snapshot_cb", (take_snapshot_cb_t *) take_snapshot );

	for( int i = 0; i < 3; i++ ) {
		entry = new_server_log_entry( "myctx", "mykey", command, &cmd_data, sizeof(cmd_data) );
		CHECK( entry != NULL );

		append_server_log_entry( entry, &htable, take_snapshot );
	}

	mock().checkExpectations();

	UNSIGNED_LONGS_EQUAL( 3, server_log->last_log_index );	// we added the three log entries
}

/* ===================================== Next group of tests ===================================== */
/*
	This group of tests will test all RAFT log functions that require entries appended to the corresponding log
*/
TEST_GROUP( TestRaftLog ) {
	raft_state_t me;
	log_entry_t *entry;	// generic log entry pointer
	static const unsigned int setup_entries = 2;
	server_conf_cmd_data_t config_data[setup_entries];	// RAFT_CONFIG data of servers
	log_entries_t *raft_log = get_raft_log( );

	void setup() {
		me.commit_index = 0;

		init_log( RAFT_LOG, 64, 10 );

		mock().disable();	// disabling mock checks (but it still returns the default mock value)

		for( int i = 0; i < setup_entries; i++ ) {
			// initilizing raft log entries for using on almost all tests
			snprintf( config_data[i].server_id, sizeof(server_id_t), "server%d", i + 1 );
			snprintf( config_data[i].target, sizeof(server_id_t), "127.0.0.%d:4560", i + 1 );

			entry = new_raft_log_entry( 1, RAFT_CONFIG, ADD_MEMBER, &config_data[i], sizeof(server_conf_cmd_data_t) );
			CHECK( entry != NULL );
			append_raft_log_entry( entry );
		}

		mock().enable();
	}

	void teardown() {
		mock().clear();

		if( raft_log->last_log_index > 0 ) {
			mock().disable();	// disabling mock checks (but it still returns the default mock value)
			remove_raft_conflicting_entries( 0, 0, 0 );
			mock().enable();
		}

		if( raft_log->entries ) {
			free( raft_log->entries->data );
			free( raft_log->entries );
			raft_log->entries = NULL;
		}
	}
};

TEST( TestRaftLog, RemoveAllRaftConflictingEntries ) {
	mock()
		.expectOneCall( "raft_config_remove_server" )
		.withParameter( "server_id", config_data[1].server_id );

	mock()
		.expectOneCall( "raft_config_remove_server" )
		.withParameter( "server_id", config_data[0].server_id );

	// removing all log entries
	remove_raft_conflicting_entries( 0, 0, 0 );

	mock().checkExpectations();

	CHECK_EQUAL( 0, raft_log->last_log_index );
	UNSIGNED_LONGS_EQUAL( 0, raft_log->memsize );
}

TEST( TestRaftLog, RemoveTheLastRaftConflictingEntry ) {
	mock()
		.expectOneCall( "raft_config_remove_server" )
		.withParameter( "server_id", config_data[1].server_id );

	log_entry_t *e = log_ring_get( raft_log->entries, 2 );
	CHECK_TRUE( e );
	size_t msize = raft_log->memsize - sizeof(log_entry_t) - e->dlen;

	// removing only the second log entry (the previous has index 1 and term 1)
	remove_raft_conflicting_entries( 1, 1, 0 );

	mock().checkExpectations();

	UNSIGNED_LONGS_EQUAL( 1, raft_log->last_log_index );
	UNSIGNED_LONGS_EQUAL( msize, raft_log->memsize );
}

/*
	Trying to remove a committed log index (from_index < commit_index)
*/
TEST( TestRaftLog, RemoveRaftConflictingEntries_IndexLowerThanCommitIndex ) {

	me.commit_index = 2;
	remove_raft_conflicting_entries( setup_entries - 1, 1, me.commit_index );	// lower than

	UNSIGNED_LONGS_EQUAL( setup_entries, raft_log->last_log_index );
}

/*
	Trying to remove a committed log index (from_index == commit_index)
*/
TEST( TestRaftLog, RemoveRaftConflictingEntries_IndexEqualsCommitIndex ) {

	me.commit_index = 2;
	remove_raft_conflicting_entries( setup_entries, 1, me.commit_index );	// equal

	UNSIGNED_LONGS_EQUAL( setup_entries, raft_log->last_log_index );
}

TEST( TestRaftLog, RemoveRaftConflictingEntries_IndexGreaterThanHead ) {
	mock()
		.expectOneCall( "raft_config_remove_server" )
		.withParameter( "server_id", config_data[1].server_id );
	mock()
		.expectOneCall( "raft_config_remove_server" )
		.withParameter( "server_id", config_data[0].server_id );

	remove_raft_conflicting_entries( 100, 1, 0 );

	mock().checkExpectations();

	UNSIGNED_LONGS_EQUAL( 0, raft_log->last_log_index );
}

/*
	Removes a conflicting entry with the matching index but an conflicting term
	Should stop at the committed_index
*/
TEST( TestRaftLog, RemoveRaftConflictingEntries_IndexButTerm ) {
	mock()
		.expectOneCall( "raft_config_remove_server" )
		.withParameter( "server_id", config_data[1].server_id );
	/*
		stored logs have term 1
		we assume the commit index is 1
	*/
	remove_raft_conflicting_entries( 2, 2, 1 );

	mock().checkExpectations();

	UNSIGNED_LONGS_EQUAL( 1, raft_log->last_log_index );
}

/*
	Removing a DEL_MEMBER conflicting log entry actually adds it back to
	the raft configuration
*/
TEST( TestRaftLog, RemoveRaftConflictingEntries_DeleteMember ) {
	log_entry_t *del_entry;
	server_conf_cmd_data_t del_server;
	size_t msize;

	snprintf( del_server.server_id, sizeof(server_id_t), "server3" );
	snprintf( del_server.target, sizeof(server_id_t), "127.0.0.3:4560" );

	del_entry = new_raft_log_entry( 1, RAFT_CONFIG, DEL_MEMBER, &del_server, sizeof(server_conf_cmd_data_t) );
	CHECK( del_entry != NULL );
	mock().disable();
	append_raft_log_entry( del_entry );
	mock().enable();

	UNSIGNED_LONGS_EQUAL( 3, raft_log->last_log_index );

	mock()
		.expectOneCall( "raft_config_add_server" )
		.withParameter( "server_id", del_server.server_id )
		.withParameter( "target", del_server.target )
		.withParameter( "last_log_index", 0 )
		.andReturnValue( 1 );

	mock()
		.expectOneCall( "raft_config_set_server_status" )
		.withParameter( "server_id", del_server.server_id )
		.withParameter( "status", VOTING_MEMBER )
		.andReturnValue( 1 );

	// calculating the expected size of the log after removing the conflicting entry
	msize = raft_log->memsize;
	msize -= sizeof(log_entry_t) + del_entry->dlen;

	// removing the DEL_MEMBER conflicting log entry
	remove_raft_conflicting_entries( setup_entries, 1, me.commit_index );
	mock().checkExpectations();

	UNSIGNED_LONGS_EQUAL( setup_entries, raft_log->last_log_index );
	UNSIGNED_LONGS_EQUAL( msize, raft_log->memsize );
}

TEST( TestRaftLog, CheckRaftLogConsistency_LogsConsistent ) {
	int ret = check_raft_log_consistency( setup_entries, 1, 1 );
	CHECK_TRUE( ret );

	UNSIGNED_LONGS_EQUAL( setup_entries, raft_log->last_log_index );
}

TEST( TestRaftLog, CheckRaftLogConsistency_ConflictingHigherTerm ) {
	mock()
		.expectOneCall( "raft_config_remove_server" )
		.withParameter( "server_id", config_data[1].server_id );
	mock()
		.expectOneCall( "get_raft_snapshot_last_term" )
		.andReturnValue( 0 );
	mock()
		.expectOneCall( "get_raft_snapshot_last_index" )
		.andReturnValue( 0 );

	int ret = check_raft_log_consistency( setup_entries, 2, 1 );
	mock().checkExpectations();

	CHECK_FALSE( ret );
	UNSIGNED_LONGS_EQUAL( 1, raft_log->last_log_index );
}

TEST( TestRaftLog, CheckRaftLogConsistency_ConflictingLowerTerm ) {
	entry = get_raft_log_entry( raft_log->last_log_index );
	entry->term = 2;

	mock()
		.expectOneCall( "raft_config_remove_server" )
		.withParameter( "server_id", config_data[1].server_id );
	mock()
		.expectOneCall( "get_raft_snapshot_last_term" )
		.andReturnValue( 0 );
	mock()
		.expectOneCall( "get_raft_snapshot_last_index" )
		.andReturnValue( 0 );

	int ret = check_raft_log_consistency( setup_entries, 1, 1 );
	mock().checkExpectations();

	CHECK_FALSE( ret );
	UNSIGNED_LONGS_EQUAL( 1, raft_log->last_log_index );
}

TEST( TestRaftLog, CheckRaftLogConsistency_ConflictingHigherIndex ) {
	mock()
		.expectOneCall( "raft_config_remove_server" )
		.withParameter( "server_id", config_data[1].server_id );
	mock()
		.expectOneCall( "get_raft_snapshot_last_term" )
		.andReturnValue( 0 );
	mock()
		.expectOneCall( "get_raft_snapshot_last_index" )
		.andReturnValue( 0 );

	int ret = check_raft_log_consistency( setup_entries + 1, 1, 1 );
	mock().checkExpectations();

	CHECK_FALSE( ret );
}

TEST( TestRaftLog, CheckRaftLogConsistency_ConflictingLowerIndex ) {
	mock()
		.expectOneCall( "get_raft_snapshot_last_term" )
		.andReturnValue( 0 );
	mock()
		.expectOneCall( "get_raft_snapshot_last_index" )
		.andReturnValue( 0 );
	mock()
		.expectOneCall( "raft_config_remove_server" )
		.withParameter( "server_id", config_data[1].server_id );

	// commit_index is 1, thus, only the second server is removed
	int ret = check_raft_log_consistency( 0, 1, 1 );
	mock().checkExpectations();

	CHECK_FALSE( ret );
	UNSIGNED_LONGS_EQUAL( 1, raft_log->last_log_index );
}

TEST( TestRaftLog, GetRaftLogEntry_LogInitialized ) {
	log_entry_t *entry;

	entry = get_raft_log_entry( 0 );	// invalid index
	CHECK_FALSE( entry );

	entry = get_raft_log_entry( 3 );	// out of range index
	CHECK_FALSE( entry );

	entry = get_raft_log_entry( 2 );	// valid index
	CHECK_TRUE( entry );
	UNSIGNED_LONGS_EQUAL( 2, entry->index );
}

TEST( TestRaftLog, GetRaftLastLogIndex ) {
	UNSIGNED_LONGS_EQUAL( 2, get_raft_last_log_index( ) );
}

TEST( TestRaftLog, GetRaftLastLogTerm ) {
	UNSIGNED_LONGS_EQUAL( 1, get_raft_last_log_term( ) );
}

TEST( TestRaftLog, SerializeRaftLogEntries ) {
	unsigned int bytes;
	unsigned int esize = 0;		// size of the serialized log entries
	unsigned char *buf = NULL;	// the serialize function will do memory allocation
	unsigned int buf_len = 0;	// the serialize function will populate this value
	unsigned int n_entries = setup_entries;
	// the expected entries (i.e. log entries in the original ring)
	log_entry_t **exp = (log_entry_t **) raft_log->entries->data;

	// test with insufficient message size (1 byte)
	bytes = serialize_raft_log_entries( 1, &n_entries, &buf, &buf_len, 1 );
	UNSIGNED_LONGS_EQUAL( 0, bytes );
	UNSIGNED_LONGS_EQUAL( 0, n_entries );
	UNSIGNED_LONGS_EQUAL( 0, buf_len);
	CHECK_FALSE( buf );

	for( int i = 0; i < setup_entries; i++ ) {
		esize += RAFT_LOG_ENTRY_HDR_SIZE + exp[i]->dlen;
	}

	// test with insufficient message size (passing only the required message header size)
	n_entries = setup_entries;	// the serialize function changes *n_entries
	bytes = serialize_raft_log_entries( 1, &n_entries, &buf, &buf_len, APND_ENTR_HDR_LEN );
	UNSIGNED_LONGS_EQUAL( 0, bytes );
	UNSIGNED_LONGS_EQUAL( 0, n_entries );
	UNSIGNED_LONGS_EQUAL( 0, buf_len);
	CHECK_FALSE( buf );

	// test with insufficient message size (passing only 1 byte to the message payload)
	n_entries = setup_entries;
	bytes = serialize_raft_log_entries( 1, &n_entries, &buf, &buf_len, APND_ENTR_HDR_LEN + 1 );
	UNSIGNED_LONGS_EQUAL( 0, bytes );
	UNSIGNED_LONGS_EQUAL( 0, n_entries );
	UNSIGNED_LONGS_EQUAL( 0, buf_len);
	CHECK_FALSE( buf );

	/*
		forcing to get a non-existent log index (or a compacted index)
		should set errno to ENODATA to trigger a send snapshot
	*/
	n_entries = setup_entries;
	bytes = serialize_raft_log_entries( 0, &n_entries, &buf, &buf_len, APND_ENTR_HDR_LEN + 1 );
	LONGS_EQUAL( ENODATA, errno )
	UNSIGNED_LONGS_EQUAL( 0, bytes );
	UNSIGNED_LONGS_EQUAL( 0, n_entries );
	UNSIGNED_LONGS_EQUAL( 0, buf_len);
	CHECK_FALSE( buf );

	// testing with the same message size than the serialized log entries size + append entries header length
	n_entries = setup_entries;	// the serialize function changes *n_entries
	bytes = serialize_raft_log_entries( 1, &n_entries, &buf, &buf_len, esize + APND_ENTR_HDR_LEN );
	UNSIGNED_LONGS_EQUAL( esize, bytes );
	UNSIGNED_LONGS_EQUAL( setup_entries, n_entries );
	UNSIGNED_LONGS_EQUAL( esize, buf_len);
	CHECK_TRUE( buf );

	if( buf )
		free( buf );
}

TEST( TestRaftLog, DeserializeRaftLogEntries ) {
	unsigned int bytes;
	unsigned int esize = 0;		// size of the serialized log entries
	unsigned char *buf = NULL;	// the serialize function will do memory allocation
	unsigned int buf_len = 0;	// the serialize function will populate this value
	unsigned int n_entries = setup_entries;
	log_entry_t **entries = NULL;
	server_conf_cmd_data_t *exp_cmd_data;	// expected data
	server_conf_cmd_data_t *e_data;			// entry data
	// the expected entries (i.e. log entries in the original ring)
	log_entry_t **exp = (log_entry_t **) raft_log->entries->data;

	for( int i = 0; i < setup_entries; i++ ) {
		esize += RAFT_LOG_ENTRY_HDR_SIZE + exp[i]->dlen;
	}

	//	max_msg_size if the sum of all log entries size (esize) and the append entries header length
	bytes = serialize_raft_log_entries( 1, &n_entries, &buf, &buf_len, esize + APND_ENTR_HDR_LEN );
	UNSIGNED_LONGS_EQUAL( esize, bytes );

	entries = (log_entry_t **) malloc( n_entries * sizeof(log_entry_t *) );
	CHECK( entries != NULL );
	memset( entries, 0, n_entries * sizeof(log_entry_t *) );

	deserialize_raft_log_entries( buf, n_entries, entries );
	/*
		IMPORTANT: in case of errors, they also might be checked at the
		serialization function, since it could introduce a particular bug.
		We do not test data of the serialized log, as we are deserializing
		here and thus, this test is executed here (no duplicated tests).
	*/

	for( int i = 0; i < n_entries; i++ ) {
		UNSIGNED_LONGS_EQUAL( exp[i]->term, entries[i]->term );
		UNSIGNED_LONGS_EQUAL( exp[i]->index, entries[i]->index );
		UNSIGNED_LONGS_EQUAL( exp[i]->dlen, entries[i]->dlen );
		LONGS_EQUAL( exp[i]->command, entries[i]->command );
		LONGS_EQUAL( exp[i]->type, entries[i]->type );

		if( entries[i]->type == RAFT_CONFIG ) {
			CHECK( entries[i]->context == NULL );
			CHECK( entries[i]->key == NULL );
			UNSIGNED_LONGS_EQUAL( 0, entries[i]->clen );
			UNSIGNED_LONGS_EQUAL( 0, entries[i]->klen );
			exp_cmd_data = (server_conf_cmd_data_t *) exp[i]->data;
			e_data = (server_conf_cmd_data_t *) entries[i]->data;
			STRCMP_EQUAL( exp_cmd_data->server_id, e_data->server_id );
			STRCMP_EQUAL( exp_cmd_data->target, e_data->target );
		} // else if( entries[i]->type == RAFT_COMMAND ){}

		free_log_entry( entries[i] );
	}
	if( entries )
		free( entries );
	if( buf )
		free( buf );
}

TEST( TestRaftLog, CompactAllRaftLogs ) {
	UNSIGNED_LONGS_EQUAL( 0, raft_log->index_offset );
	UNSIGNED_LONGS_EQUAL( 2, raft_log->last_log_index );

	compact_raft_log( 2 );

	UNSIGNED_LONGS_EQUAL( 2, raft_log->index_offset );
	UNSIGNED_LONGS_EQUAL( 2, raft_log->last_log_index );
	UNSIGNED_LONGS_EQUAL( 0, raft_log->memsize );
}

TEST( TestRaftLog, CompactOneRaftLog ) {
	size_t size = raft_log->memsize;

	UNSIGNED_LONGS_EQUAL( 0, raft_log->index_offset );
	UNSIGNED_LONGS_EQUAL( 2, raft_log->last_log_index );

	compact_raft_log( 1 );	// only the first log entry is fully replicated

	UNSIGNED_LONGS_EQUAL( 1, raft_log->index_offset );
	UNSIGNED_LONGS_EQUAL( 2, raft_log->last_log_index );
	UNSIGNED_LONGS_EQUAL( size / 2, raft_log->memsize );	// we had only two entries with equal size
}

/* ===================================== Next group of tests ===================================== */
/*
	This group of tests will test all SERVER log functions that require entries appended to the corresponding log
*/
TEST_GROUP( TestServerLog ) {
	log_entry_t *entry;		// generic log entry pointer
	const char *context = "myctx";
	const char *key = "mykey";
	const unsigned int setup_entries = 2;	// number of entries initialized in setup
	int command = 1;
	int cmd_data = 50;
	log_entries_t *server_log = get_server_log( );

	void setup() {
		init_log( SERVER_LOG, 64, 10 );

		mock().disable();
		for( int i = 0; i < setup_entries; i++ ) {
			entry = new_server_log_entry( context, key, command, &cmd_data, sizeof(cmd_data) );
			CHECK( entry != NULL );
			append_server_log_entry( entry, NULL, NULL );
		}
		mock().enable();
	}

	void teardown() {
		mock().clear();

		if( server_log->entries ) {
			// freeing all log entries
			free_all_log_entries( server_log, 0 );
			// freeing the log itself
			free( server_log->entries->data );
			free( server_log->entries );
			server_log->entries = NULL;
		}
	}
};

TEST( TestServerLog, GetServerLogEntry ) {
	entry = get_server_log_entry( 0 );	// invalid log index
	CHECK_FALSE( entry );

	for( int i = 1; i <= setup_entries; i++ ) {
		entry = get_server_log_entry( i );
		CHECK_TRUE( entry );
		UNSIGNED_LONGS_EQUAL( i, entry->index );
	}

	entry = get_server_log_entry( setup_entries + 1 );	// out of range
	CHECK_FALSE( entry );
}

TEST( TestServerLog, GetServerLastLogIndex ) {
	UNSIGNED_LONGS_EQUAL( setup_entries, get_server_last_log_index( ) );
}

TEST( TestServerLog, SerializeServerLogEntries ) {
	unsigned int bytes;
	unsigned int esize = 0;		// size of the serialized log entries
	unsigned char *buf = NULL;	// the serialize function will do memory allocation
	unsigned int buf_len = 0;	// the serialize function will populate this value
	unsigned int n_entries = setup_entries;
	// the expected entries (i.e. log entries in the original ring)
	log_entry_t **exp = (log_entry_t **) server_log->entries->data;

	// test with insufficient message size (1 byte)
	bytes = serialize_server_log_entries( 1, &n_entries, &buf, &buf_len, 1 );
	UNSIGNED_LONGS_EQUAL( 0, bytes );

	for( int i = 0; i < setup_entries; i++ ) {
		esize += XAPP_LOG_ENTRY_HDR_SIZE + exp[i]->dlen + exp[i]->clen + exp[i]->klen;
	}

	// test with insufficient message size (passing only the required message header size)
	n_entries = setup_entries;	// the serialize function changes *n_entries
	bytes = serialize_server_log_entries( 1, &n_entries, &buf, &buf_len, REPL_REQ_HDR_LEN );
	UNSIGNED_LONGS_EQUAL( 0, bytes );

	// test with insufficient message size (passing only 1 byte to the message payload)
	n_entries = setup_entries;
	bytes = serialize_server_log_entries( 1, &n_entries, &buf, &buf_len, REPL_REQ_HDR_LEN + 1 );
	UNSIGNED_LONGS_EQUAL( 0, bytes );

	/*
		forcing to get a non-existent log index (or a compacted index)
		should set errno to ENODATA to trigger a send snapshot
	*/
	n_entries = setup_entries;
	bytes = serialize_server_log_entries( 0, &n_entries, &buf, &buf_len, esize + REPL_REQ_HDR_LEN );
	LONGS_EQUAL( ENODATA, errno );
	UNSIGNED_LONGS_EQUAL( 0, bytes );

	// testing with the same message size than the serialized log entries size + append entries header length
	n_entries = setup_entries;	// the serialize function changes *n_entries
	bytes = serialize_server_log_entries( 1, &n_entries, &buf, &buf_len, esize + REPL_REQ_HDR_LEN );
	UNSIGNED_LONGS_EQUAL( esize, bytes );

	if( buf )
		free( buf );
}

TEST( TestServerLog, DeserializeServerLogEntries ) {
	unsigned int i;
	unsigned int bytes;
	unsigned int esize = 0;		// size of the serialized log entries
	unsigned int n_entries;
	unsigned char *buf = NULL;	// the serialize function will do memory allocation
	unsigned int buf_len = 0;	// the serialize function will populate this value
	log_entry_t **entries = NULL;
	// pointing the expected data to the entries in the ring
	log_entry_t **exp = (log_entry_t **) server_log->entries->data;

	for( i = 0; i < setup_entries; i++ ) {
		esize += XAPP_LOG_ENTRY_HDR_SIZE + exp[i]->dlen + exp[i]->clen + exp[i]->klen;
	}

	n_entries = setup_entries;	// the serialize function changes *n_entries
	bytes = serialize_server_log_entries( 1, &n_entries, &buf, &buf_len, esize + REPL_REQ_HDR_LEN );
	UNSIGNED_LONGS_EQUAL( esize, bytes );

	entries = (log_entry_t **) malloc( n_entries * sizeof(log_entry_t *) );
	CHECK( entries != NULL );
	memset( entries, 0, n_entries * sizeof(log_entry_t *) );

	deserialize_server_log_entries( buf, n_entries, entries );
	/*
		IMPORTANT: in case of errors, they also might be checked at the
		serialization function, since it could introduce a particular bug.
		We do not test data of the serialized log, as we are deserializing
		here and thus, this test is executed here (no duplicated tests).
	*/

	for( i = 0; i < n_entries; i++ ) {
		// UNSIGNED_LONGS_EQUAL( exp[i]->term, entries[i]->term );	// not used by server logs
		UNSIGNED_LONGS_EQUAL( exp[i]->index, entries[i]->index );
		LONGS_EQUAL( exp[i]->command, entries[i]->command );
		LONGS_EQUAL( exp[i]->type, entries[i]->type );
		UNSIGNED_LONGS_EQUAL( exp[i]->dlen, entries[i]->dlen );
		UNSIGNED_LONGS_EQUAL( exp[i]->clen, entries[i]->clen );
		UNSIGNED_LONGS_EQUAL( exp[i]->klen, entries[i]->klen );
		STRCMP_EQUAL( exp[i]->context, entries[i]->context );
		STRCMP_EQUAL( exp[i]->key, entries[i]->key );
		LONGS_EQUAL( *( (int *) exp[i]->data ), *( (int *) entries[i]->data ) );

		free_log_entry( entries[i] );
	}
	if( entries )
		free( entries );
	if( buf )
		free( buf );
}

TEST( TestServerLog, CompactAllServerLogs ) {
	UNSIGNED_LONGS_EQUAL( 0, server_log->index_offset );
	UNSIGNED_LONGS_EQUAL( 2, server_log->last_log_index );

	compact_server_log( 2 );

	UNSIGNED_LONGS_EQUAL( 2, server_log->index_offset );
	UNSIGNED_LONGS_EQUAL( 2, server_log->last_log_index );
	UNSIGNED_LONGS_EQUAL( 0, server_log->memsize );
}

TEST( TestServerLog, CompactOneServerLog ) {
	size_t size = server_log->memsize;

	UNSIGNED_LONGS_EQUAL( 0, server_log->index_offset );
	UNSIGNED_LONGS_EQUAL( 2, server_log->last_log_index );

	compact_server_log( 1 );	// only the first log entry is fully replicated

	UNSIGNED_LONGS_EQUAL( 1, server_log->index_offset );
	UNSIGNED_LONGS_EQUAL( 2, server_log->last_log_index );
	UNSIGNED_LONGS_EQUAL( size / 2, server_log->memsize );	// we had only two entries with equal size
}
