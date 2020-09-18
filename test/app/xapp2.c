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

#include <rmr/rmr.h>

#include "rft.h"
#include "logger.h"

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

long pri_state = 0;			// state maintained by this xapp (primary)
long bkp_state = 0;			// state replicated from another xapp (backup)

pthread_mutex_t pri_lock = PTHREAD_MUTEX_INITIALIZER;	// primary state lock
pthread_mutex_t bkp_lock = PTHREAD_MUTEX_INITIALIZER;	// backup state lock
const long ivalue = 1;		// increment value

// function that will be called by the rft library
void apply_rstate( const int cmd, const char *context, const char *key, const unsigned char *data, const size_t dlen ) {

	switch ( cmd ) {
		case SET_RSTATE:
			pthread_mutex_lock( &bkp_lock );
			bkp_state = (long) *data;
			pthread_mutex_unlock( &bkp_lock );
			break;

		case ADD_RSTATE:
			pthread_mutex_lock( &bkp_lock );
			bkp_state += (long) *data;
			pthread_mutex_unlock( &bkp_lock );
			break;

		case SUB_RSTATE:
			pthread_mutex_lock( &bkp_lock );
			bkp_state -= (long) *data;
			pthread_mutex_unlock( &bkp_lock );
			break;

		default:
			logger_warn( "unrecognized FSM command" );
	}

	// logger_warn( "replica's xapp state changed to %d, context: %s, key: %s", bkp_state, context, key );
}

// function called by the rft library
size_t take_snapshot( char **contexts, int nctx, unsigned int *items, unsigned char **data ) {
	// an example of iterating over contexts
	/* int i;
	for( i = 0; i < nctx; i++ ) {
		hashtable_get( table, contexts[i] )
		// it could be: clen|context|klen|key|vlen|value|clen|context|klen|key|vlen|value...
		memcpy( data, &state, size );
		// each item corresponds to: clen|context|klen|key|vlen|value
	} */

	*data = realloc( *data, sizeof(long) );
	if( *data == NULL ) {
		logger_fatal( "unable to reallocate data while taking snapshot" );
		exit( 1 );	// this will only exit the child process
	}

	*items = 1;

	memcpy( *data, &pri_state, sizeof(long) );

	return sizeof(long);	// size of the snapshot data
}

void install_snapshot( unsigned int items, const unsigned char *data ) {
	// an example of iterating over contexts
	/* int i;
	for( i = 0; i < items; i++ ) {
		// it could be: clen|context|klen|key|vlen|value|clen|context|klen|key|vlen|value...
		memcpy( &value, &state, vlen );
		// might involve use of hastable to get the context and its corresponding keys
	} */
	pthread_mutex_lock( &bkp_lock );

	memcpy( &bkp_state, data, sizeof(long) );
	logger_warn( "snapshot installed, bkp_state: %ld", bkp_state );

	pthread_mutex_unlock( &bkp_lock );
}

void *listener( void *mrc ) {
	rmr_mbuf_t	*msg = NULL;
	int			rts_count;

	#if LOGGER_LEVEL >= LOGGER_ERROR
		mpl_t		*payload;
	#endif

	#if LOGGER_LEVEL >= LOGGER_WARN
		unsigned char target[RMR_MAX_SRC];
	#endif

	while ( 1 ) { // listener for all incomming messages
		msg = rmr_rcv_msg( mrc, msg );

		if ( msg && msg->state == RMR_OK ) {

			switch ( msg->mtype ) {

				case APPEND_ENTRIES_REQ:
				case APPEND_ENTRIES_REPLY:
				case VOTE_REQ:
				case VOTE_REPLY:
				case MEMBERSHIP_REQ:
				case REPLICATION_REQ:
				case REPLICATION_REPLY:
				case SNAPSHOT_REQ:
				case SNAPSHOT_REPLY:
					logger_trace( "%-*s type: %d, len: %3d, mrc: %p, msg: %p", LOGGER_PADDING, "receiving message", msg->mtype, msg->len, mrc, msg );

					#ifndef NORFT
					rft_enqueue_msg( msg );
					#endif
					break;

				case RAN1_MSG:
				case RAN2_MSG:
				case RAN3_MSG:
					#if LOGGER_LEVEL >= LOGGER_ERROR
						payload = (mpl_t *) msg->payload;
					#endif

					/*
						On multi-threaded xApps, all primary contexts should to be locked before the last change of the
						same thread that is going to replicate that change.
						The lock can be released right after the replicate function returns to the xApp code.
						This is required since all primary contexts need to be preserved up to the fork system call returns,
						and we have a copy of the whole process' memory.
						If the lock is not acquired, than the state may get inconsistent among the replicas and the master server.
						IMPORTANT: this lock is not required if you are implementing xapps using the approximate state approach
					*/
					pthread_mutex_lock( &pri_lock );
					#ifndef NORFT
						rft_replicate( ADD_RSTATE, "UE_RAN_Element", "UE_Counter", (unsigned char *) &ivalue, sizeof(long) );
					#endif
					/*
						The local state needs to come after rft_replicate, as when a snapshot is taken the local state (pri_state)
						would already be modified, and the "same local" command would be applied twice in the backup replicas, thus
						leading to inconsistent states among different replicas
					*/
					pri_state++;
					pthread_mutex_unlock( &pri_lock );

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
									logger_error( "reply failed, mtype: %d, state: %d, strerr: %s\n",
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
			logger_error( "unable to receive message, type :%d, state: %d, errno: %d", msg->mtype, msg->state, errno );
		}
	}

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

	#ifndef NORFT
	rft_init( mrc, listen_port, MAX_RCV_BYTES, apply_rstate, take_snapshot, install_snapshot );
	#endif

	/* ===== Creating threads ===== */
	for( i = 0; i < nthreads; i++ ) {
		ret = pthread_create( &threads[i], NULL, listener, mrc );
		if( ret != 0 ) {
			logger_fatal( "error on creating thread: %s\n", strerror( ret ) );
			exit( 1 );
		}
	}

	logger_info( "listening on port %s", listen_port );

	/* ===== Stats ===== */
	#if LOGGER_LEVEL >= LOGGER_INFO
		while( 1 ) {
			sleep( 5 );
			if( last_count != count ) {
				logger_info( "========== Requests: %ld\tReplied: %ld\tRetries: %ld\tSend Failed: %ld\t\tErrors: %ld ==========",
							count, replied, retries, sfailed, errors );
							last_count = count;
			}
			}
	#endif
	// unreachable with LOGGER_INFO
	for( i = 0; i < nthreads; i++ ) {
			pthread_join( threads[i], NULL );
	}
	free( threads );

	return 0;
}
