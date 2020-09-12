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
	Mnemonic:	mock_config.cpp
	Abstract:	Implements mock features for the RFT config module

	Date:		2 September 2020
	Author:		Alexandre Huff
*/

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

extern "C" {
	#include "config.h"
}

raft_config_t *raft_get_config( ) {
	return (raft_config_t *)mock().actualCall(__func__)
		.returnPointerValue();
}

log_entries_t *get_server_log( server_id_t *server_id ) {
	return (log_entries_t *)mock().actualCall(__func__)
	.withParameter("server_id", *server_id)
	.returnPointerValue( );
}

int raft_config_add_server( server_id_t *server_id, char *target, index_t last_log_index ) {
	return mock().actualCall(__func__)
		.withParameter("server_id", *server_id)
		.withParameter("target", target)
		.withParameter("last_log_index", last_log_index)
		.returnIntValueOrDefault( 1 );	// if we disable mocking framework, 1 is always returned
}

void raft_config_remove_server( server_id_t *server_id ) {
	mock().actualCall(__func__)
		.withParameter("server_id", *server_id);
}

server_t *raft_config_get_server( server_id_t *server_id ) {
	return (server_t *)mock().actualCall(__func__)
		.withParameter("server_id", *server_id)
		.returnPointerValueOrDefault( NULL );	// if we disable mocking framework, NULL is always returned
}

int has_majority_of_votes( unsigned int rcv_votes ) {
	return mock().actualCall(__func__)
		.withParameter("rcv_votes", rcv_votes)
		.returnIntValue();
}

void raft_config_reset_votes( ) {
	mock().actualCall(__func__);
}

int raft_config_set_new_vote( server_id_t *server_id ) {
	return mock().actualCall(__func__)
		.withParameter("server_id", *server_id)
		.returnIntValue();
}

int raft_config_set_server_status( server_id_t *server_id, raft_voting_member_e status ) {
	return mock().actualCall(__func__)
		.withParameter("server_id", *server_id)
		.withParameter("status", status)
		.returnIntValueOrDefault( 1 );	// if we disable mocking framework, 1 is always returned
}

void raft_config_set_all_server_indexes( index_t raft_last_log_index ) {
	mock().actualCall(__func__)
		.withParameter("raft_last_log_index", raft_last_log_index);
}

int has_majority_of_match_index( index_t match_index ) {
	return mock().actualCall(__func__)
		.withParameter("match_index", match_index)
		.returnIntValue();
}

int is_server_caught_up( server_t *server, int *rounds, struct timespec *heartbeat_timeout, int *progress ) {
	return mock().actualCall(__func__)
		.withPointerParameter("server", server)
		.withPointerParameter("rounds", rounds)
		.withPointerParameter("heartbeat_timeout", heartbeat_timeout)
		.withPointerParameter("progress", progress)
		.returnIntValue();
}

int set_configuration_changing( int is_changing ) {
	return mock().actualCall(__func__)
		.withParameter("is_changing", is_changing)
		.returnIntValue();
}

int is_configuration_changing( ) {
	return mock().actualCall(__func__)
		.returnIntValue();
}

void get_replica_servers( server_id_t *me_self_id, replicas_t *replicas, unsigned int n_replicas ) {
	mock().actualCall(__func__)
		.withParameter("me_self_id", *me_self_id)
		.withPointerParameter("replicas", replicas)
		.withParameter("n_replicas", n_replicas);
}

unsigned int raft_get_num_servers( ) {
	return (unsigned int)mock().actualCall(__func__)
		.returnUnsignedIntValue();
}
