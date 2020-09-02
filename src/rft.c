// :vim ts=4 sw=4 noet:
/*
==================================================================================
	Copyright (c) 2019 AT&T Intellectual Property.

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
	Mnemonic:	rft.c
	Abstract:	Implements the RFT library to provide fault-tolerant xApps
				Uses RAFT algorithm for membership management

	Date:		22 October 2019
	Author:		Alexandre Huff
*/

#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <fcntl.h>
#include <math.h>
#include <assert.h>

#include <pthread.h>
#include <rmr/rmr.h>
#include "rft.h"
#include "rft_private.h"
#include "logger.h"
#include "utils.h"
#include "config.h"
#include "log.h"
#include "mtl.h"

#include "static/queue.c"		// static queue
#include "static/hashtable.c"	// static hashtable

/* ############ Prototypes ############ */

void *worker( );
void *trigger_election_timeout( );
void *state_replication( );
void become_follower( );
void become_leader( );
static inline void raft_commit_log_entries( index_t new_commit_index );

/* ############ Global constants ############ */
static const int primary_replica = 1;	// used by the context hashtable to store the xApp's role
// static const int backup_replica = 0;	// used by the context hashtable to store the xApp's role


/* ############ Global variables ############ */

struct timespec election_timeout;	// defines the term deadline used as a thread sleep counter (pthread_cond_timedwait)
									// pthreads doesn't allow a thread issue a sleep command targeting other threads
int rand_timeout_ms;				// defines the current election timeout in ms


/*
	In case of deadlocks check man page of pthread_mutex_lock looking at PTHREAD_MUTEX_ERRORCHECK
*/
pthread_mutex_t tasks_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t raft_state_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t election_timeout_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t follower_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t leader_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t replica_lock = PTHREAD_MUTEX_INITIALIZER;

pthread_cond_t	tasks_cond = PTHREAD_COND_INITIALIZER;
pthread_cond_t	raft_state_cond = PTHREAD_COND_INITIALIZER;
pthread_cond_t	election_timeout_cond = PTHREAD_COND_INITIALIZER;
pthread_cond_t	follower_cond = PTHREAD_COND_INITIALIZER;
pthread_cond_t	leader_cond = PTHREAD_COND_INITIALIZER;
pthread_cond_t	replica_cond = PTHREAD_COND_INITIALIZER;


static raft_state_t me = {	// defines this RAFT server state, shared among all threads
	.state = INIT_SERVER,
	.current_term = 0L,
	.self_id = {'\0'},
	.voted_for = {'\0'},
	.current_leader = {'\0'},
	.commit_index = 0,
	.last_applied = 0
};

static void *mrc = NULL;				// rmr message router context

static queue_t *tasks = NULL;			// queue rft messages forwarded by xapps

pthread_t	worker_th;					// thread id for the listener
pthread_t	election_timeout_th;		// thread id for in charge of leader election timeout
pthread_t	replication_th;				// thread id for state replication

static replication_type_e repl_type;	// replication type based on the RFT_REPLICA_SERVERS environmet variable
static unsigned int num_replicas;		// stores the number of replica servers to replicate the xApp's state
static int rep_intvl;					// defines the replication interval for the xApp's state
static server_t *replica = NULL;		// defines which current replicas this server is replicating its xapp's state
static replicas_t replica_servers = {	// set of servers that will be used to replicate the xApp's state
	.len = 0,
	.servers = NULL
};

static hashtable_t *ctxtab = NULL;	// hastable used to store if this xApp instance is the Primary or the Backup of a context

/*
	stores the maximum msg size (in bytes) that RMR is able to receive
	defined by the user in rmr_init() function
	we cannot send more than those bytes when using SI95 with RMR (message will be dropped on the receiver)
*/
static int max_msg_size;

apply_state_cb_t apply_command_cb;


/* ############ Implementation ############ */

/*
	Only used for testing purposes, should not be into the public rft.h header
*/
raft_state_t *get_me( ) {
	return &me;
}

/*
	Returns the address containing the self_id
	This can be safely read without locks since it is set up only by the init function
*/
server_id_t *get_myself_id( ) {
	return &me.self_id;
}

/*
	Only used for testing purposes, should not be into the public rft.h header
*/
void set_mrc( void *_mrc ) {
	mrc = _mrc;
}

/*
	Initializes the RFT library

	_mrc: message router context (rmr)
	listen_port: rmr's listening port
	rmr_max_msg_size: is the maximum receive message size passed as argument in rmr_init() function
	apply_state_cb: is the callback function used by RFT to apply state changes
*/
void rft_init( void *_mrc, char *listen_port, int rmr_max_msg_size, apply_state_cb_t apply_state_cb ) {
	server_id_t wbuf;
	char *rft_id;			// used to change the server_id's name
	char *envptr;			// general pointer to an environment variable value
	char *bootstrap;		// holds the server_id that must initialize the raft configuration
	char target_buf[256];
	int err = 0;
	log_entry_t *entry = NULL;
	server_conf_cmd_data_t cmd_data = {
		.server_id = {'\0'},
		.target = {'\0'}
	};

#if LOGGER_LEVEL < LOGGER_INFO
	fprintf( stderr, "%lu %d/RFT [INFO] initializing RFT library\n", (unsigned long)time( NULL ), getpid( ) );
#else
	logger_info( "initializing RFT library" );
#endif

	if( ELECTION_TIMEOUT < HEARTBEAT_TIMETOUT ) {
		logger_fatal( "election timeout cannot be smaller than heartbeat timeout" );
		exit( 1 );
	}
	if( _mrc == NULL ) {
		logger_fatal( "invalid _mrc (message router context): null?" );
		exit( 1 );
	}
	if( listen_port == NULL ) {
		logger_fatal( "invalid listening port: null?" );
		exit( 1 );
	}

	if( rmr_max_msg_size < 512 ) {		// a minimum of sanity
		logger_fatal( "a minimum of 512 bytes for max_rmr_msg_size is required" );
		exit( 1 );
	}
	max_msg_size = rmr_max_msg_size;

	mrc = _mrc;

	apply_command_cb = apply_state_cb;	// registering callback for xapp's commands

	envptr = getenv( "RFT_REPLICATION_INTERVAL" );
	if( envptr != NULL ) {
		rep_intvl = parse_int( envptr );
	} else {
		rep_intvl = REPLICATION_INTERVAL;	// default number of the xApp's replication interval (not RAFT replication)
	}

	envptr = getenv( "RFT_REPLICA_SERVERS" );
	if( envptr != NULL ) {
		if( strcmp( envptr, "all" ) == 0 ) {
			repl_type = GLOBAL;
		} else {
			repl_type = PARTIAL;
			num_replicas = parse_uint( envptr );
		}
	} else {
		repl_type = PARTIAL;
		num_replicas = 1;		// default number of replica servers
	}

	rft_id = getenv( "RFT_SELF_ID" );
	if ( rft_id != NULL && strlen( rft_id ) >= sizeof( me.self_id ) ) {
		logger_fatal( "RFT_SELF_ID is too long, max: %d", sizeof( me.self_id ) - 1 );
		exit( 1 );
	}

	if ( rft_id == NULL ) {
		err = gethostname( wbuf, sizeof( me.self_id ) );
		if ( err != 0 ) {
			if ( errno == ENAMETOOLONG )
				logger_fatal( "hostname too long for server_id" );
			else
				logger_fatal( "error on getting hostname for server id, errno: %d", errno );
			exit( 1 );
		}
		rft_id = wbuf;
	}

	assert( strlen( rft_id ) > 0 );
	snprintf( me.self_id, sizeof( me.self_id ), "%s", rft_id );

	logger_debug( "RFT server_id: %s", me.self_id );

	srand( time( NULL ) );

	/*
		Getting environment variable to check if rft cluster needs to be boostrapped
	*/
	bootstrap = getenv( "RFT_BOOTSTRAP" );
	if( bootstrap != NULL ) {
		if( strcmp( bootstrap, rft_id ) == 0 ) {	// checks if this server is in charge of boostrapping the cluster
			logger_info( "bootstrapping RFT cluster" );

			snprintf( target_buf, sizeof(target_buf), "%s:%s", rft_id, listen_port );
			if( ! raft_config_add_server( (server_id_t *) rft_id, target_buf, 0 ) ) {	// add itself to raft configuration servers
				logger_fatal( "unable to initialize RFT cluster configuration in rft_init" );
				exit( 1 );
			}

			// add myself configuration in the very first append entries log and commit it (this is the only server in the cluster so far)
			strcpy( cmd_data.server_id, rft_id );
			strcpy( cmd_data.target, target_buf );
			entry = new_raft_log_entry( 1, RAFT_CONFIG, ADD_MEMBER, &cmd_data, sizeof(server_conf_cmd_data_t) );
			if( entry == NULL ) {
				logger_fatal( "unable to allocate log entry memory for bootstrapping RFT configuration" );
				exit( 1 );
			}
			append_raft_log_entry( entry );
			raft_commit_log_entries( get_raft_last_log_index( ) );	// it should be 1, calling the function just to make sure

			// we need senf first message here, since at applying the first entry, this server is not the leader yet
			logger_info( "send message to routing manager ===== %s %s =====", "ADD_ROUTE", target_buf );
		}
	}

	tasks = new_queue( );
	if (tasks == NULL ) {
		logger_fatal( "unable to initialize the task queue for RFT" );
		exit( 1 );
	}

	ctxtab = hashtable_new( STRING_KEY, 127 );
	if( ctxtab == NULL ) {
		logger_fatal( "unable to initialize the context hashtable" );
		exit( 1 );
	}

	pthread_create( &worker_th, NULL, worker, NULL );
	pthread_create( &election_timeout_th, NULL, trigger_election_timeout, NULL );
	pthread_create( &replication_th, NULL, state_replication, NULL );
}

/*
	Enqueues rft messages forwarded by xapps (producer)
*/
void rft_enqueue_msg( rmr_mbuf_t *msg ) {
	int signal = 0;
	int plen; 	// payload length
	rmr_mbuf_t *msg_copy = NULL;

	if( msg == NULL ) {
		logger_error( "invalid msg to enqueue: null?" );
		return;
	}

	plen = rmr_payload_size( msg );
	msg_copy = rmr_realloc_payload( msg, plen, 1, 1 );
	if( msg_copy == NULL ) {
		logger_error( "unable to realloc a message copy to be enqueued: %s", strerror( errno ) );
		return;
	}

	pthread_mutex_lock( &tasks_lock );

	if ( tasks->len == 0 )		// queue was empty, so worker thread needs a signal to dequeue the message
		signal = 1;

	if( ! enqueue( tasks, msg_copy ) ) {
		logger_fatal( "unable to enqueue rft message" );
		exit( 1 );
	}

	if ( signal )
		pthread_cond_signal( &tasks_cond );

	pthread_mutex_unlock( &tasks_lock );
}

/*
	Dequeues rft messages consumed by rft's worker thread (consumer)
*/
rmr_mbuf_t *rft_dequeue_msg( ) {

	rmr_mbuf_t *msg = NULL;

	pthread_mutex_lock( &tasks_lock );

	if ( tasks->len < 1 )		// no messages in the queue
		pthread_cond_wait( &tasks_cond, &tasks_lock );

	msg = ( rmr_mbuf_t	*) dequeue( tasks );

	pthread_mutex_unlock( &tasks_lock );

	return msg;
}

/*
	Replicates an xapp's command on the replica server
	If no server is available, the replication is scheduled to
	start when a server becomes available

	Returns 1 if replication was scheduled, 0 on error
	In case of error, the errno is set as follows:
		- EINVAL: invalid argument
		- ENOTCONN: There is no server to replicate that command. Also, no log entry is created.
*/
int rft_replicate( int command, const char *context, const char *key, unsigned char *value, size_t len ) {
	log_entry_t *entry = NULL;

	if( context == NULL || key == NULL || value == NULL ) {
		logger_error( "unable to replicate command %d: context, key, and value need to have a value", command );
		errno = EINVAL;
		return 0;
	}

	if( raft_get_num_servers( ) == 1 ) {
		errno = ENOTCONN;
		return 0;
	}

	entry = new_server_log_entry( context, key, command, value, len );

	append_server_log_entry( entry, &me.self_id );

	// TODO Future: will be changed, most likely when raft configuration changes
	if( hashtable_get( ctxtab, key ) == NULL ) {	// we assume that if this replica receives the first message, then it is the primary
		if( ! hashtable_insert( ctxtab, key, (void *) &primary_replica ) ) {
			logger_error( "unable to insert context %s in ctxtable as primary replica", context );
			return 0;
		}
	}

	return 1;
}

/*
	Generic function to send message using RMR's routing table

	This function uses rmr_send_msg() and requires that the payload has been set up on msg

	Returns 1 on success, 0 otherwise
*/
int rft_send_msg( rmr_mbuf_t **msg, int mtype, int payload_size ) {
	int sent = 1;
	int retries = MAX_RMR_RETRIES;

	if( payload_size > max_msg_size ) {
		logger_error( "unable to send message type %d, reason: payload_size (%d) > max_msg_size (%d)", mtype, payload_size, max_msg_size );
		return 0;
	}

	(*msg)->mtype = mtype;
	(*msg)->sub_id = -1;
	(*msg)->len = payload_size;
	(*msg)->state = RMR_OK;

	logger_trace( "%-*s type: %d, len: %3d, mrc: %p, msg: %p", LOGGER_PADDING, "sending message", mtype, (*msg)->len, mrc, *(msg) );

	(*msg) = rmr_send_msg( mrc, (*msg) );

	while ((*msg) && (*msg)->state == RMR_ERR_RETRY && retries ) {
		retries--;
		usleep( 2 );
		(*msg) = rmr_send_msg( mrc, (*msg) );
	}

	if ( (*msg)->state == RMR_ERR_RETRY ) {
			logger_warn( "message dropped with state RMR_ERR_RETRY, mtype: %d", mtype );
			logger_trace( "message dropped with state RMR_ERR_RETRY, mtype: %d, mrc: %p, msg: %p ", mtype, mrc, (*msg) );
			sent = 0;
	} else if ( (*msg)->state != RMR_OK ) {
			logger_warn( "send failed, mtype: %d, state: %d, strerr: %s", mtype, (*msg)->state, strerror( errno ) );
			sent = 0;
	}

	if( sent )
		logger_trace( "%-*s type: %d, len: %3d, mrc: %p, msg: %p", LOGGER_PADDING, "message sent", mtype, (*msg)->len, mrc, (*msg) );

	return sent;
}

/*
	Generic function to reply message to the sender using RMR

	This function uses rmr_rts_msg() and requires that the payload has been set up on msg

	Returns 1 on success, 0 otherwise
*/
int rft_rts_msg( rmr_mbuf_t **msg, int mtype, int payload_size, server_id_t *server_id ) {
	int replied = 1;
	int retries = MAX_RMR_RETRIES;

	if( payload_size > max_msg_size ) {
		logger_error( "unable to reply message type %d, reason: payload_size (%d) > max_msg_size (%d)", mtype, payload_size, max_msg_size );
		return 0;
	}

	(*msg)->mtype = mtype;
	(*msg)->sub_id = -1;
	(*msg)->len = payload_size;
	(*msg)->state = RMR_OK;

	logger_trace( "%-*s type: %d, len: %3d, mrc: %p, msg: %p", LOGGER_PADDING, "replying message", mtype, payload_size, mrc, *(msg) );

	(*msg) = rmr_rts_msg( mrc, (*msg) );
	while ((*msg) && (*msg)->state == RMR_ERR_RETRY && retries ) {
		retries--;
		usleep( 2 );
		(*msg) = rmr_rts_msg( mrc, (*msg) );
	}

	if ( (*msg)->state == RMR_ERR_RETRY ) {
			logger_warn( "message dropped with state RMR_ERR_RETRY, target: %s, mtype: %d", server_id, mtype );
			replied = 0;
	} else if ( (*msg)->state != RMR_OK ) {
			logger_warn( "reply failed, mtype: %d, target: %s, state: %d, strerr: %s", mtype, server_id, (*msg)->state, strerror( errno ) );
			replied = 0;
	}

	return replied;
}

/*
	Generic function to send message using RMR's wormhole

	This function uses rmr_wh_send_msg() and requires that the payload has been set up on msg

	Returns 1 on success, 0 otherwise
*/
int rft_send_wh_msg( rmr_mbuf_t **msg, rmr_whid_t whid, int mtype, int payload_size, server_id_t *server_id ) {
	int sent = 1;
	int retries = MAX_RMR_RETRIES;

	if( payload_size > max_msg_size ) {
		logger_error( "unable to send message type %d to server %s, reason: payload_size (%d) > max_msg_size (%d)",
					mtype, server_id, payload_size, max_msg_size );
		return 0;
	}

	(*msg)->mtype = mtype;
	(*msg)->sub_id = -1;
	(*msg)->len = payload_size;
	(*msg)->state = RMR_OK;

	logger_trace( "%-*s type: %d, len: %3d, mrc: %p, msg: %p", LOGGER_PADDING, "sending wh message", mtype, (*msg)->len, mrc, *(msg) );

	(*msg) = rmr_wh_send_msg( mrc, whid, (*msg) );

	while ((*msg) && (*msg)->state == RMR_ERR_RETRY && retries ) {
		retries--;
		usleep( 2 );
		(*msg) = rmr_wh_send_msg( mrc, whid, (*msg) );
	}

	if ( (*msg)->state == RMR_ERR_RETRY ) {
			logger_warn( "message dropped with state RMR_ERR_RETRY, target: %s, mtype: %d", server_id, mtype );
			logger_trace( "message dropped with state RMR_ERR_RETRY, target: %s, mtype: %d, mrc: %p, msg: %p ", server_id, mtype, mrc, (*msg) );
			sent = 0;
	} else if ( (*msg)->state != RMR_OK ) {
			logger_warn( "send failed, mtype: %d, target: %s, state: %d, strerr: %s", mtype, server_id, (*msg)->state, strerror( errno ) );
			sent = 0;
	}

	if( sent )
		logger_trace( "%-*s type: %d, len: %3d, mrc: %p, msg: %p", LOGGER_PADDING, "message sent", mtype, (*msg)->len, mrc, (*msg) );

	return sent;
}

/*
	Sends membership requests (discovery messages) to all servers in the cluster waiting for the leader reply

	Expects that the leader of the cluster sends log entries that must be commited by this server through AppendEntry messages
	NOTE: this function does not process AppendEntries, it only waits for an election_timeout and sends the discovery message again
		  in case of no leader reply
*/
void send_membership_request( rmr_mbuf_t **msg ) {
	membership_request_t *membership_msg = NULL;
	struct timespec timeout;
	index_t prior;		// defines the last_log_index of the previous timeout
	index_t current;	// defines the last_log_index of the current timeout

	assert( (*msg) != NULL );
	if( me.state != INIT_SERVER ) {
		logger_fatal( "only servers on INIT_SERVER state can send membership requests" );
		exit( 1 );
	}

	rand_timeout_ms = randomize_election_timeout( );

	logger_info( "discovering the leader of the cluster" );

	prior = get_raft_last_log_index( );

	pthread_mutex_lock( &raft_state_lock );

	while( me.state == INIT_SERVER ) {

		current = get_raft_last_log_index( );

		if( prior == current ) {	// it means that this server does not make progress since last timeout
			membership_msg = (membership_request_t *) (*msg)->payload;
			membership_msg->last_log_index = get_raft_last_log_index( );
			strcpy( membership_msg->server_id, me.self_id );

			logger_debug( "sending membership request last_log_index: %lu", membership_msg->last_log_index );
			rft_send_msg( msg, MEMBERSHIP_REQ, sizeof( *membership_msg ) );

			prior = current;
		}

		timespec_get( &timeout, TIME_UTC);
		timespec_add_ms( timeout, rand_timeout_ms );

		pthread_cond_timedwait( &raft_state_cond, &raft_state_lock, &timeout );
	}

	pthread_mutex_unlock( &raft_state_lock );
}

/*
	Steps down a leader or candidate to a follower state

	Assumes that raft_state_lock mutex is locked by the caller
*/
void become_follower( ) {
	logger_info( "becoming FOLLOWER" );

	me.state = FOLLOWER;

	rand_timeout_ms = randomize_election_timeout( );

	pthread_cond_signal( &follower_cond );

}

void become_candidate( rmr_mbuf_t **msg ) {
	request_vote_t *req_vote_msg = NULL;

	assert( (*msg) != NULL );
	if( me.state != FOLLOWER ) {
		logger_fatal( "only followers can become candidate" );
		exit( 1 );
	}

	pthread_mutex_lock( &raft_state_lock );

	logger_info( "becoming CANDIDATE" );

	me.state = CANDIDATE;

	while ( me.state == CANDIDATE ) {
		req_vote_msg = (request_vote_t *) (*msg)->payload;

		me.current_term++;						// increments the term
		// persist_term( me.current_term );		// save it on stable storage (not needed we are using in-memory FSM)

		strcpy( me.voted_for, me.self_id ); 	// votes for self
		// persist_vote( &me.voted_for );		// save voted_for on stable storage (not needed we are using in-memory FSM)

		me.rcv_votes = 1;						// initializing and accounting for received votes in the new term (vote for myself)

		raft_config_reset_votes( );				// initializes the quorum of voters for this new term
		if( ! raft_config_set_new_vote( &me.self_id ) ) {	// setting candidate's vote int the raft configuration array
			logger_fatal( "candidate was unable to grant itself vote in raft configuration array" );
			exit( 1 );
		}

		/*
			Checks if it is the only server in the raft configuration
			In case yes, then is safe to become leader, since it get here only after raft bootstrapping RAFT_INIT => FOLLOWER => CANDIDATE
		*/
		if( has_majority_of_votes( me.rcv_votes ) ) {
			become_leader( );

		} else {
			/* creates the raft message */
			req_vote_msg->term = me.current_term;
			strcpy( req_vote_msg->candidate_id, me.self_id );

			req_vote_msg->last_log_index = get_raft_last_log_index( );
			req_vote_msg->last_log_term = get_raft_last_log_term( );

			rand_timeout_ms = randomize_election_timeout( );
			timespec_get( &election_timeout, TIME_UTC);
			timespec_add_ms( election_timeout, rand_timeout_ms );

			logger_debug( "sending vote request term: %lu", req_vote_msg->term );
			rft_send_msg( msg, VOTE_REQ, sizeof( *req_vote_msg ) );

			pthread_cond_timedwait( &raft_state_cond, &raft_state_lock, &election_timeout );
		}

	}

	pthread_mutex_unlock( &raft_state_lock );
}


/*
	Initializes the leader

	Assumes that raft_state_lock mutex is locked by the caller

	NOTE: There is no one specific leader thread, it is implemented by threads that send appendEntries to their
		  corresponding servers in the cluster.
		  Those threads just send messages when its server's state is LEADER
		  On converting to FOLLOWER those threads have to put themselves to sleep
		  On becoming leader all server threads have to be awaked again

*/
void become_leader( ) {
	log_entry_t *entry;

	if( me.state != CANDIDATE ) {
		logger_fatal( "only candidates can become leader" );
		exit( 1 );
	}

	logger_info( "becoming LEADER" );

	me.state = LEADER;

	entry = new_raft_log_entry( me.current_term, RAFT_NOOP, 0, NULL, 0 );
	if( entry == NULL ) {
		logger_fatal( "unable to create a new RAFT_NOOP log entry" );
		exit( 1 );
	}

	append_raft_log_entry( entry );

	raft_config_set_all_server_indexes( get_raft_last_log_index( ) );

	/*
		sending a signal to wakeup all append entries threads (now we are the leader)
	*/
	pthread_cond_broadcast( &leader_cond );
}

/*
	Generic function that ensures that currentTerm is up-to-date with the message's term
	If RPC request or response contains term T > currentTerm: set currentTerm = T, convert to follower (§5.1)

	Returns:
		E_OUTDATED_MSG	if term < currentTerm
		E_TERMS_MATCH	if terms match
		E_OUTDATED_TERM	if term > currentTerm

	Not thread-safe, assumes that calling owns raft_state_lock
*/
term_match_e match_terms( term_t term ) {
	if ( term < me.current_term )
		return E_OUTDATED_MSG;

	if ( term == me.current_term )
		return E_TERMS_MATCH;

	// if we get here, then term is greater than current_term
	logger_debug( "found a term greater than %lu, switching to term %lu", me.current_term, term );
	me.current_term = term;
	// persist_term( term );			// save it on stable storage (not needed we are using in-memory FSM)
	memset( me.voted_for, 0, sizeof( me.voted_for ) );
	// persist_vote( &me.voted_for );	// save it on stable storage (not needed we are using in-memory FSM)

	memset( me.current_leader, 0, sizeof( me.current_leader ) );	// removing the leader information

	if( me.state == LEADER || me.state == CANDIDATE ) {
		become_follower( );
	}

	return E_OUTDATED_TERM;
}

/*
	Implements a thread for each raft server in the configuration

	Acquires raft_state_lock
*/
void *send_append_entries( void *raft_server ) {
	unsigned int i;	// general counter used to iterate over the replica_servers array
	int count = 10;	// counter for rmr_wh_open retries
	int mlen;		// the size of data in RMR payload, must be int and check for negative, in case of overflow (see rmr_mbuf_t len)
	rmr_whid_t whid = -1;
	rmr_mbuf_t *msg = NULL;
	appnd_entr_hdr_t *payload = NULL;
	struct timespec next_heartbeat = {0, 0};	// defines the timeout to trigger the next heartbeat for this thread
	pthread_mutex_t heartbeat_lock = PTHREAD_MUTEX_INITIALIZER;
	pthread_cond_t heartbeat_cond = PTHREAD_COND_INITIALIZER;
	unsigned char *srlz_buf = NULL;		// serialization buffer
	unsigned int buf_len;				// size of the serialization buffer
	unsigned int bytes;					// number of bytes serialized in the message
	unsigned int n_entries;				// number of log entries transmitted in the message
	log_entry_t *prev_log_entry = NULL;
	index_t last_log_index;
	int rounds = 10;		// run catch-up for a fixed number of rounds, fig 4.1 in raft dissertation
	int progress = 0;		// identifies if a catching-up server made a progress within a heartbeat
	log_entry_t *new_entry = NULL;	// pointer to a new server's log entry configuration to be appended into log entries
	int logs_match;			// conditional variable used to check if leader's log and remote server's log match
	server_conf_cmd_data_t data = {	// command data to add catching-up server in the log
		.server_id = {'\0'},
		.target = {'\0'}
	};

	assert( raft_server != NULL);
	server_t *server = (server_t *) raft_server;

	server->heartbeat_cond = &heartbeat_cond;	// this thread needs to be awaked upon receiving an append reply with success = 0
	server->leader_cond = &leader_cond;	// this thread needs to be awaked if not in leader state (upon deleting this server from cluster)
	server->whid = &whid;	// required by the xapp state replication thread

	server->active = RUNNING;	// needs come after pthread conditions

	msg = rmr_alloc_msg( mrc, sizeof( request_append_entries_t ) );
	if( msg == NULL ) {
		logger_error( "unable to allocate rmr_mbuf to send append entries for server %s", server->server_id );
	}

	buf_len = 512;
	srlz_buf = (unsigned char *) malloc( buf_len );	// minimum buffer size, it can be reallocated by the serializer function
	if( srlz_buf == NULL ) {
		logger_error( "unable to allocate a buffer to serialize append entries for server %s", server->server_id );
	}

	while ( count > 0 )	{
		whid = rmr_wh_open( mrc, server->target );
		if( whid < 0 ) {
			logger_error( "unable to connect to %s", server->target );
			usleep( 1 + rand() % 50 );	// wait random time from 1us to 50us before retrying
			count--;
			continue;
		}
		break;
	}
	if( whid < 0 ) {
		logger_error( "unable to connect to %s\tgiving up!", server->target );
	} else {
		logger_trace( "connected to %s by wormhole id %d", server->target, whid );
	}

	if( ( msg == NULL ) || ( srlz_buf == NULL ) || whid < 0 ) {
		raft_config_remove_server( &server->server_id );
	} else {
		// TODO Send config snapshot here if "me" is the LEADER....
	}

	while ( server->active ) {
		/*
			Only used to put this thread to sleep (cond_wait)
			However, we can save some important CPU cycles and ns by avoiding doing extra lock/unlock in each loop
		*/
		pthread_mutex_lock( &heartbeat_lock );

		/*
			this server will check again if it is still the leader before send a message
			so, we don't to lock server_state here, no harmful code will be executed
		*/
		while( me.state == LEADER && server->active == RUNNING ) {

			pthread_mutex_lock( &server->index_lock ); // required to avoid modification on match_index, next_index
			/*
				Until the leader has discovered the point where its and the follower’s logs match, the leader can send
				AppendEntries with no entries (like heartbeats) to save bandwidth. Then, once the matchIndex
				immediately precedes the nextIndex, the leader should begin to send the actual entries.
				pg. 21 raft dissertation
			*/
			logs_match = ( server->match_index == server->next_index - 1 );
			if( logs_match ) {
				last_log_index = get_raft_last_log_index( );
				n_entries = (unsigned int) last_log_index - ( server->next_index - 1 );
			} else {
				n_entries = 0;
			}

			if( n_entries ) {
				bytes = serialize_raft_log_entries( server->next_index, &n_entries, &srlz_buf, &buf_len, max_msg_size );
			} else {
				bytes = 0;
			}

			prev_log_entry = get_raft_log_entry( server->next_index - 1 );

			pthread_mutex_unlock( &server->index_lock );

			mlen = (int) ( sizeof( appnd_entr_hdr_t ) + bytes );	// getting required size for the whole message
			if( mlen < 0 ){
				logger_fatal( "size overflow of serialized append entries" );
				exit( 1 );
			}

			if( rmr_payload_size( msg ) < mlen ) {
				msg = (rmr_mbuf_t *) rmr_realloc_payload( msg, mlen, 0, 0 );
				if( msg == NULL ) {
					logger_fatal( "unable to reallocate rmr_mbuf payload for the serialized append entries" );
					exit( 1 );
				}
			}
			payload = (appnd_entr_hdr_t *) msg->payload;

			if( prev_log_entry != NULL ) {
				payload->prev_log_index = prev_log_entry->index;
				payload->prev_log_term = prev_log_entry->term;
			} else {
				payload->prev_log_index = 0;
				payload->prev_log_term = 0;
			}

			payload->slen = bytes;			// setting serialized append entries payload size
			payload->n_entries = n_entries;
			if( n_entries )
				memcpy( APND_ENTR_PAYLOAD_ADDR( msg->payload ), srlz_buf, bytes );

			pthread_mutex_lock( &raft_state_lock );

			if( me.state == LEADER ) {		// if I'm still the leader then that message can be sent to the followers
				payload->term = me.current_term;
				strcpy( payload->leader_id, me.self_id );
				payload->leader_commit = me.commit_index;

				logger_trace( " sending	append entries request to %s, term: %lu, n_entries: %u, prev_log_index: %lu, prev_log_term: %lu, leader_commit: %lu",
							server->server_id, payload->term, payload->n_entries, payload->prev_log_index,
							payload->prev_log_term, payload->leader_commit );

				rft_send_wh_msg( &msg, whid, APPEND_ENTRIES_REQ, mlen, &server->server_id );
			}

			pthread_mutex_unlock( &raft_state_lock );

			pthread_cond_timedwait( &heartbeat_cond, &heartbeat_lock, &next_heartbeat ); // can be awaked upon append entries reply

			/* catching-up routine for new servers */
			if( ( server->status == NON_VOTING_MEMBER ) && logs_match ) {
				if( is_server_caught_up( server, &rounds, &next_heartbeat, &progress ) ) {

					pthread_mutex_lock( &raft_state_lock );
					if( me.state == LEADER ) {
						if( set_configuration_changing( 1 ) ) {
							// appending server configuration to the log
							strcpy( data.server_id, server->server_id );
							strcpy( data.target, server->target );

							new_entry = new_raft_log_entry( me.current_term, RAFT_CONFIG, ADD_MEMBER, &data, sizeof(server_conf_cmd_data_t) );
							if( new_entry == NULL ) {
								logger_fatal( "unable to allocate memory for a new server configuration log entry" );
								exit( 1 );
							}
							append_raft_log_entry( new_entry );
						}
					}
					pthread_mutex_unlock( &raft_state_lock );
				}
			}

			pthread_mutex_lock( &raft_state_lock );
			if( me.state == LEADER ) {
				server->hb_timeouts++;	// increment heartbeat timeout counter, will be set to 0 in append entries reply handler
				if( ( server->hb_timeouts > MAX_HEARBEAT_TIMEOUTS ) && ( set_configuration_changing( 1 ) ) ) {
					// appending server delete to the log
					strcpy( data.server_id, server->server_id );
					strcpy( data.target, server->target );
					new_entry = new_raft_log_entry( me.current_term, RAFT_CONFIG, DEL_MEMBER, &data, sizeof(server_conf_cmd_data_t) );
					if( new_entry == NULL ) {
						logger_fatal( "unable to allocate memory for a delete server configuration log entry" );
						exit( 1 );
					}
					append_raft_log_entry( new_entry );
				}
			}
			pthread_mutex_unlock( &raft_state_lock );

			timespec_get( &next_heartbeat, TIME_UTC );
			timespec_add_ms( next_heartbeat, HEARTBEAT_TIMETOUT );	// setting the next heartbeat timeout
		}

		pthread_mutex_unlock( &heartbeat_lock );

		if( server->active ) {	// this server could be removed and so it do not need to wait for a signal to wake-up and finish

			// putting this thread to sleep, waiting for a BROADCAST signal from become_leader
			pthread_mutex_lock( &leader_lock );
			/*
				it may not be awaked if lock is get just after a broadcast signal,
				no crash will happen, just the target server will timeout and become candidate
			*/
			logger_trace( "server %s is going to sleep: append_entries", server->server_id );
			pthread_cond_wait( &leader_cond, &leader_lock );
			logger_trace( "server %s awaked: append_entries", server->server_id );
			pthread_mutex_unlock( &leader_lock );
		}

	}

	// clean up routines
	logger_debug( "running thread clean up of server %s", server->server_id );

	logger_trace( "closing wormhole id %d of server %s", whid, server->server_id );
	if( whid != -1 )
		rmr_wh_close( mrc, whid );

	logger_trace( "freeing rmr mbuf of server %s", server->server_id );
	if( msg != NULL )
		rmr_free_msg( msg );

	logger_trace( "freeing serializing buffer of server %s", server->server_id );
	if( srlz_buf != NULL )
		free( srlz_buf );

	pthread_mutex_lock( &replica_lock );
	for( i = 0; i < replica_servers.len; i++ ) {
		if( server == replica_servers.servers[i] ) {	// comparing pointers, if equal it means that the server being removed is my replica
			logger_trace( "setting replica server %s to NULL", server->server_id );
			/*
				server has to be set to NULL here, since the raft config module has no access to the replica_servers array
				this is a requirement to avoid segfaults in state_replication function by trying access a pointer after
				its memory was released (free)
			*/
			replica_servers.servers[i] = NULL;
		}
	}
	pthread_mutex_unlock( &replica_lock );

	logger_trace( "freeing server %s", server->server_id );
	free( server );

	return NULL;
}

/*
	Implements a thread in charge of replicating the primary xapp state to its backup replicas
*/
void *state_replication( ) {
	rmr_mbuf_t *msg = NULL;
	index_t	last_log_index;				// xapps' last log index to replicate
	index_t master_index;				// replicated index (used as a buffer to release lock sooner)
	unsigned char *srlz_buf = NULL;		// serialization buffer
	unsigned int buf_len;				// size of the serialization buffer
	unsigned int bytes;					// number of bytes serialized in the message
	unsigned int n_entries = 0;			// number of log entries transmitted in the message
	int mlen;		// the size of data in RMR payload, must be int and check for negative, in case of overflow (see rmr_mbuf_t len)
	repl_req_hdr_t *request = NULL;
	struct timespec timeout;
	unsigned int rep_i;					// counter to iterate over replica_servers' array
	int must_lock;						// controls if it is required to get the replica_lock before going to sleep

	msg = rmr_alloc_msg( mrc, RMR_MAX_RCV_BYTES );
	if( msg == NULL ) {
		logger_error( "unable to allocate rmr_mbuf for xapp state replication" );
	}

	buf_len = 512;
	srlz_buf = (unsigned char *) malloc( buf_len );	// minimum buffer size, it can be reallocated by the serializer function
	if( srlz_buf == NULL ) {
		logger_error( "unable to allocate buffer for serializing state replication" );
	}

	pthread_mutex_lock( &replica_lock );
	must_lock = 0;		// there is no need to get the lock again before going to sleep

	while( 1 ) {
		/*
			When moving from a two server cluster to one server due to failure, the raft_get_num_servers()
			guarantees that the replication thread is going to sleep
		*/
		while( replica_servers.len == 0 || raft_get_num_servers( ) == 1 ) {
			logger_info( "waiting for a replica to initialize the xapp state replication" );
			pthread_cond_wait( &replica_cond, &replica_lock );

			if( replica_servers.len > 0 )
				logger_info( "xapp state replication initialized" );
		}

		for( rep_i = 0; rep_i < replica_servers.len; rep_i++ ) {	// needs to own the replica_lock here
			replica = replica_servers.servers[rep_i];
			if( replica == NULL )	// replica_server[i] was set to NULL on send_append_entries exiting
				continue;

			master_index = replica->master_index;	// we already have the lock here
			pthread_mutex_unlock( &replica_lock );
			must_lock = 1;

			last_log_index = get_server_last_log_index( &me.self_id );
			n_entries = (unsigned int) last_log_index - master_index;

			if( n_entries ) {
				// creating replication request
				bytes = serialize_server_log_entries( master_index + 1, &n_entries, &srlz_buf,
														&buf_len, max_msg_size, &me.self_id );

				mlen = (int) ( sizeof( repl_req_hdr_t ) + bytes );	// getting required size for the whole message
				if( mlen < 0 ){
					logger_fatal( "size overflow of serialized append entries" );
					exit( 1 );
				}

				if( rmr_payload_size( msg ) < mlen ) {
					msg = (rmr_mbuf_t *) rmr_realloc_payload( msg, mlen, 0, 0 );
					if( msg == NULL ) {
						logger_fatal( "unable to reallocate rmr_mbuf payload for serializing state replication" );
						exit( 1 );
					}
				}
				request = (repl_req_hdr_t *) msg->payload;
				request->master_index = master_index;
				strcpy( request->server_id, me.self_id );
				request->slen = bytes;			// setting serialized append entries payload size
				request->n_entries = n_entries;
				memcpy( REPL_REQ_PAYLOAD_ADDR( msg->payload ), srlz_buf, bytes );

				pthread_mutex_lock( &replica_lock );
				must_lock = 0;	// means that there is no need to get the replica_lock again, leading to deadlocks
				if( replica ) {
					logger_debug( "sending   replication request to %s, n_entries: %u, bytes: %u, master_index: %lu",
								replica->server_id, request->n_entries, bytes, request->master_index );

					rft_send_wh_msg( &msg, *replica->whid, REPLICATION_REQ, mlen, &replica->server_id );
				}
			}
		}

		timespec_get( &timeout, TIME_UTC);
		timespec_add_ms( timeout, rep_intvl );

		if( must_lock )
			pthread_mutex_lock( &replica_lock );

		pthread_cond_timedwait( &replica_cond, &replica_lock, &timeout );
		must_lock = 0;		// no lock is required, as we have the lock
	}
}

/*
	This function process a raft vote request message

	1. Reply false if term < current_term
	2. If voted_for is null or candidateId, and candidate’s log is at
	   least as up-to-date as receiver’s log, grant vote
*/
void handle_vote_request( request_vote_t *req_vote_msg, reply_vote_t *reply_vote_msg ) {
	struct timespec now;
	int log_ok;				// used to check if candidate's log is at least up-to-date to the voter's log
	int has_voted;			// used to check if this server has already voted in the current_term
	int timed_out;
	term_match_e match;		// used to check if terms match

	assert( req_vote_msg != NULL );
	assert( reply_vote_msg != NULL );

	timespec_get( &now, TIME_UTC );

	strcpy( reply_vote_msg->voter, me.self_id );

	pthread_mutex_lock( &election_timeout_lock );
	timed_out = timespec_cmp( now, election_timeout, < );
	pthread_mutex_unlock( &election_timeout_lock );

	pthread_mutex_lock( &raft_state_lock );

	match = match_terms( req_vote_msg->term );

	reply_vote_msg->term = me.current_term;

	if ( ( !timed_out ) && ( me.state == FOLLOWER ) ) {	// messages should not arrive before end of election_timeout, since there is an active leader

		logger_warn( "rejecting vote for %s, since election timeout is not over (messages being dropped or %s has crashed)",
					req_vote_msg->candidate_id, req_vote_msg->candidate_id );
		reply_vote_msg->granted = 0;			// vote rejected

	} else if( match == E_OUTDATED_MSG ) {		// term < currentTerm
		logger_warn( "rejecting vote for %s, since msg term is %lu and current term is %ld",
					 req_vote_msg->candidate_id, req_vote_msg->term, me.current_term );
		reply_vote_msg->granted = 0;

	} else {	// match is E_TERMS_MATCH or E_OUTDATED_TERM

		log_ok = ( req_vote_msg->last_log_term > get_raft_last_log_term( ) ||
				( req_vote_msg->last_log_term == get_raft_last_log_term( ) &&
					req_vote_msg->last_log_index >= get_raft_last_log_index( ) ) );

		has_voted = strlen( me.voted_for );		// will be emptied when increasing server's current_term

		if ( log_ok && !( has_voted ) ) {
			logger_info( "voting for %s => term: %lu", req_vote_msg->candidate_id, me.current_term );

			strcpy( me.voted_for, req_vote_msg->candidate_id );
			// persist_vote( &me.voted_for );		// save it on stable storage (not needed we are using in-memory FSM)
		}

		reply_vote_msg->granted = ( strcmp( req_vote_msg->candidate_id, me.voted_for ) == 0 );

	}

	pthread_mutex_unlock( &raft_state_lock );
}

/*
	Computes all received votes and decides if this server has the majority of votes to become leader

	If this sever has majority of votes, then this function calls become_leader
*/
void handle_vote_reply( reply_vote_t *reply_vote_msg ) {
	term_match_e match;
	int is_new_vote;

	pthread_mutex_lock( &raft_state_lock );

	match = match_terms( reply_vote_msg->term );

	// only has to count a vote if state is CANDIDATE and terms match (message delay)
	if( ( match == E_TERMS_MATCH ) && ( me.state == CANDIDATE ) && ( reply_vote_msg->granted ) ) {

		is_new_vote = raft_config_set_new_vote( &reply_vote_msg->voter );
		if( is_new_vote ) {
			// incrementing counter of votes, so we do not need to visit all server again to check if is has majority of votes
			me.rcv_votes++;

			if( has_majority_of_votes( me.rcv_votes ) ) {
				become_leader( );
			}
		}
	}

	pthread_mutex_unlock( &raft_state_lock );
}

/*
	On receiving a membership_request message, the corresponding server must be added
	to the cluster configuration as NON_VOTING_MEMBER

	A new thread is started to send AppendEntries to that server
*/
void handle_membership_request( membership_request_t *membership_msg, char *src_addr ) {

	pthread_mutex_lock( &raft_state_lock );

	if( me.state == LEADER ) {	// Only the leader can manage membership request messages

		if( strcmp( membership_msg->server_id, me.self_id ) != 0 ) {	// do not need to handle myself messages

			/*
				Do not need handle error on creating this server, all resources will be released on error
				and in the worst case a new membership request will be received
			*/
			raft_config_add_server( &membership_msg->server_id, src_addr, membership_msg->last_log_index );
		}
	}

	pthread_mutex_unlock( &raft_state_lock );
}

/*
	Applies new commited entries sequentialy up to the me.commit_index (including it)

	Not thread-safe, assumes that the caller has the lock of raft_state_lock
*/
static inline void raft_apply_log_entries( ) {
	log_entry_t *entry;
	server_conf_cmd_data_t *data;
	server_t *server = NULL;
	int config_myself;		// defines it the config command is for me
	int cfg_change = 0;		// identifies if a new configuration is being applied
	char wbuf[50];

	logger_debug( "applying raft log entries in FSM from index: %lu, to index: %lu", me.last_applied, me.commit_index );

	while( me.last_applied < me.commit_index ) {
		entry = get_raft_log_entry( me.last_applied + 1 );
		if( entry->type == RAFT_CONFIG ) {
			data = (server_conf_cmd_data_t *) entry->data;

			if( entry->command == ADD_MEMBER ) {
				server = raft_config_get_server( &data->server_id );
				config_myself = 0;	// ensuring no garbage from previous loop
				if( server )		// might be NULL if a server_id is not present in the raft configuration (failed previously)
					config_myself = ( strcmp( server->server_id, me.self_id ) == 0 ); // checks if configuration changing is for myself

				if( config_myself ) {
					become_follower( );	// just become follower when we are sure that we were added in the raft configuration
				}

				snprintf( wbuf, 50, "ADD_ROUTE" );

			} else if( entry->command == DEL_MEMBER ) {

				snprintf( wbuf, 50, "DEL_ROUTE" );
			}

			cfg_change = 1;

			if( me.state == LEADER ) {
				logger_info( "send message to routing manager ===== %s %s =====", wbuf, data->target );
			}

		} else if( entry->type == RAFT_NOOP ) {
			logger_debug( "Applied RAFT_NOOP log entry, term: %lu, index: %lu", entry->term, entry->index );
			// nothing else to do here

		} else {	// the other entry type is RAFT_COMMAND
			/*
				If the log entry is an xapp "user" command, then an apply callback is called
				Not implemented here (see replication request handler)
			*/
			logger_error( "===> IMPLEMENT APPLY RAFT LOG COMMAND HERE <===" );
		}

		me.last_applied++;
	}

	if( cfg_change && me.state != INIT_SERVER ) {
		pthread_mutex_lock( &replica_lock );

		get_replica_servers( &me.self_id, &replica_servers, repl_type == PARTIAL ? num_replicas : raft_get_num_servers( ) - 1 ); // me is not included
		pthread_cond_signal( &replica_cond );

		pthread_mutex_unlock( &replica_lock );
	}
}

/*
	Commits log entries and apply them sequentially

	NOTE: Not thread safe, assumes that the caller has the lock of server_state

	LEADER: Checks for logs that are safe to be commited
			Commits and applies a log entry if:
			There exists an N such that N > commitIndex, a majority
			of matchIndex[i] >= N, and log[N].term == currentTerm:
			set commitIndex = N (§5.3, §5.4 raft paper).

	OTHER STATES: Commit and apply all log entries up to new_commit_index param

	new_commit_index param:
		LEADER: has to pass the expected next log index to be commited
		OTHERS: have to pass the leader_commit received in append entries request
*/
static inline void raft_commit_log_entries( index_t new_commit_index ) {
	log_entry_t *entry;
	index_t last_log_index;
	index_t cindex;		// used to check if the commit entry is a cluster configuration

	/*
		New commit index needs to be less|equal than raft last log index to commit a log entry
		This can happen when a server restarts before the leader was able to remove that server from the configuration
		The new commit index will be greater that the raft last log index, as the later will be 0 (zero) after server restarts (in memory FSM)
	*/
	if( new_commit_index <= get_raft_last_log_index( ) ) {
		logger_debug( "committing raft log entries" );

		if( me.state == LEADER ) {
			/*
				iterating back up to find the safe new commit_index
				the new_commit_index not necessary is replicated on the majority of servers, but a previous one can be
			*/
			while( new_commit_index > me.commit_index) {	// there exists an N such that N > commitIndex
				entry = get_raft_log_entry( new_commit_index );
				if( entry == NULL )
					return;

				/*
					Raft never commits log entries from previous terms by counting replicas.
					Only log entries from the leader’s current term are committed by counting replicas.
					Section 5.4.2 and Fig. 8 from raft paper

					Once an entry from the current term has been committed in this way, then all prior
					entries are committed indirectly because of the Log Matching Property.
					Section 3.6.2 in raft dissertation
				*/
				if( entry->term != me.current_term )	// ...and log[N].term == currentTerm
					return;

				if( has_majority_of_match_index( new_commit_index ) )	// a majority of matchIndex[i] >= N
					break;	// we found the most up-to-date log replicated on the majority of servers

				new_commit_index--;	//using the same variable to iterate back
			}

			// searching for a configuration entry, since we need to release the configuration changing flag
			cindex = me.commit_index + 1;
			while( cindex <= new_commit_index ) {
				entry = get_raft_log_entry( cindex );
				if( entry == NULL ) {
					logger_error( "unable to find log entry by index %ld", cindex );
					return;
				}
				if( entry->type == RAFT_CONFIG ) {
					set_configuration_changing( 0 );	// finishing the cluster (re)configuration
					break;	// we found the only one allowed configuration to be committed at a time
				}
				cindex++;
			}
		}

		// this will work to commit entries by the servers is all statuses, including the leader
		if( new_commit_index > me.commit_index	) {	// need to ensure that N > commitIndex
			/*
				Receiver implementation fig. 2 raft paper
				If leaderCommit > commitIndex, set commitIndex = min(leaderCommit, index of last** new entry)
				** leader commit can be greater than our last log index
				here leaderCommit is the new_commit_index
			*/
			last_log_index = get_raft_last_log_index( );	// at this point all new log in append entries were added to the log

			// min(leaderCommit, index of last new entry)
			me.commit_index = MIN( new_commit_index, last_log_index );

			// persist_logs( &me, me.commit_index );	// saving logs on stable storage (not needed we are using in-memory FSM)

			raft_apply_log_entries( );
		}
	}
}

void handle_append_entries_request( request_append_entries_t *request_msg, reply_append_entries_t *response_msg) {
	int match;
	int no_conflict = 1;	// identifies if append entries conflicted with the commited log, mainly because of duplicate messages
	unsigned int i;
	log_entry_t *log_entry = NULL;

	assert( request_msg != NULL );
	assert( response_msg != NULL );

	pthread_mutex_lock( &raft_state_lock );

	match = match_terms( request_msg->term );

	response_msg->term = me.current_term;
	strcpy( response_msg->server_id, me.self_id );

	if( match == E_OUTDATED_MSG ) {
		logger_warn( "rejecting append entries for %s, since msg term is %lu and current term is %ld",
					 request_msg->leader_id, request_msg->term, me.current_term );
		response_msg->success = 0;

	} else {

		if( strlen( me.current_leader ) == 0 ) {	// if term changed it was cleaned by on matching terms
			strcpy( me.current_leader, request_msg->leader_id );	// setting the current leader we believe is for current term
		}

		/*
			call get_raft_log_entry from log module
			compare if the request_msg prev_log_term (and prev_log_index) is equal to the retrieved log entry
			if do not, return success = 0
			See section 5.3 in raft paper
			prev_log_index can be different of 0 at first log, (e.g. starting log from a leader snapshot)
			IMPORTANT: Need to release log inconsistencies starting from prev_log_index + 1
		*/
		log_entry = get_raft_log_entry( request_msg->prev_log_index );

		if( get_raft_last_log_index( ) > request_msg->prev_log_index ) {	// checking for log inconsistencies
			no_conflict = remove_raft_conflicting_entries( request_msg->prev_log_index + 1, &me );
		}

		// last_log_index equals 0 means that the log is empty
		if( ( log_entry && ( log_entry->term == request_msg->prev_log_term ) && (log_entry->index == request_msg->prev_log_index) ) ||
			( get_raft_last_log_index( ) == 0 ) ) {

			if( no_conflict ) {
				if( request_msg->n_entries ) {	// if no n_entries, this message is a hertbeat
					logger_debug( "appending %u new log entries", request_msg->n_entries );

					for( i = 0; i < request_msg->n_entries; i++ ) {		// adding all new log entries to the log
						append_raft_log_entry( request_msg->entries[i] );
					}
				}

				/*
					we need to guarantee that leader_commit is not greater than prev_log_index
					this is required because the leader could have chopped the number of serialized log entries (see RMR max_msg_size),
					even though it has already commited further entries
					implementing this here simplifies the code, rather than in the raft send append entries thread
				*/
				request_msg->leader_commit = MIN( request_msg->leader_commit, request_msg->prev_log_index );

				if( me.commit_index < request_msg->leader_commit ) {
					// do not need to call raft commit log entries for every append entries msg
					raft_commit_log_entries( request_msg->leader_commit );
				}
			}	// if there is a conflict, we simply ignore that append entries message, sec 3.7 in raft dissertation

			response_msg->success = 1;

		} else {

			response_msg->success = 0;

		}

		// by replying the last_log_index the leader can cap the next index correctly and quickly (and simplifies the code)
		response_msg->last_log_index = get_raft_last_log_index( );

		pthread_mutex_lock( &election_timeout_lock );
		timespec_get( &election_timeout, TIME_UTC );
		timespec_add_ms( election_timeout, rand_timeout_ms );
		pthread_mutex_unlock( &election_timeout_lock );

	}

	pthread_mutex_unlock( &raft_state_lock );
}

void handle_append_entries_reply( reply_append_entries_t *reply_msg ) {
	term_match_e match;
	server_t *server = NULL;

	pthread_mutex_lock( &raft_state_lock );

	match = match_terms( reply_msg->term );

	// only has to process if state is LEADER and terms match (message delay)
	if( ( match == E_TERMS_MATCH ) && ( me.state == LEADER ) ) {
		server = raft_config_get_server( &reply_msg->server_id );
		if( server != NULL ) {

			/*
				if server is NON_VOTING_MEMBER and replies two subsequent rounds within the heartbeat timeout,
				we assume its log is up-to-date
				this check is called by the send append entries thread
			*/
			timespec_get( &server->replied_ts, TIME_UTC ); // currently used only to check of non_voting_member is caught-up

			pthread_mutex_lock( &server->index_lock );	// lock required to modify match_index, and commit_index

			/*
				If successful: update nextIndex and matchIndex for follower (§5.3 raft paper)
				If AppendEntries fails because of log inconsistency: decrement nextIndex and retry (§5.3 raft paper)

				Changed: now the request append entries message is returning last_log_index,
				so we can get this information from there all the times
				This allows to cap the correct next index accordingly more quickly withou back-and-forth
				of heartbeats
			*/
			server->match_index = reply_msg->last_log_index;
			server->next_index = reply_msg->last_log_index + 1;

			if( reply_msg->success ) {

				if( server->match_index > me.commit_index	) {	// ensuring that N > commitIndex
					raft_commit_log_entries( server->match_index );
				}
			}

			server->hb_timeouts = 0;	// will be incremented by the send append entries thread

			pthread_mutex_unlock( &server->index_lock );
		}
	}
	pthread_mutex_unlock( &raft_state_lock );
}

void handle_replication_request( replication_request_t *request, replication_reply_t *reply ) {
	server_t *server = NULL;
	unsigned int i;

	assert( request != NULL );
	assert( reply != NULL );

	strcpy( reply->server_id, me.self_id );

	server = raft_config_get_server( &request->server_id );
	/*
		we are expecting to match our indexes
	*/
	if( server ) {
		if( request->master_index == server->replica_index ) {

			if( apply_command_cb ) {	// just run callback if it was registered
				logger_debug( "applying  xapp state  replication for server %s", request->server_id );

				for( i = 0; i < request->n_entries; i++, server->replica_index++ ) {
					// calling registered callback
					apply_command_cb( request->entries[i]->command, request->entries[i]->context, request->entries[i]->key,
										request->entries[i]->data, request->entries[i]->dlen );
				}

				reply->success = 1;

			} else {
				logger_error( "there is no registered apply callback function, nothing applied" );
				reply->success = 0;
			}

		} else {
			reply->success = 0;
		}

		reply->replica_index = server->replica_index;

	} else {
		reply->replica_index = 0;
		reply->success = 0;
	}

}

void handle_replication_reply( replication_reply_t *reply ) {
	server_t *server = NULL;

	assert( reply != NULL );

	server = raft_config_get_server( &reply->server_id );

	// if( reply->success )	// even if success is 0, we have to update the replicated index
	if( server )
		server->master_index = reply->replica_index;	// master_index receives the confirmation of replicas' replica_index
}

/*
	Implements a election timeout thread

	This thread puts itself to sleep up to the next election_timeout

	When server becomes leader, this thread put itself to sleep. Requires a signal when happens a transition to FOLLOWER
*/
void *trigger_election_timeout( ) {
	int state;
	struct timespec now;
	rmr_mbuf_t *mbuf = NULL;

	mbuf = rmr_alloc_msg( mrc, RMR_MAX_RCV_BYTES );
	if( mbuf == NULL ) {
		logger_fatal( "unable to allocate memory on trigger_election_timeout: %s", strerror( errno ) );
		exit( 1 );
	}

	while ( 1 ) {

		pthread_mutex_lock( &raft_state_lock );
		state = me.state;
		pthread_mutex_unlock( &raft_state_lock );

		switch ( state ) {
			case FOLLOWER:
				timespec_get( &now, TIME_UTC );

				pthread_mutex_lock( &election_timeout_lock );
				if ( timespec_cmp( now, election_timeout, < ) ) {		// wait for the next election_timeout
					pthread_cond_timedwait( &election_timeout_cond, &election_timeout_lock, &election_timeout );
					pthread_mutex_unlock( &election_timeout_lock );

				} else {	// start a new leader election
					pthread_mutex_unlock( &election_timeout_lock );
					become_candidate( &mbuf );
				}
				break;

			case LEADER:	// just stops running this thread and waits for a signal originated when it became follower for some reason
				pthread_mutex_lock( &follower_lock );
				pthread_cond_wait( &follower_cond, &follower_lock );
				pthread_mutex_unlock( &follower_lock );

				pthread_mutex_lock( &election_timeout_lock );
				timespec_get( &election_timeout, TIME_UTC );
				timespec_add_ms( election_timeout, rand_timeout_ms );
				pthread_mutex_unlock( &election_timeout_lock );
				break;

			case INIT_SERVER:
				send_membership_request( &mbuf );

				break;

			default:
				logger_fatal( "unexpected server state %d in trigger_election_timeout\n", me.state);
				exit( 1 );
		}

	}

}

/*
	This function is in charge of getting a message from the task queue
	and dispatch it according to the message type
*/
void *worker( ) {
	unsigned int i;
	rmr_mbuf_t *msg = NULL;
	request_vote_t *req_vote_buf = NULL;
	request_vote_t *req_vote_msg = NULL;
	reply_vote_t *reply_vote_msg = NULL;
	membership_request_t *req_membership_msg = NULL;
	appnd_entr_hdr_t *appndtrs_hdr = NULL;
	request_append_entries_t *req_appndtrs_msg = NULL;
	reply_append_entries_t *reply_appndtrs_msg = NULL;
	repl_req_hdr_t *rep_hdr = NULL;		// replication request header
	replication_request_t *rep_req_msg = NULL;
	replication_reply_t *rep_reply_msg = NULL;
	unsigned char src_buf[RMR_MAX_SRC];	// rmr source address buffer used to connect to that server to send AppendEntries
	unsigned char *src_buf_res = NULL;	// used to identify if the rmr_get_src ran successfuly

	req_vote_buf = (request_vote_t *) malloc( sizeof( request_vote_t ) );
	if( req_vote_buf == NULL ) {
		logger_fatal( "unable to allocate buffer memory for req_vote_buf" );
		exit( 1 );
	}

	req_appndtrs_msg = (request_append_entries_t *) malloc( sizeof( request_append_entries_t ) );
	if( req_appndtrs_msg == NULL ) {
		logger_fatal( "unable to allocate buffer memory to the request append entries message" );
		exit( 1 );
	}
	req_appndtrs_msg->entries = NULL;	// assuring that realloc wont fail due an unsafe pointer

	rep_req_msg = (replication_request_t *) malloc( sizeof( replication_request_t ) );
	if( rep_req_msg == NULL ) {
		logger_fatal( "unable to allocate buffer memory to the replication request message" );
		exit( 1 );
	}
	rep_req_msg->entries = NULL;		// assuring that realloc wont fail due an unsafe pointer

	while( 1 ) {	// This is a worker thread that processes messages enqueued by the xApp

		msg = rft_dequeue_msg( );
		if( msg == NULL ) {
			logger_fatal( "unable to dequeue an RFT message on worker" );
			exit( 1 );
		}

		switch ( msg->mtype ) {
			case APPEND_ENTRIES_REQ:		// all servers
				appndtrs_hdr = (appnd_entr_hdr_t *) msg->payload;

				// copying data from header to msg, memcpy cannot be used here because of unaligned data
				appnd_entr_header_to_msg_cpy( appndtrs_hdr, req_appndtrs_msg );

				logger_trace( "receiving append entries request from %s, term: %lu, n_entries: %u, prev_idx: %lu, prev_term: %lu, commit: %lu",
								req_appndtrs_msg->leader_id, req_appndtrs_msg->term, req_appndtrs_msg->n_entries,
								req_appndtrs_msg->prev_log_index, req_appndtrs_msg->prev_log_term, req_appndtrs_msg->leader_commit );

				if( req_appndtrs_msg->n_entries ) {	// if there is no entry, it is a heartbeat

					// reallocating array to store pointers for deserialized append entries
					req_appndtrs_msg->entries = (log_entry_t **) realloc( req_appndtrs_msg->entries, req_appndtrs_msg->n_entries * sizeof( log_entry_t **) );
					if( req_appndtrs_msg->entries == NULL ) {
						logger_fatal( "unable to allocate memory for new log entries in append entries request" );
						exit( 1 );
					}

					// deserializing log entries delivered by the network message
					deserialize_raft_log_entries( APND_ENTR_PAYLOAD_ADDR( msg->payload ), req_appndtrs_msg->n_entries, req_appndtrs_msg->entries );
				}

				reply_appndtrs_msg = (reply_append_entries_t *) msg->payload;

				handle_append_entries_request( req_appndtrs_msg, reply_appndtrs_msg );

				logger_trace( "replying	append entries to %s, term: %lu, last_log_index: %lu, success: %d",
								req_appndtrs_msg->leader_id, reply_appndtrs_msg->term,
								reply_appndtrs_msg->last_log_index, reply_appndtrs_msg->success );

				rft_rts_msg( &msg, APPEND_ENTRIES_REPLY, sizeof( *reply_appndtrs_msg ), &req_appndtrs_msg->leader_id );

				/*
					there is no need to free deserialized log entries (like in REPLICATION_REQ)
					they are referenced by raft_log (check on append_raft_log_entry() )
				*/

				break;

			case APPEND_ENTRIES_REPLY:		// leader
				reply_appndtrs_msg = (reply_append_entries_t *) msg->payload;

				logger_trace( "receiving append entries reply from %s, term: %lu, last_log_index: %lu, success: %d",
								reply_appndtrs_msg->server_id, reply_appndtrs_msg->term,
								reply_appndtrs_msg->last_log_index, reply_appndtrs_msg->success );

				handle_append_entries_reply( reply_appndtrs_msg );

				break;

			case REPLICATION_REQ:
				rep_hdr = (repl_req_hdr_t *) msg->payload;

				// copying data from header to msg, memcpy cannot be used here because of unaligned data
				repl_req_header_to_msg_cpy( rep_hdr, rep_req_msg );

				logger_debug( "receiving replication request from %s, n_entries: %u, bytes: %u, master_index: %lu",
								rep_req_msg->server_id, rep_req_msg->n_entries, rep_hdr->slen, rep_req_msg->master_index );


				// reallocating array to store pointers for deserialized append entries
				rep_req_msg->entries = (log_entry_t **) realloc( rep_req_msg->entries, rep_req_msg->n_entries * sizeof( log_entry_t **) );
				if( rep_req_msg->entries == NULL ) {
					logger_fatal( "unable to reallocate memory for new log entries in replication request" );
					exit( 1 );
				}

				// deserializing log entries delivered by the network message
				deserialize_server_log_entries( REPL_REQ_PAYLOAD_ADDR( msg->payload ), rep_req_msg->n_entries, rep_req_msg->entries );

				rep_reply_msg = (replication_reply_t *) msg->payload;

				handle_replication_request( rep_req_msg, rep_reply_msg );

				logger_debug( "replying  replication request to %s, replica_index: %lu, success: %d",
								rep_req_msg->server_id, rep_reply_msg->replica_index, rep_reply_msg->success );

				rft_rts_msg( &msg, REPLICATION_REPLY, sizeof( *rep_reply_msg ), &rep_reply_msg->server_id );

				// freeing uneeded deserialized log entries after applying xapp's state
				for( i = 0; i < rep_req_msg->n_entries; i++ ) {
					free_log_entry( rep_req_msg->entries[i] );
				}

				break;

			case REPLICATION_REPLY:
				rep_reply_msg = (replication_reply_t *) msg->payload;

				logger_debug( "receiving replication reply from %s, replica_index: %lu, success: %d",
							rep_reply_msg->server_id, rep_reply_msg->replica_index, rep_reply_msg->success );

				handle_replication_reply( rep_reply_msg );

				break;

			case VOTE_REQ:		// followers and candidates
				req_vote_msg = (request_vote_t *) msg->payload;

				memcpy( req_vote_buf, req_vote_msg, sizeof( *req_vote_msg ) ); // needs a buffer since reply vote is going to change the rmr payload

				reply_vote_msg = (reply_vote_t *) msg->payload;

				if ( strcmp( req_vote_buf->candidate_id, me.self_id ) == 0 )
					break;		// nothing to do, it has already voted to himself (candidate)

				logger_debug( "receiving vote request from candidate %s => term: %lu, last_log_index: %lu, last_log_term: %lu",
								req_vote_buf->candidate_id, req_vote_buf->term,
								req_vote_buf->last_log_index, req_vote_buf->last_log_term );

				handle_vote_request( req_vote_buf, reply_vote_msg );

				logger_debug( "replying vote request from candidate %s => term: %lu granted: %d",
								req_vote_buf->candidate_id, reply_vote_msg->term, reply_vote_msg->granted );

				rft_rts_msg( &msg, VOTE_REPLY, sizeof( *reply_vote_msg ), &req_vote_buf->candidate_id ); // sending reply_vote_msg

				break;

			case VOTE_REPLY:		// candidates
				reply_vote_msg = ( reply_vote_t * ) msg->payload;		// receiving reply_vote_msg

				logger_debug( "receiving vote reply from %s => term: %lu granted: %d",
							reply_vote_msg->voter, reply_vote_msg->term, reply_vote_msg->granted );

				handle_vote_reply( reply_vote_msg );

				break;

			case MEMBERSHIP_REQ:	// leader
				req_membership_msg = (membership_request_t *) msg->payload;

				logger_debug( "receiving membership request from %s", req_membership_msg->server_id );

				memset( src_buf, 0, sizeof( src_buf ) );
				src_buf_res = rmr_get_src( msg, src_buf );
				if( src_buf_res == NULL ) {
					logger_error( "unable to get source address from server %s\tdiscarding request", req_membership_msg->server_id );
					break;
				}

				handle_membership_request( req_membership_msg, (char *) src_buf );	// replies are append entries

				break;

			default:
				logger_warn( "unrecognized rft message type: %d", msg->mtype);
				break;
		}

		rmr_free_msg( msg );			// message must be freed since it is a copy dequeued from rft's queue
	}

}
