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
	Mnemonic:	snapshot.h
	Abstract:	Header file of the snapshotting functionalities

	Date:		14 May 2020
	Author:		Alexandre Huff
*/


#ifndef RFT_SNAPSHOT_H
#define RFT_SNAPSHOT_H

#include "types.h"
#include "rft.h"
#include "hashtable.h"


/*
	Defines the contexts which are playing the primary role
*/
typedef struct primary_ctx {
	unsigned int size;		// size of the alocatted contexts
	char **contexts;		// contexts to take snapshot
	unsigned int len;		// number of contexts to take snapshot
} primary_ctx_t;			// primary role contexts

/*
	Defines metadata of the snapshot taken either from the xapp or the raft configuration
	This metadata is sent through the pipe before the serialized snapshot data is sent through the pipe
	This is required since we need to know how much data will be read from the pipe
*/
typedef struct pipe_metabuf {
	term_t last_term;	// only used by raft snapshots
	index_t last_index;	// defines the last log index the raft snapshot replaces
	size_t dlen;		// data length of the snapshot
	/*
		number of items in the snapshot data
		each item corresponds to:
		xapp: clen | context | klen | key | vlen | value
		raft: server_id | target | raft_voting_member_e
	*/
	unsigned int items;
} pipe_metabuf_t;

/*
	Defines the content of the snapshot of a raft config server instance
	This structure is used to access the server data in the array of chars
	for both, to take snapshot and to install the raft config snapshot
	Please, do NOT use pointers here.
*/
typedef struct raft_server_snapshot {
	server_id_t server_id;
	target_t target;
	raft_voting_member_e status;
} raft_server_snapshot_t;


int read_pipe( int readfd, void *dest, size_t len );
int write_pipe( int writefd, void *src, size_t len );
void take_xapp_snapshot( hashtable_t *ctxtable, take_snapshot_cb_t take_snapshot_cb );
void take_raft_snapshot( );
int install_raft_snapshot( raft_snapshot_t *snapshot );
int serialize_xapp_snapshot( rmr_mbuf_t **msg, server_id_t *server_id );
int serialize_raft_snapshot( rmr_mbuf_t **msg );
int *get_xapp_in_progress( );			// testing purposes
int *get_raft_in_progress( );			// testing purposes
pipe_metabuf_t *get_xapp_metabuf( );	// testing purposes
pipe_metabuf_t *get_raft_metabuf( );	// testing purposes
void *parent_xapp_thread( );			// testing purposes
void *parent_raft_thread( );			// testing purposes
xapp_snapshot_t *get_xapp_snapshot( );	// testing purposes
primary_ctx_t *get_primary_ctxs( );		// testing purposes
void primary_ctx_handler( hashtable_t *table, const char *key );	// testing purposes
raft_snapshot_t *get_raft_snapshot( );	// testing purposes
void lock_raft_snapshot( );
void unlock_raft_snapshot( );
index_t get_raft_snapshot_last_index( );
index_t get_raft_snapshot_last_term( );

#endif
