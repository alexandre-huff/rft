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
	Mnemonic:	rft_private.h
	Abstract:	Private header file for the RFT library

	Date:		6 April 2020
	Author:		Alexandre Huff
*/

#ifndef _RFT_PRIV_H
#define _RFT_PRIV_H

#include "types.h"

/*
	This is the very first state used by a server to catch up all log entries from the leader before be added to the cluster
*/
#define INIT_SERVER 0
/*
	Defines the main server states
*/
#define FOLLOWER	1
#define CANDIDATE	2
#define LEADER		3


/* ############## Private RFT functions ############## */

int set_max_msg_size( int size );
void *raft_server( void *new_server );
void handle_membership_request( membership_request_t *membership_msg, target_t *rmr_src );
void handle_append_entries_request( request_append_entries_t *request_msg, reply_append_entries_t *response_msg);
void handle_append_entries_reply( reply_append_entries_t *reply_msg );
raft_state_t *get_me( );
server_id_t *get_myself_id( );
void set_mrc( void *_mrc );
index_t get_full_replicated_log_index( );
void lock_raft_state( );
void unlock_raft_state( );
term_t get_raft_current_term( );
index_t get_raft_last_applied( );
void set_raft_current_term( term_t term );
void set_raft_last_applied( index_t last_applied );
void set_raft_commit_index( index_t index );
void update_replica_servers( );


#endif
