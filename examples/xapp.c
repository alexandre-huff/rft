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
	Mnemonic:	xapp.c
	Abstract:	Implements an xapp that replicates its state using the RFT library

				Compile it with -DNORFT to disable the RFT LIBRARY

	Date:		9 November 2019
	Author:		Alexandre Huff
*/

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <errno.h>

#include <rmr/rmr.h>

#include <rft/rft.h>
#include <rft/logger.h>

#include "examples.h"

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

long my_state = 0;			// state maintained by this xapp (master)
long rep_state = 0;			// state replicated from another xapp (slave)

// function that will be called by the rft library
void apply_rstate( const int cmd, const char *context, const char *key, const unsigned char *data, const size_t dlen ) {
	long value = *((long *) data);	// the same type of rep_state that will be passed from *data

	switch ( cmd ) {
		case SET_RSTATE:
			rep_state = value;
			break;

		case ADD_RSTATE:
			rep_state += value;
			break;

		case SUB_RSTATE:
			rep_state -= value;
			break;

		default:
			logger_warn( "unrecognized FSM command" );
	}

	logger_warn( "replica's xapp state changed to %d, context: %s, key: %s", rep_state, context, key );
}


int main( int argc, char **argv ) {
	rmr_mbuf_t *msg = NULL;
	int 		ai = 1;					// argument index
	long		timeout;				// timeout to wait RMR configure routing table
	void		*mrc;					// msg router context
	char		*listen_port = NULL;	// RMR port to exchange messages
	int			max_retries = 1;		// RMR max retries before giving up and returning to the xapp with RMR_ERR_RETRY
	int			rts_retries = 0;		// max loop retries for rmr_rts_msg
	int			rts_count;

	#if LOGGER_LEVEL >= LOGGER_WARN
		unsigned char target[RMR_MAX_SRC];
		long num_msgs = 0;
	#endif

	// ===== Experiment =====
	mpl_t		*payload;				// payload received in the experiment
	long		count = 0;				// message counter for reporting purposes
	long		replied = 0;
	long		retries = 0;
	long		errors = 0;
	long		timeouts = 0;

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

				default:
					fprintf( stderr, "[FAIL] unrecognized option: %s\n", argv[ai] );
					fprintf( stderr, "\nUsage: %s [-p port] [-r max_rmr_retries] [-l max_rts_loop_retries]\n", argv[0] );
					exit( 1 );
			}

			ai++;
		} else {
			break;		// not an option, leave with a1 @ first positional parm
		}
	}

	if( ! listen_port )
		listen_port = "4560";

	mrc = rmr_init( listen_port, RMR_MAX_RCV_BYTES, RMRFL_NONE );
	if( mrc == NULL ) {
		logger_fatal( "unable to initialize RMR" );
		exit( 1 );
	}
	rmr_set_fack( mrc );

	if( rmr_set_stimeout( mrc, max_retries ) != RMR_OK )
		logger_error( "unable to set rmr max retries" );

	timeout = time( NULL ) + 20;
	while( ! rmr_ready( mrc ) ) {								// wait for RMR configuring the routing table
		logger_info( "waiting for RMR to show ready" );
		sleep( 1 );

		if( time( NULL ) > timeout ) {
			logger_fatal( "giving up" );
			exit( 1 );
		}
	}

	#ifndef NORFT
	rft_init( mrc, listen_port, RMR_MAX_RCV_BYTES, apply_rstate );
	#endif

	logger_info( "listening on port %s", listen_port );

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
					logger_trace( "%-*s type: %d, len: %3d, mrc: %p, msg: %p", LOGGER_PADDING, "receiving message", msg->mtype, msg->len, mrc, msg );

					#ifndef NORFT
					rft_enqueue_msg( msg );
					#endif
					break;

				case RAN1_MSG:
				case RAN2_MSG:
				case RAN3_MSG:
					payload = (mpl_t *) msg->payload;

					#if LOGGER_LEVEL >= LOGGER_WARN		// reseting counter when runnning a new experiment
						if( payload->seq == 1 ) {
							count = replied = retries = timeouts = errors = 0;
							num_msgs = payload->num_msgs;
						}
					#endif

					count++;

					#ifndef NORFT
					rft_replicate( SET_RSTATE, "REL1", "counter", (unsigned char *) &count, sizeof(long) );
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
								replied++;
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

							case RMR_ERR_TIMEOUT:
								timeouts++;
								break;

							default:
								logger_error( "send failed, msg: %ld, mtype: %d, mstate: %d, errno: (%d) %s",
											count, msg->mtype, msg->state, errno, strerror( errno ) );
								errors++;
						}

					} else {
						logger_fatal( " extreme failure, unable to send message using RMR" );
						exit( 1 );
					}

					#if LOGGER_LEVEL >= LOGGER_WARN
						if( num_msgs == count ) {
							logger_info( "========== Requests: %ld\tReplied: %ld\tRetries: %ld\tTimeouts: %ld\tErrors: %ld ==========",
							count, replied, retries, timeouts, errors );
						}
					#endif

					break;

				default:
					logger_warn( "unrecognized message type: %d", msg->mtype);
				break;
			}
		}
	}

	return 0;
}