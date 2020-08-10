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
	Mnemonic:	log.h
	Abstract:	Header file for the log functionalities

	Date:		1 November 2019
	Author:		Alexandre Huff
*/

#ifndef _RFT_LOG_H		 /* dup include prevention */
#define _RFT_LOG_H

#include "types.h"

#define INITIAL_LOG_ENTRIES 128


log_entries_t *get_raft_log( );	// testing purposes
void append_raft_log_entry( log_entry_t *log_entry );
void append_server_log_entry( log_entry_t *log_entry, server_id_t *server_id );
log_entry_t *get_raft_log_entry( index_t log_index );
index_t get_raft_last_log_index( );
index_t get_server_last_log_index( server_id_t *server_id );
term_t get_raft_last_log_term( );
void free_log_entry( log_entry_t *entry );
unsigned int serialize_raft_log_entries( index_t from_index, unsigned int *n_entries, unsigned char **lbuf,
										 unsigned int *buf_len, int max_msg_size );
unsigned int serialize_server_log_entries( index_t from_index, unsigned int *n_entries, unsigned char **lbuf,
											unsigned int *buf_len, int max_msg_size, server_id_t *server_id );
void deserialize_raft_log_entries( unsigned char *s_entries, unsigned int n_entries, log_entry_t **entries );
void deserialize_server_log_entries( unsigned char *s_entries, unsigned int n_entries, log_entry_t **entries );
int remove_raft_conflicting_entries( index_t from_index, raft_state_t *me );
log_entry_t *new_raft_log_entry( term_t term, log_entry_type_e type, int command, void *data, size_t len );
log_entry_t *new_server_log_entry( const char *context, const char *key, int command, void *data, size_t len );


#endif			/* dup include prevention */