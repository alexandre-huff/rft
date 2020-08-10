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
	Mnemonic:	rft_test.c
	Abstract:	Implements unit tests for the main module of the RFT library.
				NOTE: not all functions have their respective unit coverage
				tests implemented so far.

	Date:		23 November 2019
	Author:		Alexandre Huff
*/

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <rmr/rmr.h>

#include "types.h"
#include "rft.h"
#include "rft_private.h"
#include "logger.h"
#include "config.h"


long raft_servers = 3;	// tests take into account that one of this servers is "me". default = 3
raft_state_t *me = NULL;
raft_config_t *raft_config = NULL;

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

void test_handle_membership_request( ) {
	int i;
	membership_request_t msg;
	server_id_t target;

	// Must configure to LEADER to test server threads in raft configuration
	me->state = LEADER;

	msg.last_log_index = 0;
	for (i = 0; i < raft_servers; i++ ){
		snprintf(msg.server_id, sizeof( server_id_t ), "server%d", i + 10 );
		snprintf( target, sizeof( server_id_t ), "127.0.0.%d:4560", i + 10 );
		handle_membership_request( &msg, target );

		assert( raft_config->size == i+1 );		 // server_id is new... so it should be inserted
		assert( strcmp( (*raft_config->servers[i]).server_id, msg.server_id ) == 0 );
		assert( strcmp( (*raft_config->servers[i]).target, target ) == 0 );
	}

	handle_membership_request( &msg, "localhost:4560" );
	assert( raft_config->size == raft_servers );	// this server could not be inserted (server_id alredy was inserted)

	usleep( 1000000 ); // let threads work a bit

	me->state = FOLLOWER;	// simulating step down

	for (i = raft_servers - 1; i >= 0; i-- ) {
		raft_config_remove_server( &(*raft_config->servers[i]).server_id );
		assert( raft_config->size == i );
	}
}

void test_rft_init( void *mrc, char *port ) {

	rft_init( mrc, port, RMR_MAX_RCV_BYTES, NULL );
}

int main(int argc, char const *argv[]) {
	char *endptr;
	void *mrc = NULL;
	char *port = "4560";

	if( argc > 2 ) {
		fprintf(stderr, "Usage: %s [num_servers]\n", argv[0]);
		exit( 1 );
	}

	if( argc == 2 )
		raft_servers = strtol( argv[1], &endptr, 10 );

	mk_rt( );

	mrc = rmr_init( port, RMR_MAX_RCV_BYTES, RMRFL_NONE );
	assert( mrc != NULL );
	logger_trace( "test main() mrc: %p", mrc );
	set_mrc( mrc );

	test_rft_init( mrc, port );
	usleep( 1000000 );	// needs waiting for worker and trigger_election_timeout threads starting up

	me = get_me( );
	raft_config = raft_get_config( );

	test_handle_membership_request( );

	printf( "\nTest raft.c: OK for %ld servers!\n\n", raft_servers );

	return 0;
}
