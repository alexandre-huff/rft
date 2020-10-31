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
	Mnemonic:	mock_log.cpp
	Abstract:	Implements mock features for the RFT log module

	Date:		18 September 2020
	Author:		Alexandre Huff

	Changed:	22 October 2020
				Added the required mocks to test the RFT module
*/

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

extern "C" {
	#include "log.h"
	#include <stdint.h>
	#include <stddef.h>
}


int init_log( log_type_e type, u_int32_t size, u_int32_t threshold ) {
	return mock().actualCall(__func__)
		.withParameter("type", type)
		.withParameter("size", size)
		.withParameter("threshold", threshold)
		.returnIntValueOrDefault( 0 );
}

log_entries_t *get_raft_log( ) {
	return (log_entries_t *)mock().actualCall(__func__)
		.returnPointerValueOrDefault( NULL );
}

log_entries_t *get_server_log( ) {
	return (log_entries_t *)mock().actualCall(__func__)
		.returnPointerValueOrDefault( NULL );
}

void append_raft_log_entry( log_entry_t *log_entry ) {
	mock().actualCall(__func__)
		.withPointerParameter("log_entry", log_entry);
}

void append_server_log_entry( log_entry_t *log_entry, hashtable_t *ctxtable, take_snapshot_cb_t take_xapp_snapshot_cb ) {
	mock().actualCall(__func__)
		.withPointerParameter("log_entry", log_entry)
		.withPointerParameter("ctxtable", ctxtable)
		.withPointerParameter("take_xapp_snapshot_cb", (take_snapshot_cb_t *) take_xapp_snapshot_cb);
}

log_entry_t *get_raft_log_entry( index_t log_index ) {
	return (log_entry_t *)mock().actualCall(__func__)
		.withParameter("log_index", log_index)
		.returnPointerValueOrDefault( NULL );
}

log_entry_t *get_server_log_entry( index_t log_index ) {
	return (log_entry_t *)mock().actualCall(__func__)
		.withParameter("log_index", log_index)
		.returnPointerValueOrDefault( NULL );
}

index_t get_raft_last_log_index( ) {
	return (index_t)mock().actualCall(__func__)
		.returnUnsignedLongIntValueOrDefault( 0 );
}

index_t get_server_last_log_index( ) {
	return (index_t)mock().actualCall(__func__)
		.returnUnsignedLongIntValueOrDefault( 0 );
}

term_t get_raft_last_log_term( ) {
	return (term_t)mock().actualCall(__func__)
		.returnUnsignedLongIntValueOrDefault( 0 );
}

void free_log_entry( log_entry_t *entry ) {
		mock().actualCall(__func__)
		.withPointerParameter("entry", entry);
}

unsigned int serialize_raft_log_entries( index_t from_index, unsigned int *n_entries, unsigned char **lbuf,
											unsigned int *buf_len, int max_msg_size ) {
	if( mock().hasData( "errno" ) )
		errno = mock().getData( "errno" ).getIntValue();
	return (unsigned int)mock().actualCall(__func__)
		.withParameter("from_index", from_index)
		.withOutputParameter("n_entries", n_entries)
		.withOutputParameter("lbuf", lbuf)
		.withOutputParameter("buf_len", buf_len)
		.withParameter("max_msg_size", max_msg_size)
		.returnUnsignedIntValueOrDefault( 0 );
}

unsigned int serialize_server_log_entries( index_t from_index, unsigned int *n_entries, unsigned char **lbuf,
											unsigned int *buf_len, int max_msg_size ) {
	return (unsigned int)mock().actualCall(__func__)
		.withParameter("from_index", from_index)
		.withOutputParameter("n_entries", n_entries)
		.withOutputParameter("lbuf", lbuf)
		.withOutputParameter("buf_len", buf_len)
		.withParameter("max_msg_size", max_msg_size)
		.returnUnsignedIntValueOrDefault( 0 );
}

void deserialize_raft_log_entries( unsigned char *s_entries, unsigned int n_entries, log_entry_t **entries ) {
	mock().actualCall(__func__)
		.withPointerParameter("s_entries", s_entries)
		.withParameter("n_entries", n_entries)
		.withOutputParameter("entries", entries);
}

void deserialize_server_log_entries( unsigned char *s_entries, unsigned int n_entries, log_entry_t **entries ) {
	mock().actualCall(__func__)
		.withPointerParameter("s_entries", s_entries)
		.withParameter("n_entries", n_entries)
		.withOutputParameter("entries", entries);
}

int check_raft_log_consistency( index_t prev_log_index, term_t prev_log_term, index_t committed_index ) {
	return mock().actualCall(__func__)
		.withParameter("prev_log_index", prev_log_index)
		.withParameter("prev_log_term", prev_log_term)
		.withParameter("committed_index", committed_index)
		.returnIntValueOrDefault( 0 );
}

log_entry_t *new_raft_log_entry( term_t term, log_entry_type_e type, int command, void *data, size_t len ) {
	return (log_entry_t *)mock().actualCall(__func__)
		.withParameter("term", term)
		.withParameter("type", type)
		.withParameter("command", command)
		.withPointerParameter("data", data)
		.withParameter("len", len)
		.returnPointerValueOrDefault( NULL );
}

log_entry_t *new_server_log_entry( const char *context, const char *key, int command, void *data, size_t len ) {
	return (log_entry_t *)mock().actualCall(__func__)
		.withParameter("context", context)
		.withParameter("key", key)
		.withParameter("command", command)
		.withPointerParameter("data", data)
		.withParameter("len", len)
		.returnPointerValueOrDefault( NULL );
}

void lock_server_log( ) {
	mock().actualCall(__func__);
}

void unlock_server_log( ) {
	mock().actualCall(__func__);
}

void compact_server_log( index_t to_index ) {
	mock().actualCall(__func__)
		.withParameter("to_index", to_index);
}

void compact_raft_log( index_t last_index	) {
	mock().actualCall(__func__)
		.withParameter("last_index", last_index);
}

void lock_raft_log( ) {
	mock().actualCall(__func__);
}

void unlock_raft_log( ) {
	mock().actualCall(__func__);
}

void free_all_log_entries( log_entries_t *log, index_t last_applied_log_index ) {
	mock().actualCall(__func__)
		.withPointerParameter("log", log)
		.withParameter("last_applied_log_index", last_applied_log_index);
}
