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
	Mnemonic:	config_test.c
	Abstract:	Implements unit tests for the config module of the RFT library.
				NOTE: not all functions have their respective unit coverage
				tests implemented so far.

	Date:		24 November 2019
	Author:		Alexandre Huff
*/

#include <stdio.h>
#include <time.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>

#include <rmr/rmr.h>

#include "types.h"
#include "config.h"
#include "rft.h"
#include "rft_private.h"
#include "logger.h"


long raft_servers = 3;	// tests take into account that one of this servers is "me". default = 3
raft_config_t *config = NULL;
server_state_t *me = NULL;

static void mk_rt( ) {
	int 	fd;
	char	buf[128];
	char*	contents = "newrt|start\nmse | 100 | -1 | localhost:4560\nnewrt|end\n";

	snprintf( buf, sizeof( buf ), "/tmp/test.rt" );
	fd = open( buf, O_CREAT | O_WRONLY, 0664 );
	if( fd < 0 ) {
		fprintf( stderr, "unable to create test routing table: %s %s\n", buf, strerror( errno ) );
		return;
	}

	write( fd, contents, strlen( contents ) );
	if( (close( fd ) < 0 ) ) {
		fprintf( stderr, "unable to close test routing table: %s: %s\n", buf, strerror( errno ) );
		return;
	}

	setenv( "RMR_SEED_RT", buf, 1 );
}

void test_raft_config_add_server( ) {
	int i;
	server_id_t server_id;
	server_id_t target;

	for ( i = 0; i < raft_servers; i++ ) {
		snprintf( server_id, sizeof( server_id_t ), "server%d", i + 10 );
		snprintf( target, sizeof( server_id_t ), "127.0.0.%d:4560", i + 10 );

		raft_config_add_server( &server_id, target, 0 );

		assert( config->size == i+1 );
		assert( strcmp( (*config->servers[i]).server_id, server_id ) == 0 );
		assert( strcmp( (*config->servers[i]).target, target ) == 0 );
	}

	// trying to insert the last server again
	raft_config_add_server( &server_id, target, 0 );
	assert( config->size == i );
}

void test_raft_config_remove_server( ) {
	int i, index;
	server_id_t server_id;
	srand( time( NULL ) );

	i = config->size;
	while( config->size > 0 ) {
		index = rand( ) % config->size;
		snprintf( server_id, sizeof( server_id_t ), "%s", config->servers[index]->server_id );

		raft_config_remove_server( &server_id );
		i--;

		assert( config->size == i );
	}
}

void test_raft_config_get_server( ) {
	int i;
	raft_server_t *server;
	server_id_t server_id;
	server_id_t target;

	for ( i = 0; i < config->size; i++ ) {
		snprintf( server_id, sizeof( server_id_t ), "server%d", i + 10 );
		snprintf( target, sizeof( server_id_t ), "127.0.0.%d:4560", i + 10 );

		server = raft_config_get_server( &server_id );

		assert( strcmp( server->server_id, server_id ) == 0 );
		assert( strcmp( server->target, target ) == 0 );
	}

	snprintf( server_id, sizeof( server_id_t ), " " );	// getting a server that it is not in configuration
	server = raft_config_get_server( &server_id );
	assert( server == NULL );
}

void test_raft_config_set_new_vote( ) {
	int i, changed;
	int is_new_vote;
	server_id_t server_id;

	for( i = 0; i < config->size; i++ ) {
		snprintf( server_id, sizeof( server_id_t ), "server%d", i + 10 );

		is_new_vote = raft_config_set_new_vote( &server_id );
		assert( is_new_vote == 0 ); // server is non voting member up to here, so no vote should be accounted for

		changed = raft_config_set_server_status( &server_id, VOTING_MEMBER );
		assert( changed == 1 );	// server status should have changed to voting member here

		is_new_vote = raft_config_set_new_vote( &server_id );
		assert( is_new_vote == 1 );	// now server has to grant its vote, since it is a vonting member and no vote was issued previously
	}

	is_new_vote = raft_config_set_new_vote( &server_id );
	assert( is_new_vote == 0 ); // server already voted, no vote should be accounted for
}

/*
	The purpose of this function is only to reset all server to NON_VOTING_MEMBER status before
	testing the has_majority_of_votes function
*/
void set_servers_to_non_voting_members( ) {
	int i;

	for( i = 0; i < config->size; i++ ) {
		if( config->servers[i]->status == VOTING_MEMBER ) {
			raft_config_set_server_status( &config->servers[i]->server_id, NON_VOTING_MEMBER );
		}
	}
	assert( config->voting_members == 0 );
}

void test_has_majority_of_votes( ) {
	int i;
	server_id_t server_id;

	set_servers_to_non_voting_members( );	// reseting to non-voting members

	for( i = 0; i < raft_servers; i++ ) {
		snprintf( server_id, sizeof( server_id_t ), "server%d", i + 10 );

		raft_config_set_server_status( &server_id, VOTING_MEMBER );

		if( i < ( raft_servers / 2 ) )	// granting half of the votes to the server, including itself vote
			if ( raft_config_set_new_vote( &server_id ) )
				me->rcv_votes++;
	}
	assert( has_majority_of_votes( me->rcv_votes ) == 0 );	// do not have majority of votes

	// granting the required vote in the last server to become leader
	if ( raft_config_set_new_vote( &server_id ) )
		me->rcv_votes++;

	assert( has_majority_of_votes( me->rcv_votes ) != 0 );	// it should be the leader
}

void test_reset_server_votes( ) {
	int i, voted_for_me;

	raft_config_reset_votes( );

	for ( i = 0; i < config->size; i++ ) {
		voted_for_me = (*config->servers[i]).voted_for_me;
		assert( voted_for_me == 0 );
	}
}

int main(int argc, char const *argv[]) {
	char *endptr;
	void *mrc;

	if( argc > 2 ) {
		fprintf(stderr, "Usage: %s [num_servers]\n", argv[0]);
		exit( 1 );
	}

	if( argc == 2)
		raft_servers = strtol( argv[1], &endptr, 10 );

	mk_rt( );

	mrc = rmr_init( "4560", RMR_MAX_RCV_BYTES, RMRFL_NONE );
	assert( mrc != NULL );

	rft_init( mrc, "4560", RMR_MAX_RCV_BYTES, NULL );
	usleep( 1000000 );	// needs waiting for worker and trigger_election_timeout threads starting up

	me = get_me( );
	me->state = LEADER;

	config = raft_get_config( );

	test_raft_config_add_server( );

	test_raft_config_get_server( );

	test_raft_config_set_new_vote( );

	test_reset_server_votes( );	 // done before test majority of votes, so no server has voted at that time

	test_has_majority_of_votes( );	// set votes and tests

	test_reset_server_votes( );

	test_raft_config_remove_server( );

	fprintf( stderr, "\nTest config.c: OK for %ld servers!\n\n", raft_servers );

	return 0;
}

