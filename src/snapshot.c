// :vim ts=4 sw=4 noet:
/*
==================================================================================
	Copyright (c) 2020 AT&T Intellectual Property.

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

#include "snapshot.h"
#include "types.h"
#include "rft.h"
#include "logger.h"
#include "log.h"
#include "static/hashtable.c"


int in_progress = 0;			// defines if the snapshot is in progress
int ctx_pipe[2];				// pipe for context snapshotting
ctx_metabuf_t ctx_metabuf;		// metadata of the snapshot taken from the xapp
primary_ctx_t pctxs;			// stores temporary primary contexts which will be snapshotted
snapshot_t ctx_snapshot = {		// stores the snapshot, and must only being accessed by this module (do not use in other modules)
	.last_log_index = 0,
	.dlen = 0,
	.data = NULL
};
snapshot_t xapp_snapshot = {	// this is returned by this module and can be used safely by the other modules
	.last_log_index = 0,
	.dlen = 0,
	.data = NULL
};

pthread_mutex_t ctx_snapshot_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t in_progress_lock = PTHREAD_MUTEX_INITIALIZER;

/*
	Used only for testing purposes
*/
int *get_in_progress( ) {
	return &in_progress;
}

/*
	Used only for testing purposes
*/
ctx_metabuf_t *get_ctx_metabuf( ) {
	return &ctx_metabuf;
}

/*
	Used only for testing purposes
*/
snapshot_t *get_ctx_snapshot( ) {
	return &ctx_snapshot;
}

/*
	Used only for testing purposes
*/
primary_ctx_t *get_primary_ctxs( ) {
	return &pctxs;
}

/*
	Returns the last snapshot taken from this xApp, or NULL if no snapshot has been taken yet
*/
snapshot_t *get_xapp_snapshot( ) {
	snapshot_t *snapshot = NULL;

	pthread_mutex_lock( &ctx_snapshot_lock );

	if( ctx_snapshot.data != NULL ) {	// checking if a snapshot has been taken

		if( xapp_snapshot.last_log_index != ctx_snapshot.last_log_index ) {
			/*
				we deep-copy all snapshot information here to avoid conflicts with other modules, since
				the returned pointer could be modified in some way by this module, which would invalidate
				the data used by the other modules. This also avoids invalid memory access
			*/
			xapp_snapshot.last_log_index = ctx_snapshot.last_log_index;
			xapp_snapshot.dlen = ctx_snapshot.dlen;
			xapp_snapshot.items = ctx_snapshot.items;
			xapp_snapshot.data = realloc( xapp_snapshot.data, ctx_snapshot.dlen * sizeof(unsigned char) );
			if( xapp_snapshot.data == NULL ) {
				logger_fatal( "unable to reallocate memory to get the xapp snapshot (%s)", strerror( errno ) );
				exit( 1 );
			}
			memcpy( xapp_snapshot.data, ctx_snapshot.data, xapp_snapshot.dlen );

		}
		snapshot = &xapp_snapshot;
	}

	pthread_mutex_unlock( &ctx_snapshot_lock );

	return snapshot;
}

/*
	This function checks if the context stored in table (key param)
	is related to the primary role
	If it is, then it is added to the contexts to be snapshotted
*/
void primary_ctx_handler( hashtable_t *table, const char *key ) {
	int is_primary;

	is_primary = *(int *) hashtable_get( table, key );
	if( is_primary ) {

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
static inline int write_pipe( int writefd, void *src, size_t len ) {
	ssize_t bytes;
	ssize_t write_bytes = 0;
	ssize_t chunk;	// chunk of bytes to copy from src to pipe (needs to be with signed)
	unsigned char *buffer = src;
	long max_pipe_buf;

	max_pipe_buf = fpathconf( writefd, _PC_PIPE_BUF );

	/*
		we need a loop here since the snapshot data could be greater than the max size a pipe can
		transport on its internal kernel buffer
		If we write more than PIPE_BUF in a pipe, there is no guarantee that this write is done
		atomically
	*/
	do {
		chunk = len - write_bytes;
		if( chunk > max_pipe_buf ) {
			chunk = max_pipe_buf;
		}
		bytes = write( writefd, buffer + write_bytes, chunk );
		logger_debug( "child process wrote to the pipe: %ld bytes", bytes );
		if( bytes > 0 ) {
			write_bytes += bytes;

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
static inline int read_pipe( int readfd, void *dest, size_t len ) {
	ssize_t bytes;
	ssize_t read_bytes = 0;
	void *buffer;
	ssize_t chunk;	// chunk of bytes to copy from pipe to buffer (needs to be with signed)
	long max_pipe_buf;

	buffer = malloc( len );
	if( buffer == NULL ) {
		logger_fatal( "unable allocate buffer to read data from the pipe" );
		exit( 1 );
	}

	max_pipe_buf = fpathconf( readfd, _PC_PIPE_BUF );

	/*
		we need a loop here since the snapshot data could be greater than the max size a pipe can
		transport on its internal kernel buffer
	*/
	do {
		chunk = len - read_bytes;
		if( chunk > max_pipe_buf ) {
			chunk = max_pipe_buf;
		}
		bytes = read( readfd, buffer + read_bytes, chunk );
		logger_debug( "parent process read from the pipe: %ld bytes", bytes );
		if( bytes > 0 ) {
			read_bytes += bytes;

		} else {
			free( buffer );
			return 0;
		}
	} while( read_bytes < len );

	memcpy( dest, buffer, len );
	free( buffer );

	return 1;
}

/*
	Locks the required mutexes before forking the process to avoid
	deadlock of the child process
*/
static inline void lock_resources( ) {
	lock_server_log( );
}

/*
	Releases all mutexes acquired before forking the process
*/
static inline void unlock_resources( ) {
	unlock_server_log( );
}

/*
	This function implements the fork routines of the parent process to take snapshot as a thread,
	allowing the parent process to keep processing incoming requests in the meantime
*/
void *parent_thread( ) {
	int exit_status;			// exit status of the child

	close( ctx_pipe[1] );		// closing write fd

	if( read_pipe( ctx_pipe[0], &ctx_metabuf, sizeof(ctx_metabuf_t) ) ) {

		pthread_mutex_lock( &ctx_snapshot_lock );

		ctx_snapshot.data = realloc( ctx_snapshot.data, ctx_metabuf.dlen * sizeof(unsigned char) );
		if( ctx_snapshot.data == NULL ) {
			logger_fatal( "unable to reallocate memory for the snapshot data (%s)", strerror( errno ) );
			exit( 1 );
		}

		if( read_pipe( ctx_pipe[0], ctx_snapshot.data, ctx_metabuf.dlen ) ) {
			ctx_snapshot.last_log_index = ctx_metabuf.last_log_index;
			ctx_snapshot.dlen = ctx_metabuf.dlen;
			ctx_snapshot.items = ctx_metabuf.items;

			/*
				LOG compaction
				do log compation here, before finalizing the snapshot and changing the in_progress variable
				In this way, the other threads won't start a new snapshot while there is another snapshot in progress
			*/
			compact_server_log( );

			logger_warn( "===== snapshot ::::: last_log_index: %lu, items: %u, dlen: %lu, data: %ld ::::: =====", ctx_snapshot.last_log_index,
					ctx_snapshot.items, ctx_snapshot.dlen, *(long *) ctx_snapshot.data );

		} else {
			logger_error( "unable to read the snapshot data from the pipe" );
		}

		pthread_mutex_unlock( &ctx_snapshot_lock );

	} else {
		logger_error( "unable to read the snapshot metadata from the pipe" );
	}

	close( ctx_pipe[0] );		// closing read fd

	pthread_mutex_lock( &in_progress_lock );
	in_progress = 0;		// changing the snapshot status to "not in progress"
	pthread_mutex_unlock( &in_progress_lock );

	if( wait( &exit_status ) > 0 ) { 		// wait any child (only one)
		if( exit_status != EXIT_SUCCESS ) {
			logger_error( "snapshot child process returned exit status %d", exit_status >> 8 );
		}
	} else {
		logger_error( "unable to wait for the snapshotting process (%s)", strerror( errno ) );
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
	To get the snapshot data use the familily of functions get_***_snapshot( )
*/
void take_xapp_snapshot( hashtable_t *ctxtable, take_snapshot_cb_t take_snapshot_cb ) {
	int exit_status;			// exit status of the child
	pid_t pid;
	unsigned char *data = NULL;	// serialized data
	pthread_t th_id;

	assert( ctxtable != NULL );
	if( take_snapshot_cb == NULL ) {
		logger_warn( "there is no snapshot callback registered to the RFT" );
		return;
	}

	pthread_mutex_lock( &in_progress_lock );

	if( !in_progress ) {	// checking if no snapshot is in progress
		/*
			changing the snapshot status to "in progress"
			this status will be reverted by the parent thread
		*/
		in_progress = 1;

		pthread_mutex_unlock( &in_progress_lock );

	} else {
		pthread_mutex_unlock( &in_progress_lock );
		return;
	}

	if( pipe( ctx_pipe ) == -1 ) {
		logger_fatal( "unable to create pipe to take snapshot (%s)", strerror( errno ) );
		exit( 1 );
	}

	// no longer needed since the take_xapp_snapshot function is called with the server's log locked
	// lock_resources( );

	pid = fork( );			// creating the child process
	if( pid == -1 ) {
		logger_fatal( "unable to fork process to take snapshot (%s)", strerror( errno ) );
		exit( 1 );
	}

	if( pid > 0 ) {		// parent process, reads from the pipe
		// no longer needed since the resources are unlocked by the caller function
		// unlock_resources( );

		if( pthread_create( &th_id, NULL, parent_thread, NULL ) != 0 ) {
			logger_fatal( "unable to create thread in parent process to wait for the snapshot" );
			exit( 1 );
		}

	} else {		// child process, writes to the pipe
		unlock_resources( );

		exit_status = EXIT_SUCCESS;

		close( ctx_pipe[0] );	// closing read fd

		pctxs.size = 64;
		pctxs.len = 0;
		pctxs.contexts = (char **) malloc( pctxs.size * sizeof( char *) );
		if( pctxs.contexts == NULL ) {
			logger_fatal( "unable to allocate memory to store contexts for snapshotting (%s)", strerror( errno ) );
			exit( 1 );
		}
		hashtable_foreach_key( ctxtable, primary_ctx_handler );					// iterate over all keys running handler function

		ctx_metabuf.dlen = take_snapshot_cb( pctxs.contexts, pctxs.len, &ctx_metabuf.items, &data );	// calling take snapshot function in the xapp code

		ctx_metabuf.last_log_index = get_server_last_log_index( );

		logger_warn( "===== child    ::::: last_log_index: %lu, items: %u, dlen: %lu, data: %ld ::::: =====", ctx_metabuf.last_log_index,
					ctx_metabuf.items, ctx_metabuf.dlen, *(long *) data );

		if( write_pipe( ctx_pipe[1], &ctx_metabuf, sizeof(ctx_metabuf_t) ) ) {	// writing snapshot metadata

			if( !write_pipe( ctx_pipe[1], data, ctx_metabuf.dlen) ) {			// writing snapshot data
				logger_error( "unable write snapshot data to the pipe (%s)", strerror( errno) );
				exit_status = EX_IOERR;
			}

		} else {
			logger_error ( "unable to write snapshot metadata to the pipe (%s)", strerror( errno) );
			exit_status = EX_IOERR;
		}

		close( ctx_pipe[1] );	// closing write fd

		free_contexts( );
		if( data ) {
			free( data );
		}

		_exit( exit_status );	// terminates the child process

	}
}
