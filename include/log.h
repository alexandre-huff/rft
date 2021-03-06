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
	Mnemonic:	log.h
	Abstract:	Header file for the log functionalities

	Date:		1 November 2019
	Author:		Alexandre Huff
*/

#ifndef _RFT_LOG_H		 /* dup include prevention */
#define _RFT_LOG_H

#include "types.h"

#define RAFT_LOG_SIZE	512		// size of the ring to store raft log entries (power of 2)
#define SERVER_LOG_SIZE	1048576	// size of the ring to store server (xApp) log entries (power of 2)
// TODO change SERVER_LOG_SIZE to 131072 after implementing snapshotting

int init_log( log_type_e type, u_int32_t size );
log_entries_t *get_raft_log( );		// testing purposes
log_entries_t *get_server_log( );	// testing purposes
void append_raft_log_entry( log_entry_t *log_entry );
void append_server_log_entry( log_entry_t *log_entry );
log_entry_t *get_raft_log_entry( index_t log_index );
log_entry_t *get_server_log_entry( index_t log_index );
index_t get_raft_last_log_index( );
index_t get_server_last_log_index( );
term_t get_raft_last_log_term( );
void free_log_entry( log_entry_t *entry );
unsigned int serialize_raft_log_entries( index_t from_index, unsigned int *n_entries, unsigned char **lbuf,
										 unsigned int *buf_len, int max_msg_size );
unsigned int serialize_server_log_entries( index_t from_index, unsigned int *n_entries, unsigned char **lbuf,
											unsigned int *buf_len, int max_msg_size );
void deserialize_raft_log_entries( unsigned char *s_entries, unsigned int n_entries, log_entry_t **entries );
void deserialize_server_log_entries( unsigned char *s_entries, unsigned int n_entries, log_entry_t **entries );
int remove_raft_conflicting_entries( index_t from_index, index_t committed_index );
log_entry_t *new_raft_log_entry( term_t term, log_entry_type_e type, int command, void *data, size_t len );
log_entry_t *new_server_log_entry( const char *context, const char *key, int command, void *data, size_t len );


#endif			/* dup include prevention */