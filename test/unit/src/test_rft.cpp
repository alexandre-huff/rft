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
	Mnemonic:	test_rft.cpp
	Abstract:	Tests the RFT main module

	Date:		21 October 2020
	Author:		Alexandre Huff
*/

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

extern "C" {
	#include <rmr/rmr.h>
	#include <string.h>
	#include "stubs/stub_logger.h"
	#include "stubs/stub_utils.h"
	#include "stubs/stub_pthread.h"
	#include "stubs/stub_snapshot.h"
	#include "stubs/stub_mtl.h"
	#include "rft_private.h"
	#include "rft.h"
	#include "types.h"
	#include "config.h"
	#include "log.h"
}


/*
	Tests the Raft append entries request handler (handle_append_entries_request)
*/
TEST_GROUP( TestAppendEntriesRequest ) {
	request_append_entries_t request;
	reply_append_entries_t reply;
	raft_state_t *me = get_me( );
	log_entry_t *entry;
	int i;

	void setup() {
		request.term = 1;
		request.leader_commit = 0;
		request.prev_log_index = 0;
		request.prev_log_term = 0;
		strcpy( request.leader_id, "xapp1" );
		request.n_entries = 2;
		request.entries = (log_entry_t **) malloc( request.n_entries * sizeof( log_entry_t **) );
		if( !request.entries )
			FAIL( "unable to allocate memory to setup request.entries" );

		strcpy( me->self_id, "xapp5" );
		*me->current_leader = '\0';	// no leader
		me->current_term = 1;
		me->commit_index = 0;
		me->last_applied = 0;

		for( i = 0; i < request.n_entries; i++ ) {
			entry = (log_entry_t *) malloc( sizeof(log_entry_t) );
			if( !entry )
				FAIL( "unable to allocate memory for a new log entry" );
			entry->index = i + 1;
			entry->term = 1;
			request.entries[i] = entry;
		}

		if( !set_max_msg_size( RMR_MAX_RCV_BYTES ) )
			FAIL( "unable to set the max message size for the RFT" );
	}

	void teardown() {
		for( i = 0; i < request.n_entries; i++ ) {
			free( request.entries[i] );
		}
		free( request.entries );
		mock().clear();
	}
};

TEST( TestAppendEntriesRequest, HandleOutdatedMsg ) {

	me->current_term = 10;	// this simulates the message with term 1 to be outdated (see match_terms)

	mock()
		.expectOneCall( "get_raft_last_log_index" )
		.andReturnValue( 2 );

	handle_append_entries_request( &request, &reply );

	mock().checkExpectations();

	CHECK_FALSE( reply.success );
	UNSIGNED_LONGS_EQUAL( me->current_term, reply.term );
	UNSIGNED_LONGS_EQUAL( 2, reply.last_log_index );
	STRCMP_EQUAL( me->self_id, reply.server_id );
}

/*
	Successful heartbeat request with no commit and no entries
*/
TEST( TestAppendEntriesRequest, HandleHeartbeatRequest ) {
	unsigned int tmp = request.n_entries;
	request.n_entries = 0;

	mock()
		.expectOneCall( "check_raft_log_consistency" )
		.withParameter( "prev_log_index", request.prev_log_index )
		.withParameter( "prev_log_term", request.prev_log_term )
		.withParameter( "committed_index", me->commit_index )
		.andReturnValue( 1 );

	mock()
		.expectOneCall( "get_raft_last_log_index" )
		.andReturnValue( 2 );

	handle_append_entries_request( &request, &reply );

	mock().checkExpectations();

	STRCMP_EQUAL( request.leader_id, me->current_leader );
	// reply message
	CHECK_TRUE( reply.success );
	UNSIGNED_LONGS_EQUAL( me->current_term, reply.term );
	UNSIGNED_LONGS_EQUAL( 2, reply.last_log_index );
	STRCMP_EQUAL( me->self_id, reply.server_id );

	request.n_entries = tmp;	// allowing to tear down correctly
}

/*
	Heartbeat request with inconsistent log
*/
TEST( TestAppendEntriesRequest, HandleHeartbeatRequestInconsistentLog ) {
	mock()
		.expectOneCall( "check_raft_log_consistency" )
		.withParameter( "prev_log_index", request.prev_log_index )
		.withParameter( "prev_log_term", request.prev_log_term )
		.withParameter( "committed_index", me->commit_index )
		.andReturnValue( 0 );

	mock()
		.expectOneCall( "get_raft_last_log_index" )
		.andReturnValue( 0 );

	handle_append_entries_request( &request, &reply );

	mock().checkExpectations();

	STRCMP_EQUAL( request.leader_id, me->current_leader );
	// reply message
	CHECK_FALSE( reply.success );
	UNSIGNED_LONGS_EQUAL( me->current_term, reply.term );
	UNSIGNED_LONGS_EQUAL( 0, reply.last_log_index );
	STRCMP_EQUAL( me->self_id, reply.server_id );
}

/*
	Handle append entries request with log entries but commit
*/
TEST( TestAppendEntriesRequest, HandleAppendLogEntryNoCommit ) {
	mock()
		.expectOneCall( "check_raft_log_consistency" )
		.withParameter( "prev_log_index", request.prev_log_index )
		.withParameter( "prev_log_term", request.prev_log_term )
		.withParameter( "committed_index", me->commit_index )
		.andReturnValue( 1 );
	for( int i = 0; i < request.n_entries; i++ ) {
		mock()
			.expectOneCall( "append_raft_log_entry" )
			.withPointerParameter( "log_entry", request.entries[i] );
	}
	mock()
		.expectOneCall( "get_raft_last_log_index" )
		.andReturnValue( 2 );

	handle_append_entries_request( &request, &reply );

	mock().checkExpectations();

	STRCMP_EQUAL( request.leader_id, me->current_leader );
	// reply message
	CHECK_TRUE( reply.success );
	UNSIGNED_LONGS_EQUAL( me->current_term, reply.term );
	UNSIGNED_LONGS_EQUAL( 2, reply.last_log_index );
	STRCMP_EQUAL( me->self_id, reply.server_id );
}

/*
	Handle append entries request with log entries and commit
*/
TEST( TestAppendEntriesRequest, HandleAppendLogEntryAndCommit ) {
	me->state = FOLLOWER;
	request.leader_commit = 1;
	request.entries[0]->type = RAFT_NOOP;	// to run as less as possible code on applying log entries

	mock()
		.expectOneCall( "check_raft_log_consistency" )
		.withParameter( "prev_log_index", request.prev_log_index )
		.withParameter( "prev_log_term", request.prev_log_term )
		.withParameter( "committed_index", me->commit_index )
		.andReturnValue( 1 );
	for( int i = 0; i < request.n_entries; i++ ) {
		mock()
			.expectOneCall( "append_raft_log_entry" )
			.withPointerParameter( "log_entry", request.entries[i] );
	}
	mock()	// called on raft_commit_log_entries
		.expectOneCall( "get_raft_last_log_index" )
		.andReturnValue( request.n_entries );
	mock()	// called on raft_apply_log_entries
		.expectOneCall( "get_raft_log_entry" )
		.withParameter( "log_index", me->last_applied + 1 )
		.andReturnValue( request.entries[0] );
	mock()	// called on handle_append_entries_request
		.expectOneCall( "get_raft_last_log_index" )
		.andReturnValue( request.n_entries );

	handle_append_entries_request( &request, &reply );

	mock().checkExpectations();

	STRCMP_EQUAL( request.leader_id, me->current_leader );
	// reply message
	CHECK_TRUE( reply.success );
	UNSIGNED_LONGS_EQUAL( me->current_term, reply.term );
	UNSIGNED_LONGS_EQUAL( request.n_entries, reply.last_log_index );
	STRCMP_EQUAL( me->self_id, reply.server_id );
	// we don't need to check both committed index and applied index since they are checked on their own test cases
}

/*
	Tests the Raft append entries reply handler (handle_append_entries_reply)
*/
TEST_GROUP( TestAppendEntriesReply ) {
	reply_append_entries_t reply;
	raft_state_t *me = get_me();
	server_t server;
	log_entry_t entry;

	void setup() {
		snprintf( server.server_id, sizeof(server_id_t), "xapp5" );
		server.index_lock = PTHREAD_MUTEX_INITIALIZER;
		server.match_index = 0;
		server.next_index = 0;
		server.hb_timeouts = 2;
		server.replied_ts.tv_sec = 0;

		me->state = LEADER;
		me->current_term = 1;
		me->commit_index = 0;
		me->last_applied = 0;

		reply.last_log_index = 1;
		reply.term = 1;
		reply.success = 1;
		strcpy( reply.server_id, server.server_id );

		entry.index = 1;
		entry.term = 1;
		entry.type = RAFT_NOOP;	// to run as less as possible code on applying log entries

		if( !set_max_msg_size( RMR_MAX_RCV_BYTES ) )
			FAIL( "unable to set the max message size for the RFT" );
	}

	void teardown() {
		mock().clear();
	}
};

/*
	Append entries reply handler does nothing if terms mismatch
*/
TEST( TestAppendEntriesReply, HandleTermsMismatch ) {
	reply.term++;	// just incrementing to mismatch the term
	mock()
		.expectNoCall( "raft_config_get_server" );
	handle_append_entries_reply( &reply );
	mock().checkExpectations();
}

/*
	Append entries reply handler does nothing if the raft status is not LEADER
*/
TEST( TestAppendEntriesReply, HandleNotLeader ) {
	me->state = FOLLOWER;
	mock()
		.expectNoCall( "raft_config_get_server" );
	handle_append_entries_reply( &reply );
	mock().checkExpectations();
}

/*
	Does nothing if the server_id in the reply message is not found in the raft configuration
*/
TEST( TestAppendEntriesReply, HandleServerNotFound ) {
	mock()
		.expectOneCall( "raft_config_get_server" )
		.withParameter( "server_id", server.server_id )
		.andReturnValue( (void *)NULL );
	handle_append_entries_reply( &reply );
	mock().checkExpectations();
}

/*
	Handles replies with success == false (most likely due to log inconsistencies)
*/
TEST( TestAppendEntriesReply, HandleUnsuccessfulReply ) {
	index_t tmp = server.match_index;
	reply.success = 0;

	mock()
		.expectOneCall( "raft_config_get_server" )
		.withParameter( "server_id", server.server_id )
		.andReturnValue( &server );

	handle_append_entries_reply( &reply );

	mock().checkExpectations();

	CHECK( server.replied_ts.tv_sec != 0 );
	UNSIGNED_LONGS_EQUAL( tmp, server.match_index );
	UNSIGNED_LONGS_EQUAL( reply.last_log_index + 1, server.next_index );
	UNSIGNED_LONGS_EQUAL( 0, me->commit_index );
	LONGS_EQUAL( 0, server.hb_timeouts );
}

/*
	Handles replies with no commit
*/
TEST( TestAppendEntriesReply, HandleReplyNoCommit ) {
	reply.last_log_index = me->commit_index;	// all logs were committed

	mock()
		.expectOneCall( "raft_config_get_server" )
		.withParameter( "server_id", server.server_id )
		.andReturnValue( &server );

	handle_append_entries_reply( &reply );

	mock().checkExpectations();

	CHECK( server.replied_ts.tv_sec != 0 );
	UNSIGNED_LONGS_EQUAL( reply.last_log_index, server.match_index );
	UNSIGNED_LONGS_EQUAL( reply.last_log_index + 1, server.next_index );
	UNSIGNED_LONGS_EQUAL( 0, me->commit_index );
	LONGS_EQUAL( 0, server.hb_timeouts );
}

/*
	Handles replies with commit
*/
TEST( TestAppendEntriesReply, HandleReplyWithCommit ) {
	mock()		// called in handle_append_entries_reply
		.expectOneCall( "raft_config_get_server" )
		.withParameter( "server_id", server.server_id )
		.andReturnValue( &server );
	mock()		// called in raft_commit_log_entries
		.expectOneCall( "get_raft_log_entry" )
		.withParameter( "log_index", reply.last_log_index )
		.andReturnValue( &entry );
	mock()		// called in raft_commit_log_entries
		.expectOneCall( "has_majority_of_match_index" )
		.withParameter( "match_index", reply.last_log_index )
		.andReturnValue( 1 );
	mock()		// called in raft_commit_log_entries
		.expectOneCall( "get_raft_log_entry" )
		.withParameter( "log_index", me->commit_index + 1 )
		.andReturnValue( &entry );
	mock()		// called in raft_apply_log_entries
		.expectOneCall( "get_raft_log_entry" )
		.withParameter( "log_index", me->last_applied + 1 )
		.andReturnValue( &entry );

	handle_append_entries_reply( &reply );

	mock().checkExpectations();

	CHECK( server.replied_ts.tv_sec != 0 );
	UNSIGNED_LONGS_EQUAL( reply.last_log_index, server.match_index );
	UNSIGNED_LONGS_EQUAL( reply.last_log_index + 1, server.next_index );
	UNSIGNED_LONGS_EQUAL( 1, me->commit_index );
	LONGS_EQUAL( 0, server.hb_timeouts );
}

/*
	Tests the function to send Raft append entry messages (by using raft_server)
	The send_append_entries is an inline function called by the raft_server
*/
TEST_GROUP( TestSendAppendEntries ) {
	raft_state_t *me = get_me();
	server_t server;
	log_entry_t entry;
	rmr_mbuf_t *msg;
	appnd_entr_hdr_t *payload;
	int plen = 512;	// payload size

	void setup() {
		snprintf( server.server_id, sizeof(server_id_t), "xapp1" );
		server.index_lock = PTHREAD_MUTEX_INITIALIZER;
		server.match_index = 0;
		server.next_index = 1;
		server.status = VOTING_MEMBER;
		server.hb_timeouts = 0;
		server.replied_ts.tv_sec = 0;

		me->state = LEADER;
		me->current_term = 1;
		me->commit_index = 0;
		me->last_applied = 0;

		entry.index = 1;
		entry.term = 1;
		entry.type = RAFT_NOOP;	// to run as less as possible code on applying log entries

		msg = (rmr_mbuf_t *) malloc( sizeof(rmr_mbuf_t) );
		if( msg == NULL )
			FAIL( "unable to allocate memory for an RMR message buffer" );
		msg->payload = (unsigned char *) malloc( plen );
		if( msg->payload == NULL )
			FAIL( "unable to allocate memory to the RMR payload")

		if( !set_max_msg_size( RMR_MAX_RCV_BYTES ) )
			FAIL( "unable to set the max message size for the RFT" );
	}

	void teardown() {
		free( msg->payload );
		free( msg );
		mock().clear();
	}

	void mock_init_server() {
		mock()
			.expectOneCall( "rmr_alloc_msg" )
			.ignoreOtherParameters()
			.andReturnValue( msg );
		mock()
			.expectOneCall( "rmr_wh_open" )
			.ignoreOtherParameters()
			.andReturnValue( 0 );
	}

	void mock_release_server() {
		mock()	// called by raft_server()
			.expectOneCall( "rmr_wh_close" )
			.withParameter( "whid", 0 )	// the value returned by the rmr_wh_open mock
			.ignoreOtherParameters()
			.andReturnValue( 0 );
		mock()	// called by raft_server()
			.expectOneCall( "rmr_free_msg" )
			.withPointerParameter( "mbuf", msg );
	}
};

/*
	Sends the first heartbeat message with no log entry
*/
TEST( TestSendAppendEntries, SendFirstHearbeatMessageNoEntry ) {
	mock_init_server();	// initializing the raft server

	mock()	// called by raft_server()
		.expectOneCall( "get_raft_last_log_index" )
		.andReturnValue( 0 );
	mock()	// called by send_append_entries
		.expectOneCall( "rmr_payload_size" )
		.withPointerParameter( "msg", msg )
		.andReturnValue( plen );
	mock()	// called by send_append_entries
		.expectOneCall( "get_raft_log_entry" )
		.withParameter( "log_index", 0 )	// is the value from prev_log_index of the raft server
		.andReturnValue( (void *)NULL );	// there is no previous log entry
	mock()	// called by send_append_entries
		.expectOneCall( "get_raft_snapshot_last_index" )
		.andReturnValue( 0 );	// since there is no previous log entry
	mock()	// called by send_append_entries
		.expectOneCall( "get_raft_snapshot_last_term" )
		.andReturnValue( 0 );	// since there is no previous log entry
	mock()
		.expectOneCall( "rmr_wh_send_msg" )
		.withParameter( "whid", 0 )
		.withPointerParameter( "msg", msg )
		.ignoreOtherParameters( )
		.andReturnValue( msg );

	mock_release_server();	// releasing resources of the raft server

	raft_server( &server );
	mock().checkExpectations();

	/*
		We only check information regarding the message payload used to sent append entries
		The information regarding the server is checked by the corresponding tests of the raft server function
	*/
	payload = (appnd_entr_hdr_t *) msg->payload;
	UNSIGNED_LONGS_EQUAL( 0, payload->prev_log_index );
	UNSIGNED_LONGS_EQUAL( 0, payload->prev_log_term );
	UNSIGNED_LONGS_EQUAL( 0, payload->slen );
	UNSIGNED_LONGS_EQUAL( 0, payload->n_entries );
	UNSIGNED_LONGS_EQUAL( 1, payload->term );
	UNSIGNED_LONGS_EQUAL( 0, payload->leader_commit );
	STRCMP_EQUAL( me->self_id, payload->leader_id );
}

/*
	Sends a heartbeat message with no log entry
*/
TEST( TestSendAppendEntries, SendHearbeatMessageNoEntry ) {
	server.match_index = 1;
	server.next_index = 2;

	mock_init_server();	// initializing the raft server

	mock()	// called by raft_server()
		.expectOneCall( "get_raft_last_log_index" )
		.andReturnValue( 1 );
	mock()	// called by send_append_entries
		.expectOneCall( "rmr_payload_size" )
		.withPointerParameter( "msg", msg )
		.andReturnValue( plen );
	mock()	// called by send_append_entries
		.expectOneCall( "get_raft_log_entry" )
		.withParameter( "log_index", 1 )	// is the value from prev_log_index of the raft server
		.andReturnValue( &entry );
	mock()
		.expectOneCall( "rmr_wh_send_msg" )
		.withParameter( "whid", 0 )
		.withPointerParameter( "msg", msg )
		.ignoreOtherParameters( )
		.andReturnValue( msg );

	mock_release_server();	// releasing resources of the raft server

	raft_server( &server );
	mock().checkExpectations();

	/*
		We only check information regarding the message payload used to sent append entries
		The information regarding the server is checked by the corresponding tests of the raft server function
	*/
	payload = (appnd_entr_hdr_t *) msg->payload;
	UNSIGNED_LONGS_EQUAL( 1, payload->prev_log_index );
	UNSIGNED_LONGS_EQUAL( 1, payload->prev_log_term );
	UNSIGNED_LONGS_EQUAL( 0, payload->slen );
	UNSIGNED_LONGS_EQUAL( 0, payload->n_entries );
	UNSIGNED_LONGS_EQUAL( 1, payload->term );
	UNSIGNED_LONGS_EQUAL( 0, payload->leader_commit );
	STRCMP_EQUAL( me->self_id, payload->leader_id );
}

/*
	Sends a heartbeat message when the leader's log does not match with another raft member
*/
TEST( TestSendAppendEntries, SendHearbeatMessageNoEntry_MismachLogs ) {
	server.match_index = 0;
	server.next_index = 2;

	mock_init_server();	// initializing the raft server

	mock()
		.expectNoCall( "get_raft_last_log_index" );	// shouldn't be called by raft_server()
	mock()	// called by send_append_entries
		.expectOneCall( "rmr_payload_size" )
		.withPointerParameter( "msg", msg )
		.andReturnValue( plen );
	mock()	// called by send_append_entries
		.expectOneCall( "get_raft_log_entry" )
		.withParameter( "log_index", 1 )	// is the value from prev_log_index of the raft server
		.andReturnValue( &entry );
	mock()
		.expectOneCall( "rmr_wh_send_msg" )
		.withParameter( "whid", 0 )
		.withPointerParameter( "msg", msg )
		.ignoreOtherParameters( )
		.andReturnValue( msg );

	mock_release_server();	// releasing resources of the raft server

	raft_server( &server );
	mock().checkExpectations();

	/*
		We only check information regarding the message payload used to sent append entries
		The information regarding the server is checked by the corresponding tests of the raft server function
	*/
	payload = (appnd_entr_hdr_t *) msg->payload;
	UNSIGNED_LONGS_EQUAL( 1, payload->prev_log_index );
	UNSIGNED_LONGS_EQUAL( 1, payload->prev_log_term );
	UNSIGNED_LONGS_EQUAL( 0, payload->slen );
	UNSIGNED_LONGS_EQUAL( 0, payload->n_entries );
	UNSIGNED_LONGS_EQUAL( 1, payload->term );
	UNSIGNED_LONGS_EQUAL( 0, payload->leader_commit );
	STRCMP_EQUAL( me->self_id, payload->leader_id );
}

/*
	Tests if a non Leader would send an append entries message (it shouldn't)
*/
TEST( TestSendAppendEntries, SendMessageNoEntry_NonLeader ) {
	server.match_index = 1;
	server.next_index = 2;
	me->state = FOLLOWER;

	mock_init_server();	// initializing the raft server

	mock()	// called by raft_server()
		.expectOneCall( "get_raft_last_log_index" )
		.andReturnValue( 1 );
	mock()	// called by send_append_entries
		.expectOneCall( "rmr_payload_size" )
		.withPointerParameter( "msg", msg )
		.andReturnValue( plen );
	mock()	// called by send_append_entries
		.expectOneCall( "get_raft_log_entry" )
		.withParameter( "log_index", 1 )	// is the value from prev_log_index of the raft server
		.andReturnValue( &entry );
	mock()
		.expectNoCall( "rmr_wh_send_msg" );	// only the leader can send append entries messages

	mock_release_server();	// releasing resources of the raft server

	raft_server( &server );
	mock().checkExpectations();

	/*
		We only check information regarding the message payload used to sent append entries
		The information regarding the server is checked by the corresponding tests of the raft server function
	*/
	payload = (appnd_entr_hdr_t *) msg->payload;
	UNSIGNED_LONGS_EQUAL( 1, payload->prev_log_index );
	UNSIGNED_LONGS_EQUAL( 1, payload->prev_log_term );
	UNSIGNED_LONGS_EQUAL( 0, payload->slen );
	UNSIGNED_LONGS_EQUAL( 0, payload->n_entries );
}

/*
	Sends an append entries message with a new log entry
*/
TEST( TestSendAppendEntries, SendMessageWithLogEntry ) {
	unsigned int n_entries = 1;

	mock_init_server();	// initializing the raft server

	mock()	// called by raft_server()
		.expectOneCall( "get_raft_last_log_index" )
		.andReturnValue( 1 );
	mock()
		.expectOneCall( "serialize_raft_log_entries" )
		.withParameter( "from_index", server.next_index )
		.withOutputParameterReturning( "n_entries", &n_entries, sizeof(unsigned int) )		// we are replicating only one log entry
		.withParameter( "max_msg_size", RMR_MAX_RCV_BYTES )	// should be greater than the message payload + mbuf
		.ignoreOtherParameters( )
		.andReturnValue( 8 );	// serialization returns 8 bytes in this test (should be less than plen below)
	mock()	// called by send_append_entries
		.expectOneCall( "rmr_payload_size" )
		.withPointerParameter( "msg", msg )
		.andReturnValue( plen );
	mock()	// called by send_append_entries
		.expectOneCall( "get_raft_log_entry" )
		.withParameter( "log_index", 0 )	// is the value from prev_log_index of the raft server (assuming first replication)
		.andReturnValue( (void *)NULL );
	mock()	// called by send_append_entries
		.expectOneCall( "get_raft_snapshot_last_index" )
		.andReturnValue( 0 );	// since there is no previous log entry
	mock()	// called by send_append_entries
		.expectOneCall( "get_raft_snapshot_last_term" )
		.andReturnValue( 0 );	// since there is no previous log entry
	mock()
		.expectOneCall( "rmr_wh_send_msg" )
		.withParameter( "whid", 0 )
		.withPointerParameter( "msg", msg )
		.ignoreOtherParameters( )
		.andReturnValue( msg );

	mock_release_server();	// releasing resources of the raft server

	raft_server( &server );
	mock().checkExpectations();

	/*
		We only check information regarding the message payload used to sent append entries
		The information regarding the server is checked by the corresponding tests of the raft server function
	*/
	payload = (appnd_entr_hdr_t *) msg->payload;
	UNSIGNED_LONGS_EQUAL( 0, payload->prev_log_index );
	UNSIGNED_LONGS_EQUAL( 0, payload->prev_log_term );
	UNSIGNED_LONGS_EQUAL( 8, payload->slen );		// returned from serialization
	UNSIGNED_LONGS_EQUAL( 1, payload->n_entries );	// we "serialized" only one entry in this test
	UNSIGNED_LONGS_EQUAL( 1, payload->term );
	UNSIGNED_LONGS_EQUAL( 0, payload->leader_commit );
	STRCMP_EQUAL( me->self_id, payload->leader_id );
}

/*
	Sends an append entries message with a new log entry reallocating the payload size
*/
TEST( TestSendAppendEntries, SendMessageWithLogEntryReallocPayload ) {
	unsigned int n_entries = 1;

	mock_init_server();	// initializing the raft server

	mock()	// called by raft_server()
		.expectOneCall( "get_raft_last_log_index" )
		.andReturnValue( 1 );
	mock()
		.expectOneCall( "serialize_raft_log_entries" )
		.withParameter( "from_index", server.next_index )
		.withOutputParameterReturning( "n_entries", &n_entries, sizeof(unsigned int) )		// we are replicating only one log entry
		.withParameter( "max_msg_size", RMR_MAX_RCV_BYTES )	// should be greater than the message payload + mbuf
		.ignoreOtherParameters( )
		.andReturnValue( 8 );	// serialization returns 8 bytes in this test (should be less than plen below)
	mock()	// called by send_append_entries
		.expectOneCall( "rmr_payload_size" )
		.withPointerParameter( "msg", msg )
		.andReturnValue( 0 );
	mock()	// called by send_append_entries
		.expectOneCall( "rmr_realloc_payload" )
		.withPointerParameter( "old_msg", msg )
		.withParameter( "new_len", APND_ENTR_HDR_LEN + 8 )	// 8 bytes from serialization
		.withParameter( "copy", 0 )
		.withParameter( "clone", 0 )
		.andReturnValue( msg );
	mock()	// called by send_append_entries
		.expectOneCall( "get_raft_log_entry" )
		.withParameter( "log_index", 0 )	// is the value from prev_log_index of the raft server (assuming first replication)
		.andReturnValue( (void *)NULL );
	mock()	// called by send_append_entries
		.expectOneCall( "get_raft_snapshot_last_index" )
		.andReturnValue( 0 );	// since there is no previous log entry
	mock()	// called by send_append_entries
		.expectOneCall( "get_raft_snapshot_last_term" )
		.andReturnValue( 0 );	// since there is no previous log entry
	mock()
		.expectOneCall( "rmr_wh_send_msg" )
		.withParameter( "whid", 0 )
		.withPointerParameter( "msg", msg )
		.ignoreOtherParameters( )
		.andReturnValue( msg );

	mock_release_server();	// releasing resources of the raft server

	raft_server( &server );
	mock().checkExpectations();

	/*
		We only check information regarding the message payload used to sent append entries
		The information regarding the server is checked by the corresponding tests of the raft server function
	*/
	payload = (appnd_entr_hdr_t *) msg->payload;
	UNSIGNED_LONGS_EQUAL( 0, payload->prev_log_index );
	UNSIGNED_LONGS_EQUAL( 0, payload->prev_log_term );
	UNSIGNED_LONGS_EQUAL( 8, payload->slen );		// returned from serialization
	UNSIGNED_LONGS_EQUAL( 1, payload->n_entries );	// we "serialized" only one entry in this test
	UNSIGNED_LONGS_EQUAL( 1, payload->term );
	UNSIGNED_LONGS_EQUAL( 0, payload->leader_commit );
	STRCMP_EQUAL( me->self_id, payload->leader_id );
}

/*
	Tests the function to send Raft snapshot messages (by using raft_server)
	The send_raft_snapshot is not inlined but some work is also done in the the raft_server
*/
TEST_GROUP( TestRaftServerSnapshotting ) {
	raft_state_t *me = get_me();
	server_t server;
	log_entry_t entry;
	rmr_mbuf_t *msg;
	raft_snapshot_hdr_t *payload;
	int plen = 512;	// payload size

	void setup() {
		snprintf( server.server_id, sizeof(server_id_t), "xapp1" );
		server.index_lock = PTHREAD_MUTEX_INITIALIZER;
		server.match_index = 0;
		server.next_index = 1;
		server.status = VOTING_MEMBER;
		server.hb_timeouts = 0;
		server.replied_ts.tv_sec = 0;

		me->state = LEADER;
		me->current_term = 1;
		me->commit_index = 0;
		me->last_applied = 0;

		entry.index = 1;
		entry.term = 1;
		entry.type = RAFT_NOOP;	// to run as less as possible code on applying log entries

		msg = (rmr_mbuf_t *) malloc( sizeof(rmr_mbuf_t) );
		if( msg == NULL )
			FAIL( "unable to allocate memory for an RMR message buffer" );
		msg->payload = (unsigned char *) malloc( plen );
		if( msg->payload == NULL )
			FAIL( "unable to allocate memory to the RMR payload")

		if( !set_max_msg_size( RMR_MAX_RCV_BYTES ) )
			FAIL( "unable to set the max message size for the RFT" );
	}

	void teardown() {
		free( msg->payload );
		free( msg );
		mock().clear();
	}

	void mock_init_server() {
		mock()
			.expectOneCall( "rmr_alloc_msg" )
			.ignoreOtherParameters()
			.andReturnValue( msg );
		mock()
			.expectOneCall( "rmr_wh_open" )
			.ignoreOtherParameters()
			.andReturnValue( 0 );
	}

	void mock_release_server() {
		mock()	// called by raft_server()
			.expectOneCall( "rmr_wh_close" )
			.withParameter( "whid", 0 )	// the value returned by the rmr_wh_open mock
			.ignoreOtherParameters()
			.andReturnValue( 0 );
		mock()	// called by raft_server()
			.expectOneCall( "rmr_free_msg" )
			.withPointerParameter( "mbuf", msg );
	}
};

/*
	Sends a raft snapshot message (leader)
*/
TEST( TestRaftServerSnapshotting, SendSnapshot ) {
	unsigned int n_entries = 1;

	mock_init_server();	// initializing the raft server

	mock()	// called by raft_server()
		.expectOneCall( "get_raft_last_log_index" )
		.andReturnValue( 1 );
	mock().setData( "errno", ENODATA );
	mock()
		.expectOneCall( "serialize_raft_log_entries" )
		.withParameter( "max_msg_size", RMR_MAX_RCV_BYTES )
		.ignoreOtherParameters( )
		.andReturnValue( 0 );	// the ideia is to set errno to ENODATA
	mock()	// called by send_raft_snapshot
		.expectOneCall( "serialize_raft_snapshot" )
		.withOutputParameterReturning( "msg", &msg, sizeof(rmr_mbuf_t *) )
		.andReturnValue( 32 );
	mock()
		.expectOneCall( "rmr_wh_send_msg" )
		.withParameter( "whid", 0 )
		.withPointerParameter( "msg", msg )
		.ignoreOtherParameters( )
		.andReturnValue( msg );

	mock_release_server();	// releasing resources of the raft server

	raft_server( &server );
	mock().checkExpectations();

	/*
		We only check information regarding the message payload used to send raft snapshots
		Information regarding the server and raft serialization and the server is checked by
		the corresponding test functions
	*/
	payload = (raft_snapshot_hdr_t *) msg->payload;
	UNSIGNED_LONGS_EQUAL( NTOHLL( me->current_term ), payload->term );
	STRCMP_EQUAL( me->self_id, payload->leader_id );
}

/*
	Tests if a non leader sends a snapshot to the destination (it shouldn't)
*/
TEST( TestRaftServerSnapshotting, SendSnapshot_NonLeader ) {
	me->state = FOLLOWER;

	mock_init_server();	// initializing the raft server

	mock()	// called by raft_server()
		.expectOneCall( "get_raft_last_log_index" )
		.andReturnValue( 1 );
	mock().setData( "errno", ENODATA );
	mock()
		.expectOneCall( "serialize_raft_log_entries" )
		.withParameter( "max_msg_size", RMR_MAX_RCV_BYTES )
		.ignoreOtherParameters( )
		.andReturnValue( 0 );	// the ideia is to set errno to ENODATA
	mock()	// called by send_raft_snapshot
		.expectOneCall( "serialize_raft_snapshot" )
		.withOutputParameterReturning( "msg", &msg, sizeof(rmr_mbuf_t *) )
		.andReturnValue( 32 );
	mock()
		.expectNoCall( "rmr_wh_send_msg" );	// should not send the message

	mock_release_server();	// releasing resources of the raft server

	raft_server( &server );
	mock().checkExpectations();
	/*
		We don't need to check any specific data here, all we need to do is
		to ensure that the message is not send to the destination using RMR (rmr_wh_send_msg)
	*/
}

/*
	Tests if an empty snapshot is sent to the destination (it shouldn't)
*/
TEST( TestRaftServerSnapshotting, SendSnapshot_NoSnapshot ) {
	mock_init_server();	// initializing the raft server

	mock()	// called by raft_server()
		.expectOneCall( "get_raft_last_log_index" )
		.andReturnValue( 1 );
	mock().setData( "errno", ENODATA );
	mock()
		.expectOneCall( "serialize_raft_log_entries" )
		.withParameter( "max_msg_size", RMR_MAX_RCV_BYTES )
		.ignoreOtherParameters( )
		.andReturnValue( 0 );	// the ideia is to set errno to ENODATA
	mock()	// called by send_raft_snapshot
		.expectOneCall( "serialize_raft_snapshot" )
		.withOutputParameterReturning( "msg", &msg, sizeof(rmr_mbuf_t *) )
		.andReturnValue( 0 );	// means that no snapshot has been taken yet
	mock()
		.expectNoCall( "rmr_wh_send_msg" );	// should not send the message

	mock_release_server();	// releasing resources of the raft server

	raft_server( &server );
	mock().checkExpectations();
	/*
		We don't need to check any specific data here, all we need to do is
		to ensure that the message is not send to the destination using RMR (rmr_wh_send_msg)
	*/
}
