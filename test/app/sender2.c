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
	Mnemonic:	sender2.c
	Abstract:	Measures latency and throughput of messages sent to a server

				This sender uses epool_wait instead of separate sender and
				receiver threads

				Command line options and parameters:
					- Run program passing "-" as argument

	Date:		03 February 2020
	Author:		Alexandre Huff
*/

#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <time.h>
#include <fcntl.h>
#include <sched.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <math.h>
#include <limits.h>

#include <rmr/rmr.h>

#include "app.h"


void *mrc = NULL;				// msg router context
long num2send;					// number of messages to be transmitted
int mtype = RAN1_MSG;			// default RMR message type
unsigned long interval = 0;		// waiting time (nanoseconds) before send the next message
int max_timeout = 2000;			// max timeout for waiting a reply message (ms)

struct	timespec begin_ts;		// time we begin to send messages
struct	timespec end_ts;		// time we sent all messages
long	replies = 0;
long	retries = 0;
long	lost = 0;				// lost messages, mainly due to RMR_ERR_RETRY or RMR_ERR_SENDFAILED on the xApp
long	*msg_rtt;				// array to store each message latency in mu-sec (round trip time)
long	*msg_seq;				// array to store each message sequence number
/*
	number of targets in routing table that will receive the same copy of message
	this is "the ; rule in rt" (not round-robin)
*/
int		targets = 1;

// ---------------------------------------------------------------------------
/*
	Compute the elapsed time between ts1 and ts2.
	Author:		E. Scott Daniels
*/
static inline long elapsed( struct timespec* start_ts, struct timespec* end_ts ) {
	long long start;
	long long end;
	long bin;

	start = ( start_ts->tv_sec * 1000000000) + start_ts->tv_nsec;
	end = ( end_ts->tv_sec * 1000000000) + end_ts->tv_nsec;

	// bin = (end - start) / 1000000;			// msec
	bin = (end - start) / 1000;					// mu-sec

	return bin;
}

/*
	Calculates the standard deviation from a sample size
	In this case, the sample size is the number of replies of the total requests
*/
double calculate_stddev( double mean ) {
	double stddev;
	double value;
	double sum_squares = 0.0;
	long i;

	for( i = 0; i < replies; i++ ) {
		value = (double) msg_rtt[i] - mean;
		sum_squares += pow( value, 2.0 );
	}

	stddev = sqrt( sum_squares / (replies - 1) );

	return stddev;
}

int main( int argc, char** argv ) {
	int			ai = 1;						// arg index
	long		i;
	int			listen_port = 0;			// the port we open for "backhaul" connections
	int			max_retries = 1;			// number of RMR retries before giving up sending message (RMR_ERR_RETRY)
	int			rtt = 0;					// defines if rtt timestamp records will be saved in a file
	char		wbuf[128];					// short writing buffer

	struct		epoll_event events[1];		// list of events to give to epoll
	struct		epoll_event epe;			// event definition for event to listen to
	int			ep_fd = -1;					// epoll's file descriptor (given to epoll_wait)
	int			rcv_fd;						// file that SI95 tickles -- give this to epoll to listen on
	long		count = 0;					// sender's message counter
	rmr_mbuf_t	*sbuf;						// send message buffer
	rmr_mbuf_t	*rbuf;						// receive message buffer
	mpl_t		*payload;					// the payload in a message
	int			nready;						// number of events ready to receive
	struct 		timespec nanots = {0, 0 };	// timespec for nanosleep
	char 		hostname[HOST_NAME_MAX + 1];	// meid of this sender

	float		running_time;
	float		rate_sec;
	long long 	sum_latencies;
	float		max_latency;
	float		min_latency;
	double		avg_latency;
	double		stddev_latency;
	int			ret;						// function return buffer

	FILE		*fp;
	char		*filename = NULL;
	struct stat sb;							// fstat buffer


	num2send = 10;							// number of messages to be transmitted

	// ---- simple arg parsing ------
	while( ai < argc ) {
		if( *argv[ai] == '-' ) {
			switch( argv[ai][1] ) {
				case 'p':
					ai++;
					listen_port = atoi( argv[ai] );
					break;

				case 'n':					// num msgs to send
					ai++;
					num2send = atol( argv[ai] );
					break;

				case 'm':
					ai++;
					mtype = atoi( argv[ai] );
					break;

				case 'i':					// interval to send in nanosecconds
					ai++;
					interval = strtoul( argv[ai], NULL, 10 );
					break;

				case 't':					// max waiting timeout for a reply in ms
					ai++;
					max_timeout = atoi( argv[ai] );
					break;

				case 'r':
					ai++;
					max_retries = atoi( argv[ai] );
					if ( max_retries < 0 )
						max_retries = 0;
					break;

				case 'g':
					ai++;
					targets = atoi( argv[ai] );
					break;

				case 'o':
					ai++;
					filename = argv[ai];
					break;

				case 's':					// saves all RTT timestamps in a file
					rtt = 1;
					break;

				default:
					fprintf( stderr, "\nUsage: %s [-n send msgs] [-m mtype] [-p listen port] [-i interval nsec] [-t reply timeout ms]"
									 " [-r retry attempts] [-g rt targets] [-o output filename] [-s] [-l]\n", argv[0] );
					fprintf( stderr, "\t\tOptions:\n" );
					fprintf( stderr, "\t\t[-s] saves all reply RTT records in the output file (-o is required)\n" );
					fprintf( stderr, "\t\t[-g] waits replies for the specified number of targets (each receives the same msg copy)\n\n" );
					exit( 1 );
			}

			ai++;
		} else {
			break;		// not an option, leave with a1 @ first positional parm
		}
	}

	msg_rtt = (long *) malloc( num2send * targets * sizeof(long) );
	if( msg_rtt == NULL ) {
		fprintf( stderr, "[FAIL] unable to allocate memory for msg_rtt array\n" );
		exit( 1 );
	}

	msg_seq = (long *) malloc( num2send * targets * sizeof(long) );
	if( msg_seq == NULL ) {
		fprintf( stderr, "[FAIL] unable to allocate memory for msg_seq array\n" );
		exit( 1 );
	}

	srand( time( NULL ) );

	if( getenv( "RMR_RTG_SVC" ) == NULL ) {		// setting random listener port
		snprintf( wbuf, sizeof(wbuf), "%d", 19000 + ( rand() % 1000 ) );
		setenv( "RMR_RTG_SVC", wbuf, 1 );		// set one that won't collide with the default port if running on same host
	}

	if( listen_port ) {
		snprintf( wbuf, sizeof( wbuf ), "%d", listen_port );
	} else {
		snprintf( wbuf, sizeof(wbuf), "%d", 43000 + ( rand() % 1000) );			// random listen port
	}

	if( getenv( "RMR_SEED_RT" ) == NULL )
		fprintf( stderr, "[FAIL] RMR_SEED_RT is not set\n" );

	fprintf( stderr, "[INFO] listening port: %s; sending %ld messages at interval of %lu nanosec\n", wbuf, num2send, interval );

	if( (mrc = rmr_init( wbuf, RMR_MAX_RCV_BYTES, RMRFL_NONE )) == NULL ) {
		fprintf( stderr, "[FAIL] unable to initialize RMR\n" );
		exit( 1 );
	}
	rmr_set_fack( mrc );

	rcv_fd = rmr_get_rcvfd( mrc );
	if( rcv_fd < 0 ) {
		fprintf( stderr, "[FAIL] unable to set up polling fd\n" );
		exit( 1 );
	}
	if( (ep_fd = epoll_create1( 0 )) < 0 ) {
		fprintf( stderr, "[FAIL] unable to create epoll fd: %d\n", errno );
		exit( 1 );
	}
	epe.events = EPOLLIN;
	epe.data.fd = rcv_fd;

	if( epoll_ctl( ep_fd, EPOLL_CTL_ADD, rcv_fd, &epe ) != 0 )	{
		fprintf( stderr, "[FAIL] epoll_ctl status not 0 : %s\n", strerror( errno ) );
		exit( 1 );
	}

	fprintf( stderr, "[INFO] RMR initialized\n" );

	while( ! rmr_ready( mrc ) ) {
		sleep( 1 );
	}

	if( rmr_set_stimeout( mrc, max_retries ) != RMR_OK )
		fprintf ( stderr, "[WARN] unable to set rmr max retries\n");

	if( gethostname( hostname, RMR_MAX_MEID - 1 ) != 0 ) {
		fprintf ( stderr, "[FAIL] unable to get hostname to set meid\n");
		exit( 1 );
	}
	fprintf ( stderr, "meid: %s\n", hostname );

	fprintf( stderr, "[INFO] sending messages...\n" );

	/***************** SENDER *****************/
	sbuf = rmr_alloc_msg( mrc, sizeof( mpl_t ) );			// send buffer
	rbuf = rmr_alloc_msg( mrc, sizeof( mpl_t ) );			// receive buffer

	replies = 0;

	timespec_add_ns( nanots, interval );		// defined interval between each message send

	clock_gettime( CLOCK_REALTIME, &begin_ts );
	while( count < num2send ) {								// we send n messages
		if( !sbuf ) {
			fprintf( stderr, "[FAIL] mbuf is nil?\n" );
			exit( 1 );
		}

		count++;

		payload = (mpl_t *) sbuf->payload;
		payload->num_msgs = num2send;	// create a variable with the value of num2send / targets (int div)
		payload->seq = count;
		clock_gettime( CLOCK_REALTIME, &payload->out_ts );

		sbuf->mtype = mtype;
		sbuf->sub_id = -1;
		sbuf->len =	sizeof( *payload ) ;
		sbuf->state = 0;
		if( rmr_str2meid( sbuf, (unsigned char *) hostname ) != RMR_OK ) {
			fprintf( stderr, "[FAIL] unable to set meid in the message\n" );
			exit( 1 );
		}

		sbuf = rmr_send_msg( mrc, sbuf );
		switch( sbuf->state ) {
			case RMR_OK:
				break;

			case RMR_ERR_RETRY:
			case RMR_ERR_TIMEOUT:
				retries++;
				break;

			default:
				fprintf( stderr, "[ERR] send failed, mtype: %d, state: %d, strerr: %s\n", sbuf->mtype, sbuf->state, strerror( errno ) );
				exit( 1 );
		}

		/***************** RECEIVER *****************/

		while( (nready = epoll_wait( ep_fd, events, 1, 0 )) > 0 ) {			// if something ready to receive (non-blocking check)
			if( events[0].data.fd == rcv_fd ) {			 				// we only are waiting on 1 thing, so [0] is ok
				errno = 0;

				rbuf = rmr_rcv_msg( mrc, rbuf );
				if( rbuf ) {
					if( rbuf->state == RMR_OK && rbuf->mtype == mtype ) {
					// if( rbuf->state == RMR_OK && rbuf->mtype == 150 ) {
						payload = (mpl_t *) rbuf->payload;

						clock_gettime( CLOCK_REALTIME, &end_ts );	// time stamp of last message (and in_ts) received to compute rate
						msg_rtt[replies] = elapsed( &payload->out_ts, &end_ts );
						msg_seq[replies] = payload->seq;
						replies++;									// message received

					}
				}
			}
		}

		if ( interval )
			nanosleep( &nanots, NULL );
	}

	/***************** RECEIVER AFTER ALL MESSAGES WERE SENT *****************/
	num2send *= targets;		// we want to receive n messages (each target received the same msg copy -g)
	while( replies < num2send ) {
		if( (nready = epoll_wait( ep_fd, events, 1, max_timeout )) > 0 ) {	// waits up to max_timeout to receive the last msg
			if( events[0].data.fd == rcv_fd ) {			 				// we only are waiting on 1 thing, so [0] is ok
				errno = 0;

				rbuf = rmr_rcv_msg( mrc, rbuf );
				if( rbuf ) {
					if( rbuf->state == RMR_OK && rbuf->mtype == mtype ) {
					// if( rbuf->state == RMR_OK && rbuf->mtype == 150 ) {
						payload = (mpl_t *) rbuf->payload;

						clock_gettime( CLOCK_REALTIME, &end_ts );	// timestamp of the last message (and in_ts) received to compute rate
						msg_rtt[replies] = elapsed( &payload->out_ts, &end_ts );
						msg_seq[replies] = payload->seq;
						replies++;									// message received

					}
				}
			}
		} else {
			break;		// nothing received within max timeout, giving up
		}
	}

	if( !replies )
		clock_gettime( CLOCK_REALTIME, &end_ts );	// get time in case no reply, just for report

	/***************** REPORT *****************/

	fprintf( stderr, "[INFO] generating report...\n" );

	running_time = elapsed( &begin_ts, &end_ts ) / 1000000.0;		// mu-sec to sec

	sum_latencies = max_latency = min_latency = msg_rtt[0];
	for( i = 1; i < replies; i++ ){
		sum_latencies += msg_rtt[i];
		if( msg_rtt[i] > max_latency )
			max_latency = msg_rtt[i];
		if( msg_rtt[i] < min_latency )
			min_latency = msg_rtt[i];
	}
	max_latency /= 1000.0;	// mu-sec to ms
	min_latency /= 1000.0;

	if( replies ) {
		rate_sec = ( replies ) / running_time;
		avg_latency = ( sum_latencies * 1.0 ) / replies / 1000.0;	// mu-sec to ms
		stddev_latency = calculate_stddev( avg_latency ) / 1000.0;

	} else {
		rate_sec = stddev_latency = 0.0;
		max_latency = min_latency = 0;
	}

	lost = num2send - replies - retries;

	fprintf( stderr, "\n[INFO]\tRequests: %-10ld\tReplies: %-10ld\tWG Retries: %ld\t\tUnreplied: %ld\n"
					 "\n\tRate: %.2f msg/sec\tAVG: %.3lf ms\t\tStd-dev: %.3lf ms\tSend Interval: %lu nsec\n"
					 "\n\tMax: %.3f ms\t\tMin: %.3f ms\t\tElapsed: %.3f sec\n\n",
					 num2send, replies, retries, lost,
					 rate_sec, avg_latency, stddev_latency, interval,
					 max_latency, min_latency, running_time );

	rmr_close( mrc );

	if( filename ) {
		fprintf( stderr, "[INFO] writing results into file %s\n", filename );

		if( rtt ) {
			fp = fopen( filename, "w+" );	// overwrite file
		} else {
			fp = fopen( filename, "a+" );	// append file
		}
		if( fp == NULL ) {
			fprintf( stderr, "[FAIL] unable to create/open file %s, reason: %s\n", filename, strerror( errno ) );
			exit( 1 );
		}

		ret = fstat( fileno(fp), &sb );
		if( ( ret == -1 ) || ( !S_ISREG( sb.st_mode ) ) ) {
			fprintf( stderr, "[FAIL] unable getting stat of the regular file %s, reason: %s\n", filename, strerror( errno ) );
			fclose( fp );
			exit( 1 );
		}

		ret = fgetc( fp );
		if( ret == EOF ) {	// if there is no char in the file, we write a header
			fprintf( fp, "# Requests | Replies | WG Retries | Unreplied | Send Interval (nanosec) | Rate (msg/sec) | Latency (ms) |"
						 " Std-dev (ms) | Max (ms) | Min (ms) | Elapsed (sec)\n");
		}

		fprintf( fp, "%ld | %ld | %ld | %ld | %lu | %.2f | %.3lf | %.3lf | %.3f | %.3f | %.3f\n",
						num2send, replies, retries, lost, interval, rate_sec, avg_latency,
						stddev_latency, max_latency, min_latency, running_time );

		if( rtt ) {	// write message tracking if desired
			fprintf( fp, "# MSG Sequence Number | MSG Latency (ms)\n" );
			for( i = 0; i < replies; i++ ) {
				fprintf( fp, "%ld | %.3f\n", msg_seq[i], msg_rtt[i] / 1000.0 );
			}
		}

		fclose( fp );

	}

	free( msg_rtt );
	free( msg_seq );

	return 0;
}
