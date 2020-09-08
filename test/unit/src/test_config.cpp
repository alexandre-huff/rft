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
	Mnemonic:	test_config.cpp
	Abstract:	Tests the RFT config module

	Date:		7 September 2020
	Author:		Alexandre Huff
*/

#include "CppUTest/TestHarness.h"
// #include "CppUTest/TestHarness_c.h"

extern "C" {
	#include "config.h"
	#include "stubs/stub_logger.h"
	#include "stubs/stub_rft.h"
	#include "stubs/stub_pthread.h"
}

TEST_GROUP( TestConfig ) {
	server_id_t server_id;
	char target[64];
	raft_config_t *config = raft_get_config( );

	void setup() {
		snprintf( server_id, sizeof(server_id_t), "server1" );
		snprintf( target, 64, "127.0.0.1:4560" );
	}

	void teardown() {
		for( int i = 0; i < config->size; i++ ) {
			free( config->servers[i] );
		}
		free( config->servers );
		config->servers = NULL;
		config->is_changing = 0;
		config->size = 0;
		config->voting_members = 0;
	}
};

TEST( TestConfig, RaftConfigAddServer ) {
	int ret;
	server_t *server;
	index_t last_log_index = 0;

	// trying to add a server without target
	ret = raft_config_add_server( &server_id, NULL, last_log_index );
	LONGS_EQUAL( 0, ret );

	// adding a server with correct arguments
	ret = raft_config_add_server( &server_id, target, last_log_index );
	LONGS_EQUAL( 1, ret );
	LONGS_EQUAL( 1, config->size );
	LONGS_EQUAL( 0, config->voting_members );

	server = raft_config_get_server( &server_id );
	CHECK( server != NULL );

	LONGS_EQUAL( NON_VOTING_MEMBER, server->status );
	LONGS_EQUAL( last_log_index , server->match_index );
	LONGS_EQUAL( last_log_index + 1, server->next_index );
	LONGS_EQUAL( 0, server->voted_for_me );
	STRCMP_EQUAL( server_id, server->server_id );
	STRCMP_EQUAL( target, server->target );
	LONGS_EQUAL( 0, server->replied_ts.tv_sec );
	LONGS_EQUAL( 0, server->replied_ts.tv_nsec );
	LONGS_EQUAL( 0, server->hb_timeouts );
	LONGS_EQUAL( 0, server->replica_index );
	LONGS_EQUAL( 0, server->master_index );
	LONGS_EQUAL( SHUTDOWN, server->active );

	// trying to add a server that is stored in the configuration
	last_log_index = 5;
	ret = raft_config_add_server( &server_id, target, last_log_index );
	LONGS_EQUAL( 0, ret );
	LONGS_EQUAL( 1, config->size );				// cannot change
	LONGS_EQUAL( 0, config->voting_members );	// cannot change
	// those indexes have to be set again since the server might have outdated information
	LONGS_EQUAL( last_log_index, server->match_index );
	LONGS_EQUAL( last_log_index, server->master_index );
	LONGS_EQUAL( last_log_index + 1, server->next_index );
}

TEST( TestConfig, RaftConfigGetServer ) {
	// should return NULL since there is no server in the configuration
	server_t *server =  raft_config_get_server( &server_id );
	CHECK_FALSE( server );
}

TEST_GROUP( TestConfigInitialized ) {
	server_id_t server_id;
	char target[64];
	raft_config_t *config = raft_get_config( );

	void setup() {
		snprintf( server_id, sizeof(server_id_t), "server1" );
		snprintf( target, 64, "127.0.0.1:4560" );
		int ret = raft_config_add_server( &server_id, target, 0 );
		LONGS_EQUAL( 1, ret );
	}

	void teardown() {
		for( int i = 0; i < config->size; i++ ) {
			free( config->servers[i] );
		}
		free( config->servers );
		config->servers = NULL;
		config->is_changing = 0;
		config->size = 0;
		config->voting_members = 0;
	}
};

TEST( TestConfigInitialized, RaftConfigGetServer ) {
	server_id_t tmp = { 'a', '\0' };

	// trying to get an non-existent server in config
	server_t *server = raft_config_get_server( &tmp );
	CHECK_FALSE( server );

	server = raft_config_get_server( &server_id );
	CHECK( server != NULL );

	STRCMP_EQUAL( server_id, server->server_id );
	STRCMP_EQUAL( target, server->target );
}

TEST( TestConfigInitialized, RaftConfigRemoveServer ) {

	server_t *server = raft_config_get_server( &server_id );
	CHECK( server != NULL );
	server->active = RUNNING;

	// needed to check if voting member is decreased on removing the server
	raft_config_set_server_status( &server_id, VOTING_MEMBER );

	raft_config_remove_server( &server_id );
	config->size = 0;
	config->voting_members = 0;	// checking voting members
	config->is_changing = 0;

	/*
		we have to free here, since in the real implementation the
		server's thread deallocate its memory
	*/
	free( server );
}

TEST( TestConfigInitialized, RaftConfigSetServerStatus ) {
	server_id_t tmp = { 'a', '\0' };
	int ret;

	server_t *server = raft_config_get_server( &server_id );
	CHECK( server != NULL );

	ret = raft_config_set_server_status( &tmp, VOTING_MEMBER );	// server not in configuration
	CHECK_FALSE( ret );

	ret = raft_config_set_server_status( &server_id, VOTING_MEMBER );	// server not in configuration
	CHECK_TRUE( ret );
	LONGS_EQUAL( 1, config->voting_members );

	ret = raft_config_set_server_status( &server_id, NON_VOTING_MEMBER );	// server not in configuration
	CHECK_TRUE( ret );
	LONGS_EQUAL( 0, config->voting_members );
}

TEST( TestConfigInitialized, RaftConfigSetNewVote ) {
	server_id_t tmp = { 'a', '\0' };
	int ret;

	server_t *server = raft_config_get_server( &server_id );
	CHECK( server != NULL );

	ret = raft_config_set_new_vote( &server_id );	// non-voting-member
	CHECK_FALSE( ret );

	raft_config_set_server_status( &server_id, VOTING_MEMBER );
	ret = raft_config_set_new_vote( &server_id );	// voting-member
	CHECK_TRUE( ret );
	LONGS_EQUAL( 1, server->voted_for_me );

	ret = raft_config_set_new_vote( &server_id );	// voting again
	CHECK_FALSE( ret );
	LONGS_EQUAL( 1, server->voted_for_me );			// shouldn't change
}

TEST( TestConfigInitialized, SetAndCheckConfigurationChanging ) {
	int ret;

	ret = set_configuration_changing( 1 );	// setting
	CHECK_TRUE( ret );
	CHECK_TRUE( config->is_changing );

	ret = is_configuration_changing( );	// checking
	CHECK_TRUE( ret );

	ret = set_configuration_changing( 1 ); // error, already changing
	CHECK_FALSE( ret );
	CHECK_TRUE( config->is_changing );

	ret = set_configuration_changing( 0 ); // setting
	CHECK_TRUE( ret );
	CHECK_FALSE( config->is_changing );

	ret = is_configuration_changing( );	// checking
	CHECK_FALSE( ret );
}

TEST( TestConfigInitialized, IsServerCaughtUp ) {
	int rounds = 3;
	int progress = -1;
	int ret;
	struct timespec timeout = { .tv_sec = 0, .tv_nsec = 0 };

	// comparing seconds is enough for testing purposes

	server_t *server = raft_config_get_server( &server_id );
	CHECK( server != NULL );
	server->active = RUNNING;	// we have to set it to RUNNING, otherwise it wont be removed from the configuration

	// too many log entries, or server is too slow (reached timeout)
	timeout.tv_sec = 2;
	server->replied_ts.tv_sec = 3;
	ret = is_server_caught_up( server, &rounds, &timeout, &progress );
	CHECK_FALSE( ret );
	LONGS_EQUAL( 0, progress );
	LONGS_EQUAL( 2, rounds );

	// not the server replied within timeout
	server->replied_ts.tv_sec = 1;
	ret = is_server_caught_up( server, &rounds, &timeout, &progress );
	CHECK_FALSE( ret );
	LONGS_EQUAL( 1, progress );	// the server made progress
	LONGS_EQUAL( 1, rounds );

	// the server made progress in last round and replied within timeout again
	ret = is_server_caught_up( server, &rounds, &timeout, &progress );
	CHECK_TRUE( ret );	// now the server is caught-up
	LONGS_EQUAL( 1, progress );	// the server made progress
	LONGS_EQUAL( 0, rounds );

	/*
		the server reached timeout and is in the last round
		the server will be removed from the configuration
	*/
	server->replied_ts.tv_sec = 5;
	ret = is_server_caught_up( server, &rounds, &timeout, &progress );
	CHECK_FALSE( ret );
	// the server has been removed from the configuration
	LONGS_EQUAL( 0, config->size );	// we only have one server in this test, thus, 0 is ok in the test

	free( server );	// we have to free the server, in the real implementation the server thread releases its memory byself
}

/*
	Initialized three servers in the configuration
*/
TEST_GROUP( TestConfigThreeServers ) {
	static const int max_servers = 3;
	server_id_t server_id[max_servers];
	char target[max_servers][64];
	raft_config_t *config = raft_get_config( );

	void setup() {
		int ret;
		for( int i = 0; i < max_servers; i++ ) {
			snprintf( server_id[i], sizeof(server_id_t), "server%d", i + 1 );
			snprintf( target[i], 64, "127.0.0.%d:4560", i + 1 );
			ret = raft_config_add_server( &server_id[i], target[i], 0 );
			LONGS_EQUAL( 1, ret );
		}
	}

	void teardown() {
		for( int i = 0; i < config->size; i++ ) {
			free( config->servers[i] );
		}
		free( config->servers );
		config->servers = NULL;
		config->is_changing = 0;
		config->size = 0;
		config->voting_members = 0;
	}
};

TEST( TestConfigThreeServers, RaftConfigResetVotes ) {
	server_t *server;
	int ret, i;

	for( i = 0; i < max_servers; i++ ) { // voting-member
		raft_config_set_server_status( &server_id[i], VOTING_MEMBER );
		ret = raft_config_set_new_vote( &server_id[i] );
		CHECK_TRUE( ret );

		server = raft_config_get_server( &server_id[i] );
		CHECK( server != NULL );
		LONGS_EQUAL( 1, server->voted_for_me );
	}

	raft_config_reset_votes( );	// resetting all votes

	for( i = 0; i < max_servers; i++ ) {
		server = raft_config_get_server( &server_id[i] );
		LONGS_EQUAL( 0, server->voted_for_me );
	}
}

TEST( TestConfigThreeServers, HasMajorityOfVotes ) {

	config->voting_members = 3;

	CHECK_FALSE( has_majority_of_votes( 0 ) );
	CHECK_FALSE( has_majority_of_votes( 1 ) );
	CHECK_TRUE( has_majority_of_votes( 2 ) );
	CHECK_TRUE( has_majority_of_votes( 3 ) );
}

TEST( TestConfigThreeServers, HasMajorityOfMacthIndex ) {
	server_t *server;
	int i;

	// we assume the number of voting members is 3
	for( i = 0; i < max_servers; i++ ) { // voting-member
		raft_config_set_server_status( &server_id[i], VOTING_MEMBER );
		server = raft_config_get_server( &server_id[i] );
		CHECK( server != NULL );
		server->match_index = i;
	}
	LONGS_EQUAL( max_servers, config->voting_members );

	CHECK_FALSE( has_majority_of_match_index( 3 ) );	// no server has commitIndex==3
	CHECK_TRUE( has_majority_of_match_index( 2 ) );		// one server has at least commitIndex==2, the leader can commit and apply
	CHECK_TRUE( has_majority_of_match_index( 1 ) );		// two servers have at least commitIndex==1, the leader can commit and apply
}

TEST( TestConfigThreeServers, RaftConfigSetAllServerIndexes ) {
	server_t *server;

	raft_config_set_all_server_indexes( 10 );

	for( int i = 0; i < max_servers; i++ ) {
		server = raft_config_get_server( &server_id[i] );
		CHECK( server != NULL );
		LONGS_EQUAL( 11, server->next_index );
		LONGS_EQUAL( 0, server->master_index );
	}
}

TEST( TestConfigThreeServers, GetReplicaServers ) {
	server_t *server;
	replicas_t replicas = { .len = 0, .servers = NULL };

	for( int i = 0; i < max_servers; i++ ) {	// only voting members can be replicas
		raft_config_set_server_status( &server_id[i], VOTING_MEMBER );
		server = raft_config_get_server( &server_id[i] );
		CHECK( server != NULL );
		// we have to set it to RUNNING, otherwise it wont be removed from the configuration
		server->active = RUNNING;
	}
	LONGS_EQUAL( max_servers, config->voting_members );

	// getting the next one, in this case the next is the first one (testing circular search as well)
	get_replica_servers( &server_id[max_servers - 1], &replicas, 1 );
	CHECK( replicas.servers != NULL );
	LONGS_EQUAL( 1, replicas.len );
	STRCMP_EQUAL( server_id[0], replicas.servers[0]->server_id );	// should be the first in the array

	// testing when n_replicas is greater than servers in raft configuration
	get_replica_servers( &server_id[max_servers - 1], &replicas, max_servers + 1 );
	CHECK( replicas.servers != NULL );
	LONGS_EQUAL( max_servers - 1, replicas.len );	// if config.size is 3, then there are up to 2 replicas (max_servers - 1)

	// testing if all servers were corrected picked up as backup replicas
	int primary_idx = max_servers - 1; // the index of the primary server
	int backup_idx = ( primary_idx + 1 ) % ( replicas.len + 1 );	// the index of a backup server
	while( primary_idx != backup_idx ) {
		STRCMP_EQUAL( server_id[backup_idx], replicas.servers[backup_idx]->server_id );
		backup_idx = ( backup_idx + 1 ) % ( replicas.len + 1 );
	}

	// getting replicas when there is only one server in raft configuration
	server = raft_config_get_server( &server_id[2] );
	CHECK( server != NULL );
	raft_config_remove_server( &server_id[2] );
	free( server );// in the real implementation the server thread releases its memory byself

	server = raft_config_get_server( &server_id[1] );
	CHECK( server != NULL );
	raft_config_remove_server( &server_id[1] );
	free( server );// in the real implementation the server thread releases its memory byself

	LONGS_EQUAL( 1, config->size );

	get_replica_servers( &server_id[0], &replicas, 1 );
	CHECK( replicas.servers != NULL );
	LONGS_EQUAL( 0, replicas.len );	// should have no replica server

	// finally freeing the array of replica servers
	free( replicas.servers );
}

TEST( TestConfigThreeServers, RaftGetNumServers ) {
	LONGS_EQUAL( config->size, raft_get_num_servers( ) );
}
