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
	Mnemonic:	mtl.c
	Abstract:	Implements the RFT Message Transport Layer API to exchange messages
				between RFT servers

	Date:		04 December 2019
	Author:		Alexandre Huff
*/

#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "types.h"
#include "logger.h"
#include "mtl.h"
#include "log.h"

/*
	Simply copies data from append entries request network header to an append entries request message

	It is the caller resposibility to allocate and freeing the array of log entries for the copied message

	memcpy cannot be used since of possible unaligned data
*/
void appnd_entr_header_to_msg_cpy( appnd_entr_hdr_t *header, request_append_entries_t *msg ) {
	assert( header != NULL );
	assert( msg != NULL );

	msg->term = header->term;
	strcpy( msg->leader_id, header->leader_id );
	msg->prev_log_index = header->prev_log_index;
	msg->prev_log_term = header->prev_log_term;
	msg->leader_commit = header->leader_commit;
	msg->n_entries = header->n_entries;

}

/*
	Copies data from replication request network header to a replication request message

	It is the caller responsibility to allocate and freeing the array of log entries for the copied message

	memcpy cannot be used since of possible unaligned data
*/
void repl_req_header_to_msg_cpy( repl_req_hdr_t *header, replication_request_t *msg ) {
	assert( header != NULL );
	assert( msg != NULL );

	msg->master_index = header->master_index;
	msg->n_entries = header->n_entries;
	strcpy( msg->server_id, header->server_id );

}

/*
	Copies network header data from a server snapshot request to a snapshot request message

	It is the caller responsibility to allocate and freeing both pointers

	memcpy cannot be used since of possible unaligned data
*/
void server_snapshot_header_to_msg_cpy( req_snapshot_hdr_t *header, snapshot_request_t *msg ) {
	assert( header != NULL );
	assert( msg != NULL );

	msg->type = ntohl( header->type );
	msg->snapshot.last_log_index = NTOHLL( header->last_log_index );
	msg->snapshot.dlen = NTOHLL( header->dlen );
	msg->snapshot.items = ntohl( header->items );
	strcpy( msg->server_id, header->server_id );
}
