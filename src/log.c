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
	Mnemonic:	log.c
	Abstract:	Implements log funtionalities for the RFT library

	Date:		27 November 2019
	Author:		Alexandre Huff
*/

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <string.h>

#include "log.h"
#include "logger.h"
#include "utils.h"
#include "mtl.h"
#include "config.h"
#include "static/hashtable.c"
#include "static/logring.c"
#include "snapshot.h"
#include "rft_private.h"


pthread_mutex_t raft_log_lock = PTHREAD_MUTEX_INITIALIZER;		// mutex for locking the raft_log variable
pthread_mutex_t server_log_lock = PTHREAD_MUTEX_INITIALIZER;	// mutex for locking the xapps_log variable

/*
	This is specific for raft full synchronous replication
	Stores log entries for the membership management and replication based on the raft algorithm
*/
static log_entries_t raft_log = {
	.memsize = 0,
	.mthresh = 0,
	.cthresh = 0,
	.index_offset = 0,
	.last_log_index = 0,
	.entries = NULL
};

/*
	This is specific for xApp asynchronous replication
	Stores log entries replicated by this xapp to other xapps
*/
static log_entries_t xapp_log = {
	.memsize = 0,
	.mthresh = 0,
	.cthresh = 0,
	.index_offset = 0,
	.last_log_index = 0,
	.entries = NULL
};

/*
	Acquires the mutex of the raft log
*/
void lock_raft_log( ) {
	pthread_mutex_lock( &raft_log_lock );
}

/*
	Releases the mutex of the raft log
*/
void unlock_raft_log( ) {
	pthread_mutex_unlock( &raft_log_lock );
}

/*
	Used only for testing purposes

	Returns the raft's log pointer

	Not thread-safe, assumes that the log_lock has been acquired by the caller
*/
log_entries_t *get_raft_log( ) {
	return &raft_log;
}

/*
	Acquires the mutex of the server log
*/
void lock_server_log( ) {
	pthread_mutex_lock( &server_log_lock );
}

/*
	Releases the mutex of the server log
*/
void unlock_server_log( ) {
	pthread_mutex_unlock( &server_log_lock );
}

/*
	Used only for testing purposes

	Returns a pointer of the server log (xApp state replication log)

	Not thread-safe, assumes that the log_lock has been acquired by the caller
*/
log_entries_t *get_server_log( ) {
	return &xapp_log;
}

/*
	Initializes either, RAFT or SERVER log entry structures

	Paramenters:
	type: defines the type of the log (i.e. RAFT_LOG or SERVER_LOG)
	size: defines the size of the ring to store log entries (needs to be power of 2)
	threshold: defines the threshold of the log (in Mbytes) to trigger the snapshot function

	Returns 1 on success. On error, 0 is returned and errno is set to indicate the error.
*/
int init_log( log_type_e type, u_int32_t size, u_int32_t threshold ) {
	log_entries_t *log;

	if( threshold == 0 ) {
		errno = EINVAL;
		return 0;
	}

	if( type == RAFT_LOG ) {
		log = &raft_log;
	} else if( type == SERVER_LOG ) {
		log = &xapp_log;
	} else {
		errno = EINVAL;	// invalid log type
		return 0;
	}


	log->entries = log_ring_create( size );
	if( log->entries == NULL ) {
		return 0;	// errno is already set
	}
	log->index_offset = 0;
	log->last_log_index = 0;
	log->memsize = 0;
	log->mthresh = threshold * 1048576;	// convert Mbytes to bytes
	log->cthresh = (index_t)( log_ring_size( log->entries ) * LOG_COUNT_RATIO );

	return 1;
}

/*
	Generic function to append a new log entry for both, raft's and xapp's log

	Sets the log_index of the new log entry according to its position in the log

	IMPORTANT:
		No thread-safe, assumes that the log's lock has been acquired by the caller if required
*/
static inline int append_log_entry( log_entry_t *log_entry, log_entries_t *log ) {
	assert( log_entry != NULL );
	assert( log != NULL );

	// raft starts its log index at 1, so we need to add 1 here
	log_entry->index = log->last_log_index + 1;

	if( log_ring_insert( log->entries, log_entry ) ) {
		log->last_log_index++;
		// increasing the size of the log to trigger snapshots
		log->memsize += sizeof(log_entry_t) + log_entry->dlen + log_entry->clen + log_entry->klen;
		return 1;
	}

	return 0;
}

/*
	Appends a new log entry to the raft's log

	Sets the log_index of the new log entry according to its position in the log
*/
void append_raft_log_entry( log_entry_t *log_entry ) {
	server_conf_cmd_data_t *data = NULL;

	assert( log_entry != NULL );

	pthread_mutex_lock( &raft_log_lock );

	if( ( raft_log.memsize > raft_log.mthresh ) || ( log_ring_count( raft_log.entries ) > raft_log.cthresh ) ) {
		logger_debug( "taking raft snapshot" );
		take_raft_snapshot( );
	}

	// we had to write a wrapper for a generic append log entries, so all particularities will be kept in this function
	if( !append_log_entry( log_entry, &raft_log ) ) {
		logger_fatal( "unable to append a new raft log entry (%s)", strerror( ENOBUFS ) );
		exit( 1 );
	}

	logger_trace( "raft log entry index %lu appended to the log", log_entry->index );

	if( log_entry->type == RAFT_CONFIG ) {
		data = (server_conf_cmd_data_t *) log_entry->data;

		if( log_entry->command == ADD_MEMBER ) {

			/*
				if we are the leader, that call will return a pointer, otherwise will return NULL and
				it needs to be added to the servers config
			*/
			if( raft_config_get_server( &data->server_id ) == NULL ) {	// if not found, add it
				if( ! raft_config_add_server( &data->server_id, &data->target, 0 ) ) { // we can't continue running without this server
					logger_fatal( "unable to append a log entry with a new server configuration command" );
					exit( 1 );
				}
			}

			if( ! raft_config_set_server_status( &data->server_id, VOTING_MEMBER ) ) {
				logger_fatal( "unable setting status VOTING_MEMBER for server %s", data->server_id );
				exit( 1 );
			}

		} else if( log_entry->command == DEL_MEMBER ) {
			raft_config_remove_server( &data->server_id );
		}
	} /* else if( log_entry->type == COMMAND ) {
			// nothing to do here yet
			// this is the case of full synchronous replication using raft algorithm

	} else {
		logger_error( "unknown log entry type %d", log_entry->type );
	}*/

	pthread_mutex_unlock( &raft_log_lock );
}

/*
	Appends a new log entry to the server's log

	This is for the xApp state replication log entries

	Sets the log_index of the new log entry according to its position in the log
*/
void append_server_log_entry( log_entry_t *log_entry, hashtable_t *ctxtable, take_snapshot_cb_t take_xapp_snapshot_cb ) {
	assert( log_entry != NULL );

	pthread_mutex_lock( &server_log_lock );

	if( ( xapp_log.memsize > xapp_log.mthresh ) || ( log_ring_count( xapp_log.entries ) > xapp_log.cthresh ) )
		take_xapp_snapshot( ctxtable, take_xapp_snapshot_cb );

	// wrapper as same as append raft log entry
	if( !append_log_entry( log_entry, &xapp_log ) ) {
		logger_fatal( "unable to append a new xapp log entry (%s)", strerror( ENOBUFS ) );
		exit( 1 );
	}
	pthread_mutex_unlock( &server_log_lock );
}

/*
	Removes and frees all conflicting entries starting from the last entry stored
	in the log up to the last committed index (downwards) or up to the matching
	log entry (which is found first)

	A matching log entry means that its prev_index and prev_term are the same as the leader ones

	NOTE: Not thread-safe, assumes that the caller owns both, the raft_state_lock
		and the raft_log_lock
*/
void remove_raft_conflicting_entries( index_t prev_log_index, term_t prev_log_term, index_t committed_index ) {
	/*
		There wont have conflicts in server (xApp) state replication (Only the primary stores log entries)
		Thus, there is no need to write another function to remove server conflicting entries
	*/

	log_entry_t *entry;
	server_conf_cmd_data_t *cmd_data;

	logger_debug( "removing raft conflicting log entries" );

	/*
		Checking and assuring that committed indexes wont be removed from the log
		Committed logs cannot be removed, since they likely have been applied
		The commitIndex increases monotonically (see fig 2 raft paper)
	*/
	while( raft_log.last_log_index > committed_index ) {
		entry = log_ring_extract_r( raft_log.entries );
		if( entry ) {
			if( ( entry->index == prev_log_index ) && ( entry->term == prev_log_term ) ) {	// found the matching entry
				/*
					we have to add it back to the log ring since it matches to the leader log
					insertion fails only when ring is full, but it wont as we removed the entry previously
				*/
				log_ring_insert( raft_log.entries, entry );
				return;
			}

			/*
				"Unfortunately, this decision does imply that a log entry for a configuration
				change can be removed (if leadership changes); in this case, a server must be prepared to fall back
				to the previous configuration in its log."
				Source: raft dissertation, check at the end of section 4.1
			*/
			if( entry->type == RAFT_CONFIG ) {
				cmd_data = (server_conf_cmd_data_t *) entry->data;
				if( entry->command == ADD_MEMBER ) {		// removing the added server
					raft_config_remove_server( &cmd_data->server_id );

				} else if( entry->command == DEL_MEMBER ) {	// adding back the removed server

					if( ! raft_config_add_server( &cmd_data->server_id, &cmd_data->target, 0 ) ) {
						// we can't continue running without this server
						logger_fatal( "unable to append a log entry with a new server configuration command" );
						exit( 1 );
					}

					if( ! raft_config_set_server_status( &cmd_data->server_id, VOTING_MEMBER ) ) {
						logger_fatal( "unable setting status VOTING_MEMBER for server %s", cmd_data->server_id );
						exit( 1 );
					}
				}
			}

			raft_log.memsize -= sizeof(log_entry_t) + entry->dlen + entry->clen + entry->klen;

			free_log_entry( entry );
		}
		raft_log.last_log_index--;	// pointing to the new last log entry after removal
	}

	logger_warn( "attempt to remove committed entries from the raft log, some process is slow or messages are being duplicated" );
}

/*
	Checks the consistency of the raft log stored in this server instance
	with the information of a message received from the leader

	Returns 1 on success. On error, returns 0.
*/
int check_raft_log_consistency( index_t prev_log_index, term_t prev_log_term, index_t committed_index ) {
	log_entry_t *log_entry;
	term_t prev_term;
	index_t prev_index;
	int success = 0;

	pthread_mutex_lock( &raft_log_lock );

	// checking for log inconsistencies
	log_entry = log_ring_get( raft_log.entries, raft_log.last_log_index );
	if( log_entry && ( ( log_entry->index != prev_log_index) || ( log_entry->term != prev_log_term) ) )
		remove_raft_conflicting_entries( prev_log_index, prev_log_term, committed_index );

	/*
		compare if the request_msg prev_log_term (and prev_log_index) is equal to the retrieved log entry
		if do not, return success = 0
		See section 5.3 in raft paper
		prev_log_index can be different of 0 at first log, (e.g. starting log from a leader snapshot)
	*/
	log_entry = log_ring_get( raft_log.entries, prev_log_index );
	if( log_entry ) {
		prev_term = log_entry->term;
		prev_index = log_entry->index;
	} else {	// the log might be empty due to just received an install raft snapshot message
		lock_raft_snapshot( );
		prev_term = get_raft_snapshot_last_term( );
		prev_index = get_raft_snapshot_last_index( );
		unlock_raft_snapshot( );
	}

	if( ( prev_term == prev_log_term ) && ( prev_index == prev_log_index) ) {
		success = 1;
	}

	pthread_mutex_unlock( &raft_log_lock );

	return success;
}

/*
	Gets a log entry from its raft log index
*/
log_entry_t *get_raft_log_entry( index_t log_index ) {
	log_entry_t *entry = NULL;

	pthread_mutex_lock( &raft_log_lock );

	entry = log_ring_get( raft_log.entries, log_index );

	pthread_mutex_unlock( &raft_log_lock );

	return entry;
}

/*
	Gets a log entry from its server log index
*/
log_entry_t *get_server_log_entry( index_t log_index ) {
	log_entry_t *entry = NULL;

	pthread_mutex_lock( &server_log_lock );

	entry = log_ring_get( xapp_log.entries, log_index );

	pthread_mutex_unlock( &server_log_lock );

	return entry;
}

/*
	Returns the last index from the raft's log (including not committed)
*/
index_t get_raft_last_log_index( ) {
	index_t index;

	pthread_mutex_lock( &raft_log_lock );

	index = raft_log.last_log_index;

	pthread_mutex_unlock( &raft_log_lock );

	return index;
}

/*
	Returns the last index from the servers's log
*/
index_t get_server_last_log_index( ) {
	index_t index;

	pthread_mutex_lock( &server_log_lock );

	index = xapp_log.last_log_index;

	pthread_mutex_unlock( &server_log_lock );

	return index;
}


/*
	Returns the last term from the log (including not commited)
*/
term_t get_raft_last_log_term( ) {
	log_entry_t *entry;
	term_t term;

	/*
		server's last log term not needed, thus it does not have an implementation
	*/

	pthread_mutex_lock( &raft_log_lock );

	entry = log_ring_get( raft_log.entries, raft_log.last_log_index );
	if( entry ) {
		term = entry->term;
		pthread_mutex_unlock( &raft_log_lock );
	} else {
		pthread_mutex_unlock( &raft_log_lock );
		/*
			log lock needs to be released before aquiring tha raft state lock in rft.c to avoid deadlocks
			Note: the order of acquiring locks needs to be: rft.c => log.c => and config.c
		*/
		lock_raft_state( );
		term = get_raft_current_term( );
		unlock_raft_state( );
	}

	return term;
}

/*
	Releases memory resources from a log entry

	Not thread-safe, assumes that the caller has acquired the log_lock
	log_lock is mainly required when removing conflicting log entries
*/
inline void free_log_entry( log_entry_t *entry ) {
	assert( entry != NULL );
	free( entry->data );
	if( entry->context )
		free( entry->context );
	if( entry->key )
		free( entry->key );

	free( entry );
}

/*
	Releases memory resources from all entries of a given log

	Also sets the first and last log index, as well as the memory size used by the log

	Not thread-safe, assumes that the caller has acquired the log_lock
	log_lock is mainly required when installing a snapshot
*/
void free_all_log_entries( log_entries_t *log, index_t last_applied_log_index ) {
	log_entry_t *entry;
	assert( log != NULL );

	entry = log_ring_extract( log->entries );
	while( entry != NULL ) {
		free_log_entry( entry );
		entry = log_ring_extract( log->entries );
	}

	log->memsize = 0;
	log->index_offset = last_applied_log_index;	// there is no log, thus first and last log index are the same
	log->last_log_index = last_applied_log_index;
}

/*
	Generic function to serialize log entries either for raft log entries or server log entries
	Note: this function does not frees the pointer of lbuf, that is resposibility of the caller

	IMPORTANT: not thread-safe, assumes that the caller owns the lock of the log param (if required)

	from_index: defines the starting log index that the serialization must start
	n_entries: defines the number of entries to be serialized
	**lbuf: is the log buffer where the serialized data will be written to (will be reallocated if it is not enough)
	*buf_len: defines the current size of the lbuf
	max_buf_len: indicates the maximum number of bytes that can be added in the message. This function does not take into account
				 the size of the RFT mtl header, so, it needs to be subtracted from the RMR' maximum message size before
				 calling this function. The RMR's maximum message size is defined in the xApp context and passed via argument
				 to the RFT init function.
	type:	defines which type of log we are serializing (RAFT or SERVER)

	Returns the total of serialized bytes
	In case it returns 0, then errno is set:
		ENODATA: log entry not found, required to install a snapshot
*/
static inline unsigned int serialize_log_entries( index_t from_index, unsigned int *n_entries, unsigned char **lbuf,
												unsigned int *buf_len, int max_buf_len, log_entries_t *log, log_type_e type ) {
	unsigned int bytes = 0;	 // size of the current serialization
	unsigned int esize = 0;	 // entry size
	unsigned int count = 0;	 // counter of the number of entries that have been serialized
	log_entry_t *entry = NULL;
	raft_log_entry_hdr_t raft_hdr;		// raft log entry with converted network byte order in header
	xapp_log_entry_hdr_t server_hdr;	// server log entry with converted network byte order in header
	unsigned char *bufptr;				// auxiliary offset pointer

	if( ( from_index - 1 + *n_entries ) <= log->last_log_index ) {	// needs decrease 1 from from_index since *n_entries counts for it

		for( ; (count < *n_entries) && (bytes < max_buf_len) && ( max_buf_len > 0 ); from_index++, count++ ) {

			entry = log_ring_get( log->entries, from_index );
			if( entry == NULL ) {
				errno = ENODATA;
				break;
			}

			if( type == SERVER_LOG ) {
				esize = XAPP_LOG_ENTRY_HDR_SIZE + entry->clen + entry->klen + entry->dlen;	// entry size = header + command
			} else {
				esize = RAFT_LOG_ENTRY_HDR_SIZE + entry->dlen;	// entry size = header + command
			}

			if( ( bytes + esize ) > max_buf_len )
				break;		// we cannot continue, the receiver will drop this message due its oversize

			if( ( esize + bytes ) > *buf_len ) {	// realloc if current buffer is not enough
				*buf_len *= 2;
				if( *buf_len < esize )
					*buf_len = esize;

				*lbuf = (unsigned char *) realloc( *lbuf, *buf_len );
				if( *lbuf == NULL ) {
					logger_fatal( "unable to reallocate memory to serialize log buffer" );
					exit( 1 );
				}
			}

			bufptr = *lbuf + bytes; // pointing to the next available spot

			if( type == SERVER_LOG ) {
				server_hdr.index = HTONLL( entry->index );
				server_hdr.clen = htonl( entry->clen );
				server_hdr.klen = htonl( entry->klen );
				server_hdr.dlen = HTONLL( entry->dlen );
				server_hdr.command = htonl( entry->command );

				memcpy( bufptr, &server_hdr, XAPP_LOG_ENTRY_HDR_SIZE ); // copying all header into log buffer
				memcpy( XAPP_CTX_PAYLOAD_ADDR( bufptr ), entry->context, entry->clen );	// copying the context str
				memcpy( XAPP_KEY_PAYLOAD_ADDR( bufptr ), entry->key, entry->klen );		// copying the key str
				memcpy( XAPP_DATA_PAYLOAD_ADDR( bufptr ), entry->data, entry->dlen );		// copying the cmd_data str

				bytes += XAPP_LOG_ENTRY_HDR_SIZE + entry->clen + entry->klen + entry->dlen;

			} else {	// if not a server command (xApp replication), then it only can be a raft command
				raft_hdr.term = HTONLL( entry->term );
				raft_hdr.index = HTONLL( entry->index );
				raft_hdr.dlen = HTONLL( entry->dlen );
				raft_hdr.command = htonl( entry->command );
				raft_hdr.type = htonl( entry->type );

				memcpy( bufptr, &raft_hdr, RAFT_LOG_ENTRY_HDR_SIZE ); // copying all header into log buffer
				memcpy( bufptr + RAFT_LOG_ENTRY_HDR_SIZE, entry->data, entry->dlen );	// copying command data

				bytes += RAFT_LOG_ENTRY_HDR_SIZE + entry->dlen;
			}

		}

		*n_entries = count;
		if( !count && entry )	// if count is 0 but there is a log entry to send, then the log entry exceeded the max msg size
			logger_error( "unable to serialize log entries, "
						"reason: size of a log entry is greater than the RMR's max msg size" );
	} else {
		logger_fatal( "unable to serialize log entries, out of range ==> (from_index + n_entries)=%lu <= log.last_log_index=%lu",
						from_index + *n_entries, log->last_log_index );
		exit( 1 );
	}

	return bytes;
}

/*
	Serializes raft log entries

	Note: this function does not frees the pointer of lbuf, that is resposibility of the caller

	from_index: defines the starting raft log index that the serialization must start
	n_entries: defines the number of entries to be serialized
	**lbuf: is the log buffer where the the serialized data will be written (will be reallocated if it is not enough)
	*buf_len: defines the current size of the lbuf

	Returns the total of serialized bytes
	In case it returns 0, then errno is set:
		ENODATA: log entry not found, required to install a snapshot
*/
unsigned int serialize_raft_log_entries( index_t from_index, unsigned int *n_entries, unsigned char **lbuf,
										 unsigned int *buf_len, int max_msg_size ) {
	// this is a wrapper function
	unsigned int bytes;
	int err_code;

	pthread_mutex_lock( &raft_log_lock );

	// we can only serialize RMR's max_msg_size MINUS size of the append entries mtl header
	bytes = serialize_log_entries( from_index, n_entries, lbuf, buf_len, max_msg_size - APND_ENTR_HDR_LEN, &raft_log, RAFT_LOG );

	err_code = errno;	// saving due to mutex_unlock which could change it
	pthread_mutex_unlock( &raft_log_lock );
	errno = err_code;	// restoring

	return bytes;
}

/*
	Serializes server log entries (xApp state replication)

	Note: this function does not frees the pointer of lbuf, that is resposibility of the caller

	from_index: defines the starting of server's log index that the serialization must start
	n_entries: defines the number of entries to be serialized
	**lbuf: is the log buffer where the the serialized data will be written (will be reallocated if it is not enough)
	*buf_len: defines the current size of the lbuf

	Returns the total of serialized bytes
	In case it returns 0, then errno is set:
		ENODATA: log entry not found, required to install a snapshot
*/
unsigned int serialize_server_log_entries( index_t from_index, unsigned int *n_entries, unsigned char **lbuf,
											unsigned int *buf_len, int max_msg_size ) {
	// this is a wrapper function
	unsigned int bytes;
	int err_code;

	pthread_mutex_lock( &server_log_lock );

	// we can only serialize RMR's max_msg_size MINUS size of the replication request mtl header
	bytes = serialize_log_entries( from_index, n_entries, lbuf, buf_len, max_msg_size - REPL_REQ_HDR_LEN, &xapp_log, SERVER_LOG );

	err_code = errno;	// saving due to mutex_unlock which could change it
	pthread_mutex_unlock( &server_log_lock );
	errno = err_code;	// restoring

	return bytes;
}

/*
	Deserializes n_entries of log entry from **s_entries and stores data in the array **entries

	Assumes that array of **entries is at least of size n_entries

	This function is generic: can be used to deserialize log entries for raft and server logs
*/
static inline void deserialize_log_entries( unsigned char *s_entries, unsigned int n_entries, log_entry_t **entries, log_type_e type ) {
	unsigned int i;
	log_entry_t *entry = NULL;
	raft_log_entry_hdr_t raft_hdr;		// raft log entry with converted network byte order in header
	xapp_log_entry_hdr_t server_hdr;	// server log entry with converted network byte order in header
	size_t hdr_len;						// auxiliary log entry header length
	size_t plen;						// auxiliary log entry to store the whole payload size
	unsigned char *dataptr = NULL;		// auxiliary pointer to the command data

	hdr_len = ( type == SERVER_LOG ? XAPP_LOG_ENTRY_HDR_SIZE : RAFT_LOG_ENTRY_HDR_SIZE );	// keeps our function more clear

	for( i = 0; i < n_entries; i++ ) {
		entries[i] = (log_entry_t *) malloc( sizeof( log_entry_t ) );
		entry = entries[i];
		if( entry == NULL ) {
			logger_fatal( "unable to allocate memory to deserialize a log entry" );
			exit( 1 );
		}

		if( type == SERVER_LOG ) { // xApp replication log entries
			memcpy( &server_hdr, s_entries, hdr_len );
			// entry->term = 0;	 // not used by xApp replication
			entry->index = NTOHLL( server_hdr.index) ;
			entry->command = ntohl( server_hdr.command );
			entry->dlen = NTOHLL( server_hdr.dlen );
			entry->clen = ntohl( server_hdr.clen );
			entry->klen = ntohl( server_hdr.klen );
			entry->type = SERVER_COMMAND;

			entry->context = strndup( (char *)XAPP_CTX_PAYLOAD_ADDR( s_entries ), entry->clen );
			if( entry->context == NULL ) {
				logger_fatal( "unable to duplicate memory to deserialize a log entry's context" );
				exit( 1 );
			}

			entry->key = strndup( (char *)XAPP_KEY_PAYLOAD_ADDR( s_entries ), entry->klen );
			if( entry->key == NULL ) {
				logger_fatal( "unable to duplicate memory to deserialize a log entry's key" );
				exit( 1 );
			}

			dataptr = XAPP_DATA_PAYLOAD_ADDR( s_entries );	// pointing to the command data
			plen = entry->clen + entry->klen + entry->dlen;		// computing the whole payload size

		} else {		// it only can be raft log entries
			memcpy( &raft_hdr, s_entries, hdr_len );
			entry->term = NTOHLL( raft_hdr.term );
			entry->index = NTOHLL( raft_hdr.index );
			entry->dlen = NTOHLL( raft_hdr.dlen );
			entry->command = ntohl(raft_hdr.command );
			entry->type = ntohl( raft_hdr.type );
			entry->context = NULL;	// not used by raft replication
			entry->key = NULL;		// not used by raft replication
			entry->clen = 0;		// not used by raft replication but set to avoid further segfaults
			entry->klen = 0;		// not used by raft replication but set to avoid further segfaults

			dataptr = RAFT_LOG_ENTRY_PAYLOAD_ADDR( s_entries );	// pointing to the command data
			plen = entry->dlen;		// computing the whole payload size
		}

		entry->data = (unsigned char *) malloc( plen );
		if( entry->data == NULL ) {
			logger_fatal( "unable to allocate memory to deserialize a command data" );
			exit( 1 );
		}

		memcpy( entry->data, dataptr, plen );

		s_entries += hdr_len + plen;

	}
}

void deserialize_raft_log_entries( unsigned char *s_entries, unsigned int n_entries, log_entry_t **entries ) {
	// wrapper function
	deserialize_log_entries( s_entries, n_entries, entries, RAFT_LOG );
}

void deserialize_server_log_entries( unsigned char *s_entries, unsigned int n_entries, log_entry_t **entries ) {
	// wrapper function
	deserialize_log_entries( s_entries, n_entries, entries, SERVER_LOG );
}

/*
	Generic function to allocate and set all fields for a new log entry

	len: if data is a str the len argument must include the \0 (i.e. strlen(str)+1 )

	Returns a populated new log entry or NULL in case of error
*/
static inline log_entry_t *new_log_entry( term_t term, log_entry_type_e type, const char *context,
											const char *key, int command, void *data, size_t len ) {
	log_entry_t *entry = NULL;

	entry = (log_entry_t *) malloc( sizeof( log_entry_t ) );
	if( entry != NULL ) {
		// entry->index -- it must be set by the add log entry function
		entry->term = term;
		entry->type = type;
		entry->command = command;
		/*
			context and key need to be initialized here to avoid potential
			segfaults on freeing a given log entry if an error happened on
			creating it due to incorrect arguments or insufficient memory.
			The free log entry function relies on non-null pointers	to free
			the corresponding pointer, which might point to an unnitialized
			memory address.
		*/
		entry->context = NULL;
		entry->key = NULL;
		entry->dlen = len;	// need receive len+1 when using strlen() to include the \0 too
		entry->data = (unsigned char *) malloc( entry->dlen );
		if( entry->data == NULL ) {
			logger_error( "unable to allocate log memory for the new log entry data" );
			free( entry );
			return NULL;
		}
		memcpy( entry->data, data, entry->dlen );

		if( type == SERVER_COMMAND ) {	// server command means that this entry is an xApp replication
			/*
				context and key cannot be null, otherwise we get compiler warnings to inline functions
				Note: gcc does not inline functions if lacking the -O? argument
				(see gcc -Wnnonul -O3)
			*/
			if( context && key ) {

				entry->clen = strlen( context );
				entry->klen = strlen( key );
				if( entry->clen == 0 ) {
					logger_error( "unable to allocate memory for the log's context" );
					free_log_entry( entry );
					return NULL;
				}
				entry->context = strndup( context, entry->clen );

				if( entry->klen == 0 ) {
					logger_error( "unable to allocate memory for the logs's key" );
					free_log_entry( entry );
					return NULL;
				}
				entry->key = strndup( key, entry->klen );

			} else {
				logger_error( "context and key must have a value (nil?)" );
				free_log_entry( entry );
				return NULL;
			}

		} else {		// if not a server command (xApp replication), it is a raft replication
			entry->clen = 0;	// not used by raft but it is safe to initialize
			entry->klen = 0;	// not used by raft but it is safe to initialize
		}
	}
	return entry;
}

/*
	Allocates and sets all fields for a new RAFT log entry

	len: if data is a str the len argument must include the \0 (i.e. strlen(str)+1 )

	Returns a populated new log entry or NULL in case of error
*/
log_entry_t *new_raft_log_entry( term_t term, log_entry_type_e type, int command, void *data, size_t len ) {
	/* this is a wrapper function */

	return new_log_entry( term, type, NULL, NULL, command, data, len );
}

/*
	Allocates and sets all fields for a new SERVER log entry (xApp)

	len: if data is a str the len argument must include the \0 (i.e. strlen(str)+1 )

	Returns a populated new log entry or NULL in case of error
*/
log_entry_t *new_server_log_entry( const char *context, const char *key, int command, void *data, size_t len ) {
	/* this is a wrapper function */

	return new_log_entry( 0, SERVER_COMMAND, context, key, command, data, len );
}

/*
	Does log compaction of server logs

	Only logs that are replicated on all replica servers are compacted (removed)

	This function is meant to be called right after taking each snapshot
	and before release the snapshot xapp_in_progress flag

	Params:
		- last_index: the last log index the server snaphost replaces. All log entries
			are compacted up to last_index (including)
*/
void compact_server_log( index_t last_index ) {
	index_t count;
	log_entry_t *entry;

	logger_debug( "compacting xApp log up to index %lu", last_index );

	pthread_mutex_lock( &server_log_lock );
	count = last_index - xapp_log.index_offset;
	pthread_mutex_unlock( &server_log_lock );

	while( count-- ) {

		pthread_mutex_lock( &server_log_lock );
		entry = log_ring_extract( xapp_log.entries );
		if( entry ) {
			xapp_log.memsize -= sizeof(log_entry_t) + entry->dlen + entry->clen + entry->klen;
			logger_trace( "compacting xApp log, index: %lu, remaining size: %lu bytes", entry->index, xapp_log.memsize );

			pthread_mutex_unlock( &server_log_lock );

			free_log_entry( entry );
		} else {
			logger_fatal( "unable to find and delete the xApp log entry with index %lu", index );
			exit( 1 );
		}
	}
	pthread_mutex_lock( &server_log_lock );
	xapp_log.index_offset = last_index;	// shifting to the last compacted index

	// logger_debug( "xApp logs compacted, remaining entries: %lu, size: %.3f mb",
	// 			xapp_log.last_log_index - last_index, xapp_log.memsize / (float)1048576 ); // bytes to Mbytes
	logger_debug( "xApp logs compacted, remaining entries: %lu, size: %lu bytes",
				xapp_log.last_log_index - last_index, xapp_log.memsize );

	pthread_mutex_unlock( &server_log_lock );
}

/*
	Does log compaction of raft logs

	Only logs that are replicated on all non-faulty followers are compacted (removed)

	This function is meant to be called right after taking each snapshot
	and before release the snapshot raft_in_progress flag

	Params:
		- last_index: the last log index the raft snaphost replaces. All log entries
			are compacted up to last_index (including)
*/
void compact_raft_log( index_t last_index  ) {
	index_t count;
	log_entry_t *entry;

	logger_debug( "compacting raft log up to index %lu", last_index );

	pthread_mutex_lock( &raft_log_lock );
	count = last_index - raft_log.index_offset;
	pthread_mutex_unlock( &raft_log_lock );

	while( count-- ) {

		pthread_mutex_lock( &raft_log_lock );
		entry = log_ring_extract( raft_log.entries );
		if( entry ) {
			raft_log.memsize -= sizeof(log_entry_t) + entry->dlen;
			logger_trace( "compacting raft log, index: %lu, remaining size: %lu bytes", entry->index, raft_log.memsize );

			pthread_mutex_unlock( &raft_log_lock );

			free_log_entry( entry );
		} else {	// no need to unlock mutex here
			logger_fatal( "unable to find and delete the raft log entry with index %ul", index );
			exit( 1 );
		}
	}
	pthread_mutex_lock( &raft_log_lock );
	raft_log.index_offset = last_index;	// shifting to the last compacted index

	// logger_info( "raft logs compacted, remaining entries: %lu, size: %.3f mb",
	// 			raft_log.last_log_index - last_index, raft_log.memsize / (float)1048576 ); // bytes to Mbytes
	logger_debug( "raft logs compacted, remaining entries: %lu, size: %lu bytes",
			raft_log.last_log_index - last_index, raft_log.memsize ); // bytes to Mbytes

	pthread_mutex_unlock( &raft_log_lock );

}
