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
	Mnemonic:	sender.c
	Abstract:	Measures latency and throughput of messages sent to a server

				Command line options and parameters:
					- Run program passing "-" as argument

	Date:		12 December 2019
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
#include <pthread.h>
#include <sched.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <math.h>

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
long	*msg_rtt;				// array to store each message latency (round trip time)
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
	Create an empty route table and set an environment var for RMR to find.
	This must be called before initialising RMR.

	Author:		E. Scott Daniels
*/
static void mk_rt( ) {
	int 	fd;
	char	fnb[128];
	char*	contents = "newrt|start\nmse | 100 | -1 | localhost:4560\nnewrt|end\n";

	snprintf( fnb, sizeof( fnb ), "/tmp/sender.rt" );
	fd = open( fnb, O_CREAT | O_WRONLY, 0664 );
	if( fd < 0 ) {
		fprintf( stderr, "[FAIL] could not create dummy route table: %s %s\n", fnb, strerror( errno ) );
		return;
	}

	if( write( fd, contents, strlen( contents ) ) == -1 ) {
		fprintf( stderr, "[FAIL] couldn't write routing table to file %s: %s\n", fnb, strerror( errno ) );
	}

	if( close( fd ) < 0 ) {
		fprintf( stderr, "[FAIL] couldn't close dummy route table: %s: %s\n", fnb, strerror( errno ) );
		return;
	}

	setenv( "RMR_SEED_RT", fnb, 1 );		// set it, AND overwrite it
}

static void* send_msgs( ) {
	long		count = 0;
	rmr_mbuf_t	*mbuf;					// message buffer
	mpl_t		*payload;				// the payload in a message
	struct timespec nanots = {0, 0 };

	timespec_add_ns( nanots, interval );

	mbuf = rmr_alloc_msg( mrc, sizeof( mpl_t ) );			// send buffer

	clock_gettime( CLOCK_REALTIME, &begin_ts );
	while( count < num2send ) {								// we send n messages
		if( !mbuf ) {
			fprintf( stderr, "[FAIL] mbuf is nil?\n" );
			exit( 1 );
		}

		count++;

		payload = (mpl_t *) mbuf->payload;
		payload->num_msgs = num2send;
		payload->seq = count;
		clock_gettime( CLOCK_REALTIME, &payload->out_ts );

		mbuf->mtype = mtype;
		mbuf->sub_id = -1;
		mbuf->len =	sizeof( *payload ) ;
		mbuf->state = 0;

		mbuf = rmr_send_msg( mrc, mbuf );
		switch( mbuf->state ) {
			case RMR_OK:
				break;

			case RMR_ERR_RETRY:
			case RMR_ERR_TIMEOUT:
				retries++;
				break;

			default:
				fprintf( stderr, "[ERR] send failed, mtype: %d, state: %d, strerr: %s\n", mbuf->mtype, mbuf->state, strerror( errno ) );
				exit( 1 );
		}

		if ( interval )
			nanosleep( &nanots, NULL );
	}

	return NULL;
}

/*
	Listener to get replies from the receiver
*/
static void* recv_msgs( ) {
	rmr_mbuf_t*	msg;				// message buffer
	mpl_t		*payload;
	long		n_replies;			// we have to wait for "multiplied" of replies when sending to many targets

	/*
		Setting Linux kernel scheduler of this thread and max priority
		realtime scheduler can be SCHED_FIFO or SCHED_RR, see man(7) sched

		Note: Scheduling using pthread only changes the policy inside the process, not suitable here
	*/
	struct sched_param rec_param;
	pid_t tid;	// pid of thread id
	rec_param.__sched_priority = sched_get_priority_max(SCHED_FIFO);
	tid = syscall(SYS_gettid);
	sched_setscheduler( tid, SCHED_FIFO, &rec_param);
	// fprintf(stderr, "[INFO] pthread_id: %lu, tid: %d\n", (unsigned long) pthread_self(), (int) tid );
	if( sched_getscheduler( tid ) != SCHED_FIFO)
		fprintf( stderr, "\x1b[31m[ERR]\x1b[0m	scheduler not set (**run as root**), error: %s\n", strerror( errno ) );
	/* Linux kernel thread's scheduler up to here */

	msg = rmr_alloc_msg( mrc, sizeof( *payload ) );		// receive buffer

	replies = 0;	// reinitialize it to get rid of warmup data

	n_replies = num2send * targets;
	while( replies < n_replies ) {						// we want to receive n messages
		if( !msg ) {
			fprintf( stderr, "[FAIL] mbuf is nil?\n" );
			exit( 1 );
		}

		msg = rmr_torcv_msg( mrc, msg, max_timeout );	// waits ms to receive a message
		if( msg != NULL ) {
			if( msg->state == RMR_OK && msg->mtype == mtype ) {
				payload = (mpl_t *) msg->payload;

				clock_gettime( CLOCK_REALTIME, &end_ts );	// time stamp of last message (and in_ts) received to compute rate
				msg_rtt[replies] = elapsed( &payload->out_ts, &end_ts );
				msg_seq[replies] = payload->seq;
				replies++;									// message received

			} else {
				break;				// nothing received within rmr_torcv_msg's timeout, giving up
			}
		}
	}

	if( !replies )
		clock_gettime( CLOCK_REALTIME, &end_ts );	// get time in case no reply, just for report

	return NULL;
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
	long		msgs = 10;					// number of messages to be transmitted
	int			listen_port = 0;			// the port we open for "backhaul" connections
	int			warmup = 0;					// number of warmup messages
	int			max_retries = 1;			// number of RMR retries before giving up sending message (RMR_ERR_RETRY)
	int			rtt = 0;					// defines if rtt timestamp records will be saved in a file
	char		wbuf[128];					// short writing buffer

	float		running_time;
	float		rate_sec;
	long long 	sum_latencies = 0;
	float		max_latency;
	float		min_latency;
	double		avg_latency;
	double		stddev_latency;

	pthread_t	sndr_th;					// sender thread
	pthread_t 	recv_th;					// receiver thread
	int			ret;						// function return buffer

	FILE		*fp;
	char		*filename = NULL;
	struct stat sb;							// fstat buffer

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
					msgs = atol( argv[ai] );
					break;

				case 'm':
					ai++;
					mtype = atoi( argv[ai] );
					break;

				case 'i':					// interval to send in mu-sec
					ai++;
					interval = strtoul( argv[ai], NULL, 10 );
					break;

				case 't':					// max waiting timeout for a reply in ms
					ai++;
					max_timeout = atoi( argv[ai] );
					break;

				case 'w':
					ai++;
					warmup = atoi( argv[ai] );
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
					fprintf( stderr, "\nUsage: %s [-n send msgs] [-m mtype] [-p listen port] [-i interval nsec] [-t reply timeout ms] [-w warmup msgs]"
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

	msg_rtt = (long *) malloc( msgs * targets * sizeof(long) );
	if( msg_rtt == NULL ) {
		fprintf( stderr, "[FAIL] unable to allocate memory for msg_rtt array\n" );
		exit( 1 );
	}

	msg_seq = (long *) malloc( msgs * targets * sizeof(long) );
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
		mk_rt();

	fprintf( stderr, "[INFO] listening port: %s; sending %ld messages at interval of %lu nanosec\n", wbuf, msgs, interval );

	if( (mrc = rmr_init( wbuf, RMR_MAX_RCV_BYTES, RMRFL_NONE )) == NULL ) {
		fprintf( stderr, "[FAIL] unable to initialize RMR\n" );
		exit( 1 );
	}
	rmr_set_fack( mrc );
	fprintf( stderr, "[INFO] RMR initialized\n" );

	while( ! rmr_ready( mrc ) ) {
		sleep( 1 );
	}

	if( rmr_set_stimeout( mrc, max_retries ) != RMR_OK )
		fprintf ( stderr, "[WARN] unable to set rmr max retries\n");

	/* Warmup used to avoid first message timeout on throughput and latency tests */
	if ( warmup ) {
		num2send = warmup;
		fprintf( stderr, "[INFO] warming up...\n" );
		send_msgs( );				// no thread here
		recv_msgs( );				// no thread here
	}

	num2send = msgs;				// needs to be after warmup, since warmup has its own number of messages

	fprintf( stderr, "[INFO] sending messages...\n" );

	ret = pthread_create( &recv_th, NULL, recv_msgs, NULL );
	if( ret != 0 ) {
		fprintf ( stderr, "[ERR] error on creating thread: %s\n", strerror( ret ) );
		exit( 1 );
	}

	ret = pthread_create( &sndr_th, NULL, send_msgs, NULL );
	if( ret != 0 ) {
		fprintf ( stderr, "[ERR] error on creating thread: %s\n", strerror( ret ) );
		exit( 1 );
	}

	// pthread_join( sndr_th, NULL );
	pthread_join( recv_th, NULL );

	fprintf( stderr, "[INFO] generating report...\n" );

	num2send *= targets;				// RMR has to send the same msg to each target

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
		avg_latency = ( sum_latencies * 1.0 ) / replies / 1000.0;	// mu-sec to ms;
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
