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


typedef enum log_class {
	RAFT_LOG = 0,
	SERVER_LOG
} log_class_e;

pthread_mutex_t raft_log_lock = PTHREAD_MUTEX_INITIALIZER;		// mutex for locking the raft_log variable
pthread_mutex_t server_log_lock = PTHREAD_MUTEX_INITIALIZER;	// mutex for locking the xapps_log variable

/*
	This is specific for raft full synchronous replication
	Stores log entries for the membership managent and replication based on raft algorithm
*/
static log_entries_t raft_log = {
	.size = 0,
	.head = 0,
	.entries = NULL
};

/*
	Array of logs to replicate the state of xapps

	Stores log entries replicated by this xapp and received from other xapps

	No mutex is being used yet
*/
static xapps_logs_t xapps_logs = {
	.len = 0,
	.servers_logs = NULL
};

/*
	Used only for testing purposes

	Returns the raft's log pointer

	Not thread-safe, assumes that the log_lock has been acquired by the caller
*/
log_entries_t *get_raft_log( ) {
	return &raft_log;
}

/*
	Returns a server's log pointer (xApp state replication log)
*/
log_entries_t *get_server_log( server_id_t *server_id ) {
	size_t i;

	for( i = 0; i < xapps_logs.len; i++ ) {
		if( strcmp( xapps_logs.servers_logs[i].server_id, *server_id ) == 0 ) {
			return &xapps_logs.servers_logs[i].log;
		}
	}

	return NULL;
}

/*
	Not thread-safe, assumes that the log_lock has been acquired by the caller

	IMPORTANT:
	This is a generic function that allows ensure log capacity for both, raft full sync
	replication log, and xApp state replication log
*/
static inline void ensure_log_size( log_entries_t *log ) {
	assert( log != NULL );

	if( log->head == log->size ) {
		log->size *= 2;

		if ( log->size == 0 )
			log->size = INITIAL_LOG_ENTRIES;

		// this causes memory likage, but we are using exit in the following, so it's ok in this case
		log->entries = (log_entry_t **) realloc( log->entries, log->size * sizeof( log_entry_t *) );
		if( log->entries == NULL ) {
			logger_fatal( "unable to reallocate memory to hold new log entries" );
			exit( 1 );
		}
	}
}

/*
	Generic function to append a new log entry for both, raft's and xapp's log

	Sets the log_index of the new log entry according to its position in the log

	IMPORTANT:
		Not thread-safe, assumes that the log's lock has been acquired by the caller if required
*/
static inline void append_log_entry( log_entry_t *log_entry, log_entries_t *log ) {
	assert( log_entry != NULL );
	assert( log != NULL );

	ensure_log_size( log );

	log_entry->index = log->head + 1;	// raft starts its log index at 1, so we need to add 1 here

	log->entries[ log->head ] = log_entry;
	log->head++;
}

/*
	Appends a new log entry to the raft's log

	Sets the log_index of the new log entry according to its position in the log
*/
void append_raft_log_entry( log_entry_t *log_entry ) {
	server_conf_cmd_data_t *data = NULL;

	pthread_mutex_lock( &raft_log_lock );

	// we had to write a wrapper for a generic append log entries, so all particularities will be kept in this module
	append_log_entry( log_entry, &raft_log );

	if( log_entry->type == RAFT_CONFIG ) {
		data = (server_conf_cmd_data_t *) log_entry->data;

		if( log_entry->command == ADD_MEMBER ) {

			/*
				if we are the leader, that call will return a pointer, otherwise will return NULL and
				it needs to be added to the servers config
			*/
			if( raft_config_get_server( &data->server_id ) == NULL ) {	// if not found, add it
				if( raft_config_add_server( &data->server_id, data->target, 0 ) == NULL ) { // we can't continue running without this server
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
void append_server_log_entry( log_entry_t *log_entry, server_id_t *server_id ) {
	log_entries_t *log;

	pthread_mutex_lock( &server_log_lock );

	log = get_server_log( server_id );
	if( log == NULL ) { // if not found we create it
		logger_debug( "reallocating memory for server %s log replication", server_id );
		xapps_logs.servers_logs = (server_log_t *)	realloc( xapps_logs.servers_logs, (xapps_logs.len + 1) * sizeof( server_log_t ) );
		if( xapps_logs.servers_logs == NULL ) {
			logger_fatal( "unable to reallocate memory for xapp log replication" );
			exit( 1 );
		}
		strcpy( xapps_logs.servers_logs[xapps_logs.len].server_id, *server_id );
		xapps_logs.servers_logs[xapps_logs.len].log.size = 0;
		xapps_logs.servers_logs[xapps_logs.len].log.head = 0;
		xapps_logs.servers_logs[xapps_logs.len].log.entries = NULL;

		log = &xapps_logs.servers_logs[xapps_logs.len].log;	// we don't need to call get_server_log() here, as we already have the pointer

		xapps_logs.len++;
	}

	// wrapper as same as append raft log entry
	append_log_entry( log_entry, log );

	pthread_mutex_unlock( &server_log_lock );
}

/*
	Removes and frees all conflicting entries starting (including) from the
	raft conflicting index "from_index" entry up to the end of the log

	NOTE: Not thread-safe, assumes that the caller has the lock of server_state

	If from_index is smaller than current commit index, than that entries will not
	be removed, that means that message are being duplicate likely because
	the follower is too slow

	Returns 1 if conflicting log entries were removed, 0 otherwise
*/
int remove_raft_conflicting_entries( index_t from_index, server_state_t *me ) {
	/*
		There wont have conflicts in server (xApp) state replication (Only primary stores log entries)
		Thus, there is no need to write another function to remove server conflicting entries
	*/

	log_entry_t *entry;
	server_conf_cmd_data_t *cmd_data;

	/*
		Checking to avoid underflow when decrementing
		Checking and assuring that commited indexes wont be removed from the log
		Commited logs cannot be removed, since they likely have been applied
		The commitIndex increases monotonically (see fig 2 raft paper)
	*/
	if( ( from_index > 0 ) && ( from_index > me->commit_index ) ) {
		logger_info( "removing all conflicting log entries from raft index %lu", from_index );

		from_index--;

		pthread_mutex_lock( &raft_log_lock );
		while( raft_log.head > from_index ) {
			raft_log.head--;	// pointing to the log entry that will be removed

			entry = raft_log.entries[ raft_log.head ];
			/*
				"Unfortunately, this decision does imply that a log entry for a configuration
				change can be removed (if leadership changes); in this case, a server must be prepared to fall back
				to the previous configuration in its log."
				Source: raft dissertation, check at the end of section 4.1
			*/
			if( entry->type == RAFT_CONFIG ) {
				cmd_data = (server_conf_cmd_data_t *) entry->data;
				if( entry->command == ADD_MEMBER ) {		// removing the server added server
					raft_config_remove_server( &cmd_data->server_id );

				} else if( entry->command == DEL_MEMBER ) {	// adding back the server removed server

					if( raft_config_add_server( &cmd_data->server_id, cmd_data->target, 0 ) == NULL ) {
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

			free_log_entry( entry );
		}

		pthread_mutex_unlock( &raft_log_lock );
		return 1;

	} else {
		logger_warn( "attempt to remove committed entries from the log, some process is slow or messages are being duplicated" );
	}

	return 0;
}

/*
	Gets a log entry from its raft log index
	Note: raft log index is NOT the in-memory array index
*/
log_entry_t *get_raft_log_entry( index_t log_index ) {
	log_entry_t *entry = NULL;

	/*
		needs to be converted from a raft log index (struct index) to an in-memory array log index
		Since an array starts from 0 and raft log index start from 1, we just need to decrement it
	*/
	log_index--;

	pthread_mutex_lock( &raft_log_lock );

	if( ( raft_log.size > 0 ) && ( log_index < raft_log.head ) )
		entry = raft_log.entries[ log_index ];

	pthread_mutex_unlock( &raft_log_lock );

	return entry;
}

/*
	Gets a log entry from its server log index
	Note: server log index is NOT the in-memory array index
*/
log_entry_t *get_server_log_entry( index_t log_index, server_id_t *server_id ) {
	log_entry_t *entry = NULL;
	log_entries_t *log = NULL;

	/*
		needs to be converted from a server log index (struct index) to an in-memory array log index
		Since an array starts from 0 and server log index start from 1, we just need to decrement it
	*/
	log_index--;

	pthread_mutex_lock( &server_log_lock );

	log = get_server_log( server_id );

	if( log != NULL ) {
		if( ( log->size > 0 ) && ( log_index < log->head ) )
			entry = log->entries[ log_index ];
	}

	pthread_mutex_unlock( &server_log_lock );

	return entry;
}

/*
	Returns the last index from the raft's log (including not commited)

	This can also be used to return the length of the log, 0 means the log is empty
*/
index_t get_raft_last_log_index( ) {
	index_t index;

	pthread_mutex_lock( &raft_log_lock );

	if( raft_log.size > 0 )
		index = raft_log.head;	 // as same as "log.entries[ log.head - 1 ]->index", but faster
	else
		index = 0;

	pthread_mutex_unlock( &raft_log_lock );

	return index;
}

/*
	Returns the last index from the servers's log

	This can also be used to return the length of the log, 0 means the log is empty
*/
index_t get_server_last_log_index( server_id_t *server_id ) {
	log_entries_t *log = NULL;
	size_t index = 0;

	pthread_mutex_lock( &server_log_lock );

	log = get_server_log( server_id );

	if( log )
		index = log->head;		// as same as "log.entries[ log.head - 1 ]->index", but faster

	pthread_mutex_unlock( &server_log_lock );

	return index;
}


/*
	Returns the last term from the log (including not commited)
*/
term_t get_raft_last_log_term( ) {
	term_t term;

	/*
		server's last log term not needed, thus it does not have an implementation
	*/

	pthread_mutex_lock( &raft_log_lock );

	if( raft_log.size > 0 )
		term = raft_log.entries[ raft_log.head - 1 ]->term;
	else
		term = 0;

	pthread_mutex_unlock( &raft_log_lock );

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
	Generic function to serialize log entries either for raft log entries or server log entries
	Note: this function does not frees the pointer of lbuf, that is resposibility of the caller

	IMPORTANT: not thread-safe, assumes that the caller owns the lock of the log param (if required)

	from_index: defines the starting raft log index that the serialization must start
	n_entries: defines the number of entries to be serialized
	**lbuf: is the log buffer where the the serialized data will be written (will be reallocated if it is not enough)
	*buf_len: defines the current size of the lbuf
	max_buf_len: indicates the maximum number of bytes can be added in the message. This function does not take into account
				 the size of the RFT mtl header, so, it needs to be subtracted from the RMR' maximum message size before
				 calling this function. The RMR's maximum message size is defined in the xApp context and passed via argument
				 to the RFT init function.
	type:	defines which type of log we are serializing (RAFT or SERVER)

	Returns the total of serialized bytes
*/
static inline unsigned int serialize_log_entries( index_t from_index, unsigned int *n_entries, unsigned char **lbuf,
												unsigned int *buf_len, int max_buf_len, log_entries_t *log, log_class_e class ) {
	unsigned int bytes = 0;	 // size of the current serialization
	unsigned int esize = 0;	 // entry size
	unsigned int count = 0;	 // counter of the number of entries that have been serialized
	log_entry_t *entry = NULL;
	raft_log_entry_hdr_t raft_hdr;		// raft log entry with converted network byte order in header
	server_log_entry_hdr_t server_hdr;	// server log entry with converted network byte order in header
	unsigned char *bufptr;				// auxiliary offset pointer

	/*
		from_index needs to be converted from a raft log index (struct index) to an in-memory array log index
		Since an array starts from 0 and raft log index start from 1, we just need to decrement it
	*/
	from_index--;

	if( ( from_index + *n_entries ) <= log->head ) {

		for( ; (count < *n_entries) && (bytes <= max_buf_len); from_index++, count++ ) {

			entry = log->entries[from_index];
			if( class == SERVER_LOG ) {
				esize = SERVER_LOG_ENTRY_HDR_SIZE + entry->clen + entry->klen + entry->dlen;	// entry size = header + command
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
					logger_fatal( "unable to realloc memory to serialize log buffer" );
					exit( 1 );
				}
			}

			bufptr = *lbuf + bytes; // pointing to the next available spot

			if( class == SERVER_LOG ) {
				server_hdr.index = HTONLL( entry->index );
				server_hdr.clen = htonl( entry->clen );
				server_hdr.klen = htonl( entry->klen );
				server_hdr.dlen = HTONLL( entry->dlen );
				server_hdr.command = htonl( entry->command );

				memcpy( bufptr, &server_hdr, SERVER_LOG_ENTRY_HDR_SIZE ); // copying all header into log buffer
				memcpy( SERVER_CTX_PAYLOAD_ADDR( bufptr ), entry->context, entry->clen );	// copying the context str
				memcpy( SERVER_KEY_PAYLOAD_ADDR( bufptr ), entry->key, entry->klen );		// copying the key str
				memcpy( SERVER_DATA_PAYLOAD_ADDR( bufptr ), entry->data, entry->dlen );		// copying the cmd_data str

				bytes += SERVER_LOG_ENTRY_HDR_SIZE + entry->clen + entry->klen + entry->dlen;

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
		if( !count )	// if count is 0, then no log entry can be sent
			logger_error( "unable to serialize log entries, "
							"reason: size of a log entry is greater than the RMR's max msg size" );

	} else {
		logger_fatal( "unable to serialize log entries, out of range ==> (from_index + n_entries)=%lu <= log.head=%lu",
						from_index + *n_entries, log->head );
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
*/
unsigned int serialize_raft_log_entries( index_t from_index, unsigned int *n_entries, unsigned char **lbuf,
										 unsigned int *buf_len, int max_msg_size ) {
	// this is a wrapper function
	unsigned int bytes;

	pthread_mutex_lock( &raft_log_lock );

	// we can only serialize RMR's max_msg_size MINUS size of the append entries mtl header
	bytes = serialize_log_entries( from_index, n_entries, lbuf, buf_len, max_msg_size - APND_ENTR_HDR_LEN, &raft_log, RAFT_LOG );

	pthread_mutex_unlock( &raft_log_lock );

	return bytes;
}

/*
	Serializes server log entries (xApp state replication)

	Note: this function does not frees the pointer of lbuf, that is resposibility of the caller

	from_index: defines the starting of server's log index that the serialization must start
	n_entries: defines the number of entries to be serialized
	**lbuf: is the log buffer where the the serialized data will be written (will be reallocated if it is not enough)
	*buf_len: defines the current size of the lbuf
	*server_id: defines from which server the log will be returned

	Returns the total of serialized bytes
*/
unsigned int serialize_server_log_entries( index_t from_index, unsigned int *n_entries, unsigned char **lbuf,
											unsigned int *buf_len, int max_msg_size, server_id_t *server_id ) {
	// this is a wrapper function
	unsigned int bytes;

	pthread_mutex_lock( &server_log_lock );

	// we can only serialize RMR's max_msg_size MINUS size of the replication request mtl header
	bytes = serialize_log_entries( from_index, n_entries, lbuf, buf_len, max_msg_size - REPL_REQ_HDR_LEN, get_server_log( server_id ), SERVER_LOG );

	pthread_mutex_unlock( &server_log_lock );

	return bytes;
}

/*
	Deserializes n_entries of log entry from **s_entries and stores data in the array **entries

	Assumes that array of **entries is at least of size n_entries

	This function is generic: can be used to deserialize log entries for raft and server logs
*/
static inline void deserialize_log_entries( unsigned char *s_entries, unsigned int n_entries, log_entry_t **entries, log_class_e class ) {
	unsigned int i;
	// unsigned int offset = 0;
	log_entry_t *entry = NULL;
	raft_log_entry_hdr_t raft_hdr;		// raft log entry with converted network byte order in header
	server_log_entry_hdr_t server_hdr;	// server log entry with converted network byte order in header
	size_t hdr_len;						// auxiliary log entry header length
	size_t plen;						// auxiliary log entry to store the whole payload size
	unsigned char *dataptr = NULL;		// auxiliary pointer to the command data

	hdr_len = ( class == SERVER_LOG ? SERVER_LOG_ENTRY_HDR_SIZE : RAFT_LOG_ENTRY_HDR_SIZE );	// keeps our function more clear

	for( i = 0; i < n_entries; i++ ) {
		entries[i] = (log_entry_t *) malloc( sizeof( log_entry_t ) );
		entry = entries[i];
		if( entry == NULL ) {
			logger_fatal( "unable to allocate memory to deserialize a log entry" );
			exit( 1 );
		}

		if( class == SERVER_LOG ) { // xApp replication log entries
			memcpy( &server_hdr, s_entries, hdr_len );
			// entry->term = 0;	 // not used by xApp replication
			entry->index = NTOHLL( server_hdr.index) ;
			entry->command = ntohl( server_hdr.command );
			entry->dlen = NTOHLL( server_hdr.dlen );
			entry->clen = ntohl( server_hdr.clen );
			entry->klen = ntohl( server_hdr.klen );
			entry->type = SERVER_COMMAND;

			entry->context = strndup( (char *)SERVER_CTX_PAYLOAD_ADDR( s_entries ), entry->clen );
			if( entry->context == NULL ) {
				logger_fatal( "unable to duplicate memory to deserialize a log entry's context" );
				exit( 1 );
			}

			entry->key = strndup( (char *)SERVER_KEY_PAYLOAD_ADDR( s_entries ), entry->klen );
			if( entry->key == NULL ) {
				logger_fatal( "unable to duplicate memory to deserialize a log entry's key" );
				exit( 1 );
			}

			dataptr = SERVER_DATA_PAYLOAD_ADDR( s_entries );	// pointing to the command data
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
			// entry->clen = 0;		// not used by raft replication
			// entry->klen = 0;		// not used by raft replication

			dataptr = RAFT_LOG_ENTRY_PAYLOAD_ADDR( s_entries );	// pointing to the command data
			plen = entry->dlen;		// computing the whole payload size
		}

		entry->data = (unsigned char *) malloc( plen );
		if( entry->data == NULL ) {
			logger_fatal( "unable to allocate memory to deserialize a command data" );
			exit( 1 );
		}

		memcpy( entry->data, dataptr, plen );

		// offset += hdr_len + entry->dlen;
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
		entry->dlen = len;	// need receive len+1 when using strlen() to include the \0 too
		entry->data = (unsigned char *) malloc( entry->dlen );
		if( entry->data == NULL ) {
			logger_error( "unable to allocate log memory for the new log entry data" );
			free( entry );
			return NULL;
		}
		memcpy( entry->data, data, entry->dlen );

		if( type == SERVER_COMMAND ) {	// server command means that this entry is an xApp replication
			entry->clen = strlen( context );
			entry->klen = strlen( key );
			entry->context = strndup( context, entry->clen );
			if( entry->context == NULL ) {
				logger_error( "unable to allocate memory for the log's context" );
				free( entry->data );
				free( entry );
				return NULL;
			}
			entry->key = strndup( key, entry->klen );
			if( entry->key == NULL ) {
				logger_error( "unable to allocate memory for the logs's key" );
				free( entry->context );
				free( entry->data );
				free( entry );
				return NULL;
			}

		} else {		// if not a server command (xApp replication), it is a raft replication
			entry->context = NULL;
			entry->key = NULL;
			// entry->clen = 0;	// not used by raft
			// entry->klen = 0;	// not used by raft
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
