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
	Mnemonic:	xapp2.c
	Abstract:	Implements a multi-threaded xapp with state replication

				Compile it with -DNORFT to disable the RFT LIBRARY

	Date:		24 February 2020
	Author:		Alexandre Huff
*/

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>

#include <rmr/rmr.h>

#include "rft.h"
#include "logger.h"
#include "../../src/static/hashtable.c"

#include "app.h"

#define MAX_RCV_BYTES 128000

#if LOGGER_LEVEL >= LOGGER_INFO
	#define LOCK( mutex ) pthread_mutex_lock( mutex )
	#define UNLOCK( mutex ) pthread_mutex_unlock( mutex )
#else
	#define LOCK( mutex )
	#define UNLOCK( mutex )
#endif

// commands for the state machine
enum commands {
	SET_RSTATE,
	ADD_RSTATE,
	SUB_RSTATE
};

// message numbers received from RMR (simulate RAN-MSG)
enum recv_msg {
	SET = 500,
	ADD,
	SUB
};

int		rts_retries = 0;		// max loop retries for rmr_rts_msg

/* ===== stats variables ===== */
#if LOGGER_LEVEL >= LOGGER_INFO
	pthread_mutex_t count_lock = PTHREAD_MUTEX_INITIALIZER;
	pthread_mutex_t reply_lock = PTHREAD_MUTEX_INITIALIZER;
	long	count = 0;			// only for reporting purposes
	long	last_count = 0;		// only for reporting purposes
#endif
long	replied = 0;
long	retries = 0;
long	errors = 0;
long	sfailed = 0;
/* ===== stats variables up to here ===== */

const long ivalue = 1;		// increment value
pthread_mutex_t fsm_lock = PTHREAD_MUTEX_INITIALIZER;	// state machine lock
static hashtable_t *ctx_table = NULL;

volatile sig_atomic_t ok2run = 1;

void signal_handler(int signal) {
  switch (signal) {
    case SIGINT:
      logger_info("SIGINT was received");
      break;

    case SIGTERM:
      logger_info("SIGTERM was received");
      break;

    default:
      logger_warn("signal handler received signal %s", strsignal(signal));
      break;
  }
  ok2run = 0;
}

// function that will be called by the rft library
void apply_state( const int cmd, const char *context, const char *key, const unsigned char *data, const size_t dlen ) {
	void *ptr;
	long *ctx_data;

	LOCK( &fsm_lock );

	ptr = hashtable_get( ctx_table, context );
	if( ptr ) {
		ctx_data = (long *) ptr;

	} else {
		ctx_data = (long *) malloc( sizeof(long) );
		*ctx_data = 0;
		if( ! hashtable_insert( ctx_table, context, (void *) ctx_data ) ) {
			fprintf( stderr, "unable to store initial state for context %s\n", context );
			exit( 1 );
		}
	}

	switch ( cmd ) {
		case SET_RSTATE:
			*ctx_data = (long) *data;
			break;

		case ADD_RSTATE:
			*ctx_data += (long) *data;
			break;

		case SUB_RSTATE:
			ctx_data -= (long) *data;
			break;

		default:
			logger_warn( "unrecognized FSM command" );
	}

	// logger_warn( "state changed to %ld, context: %s, key: %s", *ctx_data, context, key );

	UNLOCK( &fsm_lock );
}

// function called by the rft library
size_t take_snapshot( char **contexts, int nctx, unsigned int *items, unsigned char **data ) {
	/* an example of iterating over contexts
	int i;
	for( i = 0; i < nctx; i++ ) {
		hashtable_get( table, contexts[i] )
		// it could be: clen|context|klen|key|vlen|value|clen|context|klen|key|vlen|value...
		memcpy( data, &state, size );
		// each item corresponds to: clen|context|klen|key|vlen|value
	} */
	void *ctx_data;
	size_t size = 1024;	// we start the snapshot with size of 1K
	size_t offset = 0;
	int i;
	int clen;
	int vlen;

	*data = realloc( *data, size );
	if( *data == NULL ) {
		logger_fatal( "unable to reallocate data while taking snapshot" );
		exit( 1 );	// this will only exit the child process
	}

	for( i = 0; i < nctx; i++ ) {
		clen = strlen( contexts[i] );
		vlen = sizeof(long);	// size of our context data
		ctx_data = hashtable_get( ctx_table, contexts[i] );

		// ckecking if the snapshot data needs to resized
		if( ( offset + clen + vlen + sizeof(int)*2 ) > size ) {
			*data = realloc( *data, size );
			if( *data == NULL ) {
				logger_fatal( "unable to reallocate/resize data while taking snapshot" );
				exit( 1 );	// this will only exit the child process
			}
		}

		memcpy( *data+offset, &clen, sizeof(int) );
		offset += sizeof(int);
		memcpy( *data+offset, contexts[i], clen );
		offset += clen;

		memcpy( *data+offset, &vlen, sizeof(int) );
		offset += sizeof(int);
		memcpy( *data+offset, ctx_data, vlen );
		offset += vlen;
	}

	*items = i;

	return offset;	// size of the snapshot data
}

void install_snapshot( unsigned int items, const unsigned char *data ) {
	/* an example of iterating over contexts
	int i;
	for( i = 0; i < items; i++ ) {
		// it could be: clen|context|klen|key|vlen|value|clen|context|klen|key|vlen|value...
		memcpy( &value, &state, vlen );
		// might involve use of hashtable to get the context and its corresponding keys
	} */
	int i;
	size_t offset = 0;
	int clen;
	int vlen;
	char context[256];
	void *ptr;
	long *ctx_data;

	// we need to lock the whole state machine while installing a snapshot
	LOCK( &fsm_lock );
	for( i = 0; i < items; i++ ) {
		memcpy( &clen, data+offset, sizeof(int) );
		offset += sizeof(int);
		memcpy( context, data+offset, clen );
		context[clen] = '\0';
		offset += clen;

		ptr = hashtable_delete( ctx_table, context );
		if( ptr )
			free( ptr );

		ctx_data = (long *) malloc( sizeof(long) );
		if( ! ctx_data ) {
			fprintf( stderr, "unable to allocate memory to install snapshot\n");
			exit( 1 );
		}

		memcpy( &vlen, data+offset, sizeof(int) );
		offset += sizeof(int);
		memcpy( ctx_data, data+offset, vlen );
		offset += vlen;

		if( ! hashtable_insert( ctx_table, context, (void *) ctx_data ) ) {
			fprintf( stderr, "unable to store snapshot state for context %s\n", context );
			exit( 1 );
		}
		logger_warn( "snapshot installed, context: %s, ctx_data: %ld", context, *ctx_data );
	}
	UNLOCK( &fsm_lock );
}

void *listener( void *mrc ) {
	rmr_mbuf_t	*msg = NULL;
	int			rts_count;
	role_e		role;
	unsigned char meid[RMR_MAX_MEID];

	#if LOGGER_LEVEL >= LOGGER_ERROR
		mpl_t		*payload;
	#endif

	#if LOGGER_LEVEL >= LOGGER_WARN
		unsigned char target[RMR_MAX_SRC];
	#endif

	while ( ok2run ) { // listener for all incomming messages
		msg = rmr_torcv_msg( mrc, msg, 2000 );

		if ( msg && msg->state == RMR_OK ) {

			switch ( msg->mtype ) {

				case APPEND_ENTRIES_REQ:
				case APPEND_ENTRIES_REPLY:
				case VOTE_REQ:
				case VOTE_REPLY:
				case MEMBERSHIP_REQ:
				case REPLICATION_REQ:
				case REPLICATION_REPLY:
				case XAPP_SNAPSHOT_REQ:
				case XAPP_SNAPSHOT_REPLY:
				case RAFT_SNAPSHOT_REQ:
				case RAFT_SNAPSHOT_REPLY:
					// logger_trace( "%-*s type: %d, len: %3d, mrc: %p, msg: %p", LOGGER_PADDING, "receiving message", msg->mtype, msg->len, mrc, msg );

					#ifndef NORFT
					rft_enqueue_msg( msg );
					#endif
					break;

				case RAN1_MSG:
				case RAN2_MSG:
				case RAN3_MSG:
				case RAN4_MSG:
					#if LOGGER_LEVEL >= LOGGER_ERROR
						payload = (mpl_t *) msg->payload;
					#endif

					#ifndef NORFT
						if( rmr_get_meid( msg, meid ) == NULL ){
							logger_fatal( "unable to get meid from the message" );
							exit( 1 );
						}
						role = get_role( meid );
						if( role != RFT_BACKUP ) {
							// using meid as the context in this test example
							rft_replicate( ADD_RSTATE, (char *)meid , "UE_Counter", (unsigned char *) &ivalue, sizeof(long), meid, role );
						}
					#endif

					msg = rmr_rts_msg( mrc, msg );
					rts_count = rts_retries;
					while(msg && msg->state != RMR_OK && rts_count ) {
						rts_count--;
						usleep( 2 );
						msg = rmr_rts_msg( mrc, msg );
					}
					if( msg != NULL ) {
						switch( msg->state ) {
							case RMR_OK:
								LOCK( &reply_lock );
								replied++;
								UNLOCK( &reply_lock );
								break;

							case RMR_ERR_SENDFAILED:
								sfailed++;
								#if LOGGER_LEVEL >= LOGGER_ERROR
									logger_error( "reply failed, mtype: %d, state: %d, strerr: %s",
													msg->mtype, msg->state, strerror( errno ) );
								#endif
								break;

							case RMR_ERR_RETRY:
								retries++;
								#if LOGGER_LEVEL >= LOGGER_WARN
									if( rts_retries ) {		// only show retries by using rts loop (without loop generates a lot of retry messages)
										rmr_get_src( msg, target );
										strtok( (char *) target, ":" );	// replacing ':' to '\0'
										logger_warn( "message dropped with state RMR_ERR_RETRY, seq: %ld, target: %s, mtype: %d",
													payload->seq, (char *) target, msg->mtype );
									}
								#endif
								break;

							// case RMR_ERR_TIMEOUT:
							default:
								logger_error( "reply failed, msg: %ld, mtype: %d, mstate: %d, errno: (%d) %s",
											payload->seq, msg->mtype, msg->state, errno, strerror( errno ) );
								errors++;
						}

					} else {
						logger_fatal( "extreme failure, unable to send message using RMR");
						exit( 1 );
					}

					LOCK( &count_lock );
					#if LOGGER_LEVEL >= LOGGER_INFO
						count++;
					#endif
					UNLOCK( &count_lock );

					break;

				default:
					logger_warn( "unrecognized message type: %d", msg->mtype);
				break;
			}
		} else {
			if( msg )
			logger_error( "unable to receive message, type :%d, state: %d, errno: %d", msg->mtype, msg->state, errno );
		}
	}

	rft_shutdown( );

	return NULL;
}


int main( int argc, char **argv ) {
	int			i;
	int			ai = 1;					// argument index
	long		timeout;				// timeout to wait RMR configure routing table
	void		*mrc;					// msg router context
	char		*listen_port = NULL;	// RMR port to exchange messages
	int			max_retries = 1;		// RMR max retries before giving up and returning to the xapp with RMR_ERR_RETRY
	pthread_t	*threads;
	int			nthreads = 1;			// number of receiver threads
	int			ret;					// general return code
	char		wbuf[8];

	while( ai < argc ) {
		if( *argv[ai] == '-' ) {
			switch( argv[ai][1] ) {
				case 'p':
					ai++;
					listen_port = argv[ai];
					break;

				case 'r':
					ai++;
					max_retries = atoi( argv[ai] );
					break;

				case 'l':
					ai++;
					rts_retries = atoi( argv[ai] );
					break;

				case 'n':
					ai++;
					nthreads = atoi( argv[ai] );
					if( nthreads < 1)
						nthreads = 1;
					break;

				default:
					fprintf( stderr, "[FAIL] unrecognized option: %s\n", argv[ai] );
					fprintf( stderr, "\nUsage: %s [-p port] [-r max_rmr_retries] [-l max_rts_loop_retries] [-n num_threads]\n", argv[0] );
					exit( 1 );
			}

			ai++;
		} else {
			break;		// not an option, leave with a1 @ first positional parm
		}
	}

	if( ! listen_port )
		listen_port = "4560";

	srand( time( NULL ) );
	if( getenv( "RMR_RTG_SVC" ) == NULL ) {		// setting random listener port
		snprintf( wbuf, sizeof(wbuf), "%d", 19000 + ( rand() % 1000 ) );
		setenv( "RMR_RTG_SVC", wbuf, 1 );		// set one that won't collide with the default port if running on same host
	}

	threads = (pthread_t *) malloc( nthreads * sizeof( pthread_t ) );
	if( threads == NULL ) {
		logger_fatal( "unable to allocate memory to initilize threads" );
		exit( 1 );
	}

	mrc = rmr_init( listen_port, MAX_RCV_BYTES, RMRFL_NONE );
	if( mrc == NULL ) {
		logger_fatal( "unable to initialize RMR" );
		exit( 1 );
	}
	rmr_set_fack( mrc );

	if( rmr_set_stimeout( mrc, max_retries ) != RMR_OK )
		logger_error( "unable to set rmr max retries" );

	timeout = time( NULL ) + 20;
	while( ! rmr_ready( mrc ) ) {								// wait for RMR configuring the route table
		logger_info( "waiting for RMR to show ready" );
		sleep( 1 );

		if( time( NULL ) > timeout ) {
			logger_fatal( "giving up" );
			exit( 1 );
		}
	}

	ctx_table = hashtable_new( STRING_KEY, 503 );

	#ifndef NORFT
	rft_init( mrc, listen_port, MAX_RCV_BYTES, apply_state, take_snapshot, install_snapshot );
	#endif

	/* ===== Creating threads ===== */
	for( i = 0; i < nthreads; i++ ) {
		ret = pthread_create( &threads[i], NULL, listener, mrc );
		if( ret != 0 ) {
			logger_fatal( "error on creating thread: %s\n", strerror( ret ) );
			exit( 1 );
		}
	}

	/* ===== installing signal handlers ===== */
    struct sigaction sa;
    sa.sa_handler = &signal_handler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

	/* ===== up and running ===== */
	logger_info( "listening on port %s", listen_port );

	/* ===== Stats ===== */
	#if LOGGER_LEVEL >= LOGGER_INFO
		while( ok2run ) {
			sleep( 5 );
			if( last_count != count ) {
				logger_info( "========== Requests: %ld\tReplied: %ld\tRetries: %ld\tSend Failed: %ld\t\tErrors: %ld ==========",
							count, replied, retries, sfailed, errors );
							last_count = count;
			}
		}
	#endif

	for( i = 0; i < nthreads; i++ ) {
		pthread_join( threads[i], NULL );
	}
	free( threads );

	return 0;
}
