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
	Mnemonic:	snapshot.c
	Abstract:	Implements snapshotting functionalities

	Date:		13 May 2020
	Author:		Alexandre Huff

	Updated:	30 Sep 2020 and 27 Oct 2020
				Implemented raft snapshotting
*/


#include <stdio.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <assert.h>
#include <sysexits.h>
#include <limits.h>

#include "snapshot.h"
#include "types.h"
#include "rft.h"
#include "logger.h"
#include "log.h"
#include "config.h"
#include "rft_private.h"
#include "mtl.h"
#include "static/hashtable.c"


int xapp_in_progress = 0;		// defines if the xapp snapshot is in progress (taking | installing)
int raft_in_progress = 0;		// defines if the raft snapshot is in progress (taking | installing)
int xapp_pipe[2];				// pipe for the xapp contexts snapshotting
int raft_pipe[2];				// pipe for the raft snapshotting
pid_t xapp_pid;
pid_t raft_pid;
pipe_metabuf_t xapp_metabuf;	// metadata of the snapshot taken from the xapp
pipe_metabuf_t raft_metabuf;	// metadata of the snapshot taken from the raft configuration
primary_ctx_t pctxs;			// stores temporary primary contexts which will be snapshotted

/*
	Stores the last snapshot taken from the xApp
*/
xapp_snapshot_t xapp_snapshot = {
	.last_index = 0,
	.items = 0,
	.dlen = 0,
	.data = NULL
};

/*
	Stores the last snapshot taken from the Raft configuration
*/
raft_snapshot_t raft_snapshot = {
	.last_term = 0,
	.last_index = 0,
	.items = 0,
	.dlen = 0,
	.data = NULL
};

pthread_mutex_t xapp_snapshot_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t raft_snapshot_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t xapp_in_progress_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t raft_in_progress_lock = PTHREAD_MUTEX_INITIALIZER;


/*
	Used only for testing purposes
*/
int *get_xapp_in_progress( ) {
	return &xapp_in_progress;
}

/*
	Used only for testing purposes
*/
int *get_raft_in_progress( ) {
	return &raft_in_progress;
}

/*
	Used only for testing purposes
*/
pipe_metabuf_t *get_xapp_metabuf( ) {
	return &xapp_metabuf;
}

/*
	Used only for testing purposes
*/
pipe_metabuf_t *get_raft_metabuf( ) {
	return &raft_metabuf;
}

/*
	Used only for testing purposes
	Note: User serialize_xapp_snapshot to send the snapshot to a backup
*/
xapp_snapshot_t *get_xapp_snapshot( ) {
	return &xapp_snapshot;
}

/*
	Used only for testing purposes
*/
primary_ctx_t *get_primary_ctxs( ) {
	return &pctxs;
}

/*
	Used only for testing purposes
	Note: User serialize_raft_snapshot to send the snapshot to the followers
*/
raft_snapshot_t *get_raft_snapshot( ) {
	return &raft_snapshot;
}

/*
	Acquires the raft snapshot mutex
*/
void lock_raft_snapshot( ) {
	pthread_mutex_lock( &raft_snapshot_lock );
}

/*
	Releases the raft snapshot mutex
*/
void unlock_raft_snapshot( ) {
	pthread_mutex_unlock( &raft_snapshot_lock );
}


/*
	Returns the last index of the current snapshot

	Assumes the caller owns the raft_snapshot_lock
*/
index_t get_raft_snapshot_last_index( ) {
	return raft_snapshot.last_index;
}

/*
	Returns the last term of the current snapshot

	Assumes the caller owns the raft_snapshot_lock
*/
term_t get_raft_snapshot_last_term( ) {
	return raft_snapshot.last_term;
}

/*
	Serializes the last snapshot taken from this xApp instance in the RMR message buffer

	Reallocates the RMR's message buffer if needed

	On success, returns the number of bytes of the serialized snapshot. Returns 0 if no snapshot
	has been taken yet.
*/
int serialize_xapp_snapshot( rmr_mbuf_t **msg, server_id_t *server_id ) {
	int mlen = 0;	// snapshot message length (header + payload)
	xapp_snapshot_hdr_t *payload;

	pthread_mutex_lock( &xapp_snapshot_lock );

	if( xapp_snapshot.data != NULL ) {	// checking if a snapshot has been taken
		mlen = (int) ( XAPP_SNAPSHOT_HDR_SIZE + xapp_snapshot.dlen );	// getting required size for the whole message
		if( mlen < 0 ){
			logger_fatal( "size overflow of the serialized xapp snapshot" );
			exit( 1 );
		}
		if( rmr_payload_size( *msg ) < mlen ) {
			*msg = (rmr_mbuf_t *) rmr_realloc_payload( *msg, mlen, 0, 0 );
			if( *msg == NULL ) {
				logger_fatal( "unable to reallocate rmr_mbuf payload to send a xapp snapshot (%s)", strerror( errno ) );
				exit( 1 );
			}
		}

		logger_debug( "serializing xapp snapshot message, last_index: %lu, items: %u, payload: %lu bytes",
				xapp_snapshot.last_index, xapp_snapshot.items, xapp_snapshot.dlen );

		/*
			We copy all snapshot information directly to the RMR message buffer instead of returning a struct
			containing the snapshot information. This avoids extra copying instructions to send a snapshot and
			also prevents invalid memory access in other modules.
		*/
		payload = (xapp_snapshot_hdr_t *) (*msg)->payload;
		payload->last_index = HTONLL( xapp_snapshot.last_index );
		payload->dlen = HTONLL( xapp_snapshot.dlen );
		strcpy( payload->server_id, *server_id );
		payload->items = htonl( xapp_snapshot.items );
		memcpy( XAPP_SNAPSHOT_PAYLOAD_ADDR( (*msg)->payload ), xapp_snapshot.data, xapp_snapshot.dlen );
	}

	pthread_mutex_unlock( &xapp_snapshot_lock );

	return mlen;
}

/*
	Serializes the last snapshot taken from the Raft configuration in the RMR message buffer

	Reallocates the RMR's message buffer if needed

	On success, returns the number of bytes of the serialized snapshot. Returns 0 if no snapshot
	has been taken yet.
*/
int serialize_raft_snapshot( rmr_mbuf_t **msg ) {
	int mlen = 0;	// snapshot message length (header + payload)
	raft_snapshot_hdr_t *payload;

	pthread_mutex_lock( &raft_snapshot_lock );

	if( raft_snapshot.data != NULL ) {	// checking if a snapshot has been taken
		mlen = (int) ( RAFT_SNAPSHOT_HDR_SIZE + raft_snapshot.dlen );	// getting required size for the whole message
		if( mlen < 0 ){
			logger_fatal( "size overflow of the serialized raft snapshot" );
			exit( 1 );
		}
		if( rmr_payload_size( *msg ) < mlen ) {
			*msg = (rmr_mbuf_t *) rmr_realloc_payload( *msg, mlen, 0, 0 );
			if( *msg == NULL ) {
				logger_fatal( "unable to reallocate rmr_mbuf payload to send a raft snapshot (%s)", strerror( errno ) );
				exit( 1 );
			}
		}

		logger_debug( "serializing raft snapshot message, last_index: %lu, items: %u, payload: %lu bytes",
				raft_snapshot.last_index, raft_snapshot.items, raft_snapshot.dlen );

		/*
			We copy all snapshot information directly to the RMR message buffer instead of returning a struct
			containing the snapshot information. This avoids extra copying instructions to send a snapshot and
			also prevents invalid memory access in other modules.
		*/
		payload = (raft_snapshot_hdr_t *) (*msg)->payload;
		memcpy( RAFT_SNAPSHOT_PAYLOAD_ADDR( (*msg)->payload ), raft_snapshot.data, raft_snapshot.dlen );
		payload->dlen = HTONLL( raft_snapshot.dlen );
		payload->items = htonl( raft_snapshot.items );
		payload->last_index = HTONLL( raft_snapshot.last_index );
		payload->last_term = HTONLL( raft_snapshot.last_term );
	}

	pthread_mutex_unlock( &raft_snapshot_lock );

	return mlen;
}

/*
	This function checks if the context stored in table (key param)
	is related to the primary role
	If it is, then it is added to the contexts to be snapshotted
*/
void primary_ctx_handler( hashtable_t *table, const char *key ) {
	void *ptr;
	long is_primary;

	ptr = hashtable_get( table, key );

	if( ptr ) {
		is_primary = (long)(long *) ptr;
		if( is_primary == RFT_PRIMARY ) {
			if( pctxs.size == pctxs.len ) {	// check if it has enough room
				pctxs.size *= 2;			// double the size
				pctxs.contexts = (char **) realloc( pctxs.contexts, pctxs.size * sizeof(char *) );
				if( pctxs.contexts == NULL ) {
					logger_fatal( "unable to reallocate memory to store contexts for snapshotting (%s)", strerror( errno ) );
					exit( 1 );
				}
			}

			pctxs.contexts[pctxs.len] = strdup( key );
			if( pctxs.contexts[pctxs.len] == NULL ) {
				logger_fatal( "unable to duplicate the context key to take snapshot (%s)", strerror( errno ) );
				exit( 1 );
			}

			pctxs.len++;
		}
	}
}

static inline void free_contexts( ) {
	unsigned int i;

	for( i = 0; i < pctxs.len; i++ ) {
		free( pctxs.contexts[i] );
	}

	free( pctxs.contexts );
}

/*
	Generic function to write any amount of data to a pipe

	Params:
		writefd: the write file descriptor from the pipe
		src: a pointer to the data to copy to the pipe
		len: the total of bytes to copy to the pipe

	Returns 1 if all bytes were copied to the pipe
	Returns 0 on error

*/
int write_pipe( int writefd, void *src, size_t len ) {
	ssize_t bytes;
	ssize_t write_bytes = 0;
	ssize_t chunk;	// chunk of bytes to copy from src to pipe (needs to be with signed)
	unsigned char *buffer = (unsigned char *) src;
	/*
		we need a loop here since the snapshot data could be greater than the max size a pipe can
		transport on its internal kernel buffer
		If we write more than PIPE_BUF in a pipe, there is no guarantee that this write is done
		atomically if more than one process writes to the same pipe
	*/
	do {
		chunk = len - write_bytes;
		if( chunk > PIPE_BUF ) {
			chunk = PIPE_BUF;
		}
		bytes = write( writefd, buffer, chunk );
		logger_debug( "child process wrote to the pipe: %ld bytes", bytes );
		if( bytes > 0 ) {
			write_bytes += bytes;
			buffer += bytes;	// incrementing the pointer
		} else {
			return 0;
		}
	} while( write_bytes < len );

	return 1;
}

/*
	Generic function to read any amount of data from a pipe

	Params:
		readfd: the read file descriptor from the pipe
		dest: an allocated pointer to copy the data into
		len: the total of bytes to copy into dest. The dest param
			should have at least size of len bytes

	Returns 1 if all bytes were copied into dest
	Returns 0 on error, and the dest pointer remains untouched

*/
int read_pipe( int readfd, void *dest, size_t len ) {
	ssize_t bytes;
	ssize_t read_bytes = 0;
	unsigned char *buffer = (unsigned char *) dest;
	ssize_t chunk;	// chunk of bytes to copy from pipe to buffer (needs to be with signed)
	/*
		we need a loop here since the snapshot data could be greater than the max size a pipe can
		transport on its internal kernel buffer
	*/
	do {
		chunk = len - read_bytes;
		if( chunk > PIPE_BUF ) {
			chunk = PIPE_BUF;
		}
		bytes = read( readfd, buffer, chunk );
		logger_debug( "parent process read from the pipe: %ld bytes", bytes );
		if( bytes > 0 ) {
			read_bytes += bytes;
			buffer += bytes;	// incrementing pointer
		} else {
			return 0;
		}
	} while( read_bytes < len );

	return 1;
}

/*
	This function implements the xapp fork routines for the parent process to take snapshot as a thread,
	allowing the parent process to keep processing incoming requests in the meantime
*/
void *parent_xapp_thread( ) {
	int exit_status;			// exit status of the child

	close( xapp_pipe[1] );		// closing write fd

	if( read_pipe( xapp_pipe[0], &xapp_metabuf, sizeof(pipe_metabuf_t) ) ) {

		pthread_mutex_lock( &xapp_snapshot_lock );

		xapp_snapshot.data = realloc( xapp_snapshot.data, xapp_metabuf.dlen * sizeof(unsigned char) );
		if( xapp_snapshot.data == NULL ) {
			logger_fatal( "unable to reallocate memory for the xapp snapshot data (%s)", strerror( errno ) );
			exit( 1 );
		}

		if( read_pipe( xapp_pipe[0], xapp_snapshot.data, xapp_metabuf.dlen ) ) {
			xapp_snapshot.last_index = xapp_metabuf.last_index;
			xapp_snapshot.dlen = xapp_metabuf.dlen;
			xapp_snapshot.items = xapp_metabuf.items;

			/*
				LOG compaction
				do log compation here, before finalizing the snapshot and changing the xapp_in_progress variable
				In this way, the other threads won't start a new snapshot while there is another snapshot in progress
			*/
			compact_server_log( xapp_snapshot.last_index );

			logger_debug( "===== xapp snapshot ::::: last_log_index: %lu, items: %u, dlen: %lu, data: %ld ::::: =====",
					xapp_snapshot.last_index, xapp_snapshot.items, xapp_snapshot.dlen, *(long *) xapp_snapshot.data );

		} else {
			logger_error( "unable to read the xapp snapshot data from the pipe" );
		}

		pthread_mutex_unlock( &xapp_snapshot_lock );

	} else {
		logger_error( "unable to read the xapp snapshot metadata from the pipe" );
	}

	close( xapp_pipe[0] );		// closing read fd

	pthread_mutex_lock( &xapp_in_progress_lock );
	xapp_in_progress = 0;		// changing the snapshot status to "not in progress"
	pthread_mutex_unlock( &xapp_in_progress_lock );

	if( waitpid( xapp_pid, &exit_status, 0 ) >= 0 ) { 		// wait the xapp child
		if( WIFEXITED( exit_status ) ) {
			if( WEXITSTATUS( exit_status ) != EXIT_SUCCESS ) {
				logger_error( "child process of the xapp snapshot exited with code %d", WEXITSTATUS( exit_status ) );
			} else {	// compiler should drop this else on smaller logger levels
				logger_debug( "child process of the xapp snapshot exited with code %d", WEXITSTATUS( exit_status ) );
			}

		} else if( WIFSIGNALED( exit_status ) ) {
			logger_error( "child process of the xapp snapshot exited abnormally, signal %d", WTERMSIG( exit_status ) );
		}
	} else {
		logger_error( "unable to wait for the xapp snapshotting process (%s)", strerror( errno ) );
	}

	return NULL;
}

/*
	This function implements the raft fork routines for the parent process to take snapshot as a thread,
	allowing the parent process to keep processing incoming requests in the meantime (while snapshotting)
*/
void *parent_raft_thread( ) {
	int exit_status;			// exit status of the child

	close( raft_pipe[1] );		// closing write fd

	if( read_pipe( raft_pipe[0], &raft_metabuf, sizeof(pipe_metabuf_t) ) ) {

		pthread_mutex_lock( &raft_snapshot_lock );

		raft_snapshot.data = realloc( raft_snapshot.data, raft_metabuf.dlen * sizeof(unsigned char) );
		if( raft_snapshot.data == NULL ) {
			logger_fatal( "unable to reallocate memory for the snapshot data (%s)", strerror( errno ) );
			exit( 1 );
		}

		if( read_pipe( raft_pipe[0], raft_snapshot.data, raft_metabuf.dlen ) ) {
			raft_snapshot.last_term = raft_metabuf.last_term;
			raft_snapshot.last_index = raft_metabuf.last_index;
			raft_snapshot.dlen = raft_metabuf.dlen;
			raft_snapshot.items = raft_metabuf.items;

			/*
				LOG compaction
				do log compation here, before finalizing the snapshot and changing the raft_in_progress variable
				In this way, the other threads won't start a new snapshot while there is another snapshot in progress
			*/
			compact_raft_log( raft_snapshot.last_index );

			logger_debug( "===== raft snapshot ::::: last_term: %lu, last_index: %lu, items: %u, dlen: %lu ::::: =====",
					raft_snapshot.last_term, raft_snapshot.last_index, raft_snapshot.items, raft_snapshot.dlen );

		} else {
			logger_error( "unable to read the raft snapshot data from the pipe" );
		}

		pthread_mutex_unlock( &raft_snapshot_lock );

	} else {
		logger_error( "unable to read the raft snapshot metadata from the pipe" );
	}

	close( raft_pipe[0] );		// closing read fd

	pthread_mutex_lock( &raft_in_progress_lock );
	raft_in_progress = 0;		// changing the snapshot status to "not in progress"
	pthread_mutex_unlock( &raft_in_progress_lock );

	if( waitpid( raft_pid, &exit_status, 0 ) >= 0 ) { 		// wait the raft child
		if( WIFEXITED( exit_status ) ) {
			if( WEXITSTATUS( exit_status ) != EXIT_SUCCESS ) {
				logger_error( "child process of the raft snapshot exited with code %d", WEXITSTATUS( exit_status ) );
			} else {	// compiler should drop this else on smaller logger levels
				logger_debug( "child process of the raft snapshot exited with code %d", WEXITSTATUS( exit_status ) );
			}

		} else if( WIFSIGNALED( exit_status ) ) {
			logger_error( "child process of the raft snapshot exited abnormally, signal %d", WTERMSIG( exit_status ) );
		}
	} else {
		logger_error( "unable to wait for the raft snapshotting process (%s)", strerror( errno ) );
	}

	return NULL;
}

/*
	This function only takes snapshot from the contexts which the xApp is playing
	the Primary role (ctxtable). Contexts from Backup role are replicated by another
	xApp instance

	Params:
		- ctxtable: the hastable with role information of each context
		- take_snapshot_cb: the function implemented in the xApp to take snapshot of the primary contexts

	If a snapshot is on the way, this function does nothing

	IMPORTANT: This function does not return any data to avoid blocking the caller process.
	To send the snapshot data use serialize_xapp_snapshot function which places the snapshot in the RMR message buffer
*/
void take_xapp_snapshot( hashtable_t *ctxtable, take_snapshot_cb_t take_snapshot_cb ) {
	int exit_status;			// exit status of the child
	unsigned char *data = NULL;	// serialized data
	pthread_t th_id;

	assert( ctxtable != NULL );
	if( take_snapshot_cb == NULL ) {
		logger_warn( "there is no snapshot callback registered to the RFT" );
		return;
	}

	pthread_mutex_lock( &xapp_in_progress_lock );

	if( !xapp_in_progress ) {	// checking if no snapshot is in progress
		/*
			changing the snapshot status to "in progress"
			this status will be reverted by the parent thread
		*/
		xapp_in_progress = 1;

		pthread_mutex_unlock( &xapp_in_progress_lock );

	} else {
		pthread_mutex_unlock( &xapp_in_progress_lock );
		return;
	}

	if( pipe( xapp_pipe ) == -1 ) {
		logger_fatal( "unable to create pipe to take xapp snapshot (%s)", strerror( errno ) );
		exit( 1 );
	}

	/*
		no need to lock resources (mutexes) before forking as
		the take_xapp_snapshot function is called with the server_log_lock already locked
	*/

	xapp_pid = fork( );			// creating the child process
	if( xapp_pid == -1 ) {
		logger_fatal( "unable to fork process to take xapp snapshot (%s)", strerror( errno ) );
		exit( 1 );
	}

	if( xapp_pid > 0 ) {		// parent process, reads from the pipe
		// no need to unlock resources since they are unlocked by the caller function

		if( pthread_create( &th_id, NULL, parent_xapp_thread, NULL ) != 0 ) {
			logger_fatal( "unable to create thread in parent process to wait for the xapp snapshot" );
			exit( 1 );
		}

	} else {		// child process, writes to the pipe
		unlock_server_log( );	// we have to unlock the required mutex resources to avoid dealocks in the child process

		exit_status = EXIT_SUCCESS;

		close( xapp_pipe[0] );	// closing read fd

		pctxs.size = 64;
		pctxs.len = 0;
		pctxs.contexts = (char **) malloc( pctxs.size * sizeof( char *) );
		if( pctxs.contexts == NULL ) {
			logger_fatal( "unable to allocate memory to store contexts for snapshotting (%s)", strerror( errno ) );
			exit( 1 );
		}
		hashtable_foreach_key( ctxtable, primary_ctx_handler );					// iterate over all keys running handler function

		xapp_metabuf.dlen = take_snapshot_cb( pctxs.contexts, pctxs.len, &xapp_metabuf.items, &data );	// calling take snapshot function in the xapp code

		xapp_metabuf.last_index = get_server_last_log_index( );

		logger_debug( "===== xapp child    ::::: last_log_index: %lu, items: %u, dlen: %lu, data: %ld ::::: =====",
				xapp_metabuf.last_index, xapp_metabuf.items, xapp_metabuf.dlen, *(long *) data );

		if( write_pipe( xapp_pipe[1], &xapp_metabuf, sizeof(pipe_metabuf_t) ) ) {	// writing snapshot metadata

			if( !write_pipe( xapp_pipe[1], data, xapp_metabuf.dlen) ) {			// writing snapshot data
				logger_error( "unable to write the xapp snapshot data to the pipe (%s)", strerror( errno) );
				exit_status = EX_IOERR;
			}

		} else {
			logger_error( "unable to write the xapp snapshot metadata to the pipe (%s)", strerror( errno) );
			exit_status = EX_IOERR;
		}

		close( xapp_pipe[1] );	// closing write fd

		free_contexts( );
		if( data ) {
			free( data );
		}

		_exit( exit_status );	// terminates the child process

	}
}

/*
	Takes snapshot of the raft configuration

	If a snapshot is on the way, this function does nothing

	IMPORTANT: This function does not return any data to avoid blocking the caller process.
	To get the snapshot data use the family of functions get_***_snapshot( )
*/
void take_raft_snapshot( ) {
	int exit_status;			// exit status of the child
	unsigned char *data = NULL;	// serialized data
	pthread_t th_id;

	pthread_mutex_lock( &raft_in_progress_lock );

	if( !raft_in_progress ) {	// checking if no snapshot is in progress
		/*
			changing the snapshot status to "in progress"
			this status will be reverted by the parent thread
		*/
		raft_in_progress = 1;

		pthread_mutex_unlock( &raft_in_progress_lock );

	} else {
		pthread_mutex_unlock( &raft_in_progress_lock );
		return;
	}

	if( pipe( raft_pipe ) == -1 ) {
		logger_fatal( "unable to create pipe to take raft snapshot (%s)", strerror( errno ) );
		exit( 1 );
	}

	// locking all required resources before forking to avoid deadlocks
	lock_raft_config( );

	raft_pid = fork( );			// creating the child process
	if( raft_pid == -1 ) {
		logger_fatal( "unable to fork process to take raft snapshot (%s)", strerror( errno ) );
		exit( 1 );
	}

	if( raft_pid > 0 ) {		// parent process, reads from the pipe
		// unlocking all acquired resources
		unlock_raft_config( );

		if( pthread_create( &th_id, NULL, parent_raft_thread, NULL ) != 0 ) {
			logger_fatal( "unable to create thread in parent process to wait for the raft snapshot" );
			exit( 1 );
		}

	} else {		// child process, writes to the pipe
		// unlocking all acquired resources
		unlock_raft_config( );

		exit_status = EXIT_SUCCESS;

		close( raft_pipe[0] );	// closing read fd

		create_raft_config_snapshot( &data, &raft_metabuf );

		logger_debug( "===== raft child    ::::: last_term: %lu, last_index: %lu, items: %u, dlen: %lu ::::: =====",
				raft_metabuf.last_term, raft_metabuf.last_index, raft_metabuf.items, raft_metabuf.dlen );

		if( write_pipe( raft_pipe[1], &raft_metabuf, sizeof(pipe_metabuf_t) ) ) {	// writing snapshot metadata

			if( !write_pipe( raft_pipe[1], data, raft_metabuf.dlen) ) {				// writing snapshot data
				logger_error( "unable to write raft snapshot data to the pipe (%s)", strerror( errno) );
				exit_status = EX_IOERR;
			}

		} else {
			logger_error( "unable to write raft snapshot metadata to the pipe (%s)", strerror( errno) );
			exit_status = EX_IOERR;
		}

		close( raft_pipe[1] );	// closing write fd

		if( data ) {
			free( data );
		}

		_exit( exit_status );	// terminates the child process

	}
}

int install_raft_snapshot( raft_snapshot_t *snapshot ) {
	int success = 0;
	raft_snapshot_t *local;
	raft_snapshot_t *incoming;

	assert( snapshot != NULL );

	pthread_mutex_lock( &raft_in_progress_lock );
	if( !raft_in_progress ) {
		raft_in_progress = 1;
		pthread_mutex_unlock( &raft_in_progress_lock );

		local = &raft_snapshot;
		incoming = snapshot;

		// we can compare last_term and last_index directly since both are in network byte order
		if( ( incoming->last_term != local->last_term ) || ( incoming->last_index != local->last_index ) ) {
			commit_raft_config_snapshot( snapshot );

			// deep copy from the incoming snapshot to the local one
			raft_snapshot.last_term = snapshot->last_term;
			raft_snapshot.last_index = snapshot->last_index;
			raft_snapshot.items = snapshot->items;
			raft_snapshot.dlen = snapshot->dlen;
			raft_snapshot.data = realloc( raft_snapshot.data, snapshot->dlen );
			if( raft_snapshot.data == NULL ) {
				logger_fatal( "unable to reallocate buffer to store the incoming raft snapshot data" );
				exit( 1 );
			}
			memcpy( raft_snapshot.data, snapshot->data, raft_snapshot.dlen );

			success = 1;
		}

		pthread_mutex_lock( &raft_in_progress_lock );
		raft_in_progress = 0;
	}

	pthread_mutex_unlock( &raft_in_progress_lock );

	return success;
}
