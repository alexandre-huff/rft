// :vim ts=4 sw=4 noet:
/*
==================================================================================
	Copyright (c) 2019-2020 AT&T Intellectual Property.
	Copyright (c) 2019-2020 Alexandre Huff.

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
	Mnemonic:	config.c
	Abstract:	Implements raft cluster configuration and membership

	Date:		24 November 2019
	Author:		Alexandre Huff
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "types.h"
#include "rft.h"
#include "rft_private.h"
#include "logger.h"
#include "utils.h"
#include "config.h"
#include "mtl.h"
#include "snapshot.h"
#include "log.h"


pthread_mutex_t config_lock = PTHREAD_MUTEX_INITIALIZER;

static raft_config_t config = {
	.size = 0,
	.voting_members = 0,
	.is_changing = 0,
	.servers = NULL
};

/*
	Only used for testing purposes, should not be into the public rft.h header
*/
raft_config_t *raft_get_config( ) {
	return &config;
}

/*
	Acquires the mutex of the raft configuration
*/
void lock_raft_config( ) {
	pthread_mutex_lock( &config_lock );
}

/*
	Releases the mutex of the raft configuration
*/
void unlock_raft_config( ) {
	pthread_mutex_unlock( &config_lock );
}

/*
	Returns the amount of server in the raft config
*/
unsigned int raft_get_num_servers( ) {
	unsigned int n;
	pthread_mutex_lock( &config_lock );
	n = config.size;
	pthread_mutex_unlock( &config_lock );
	return n;
}

/*
	Gets a server instance from the raft configuration

	Returns the server pointer if found, NULL otherwise

	This is an internal function of the config module and must be called by a wrapper funtion
	inside this module

	IMPORTANT: This function is not thread-safe, it assumes that the caller owns the
	configuration lock
*/
static inline server_t *get_server( server_id_t *server_id ) {
	unsigned int i;
	assert( server_id != NULL );

	for ( i = 0; i < config.size; i++ )
		if( strcmp( config.servers[i]->server_id, *server_id ) == 0 )
			return config.servers[i];

	return NULL;
}

/*
	Gets a server from the raft configuration

	Returns the server pointer if found, NULL otherwise
*/
server_t *raft_config_get_server( server_id_t *server_id ) {
	server_t *server = NULL;

	pthread_mutex_lock( &config_lock );

	server = get_server( server_id );

	pthread_mutex_unlock( &config_lock );

	return server;
}

/*
	Adds a new server to the raft configuration as non-voting member

	Leader add server cluster configuration from membership requests
	Followers add servers in their configuration from AppendEntries messages

	On success, returns 1. On error, 0 is returned.
	An error means: if the server_id was added in a previous call, or the server's thread could not be created
	If server's thread was not created, then the server is removed from configuration

	Note: This is an internal function of the config module and must be called by a wrapper funtion
	inside this module

	IMPORTANT: This function is not thread-safe, it assumes that the caller owns the
	configuration lock
*/
static inline int add_server( server_id_t *server_id, target_t *target, index_t last_log_index ) {
	int ret;
	server_t *server = NULL;

	server = get_server( server_id );
	if( server != NULL ) {		// checking if this server was already added to the configuration
		logger_warn( "server %s was added previously in the cluster configuration", *server_id );

		pthread_mutex_lock( &server->index_lock );	// lock required to modify match_index and next_index
		/* we have to set it again, since server was not removed from the cluster
			so, we ensure restarting to send right log entries as soon as possible
			and the follower will not try to apply the same command twice
		*/
		server->match_index = last_log_index;
		server->next_index = last_log_index + 1;
		/*
			last_log_index is ok since the server has crashed or restarted, thus master index is 0 in membership request
			note that append_entries does not add a raft server if it was already added
		*/
		server->master_index = last_log_index;
		pthread_mutex_unlock( &server->index_lock );

		return 0;
	}

	// if we got here, raft server is a new one
	logger_debug( "adding server %s in raft configuration as NON_VOTING_MEMBER", *server_id );

	server = (server_t *) malloc( sizeof( server_t ) );
	if ( server == NULL ) {
		logger_fatal( "unable to allocate memory for a new raft_server: %s", strerror( errno ) );
		exit( 1 );
	}

	/*
		This approach is expensive to add/del servers from the configuration, but it is not too frequently executed
		However, it allows to take advantage of processor's cache to iterate over the array to search for a server/data
		Linked List would have a lot of cache miss
	*/
	config.servers = (server_t **) realloc( config.servers, sizeof( server_t** ) * ( config.size + 1 ) );
	if( config.servers == NULL ) {
		logger_fatal( "unable to allocate memory for the server configuration array: %s", strerror( errno ) );
		exit( 1 );
	}

	config.servers[config.size] = server;

	config.size++;

	server->status = NON_VOTING_MEMBER;
	server->match_index = last_log_index;
	server->next_index = last_log_index + 1;
	pthread_mutex_init( &server->index_lock, NULL );

	server->voted_for_me = 0;
	memccpy( server->server_id, *server_id, '\0', sizeof(server_id_t) - 1 );
	server->server_id[sizeof(server_id_t) - 1] = '\0';
	memccpy( server->target, *target, '\0', sizeof(target_t) - 1 );
	server->target[sizeof(target_t) - 1] = '\0';
	server->replied_ts.tv_sec = 0;
	server->replied_ts.tv_nsec = 0;
	server->hb_timeouts = 0;
	server->replica_index = 0;
	server->master_index = 0;
	server->active = SHUTDOWN;

	/*
		does not need to create a thread server for myself (as I do not need to replicate logs to myself)
	*/
	if( strcmp( *server_id, *get_myself_id( ) ) != 0 ) {
		ret = pthread_create( &server->th_id, NULL, raft_server, (void *) server ); // starting thread to send Append Entries
		if( ret != 0 ) {
			logger_error( "unable to create thread for server %s", server_id );
			raft_config_remove_server( server_id );
			return 0;
		}
	}

	return 1;
}

/*
	Adds a new server to the raft configuration as non-voting member

	Leader add server cluster configuration from membership requests
	Followers add servers in their configuration from AppendEntries messages

	On success, returns 1. On error, 0 is returned.
	An error means: if the server_id was added in a previous call, or the server's thread could not be created
	If server's thread was not created, then the server is removed from configuration
*/
int raft_config_add_server( server_id_t *server_id, target_t *target, index_t last_log_index ) {
	int ret;

	// This is a wrapper function as we have to lock the configuration mutex
	if( target == NULL ) {
		logger_error( "invalid target: null?" );
		return 0;
	}

	pthread_mutex_lock( &config_lock );

	ret = add_server( server_id, target, last_log_index );

	pthread_mutex_unlock( &config_lock );

	return ret;
}

/*
	Removes a server from the raft configuration

	A server is removed from the raft configuration when a remove membership log entry is
	added to the log, or when removing conflicting log entries

	Note: This is an internal function of the config module and must be called by a wrapper function
	inside this module

	IMPORTANT: This function is not thread-safe, it assumes that the caller owns the
	configuration lock
*/
static inline void remove_server( server_id_t *server_id ) {
	unsigned int i;
	server_t *server;

	server = get_server( server_id );

	if( server != NULL ) {

		logger_debug( "removing server %s from raft configuration", server_id );

		// just ensuring that the removed server won't be accounted for votes after removal
		if( server->status == VOTING_MEMBER ) {
			config.voting_members--;	// it also may be decremented on setting its state to non-voting member
		}

		if( strcmp( *server_id, *get_myself_id( ) ) != 0 ) {	// if it's not me send signals
			// required checking if the pthread conditions were setup before send signals (avoid segfault)
			while( server->active == SHUTDOWN ) {
				logger_warn( "waiting thread for server %s becoming active", server_id );
				usleep( 100 );
			}

			server->active = SHUTDOWN;	// eventually that thread will be awaked and will terminate its execution

			pthread_cond_signal( server->heartbeat_cond );
			pthread_cond_broadcast( server->leader_cond );

			// logger_debug( "joining thread of %s server", server_id );
			// pthread_join( server->th_id, NULL );	// be careful, it can lead to deadlocks if uncommented
			// logger_debug( "thread of %s server terminated successfully", server_id );
		}

		for( i = 0; i < config.size; i++ )
			if( config.servers[i] == server )	// finding the index of the server in the configuration array
				break;
		/*
			moves the remaining of the server pointers at the position of the deleted one, if trying to delete
			the last one, no memory is moved

			This approach is expensive to add/del servers from the configuration, but it is not too frequently executed
			However, this approach allows take advantage of processor's cache to iterate over the array to search the servers' pointer
			Linked List would have a lot of cache miss
		*/
		memmove( &config.servers[i], &config.servers[i + 1], sizeof( server_t** ) * ( config.size - i - 1 ) );

		config.size--;

		/*
			This approach is expensive to add/del servers from the configuration, but it is not too frequently executed
			However, this approach allows take advantage of processor's cache to iterate over the array to search the servers' pointer
			Linked List would have a lot of cache miss
		*/
		config.servers = (server_t **) realloc( config.servers, sizeof( server_t** ) * config.size );

		if( ( config.servers == NULL ) && ( config.size > 0 ) ) {
			logger_fatal( "unable to shrink memory from the server configuration array" );
			exit( 1 );
		}

	} else {
		logger_warn( "server %s not found in raft configuration", server_id );
	}
}

/*
	Removes a server from the raft configuration

	A server is removed from the raft configuration when a remove membership log entry is
	added to the log, or when removing conflicting log entries
*/
void raft_config_remove_server( server_id_t *server_id ) {
	// This is a wrapper function as we have to lock the configuration mutex
	pthread_mutex_lock( &config_lock );

	remove_server( server_id );

	pthread_mutex_unlock( &config_lock );
}

/*
	Defines if this server can be accounted for quorum (NON_VOTING_MEMBER or VOTING_MEMBER)
	Also increment/decrement value of voting_members according to the status

	This is an internal function of the config module and must be called by a wrapper funtion
	inside this module

	IMPORTANT: This function is not thread-safe, it assumes that the caller owns the
	configuration lock

	Returns 1 if the server was found and the status was changed or had been previously configured.
	On error, 0 is returned and errno is set to indicate the error.
	Error codes:
		- ENODATA: server not found
		- EINVAL: invalid status argument
*/
static inline int set_server_voting_status( server_id_t *server_id, raft_voting_member_e status ) {
	server_t *server;

	if( ( status != VOTING_MEMBER ) && ( status != NON_VOTING_MEMBER ) ) {
		logger_error( "invalid raft voting status: %d", status );
		errno = EINVAL;
		return 0;
	}

	server = get_server( server_id );
	if( server != NULL ) {
		if( server->status != status ) {	// we can only increment/decrement if the state has changed
			if( status == VOTING_MEMBER ) {
				assert( config.voting_members < config.size );
				config.voting_members++;
				logger_debug( "changed status of raft server %s to VOTING_MEMBER", server->server_id );

			} else {
				assert( config.voting_members > 0 );
				config.voting_members--;
				logger_debug( "changed status of raft server %s to NON_VOTING_MEMBER", server->server_id );
			}
			server->status = status;
		}

	} else {	// server not found
		errno = ENODATA;
		return 0;
	}

	return 1;
}

/*
	Verifies if the server_id has not voted for me yet

	If server_id is a voting member and has not granted its vote to me, than voted for me is set

	Returns 1 if this vote is new in raft configuration

	NOTE: Vote counter in "me" is not incremented by this function
*/
int raft_config_set_new_vote( server_id_t *server_id ) {
	int is_new_vote = 0;
	server_t *server = NULL;

	pthread_mutex_lock( &config_lock );

	server = get_server( server_id );

	if( server && ( server->status == VOTING_MEMBER ) && ( !server->voted_for_me ) ) {
		server->voted_for_me = 1;
		is_new_vote = 1;
	}

	pthread_mutex_unlock( &config_lock );

	return is_new_vote;
}

/*
	Defines if this server can be accounted for quorum (NON_VOTING_MEMBER or VOTING_MEMBER)
	Also increment/decrement value of voting_members according to the status

	Return 1 if server found and status was changed, 0 otherwise
*/
int raft_config_set_server_status( server_id_t *server_id, raft_voting_member_e status ) {
	/*
		This is a wrapper function used to avoid duplicate the code of the set_server_voting_status function,
		which can be called from different functions from this module
	*/
	int changed;

	pthread_mutex_lock( &config_lock );

	changed = set_server_voting_status( server_id, status );

	pthread_mutex_unlock( &config_lock );

	return changed;
}

/*
	Checks if the running server has majority of votes (quorum) to become leader

	Includes its own vote in the checking

	Returns != 0 if have majority of votes, 0 otherwise

	Called by the candidate
*/
int has_majority_of_votes( unsigned int rcv_votes ) {
	int has_quorum;

	pthread_mutex_lock( &config_lock );

	if( rcv_votes > config.voting_members ) {
		logger_fatal( "received more votes than available voting members" );
		exit( 1 );
	}

	// supposed that itself vote is accounted for rcv_votes and "me" is also accounted for voting_members
	has_quorum = ( rcv_votes > ( config.voting_members / 2 ) );

	pthread_mutex_unlock( &config_lock );

	return has_quorum;
}

/*
	Checks if the input param match_index is replicated in the majority of voting members

	Called by the LEADER

	a majority of matchIndex[i] >= N (in fig. 2 raft paper)
*/
int has_majority_of_match_index( index_t match_index ) {
	int has_majority;
	unsigned int i;
	unsigned int count = 0;
	server_t *server;

	pthread_mutex_lock( &config_lock );

	for( i = 0; i < config.size; i++ ) {
		server = config.servers[i];
		if( server->status == VOTING_MEMBER ) {
			if( server->match_index >= match_index ) {
				count++;
			}
		}
	}

	/*
		we assume that the match_index field of the LEADER server in raft configuration is equals to 0
		all match_index's fields are initialized to 0 each time a server becomes leader
		so, in this case we have to count + 1 as we are the leader and this match is not up-to-date in servers configuration
		this approach is an optimization to save CPU cycles
	*/
	has_majority = ( ( count + 1 ) > ( config.voting_members / 2 ) );

	pthread_mutex_unlock( &config_lock );

	return has_majority;
}

/*
	Sets voted_for_me to 0 in all servers of the configuration

	Called by the candidate

	NOTE: The counter of received votes for the running server (me) is not initialized in this function
*/
void raft_config_reset_votes( ) {
	unsigned int i;
	server_t **server;

	pthread_mutex_lock( &config_lock );

	server = config.servers;
	for ( i = 0; i < config.size ; i++, server++ ) {
		(*server)->voted_for_me = 0;
	}

	pthread_mutex_unlock( &config_lock );
}

/*
	Resets next_index and match_index in all servers of the configuration

	Called by the leader when it becomes leader
*/
void raft_config_set_all_server_indexes( index_t raft_last_log_index ) {
	unsigned int i;
	server_t **server;

	pthread_mutex_lock( &config_lock );

	server = config.servers;
	for ( i = 0; i < config.size ; i++, server++ ) {
		/*
			next_index: must be last log index + 1
			match_index will be initialized to 0, according of Fig. 3.1 in raft dissertation
		*/
		(*server)->next_index = raft_last_log_index + 1;
		(*server)->match_index = 0;
	}

	pthread_mutex_unlock( &config_lock );

}

/*
	Checks if a new server is caught-up up to a max number of rounds

	if the server makes progress in last round and replied before heartbeat timeout of current round, then
	we assume that server was able to catch-up all the logs

	if the limit of rounds is reached, then this server will no longer receive append entries, and
	it needs to send a new request membership message

	if rounds are over, then the server is removed from the raft configuration

	Returns 1 if server is caught-up, 0 otherwise
*/
int is_server_caught_up( server_t *server, int *rounds, struct timespec *heartbeat_timeout, int *progress ) {

	(*rounds)--;
	if( timespec_cmp( server->replied_ts, *heartbeat_timeout, > ) ) {
		// server did not reply whithin a heartbeat timeout (too many log entries or server is slow)
		*progress = 0;

	} else {
		if ( *progress == 1 ) {
			return 1;

		}
		*progress = 1;
	}

	if( *rounds < 1 ) {
		// Reached the limit of rounds for catching-up, we assume that this server is too slow to join to the cluster
		raft_config_remove_server( &server->server_id );	// this will set server status to SHUTDOWN
	}

	return 0;
}

/*
	Sets the cluster configuration status

	Setting to any value different of 0 means that a configuration is is progress

	IMPORTANT: only the leader threads needs to set this flag, other types of server do not need it

	This implies that no other server can be added to the cluster up to this
	function is called again passing the 0 argument

	It it the caller's responsibility to check if this configuration has been setup correctly

	Returns 1 if the configuration has been setup and configuration can proceed, 0 otherwise
*/
int set_configuration_changing( int is_changing ) {
	int ret;

	pthread_mutex_lock( &config_lock );
	if( config.is_changing && is_changing == 1 ) {	// do not allow setup a new a configuration if another is in progress
		ret = 0;
	} else {
		config.is_changing = is_changing;
		ret = 1;

		logger_info( "membership configuration %s", is_changing ? "in progress" : "finished" );
	}
	pthread_mutex_unlock( &config_lock );

	return ret;
}

/*
	Returns 1 if configuration is in progress, or 0 if there is no ongoing cluster configuration
*/
int is_configuration_changing( ) {
	int ret;

	pthread_mutex_lock( &config_lock );

	ret = config.is_changing ? 1 : 0;

	pthread_mutex_unlock( &config_lock );

	return ret;
}

/*
	Picks out replica servers from the configuration to work as the backup instances

	Not thread-safe: assumes that the caller owns the "replica_lock"

	Note: Only voting members are selected, since cathing-up servers can be removed from array before
	being added/committed/applied to the cluster

	n_replicas:	defines the maximum number of backup replicas to be choosen

	replicas: stores in *replicas the chosen server(s) picked up to work as backup replica(s), or NULL if no available server
				the number of available replicas is stored in the "len" field

	IMPORTANT: all servers run this function when applying raft configuration
*/
void get_replica_servers( server_id_t *me_self_id, replicas_t *replicas, unsigned int n_replicas ) {
	server_t *server = NULL;
	unsigned int i;
	unsigned int myidx = 0;
	unsigned int count = 0;		// counter of replica servers

	assert( me_self_id != NULL );
	assert( replicas != NULL );

	pthread_mutex_lock( &config_lock );

	if( n_replicas > config.size )	// checking size of n_replicas to avoid reallocate unneeded memory
		n_replicas = config.size - 1;

	// if only one server, there is no replica (this happens when bootstraping the cluster)
	if( config.size > 1 && n_replicas ) {	// n_replicas = 0 would break our code with segfault on replica servers array

		replicas->servers = (server_t **) realloc( replicas->servers, n_replicas * sizeof( server_t **) );
		if( replicas->servers == NULL ) {
			logger_fatal( "unable to reallocate memory for replica servers" );
			exit( 1 );
		}

		for( i = 0; i < config.size; i++ ) {

			if( strcmp( *me_self_id, config.servers[i]->server_id ) == 0 ) {
				myidx = i;
				break;	// found myself index in the configuration array
			}
		}

		i = (i + 1) % config.size;	// circular search
		while( i != myidx && count < n_replicas ) {
			server = config.servers[i];
			if( server->status == VOTING_MEMBER ) {	// only voting members can be replica servers
				replicas->servers[count] = server;
				count++;
			}

			i = (i + 1) % config.size;	// circular search
		}
	}
	replicas->len = count;

	/* temporary code, should be removed soon */
#if LOGGER_LEVEL >= LOGGER_DEBUG
	char wbuf[2048], sbuf[128];

	snprintf( wbuf, 2, "|");

	for( i = 0; i < config.size; i++ ) {
		snprintf( sbuf, 128, " %s |", config.servers[i]->server_id );
		strncat( wbuf, sbuf, sizeof(wbuf) - strlen( wbuf ) - 1 );

	}
	logger_debug( "array of servers: %s", wbuf );

	snprintf( wbuf, 2, "|");
	for( i = 0; i < replicas->len; i++ ) {
		snprintf( sbuf, 128, " %s |", replicas->servers[i]->server_id );
		strncat( wbuf, sbuf, sizeof(wbuf) - strlen( wbuf ) - 1 );
	}
	if( i )
		logger_debug( "replica  servers: %s", wbuf );
#endif
	/* temporary code up to here */

	pthread_mutex_unlock( &config_lock );
}

/*
	Creates a new raft configuration snaphot

	Information of all servers in the cluster is serialized in an array of bytes
	to be sent over the network to another cluster member (often new members)
	The information of all servers is serialized in the same order as they were added to the cluster.
	This function is meant to be called by the child process while taking the snapshot

	Parameters:
		data: array of bytes in which the snapshot will be serialized into. It is reallocated by this function.
		raft_metadata: stores metadata information regarding the raft state stored in the serialized bytes.

	Both data and raft_metadata are sent over the network through the send_raft_snapshot function
*/
void create_raft_config_snapshot( unsigned char **data, pipe_metabuf_t *raft_metadata ) {
	unsigned int i;
	server_t *server;
	raft_server_snapshot_t *rserver;	// snapshot server
	size_t offset = 0;
	/*
		we do not need to lock things here since the snapshot is taken by the child process, and
		only the snapshot thread is running after the fork system call
	*/
	*data = (unsigned char *) realloc( *data, config.size * sizeof(raft_server_snapshot_t) );
	if( data == NULL ) {
		logger_fatal( "unable to reallocate buffer to take raft snapshot" );
		exit( 1 );
	}

	/*
		server_id | target | raft_voting_member_e
	*/
	for( i = 0; i < config.size; i++ ) {
		server = config.servers[i];

		rserver = (raft_server_snapshot_t *) (*data + offset);
		memcpy( &rserver->server_id, &server->server_id, sizeof(server_id_t) );
		memcpy( &rserver->target, &server->target, sizeof(target_t) );
		rserver->status = htonl( server->status );	// we have to convert it here with the serialization

		offset += sizeof(raft_server_snapshot_t);
	}

	raft_metadata->items = config.size;
	raft_metadata->last_index = get_raft_last_applied( );
	raft_metadata->last_term = get_raft_current_term( );
	raft_metadata->dlen = offset;	// which is the length of the data as well
}

/*
	Commits the received snapshot in the config module

	This function is in charge of deserializing the information of the servers received in
	the snapshot and reconstructing a fresh raft state configuration following the same order
	of servers they were added to the array of bytes (in which the servers were added in the
	same order they were added to the cluster configuration).

	Parameters:
		snapshot: contains the serialized array of servers and metadata information of the raft state
*/
void commit_raft_config_snapshot( raft_snapshot_t *snapshot  ) {
	unsigned int i;
	size_t offset = 0;
	log_entries_t *log;
	raft_server_snapshot_t *rserver;

	// the lock order must be: raft state (rft.c), raft log (log.c), and raft config (config.c)
	// raft_state is already locked
	lock_raft_log( );
	pthread_mutex_lock( &config_lock );

	// cleaning all raft config and log information
	while( config.size > 0 ) {
		remove_server( &config.servers[0]->server_id );	// [0] is ok since config.size is > 0
	}
	log = get_raft_log( );
	free_all_log_entries( log, snapshot->last_index );
	// cleaning up to here

	// installing snapshot
	set_raft_current_term( snapshot->last_term );
	set_raft_last_applied( snapshot->last_index );
	set_raft_commit_index( snapshot->last_index );

	for( i = 0; i < snapshot->items; i++ ) {
		rserver = (raft_server_snapshot_t *) (snapshot->data + offset);
		if( ! add_server( &rserver->server_id, &rserver->target, snapshot->last_index ) ) {
			logger_fatal( "unable to create a raft server while installing the snapshot" );
			exit( 1 );
		}
		if( ! set_server_voting_status( &rserver->server_id, ntohl( rserver->status ) ) ) {
			logger_fatal( "unable setting raft voting status %d for server %s (%s)",
				rserver->status, rserver->server_id, strerror( errno ) );
			exit( 1 );
		}

		offset += sizeof(raft_server_snapshot_t);
	}

	pthread_mutex_unlock( &config_lock );
	update_replica_servers( );	// calls get_replica_servers which locks the config_lock

	unlock_raft_log( );
}
