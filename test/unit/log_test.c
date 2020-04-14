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
	Mnemonic:	log_test.c
	Abstract:	Implements unit tests for the log module of the RFT library.
				NOTE: not all functions have their respective unit coverage
				tests implemented so far.

	Date:		27 November 2019
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

#include "rft.h"
#include "rft_private.h"
#include "log.h"
#include "logger.h"

#define CMD (i % 2)


unsigned int max_entries = 130;	// the default number of entries that this test will run

log_entries_t *log_entries = NULL;

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

// tests append_raft_log_entry and get_raft_log_entry
void test_append_raft_log_entry( ) {
	unsigned int i;
	log_entry_t *entry = NULL;
	server_conf_cmd_data_t config_data;	// RAFT_CONFIG data
	int cmd_data;						// RAFT_COMMAND data
	server_conf_cmd_data_t buf_data;	// tmp buffer to compare data

	entry = get_raft_log_entry( 0 ); // trying to get a value in an empty log
	assert( entry == NULL );

	logger_info( "adding %u log entries", max_entries );

	for( i = 0; i < max_entries; i++ ) {

		if( CMD ) {	// RAFT_COMMAND
			cmd_data = i;
			entry = new_raft_log_entry( 1, RAFT_COMMAND, i, &cmd_data, sizeof( cmd_data ) );
			assert( entry != NULL );
		} else {	// RAFT_CONFIG
			snprintf( config_data.server_id, sizeof( server_id_t ), "server%d", i + 10 );
			snprintf( config_data.target, sizeof( server_id_t ), "127.0.0.%d:4560", i + 10 );
			entry = new_raft_log_entry( 1, RAFT_CONFIG, ADD_MEMBER, &config_data, sizeof( server_conf_cmd_data_t ) );
			assert( entry != NULL );
		}

		append_raft_log_entry( entry );

		assert( log_entries->head == i + 1 );
	}

	// it should be doubled when it runs out of size
	if( log_entries->size > INITIAL_LOG_ENTRIES )
		assert( log_entries->size != INITIAL_LOG_ENTRIES );
	else
		assert( log_entries->size == INITIAL_LOG_ENTRIES );

	entry = get_raft_log_entry( max_entries + 1 );	// trying to get a value higher than last log index
	assert( entry == NULL );

	for ( i = 0; i < max_entries; i++ ) {	// testing all insertions
		entry = get_raft_log_entry( i + 1 );

		assert( entry->index == i + 1 );
		assert( entry->term == 1 );

		if( CMD ) {	// RAFT_COMMAND
			assert( entry->type == RAFT_COMMAND );
			assert( entry->command == i );
			assert( entry->dlen == sizeof( cmd_data ) );
			memcpy( &cmd_data, entry->data, entry->dlen );
			assert( cmd_data == i );
		} else {	// RAFT_CONFIG
			snprintf( buf_data.server_id, sizeof( server_id_t ), "server%d", i + 10 );
			snprintf( buf_data.target, sizeof( server_id_t ), "127.0.0.%d:4560", i + 10 );

			assert( entry->type == RAFT_CONFIG );
			assert( entry->command == ADD_MEMBER );
			assert( entry->dlen == sizeof( server_conf_cmd_data_t ) );

			memcpy( &config_data, entry->data, entry->dlen );
			assert( strcmp( config_data.server_id, buf_data.server_id ) == 0 );
			assert( strcmp( config_data.target, buf_data.target ) == 0 );
		}
	}
}

void test_serialization( ) {
	unsigned int i;
	unsigned int bytes;
	unsigned int buf_len = 512;	// initialized with 512 bytes, it can be reallocked by the serializer function
	unsigned char *lbuf = (unsigned char *) malloc( buf_len );
	log_entry_t **entries = (log_entry_t **) malloc( sizeof( log_entry_t **) * max_entries );
	log_entry_t *entry = NULL;
	server_conf_cmd_data_t config_data;	// RAFT_CONFIG data
	int cmd_data;						// RAFT_COMMAND data
	server_conf_cmd_data_t buf_data;	// tmp buffer to compare data

	logger_info( "serializing %u log entries", max_entries );

	bytes = serialize_raft_log_entries( 1, &max_entries, &lbuf, &buf_len, RMR_MAX_RCV_BYTES );
	logger_info( "serialized bytes: %u", bytes );

	logger_info( "deserializing %u log entries", max_entries );
	deserialize_raft_log_entries( lbuf, max_entries, entries );

	for( i = 0; i < max_entries; i++) {
		entry = entries[i];

		assert( entry->term == 1 );
		assert( entry->index == i + 1 );
		assert( entry->dlen == log_entries->entries[i]->dlen);

		if( CMD ) {	// RAFT_COMMAND
			assert( entry->type == RAFT_COMMAND );
			assert( entry->command == i );
			assert( entry->dlen == sizeof( cmd_data ) );
			memcpy( &cmd_data, entry->data, entry->dlen );
			assert( cmd_data == i );

			logger_info( "type: %d, command: %d, cmd_data: %d", entry->type, entry->command, cmd_data );
		} else {	// RAFT_CONFIG
			snprintf( buf_data.server_id, sizeof( server_id_t ), "server%d", i + 10 );
			snprintf( buf_data.target, sizeof( server_id_t ), "127.0.0.%d:4560", i + 10 );

			assert( entry->type == RAFT_CONFIG );
			assert( entry->command == ADD_MEMBER );
			assert( entry->dlen == sizeof( server_conf_cmd_data_t ) );

			memcpy( &config_data, entry->data, entry->dlen );
			assert( strcmp( config_data.server_id, buf_data.server_id ) == 0 );
			assert( strcmp( config_data.target, buf_data.target ) == 0 );

			logger_info( "type: %d, command: %d, server_id: %s, target: %s", entry->type, entry->command, config_data.server_id, config_data.target );
		}
	}
}

void test_remove_conflicting_entries( ) {

	remove_raft_conflicting_entries( 1, me );	// remove all entries of the log
	assert( get_raft_last_log_index( ) == 0 );

	test_append_raft_log_entry( );	// adding all logs again
	me->commit_index = 1;			// setting commit to log index 1, this log entry cannot be removed from the log
	remove_raft_conflicting_entries( 1, me );
	assert( get_raft_last_log_index( ) == max_entries );

}

int main(int argc, char const **argv) {
	char *endptr;
	int ai = 1;
	short usage = 0;
	void *mrc;

	while( ai < argc ) {
		if( *argv[ai] == '-' ) {
			switch( argv[ai][1] ) {
				case 'n':
					ai++;
					max_entries = (unsigned int) strtol( argv[ai], &endptr, 10 );
					break;

				default:
					usage = 1;
			}
			ai++;
		} else {
			usage = 1;
		}

		if( usage ) {
			fprintf(stderr, "Usage: %s [-n log_entries]\n", argv[0]);
			exit( 1 );
		}
	}

	mk_rt( );

	mrc = rmr_init( "4560", RMR_MAX_RCV_BYTES, RMRFL_NONE );
	assert( mrc != NULL );

	rft_init( mrc, "4560", RMR_MAX_RCV_BYTES, NULL );
	usleep( 1000000 );	// needs waiting for worker and trigger_election_timeout threads starting up

	log_entries = get_raft_log( );
	me = get_me( );

	test_append_raft_log_entry( );

	test_serialization( );

	test_remove_conflicting_entries( );

	printf( "\nTest log.c: OK\n\n" );

	return 0;
}

