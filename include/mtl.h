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
	Mnemonic:	mtl.h
	Abstract:	Defines the RFT Message Transport Layer API header for
				exchanging messages between replication servers

	Date:		04 December 2019
	Author:		Alexandre Huff
*/

#ifndef RFT_MTL_H
#define RFT_MTL_H

#include <inttypes.h>
#include <arpa/inet.h>

#include "types.h"

/* ===================== Conversion of 64 bit integers using two 32 bit htonl|ntofl functions ===================== */
/*
	Converts 64 bit integer from host to network using standard-32-bit functions htonl twice (one for each 32 bit word)
*/
#define HTONLL(x) ((1==htonl(1)) ? (x) : (((uint64_t)htonl((x) & 0xFFFFFFFFUL)) << 32) | htonl((uint32_t)((x) >> 32)))
/*
	Converts 64 bit integer from network to host using standard-32-bit functions nltoh twice (one for each 32 bit word)
*/
#define NTOHLL(x) ((1==ntohl(1)) ? (x) : (((uint64_t)ntohl((x) & 0xFFFFFFFFUL)) << 32) | ntohl((uint32_t)((x) >> 32)))


/*
	This is the append entries request header used as a transport
	layer of log entries in the network
	Invoked by leader to replicate log entries
	Also used as heartbeat (without entries)
	IMPORTANT: do NOT define pointers here, it will break serialization
*/
typedef struct appnd_entr_hdr {
	term_t term;				// leader’s term
	server_id_t leader_id;		// so follower can redirect clients
	index_t prev_log_index;		// index of log entry immediately preceding new ones
	term_t prev_log_term;		// term of prevLogIndex entry
	index_t leader_commit;		// leader’s commitIndex
	unsigned int n_entries;		// defines the number of the entries carried by this message
	unsigned int slen;			// holds the serialized payload length in bytes
} appnd_entr_hdr_t;

#define APND_ENTR_HDR_LEN ( sizeof(appnd_entr_hdr_t) )								// Defines the request append entries header length
#define APND_ENTR_PAYLOAD_ADDR(hdr) ( (unsigned char *)hdr + APND_ENTR_HDR_LEN )	// Gets the request append entries payload address
#define APND_ENTR_PAYLOAD_LEN(hdr) ( (appnd_entr_hdr_t *) hdr->slen )				// Gets the request append entries payload length

/*
	This is the replication request header used as a transport
	layer of xapp's log entries in the network

	IMPORTANT: do NOT define pointers here, it will break serialization
*/
typedef struct repl_req_hdr {
	index_t master_index;		// index of log entry immediately preceding new ones
	server_id_t server_id;		// identified from which server this replication is comming
	unsigned int n_entries;		// defines the number of the entries carried by this message
	unsigned int slen;			// holds the serialized payload length in bytes
} repl_req_hdr_t;

#define REPL_REQ_HDR_LEN ( sizeof(repl_req_hdr_t) )								// Defines the replication request header length
#define REPL_REQ_PAYLOAD_ADDR(hdr) ( (unsigned char *)hdr + REPL_REQ_HDR_LEN )	// Gets the payload address of replication request
#define REPL_REQ_PAYLOAD_LEN(hdr) ( (repl_req_hdr_t *) hdr->slen )				// Gets the payload length of replication request

/*
	Defines a raft log entry header to transport commands for the state machine
	Note: on adding or removing fields, check RAFT_LOG_ENTRY_HDR_SIZE macro and serialize/deserialize functions
	DANGER:	keep this struct aligned to avoid serialize/deserialize wrong information,
			care must be taken when adding, removing, or moving a field from its position
*/
typedef struct raft_log_entry_hdr {
	term_t	term;				// term when entry was received by the leader (first index term is 1)
	index_t	index;				// index of the log entry (populated be the append log entries according to its position in the log)
	size_t	dlen;				// length of the data in bytes
	log_entry_type_e type;		// defines if the log entry is a regular command or a raft configuration
	int command;				// command for state machine (user defined)
} raft_log_entry_hdr_t;

/*
	Defines the header's size of a raft log entry
	Care must be taken when moving the position of log_entry_t's fields (struct misalignment)
*/
// #define RAFT_LOG_ENTRY_HDR_SIZE ( sizeof(term_t) + sizeof(index_t) + sizeof(size_t) + sizeof(log_entry_type_e) + sizeof(int) )
#define RAFT_LOG_ENTRY_HDR_SIZE ( sizeof(raft_log_entry_hdr_t) )
#define RAFT_LOG_ENTRY_PAYLOAD_ADDR(hdr) ( (unsigned char *)hdr + RAFT_LOG_ENTRY_HDR_SIZE )

/*
	Defines a server log entry header to transport xApp's commands for the state machine
	Note: on adding or removing fields, check SERVER_LOG_ENTRY_HDR_SIZE and its related macros, and serialize/deserialize functions
	DANGER:	keep this struct aligned to avoid serialize/deserialize wrong information,
			care must be taken when adding, removing, or moving a field from its position
*/
typedef struct server_log_entry_hdr {
	index_t index;				// index of log entry (populated be the append log entries according to its position in the log)
	size_t dlen;				// length of the data in bytes
	unsigned int clen;			// length of the context in bytes
	unsigned int klen;			// lengh of the key in bytes
	int command;				// command for state machine (user defined)
	// int padding4bytes;		// check if required - not used, just for struct misalignment (aligned by 8 bytes)
} server_log_entry_hdr_t;

/*
	Defines the header's size of a server log entry (xApp replication)
	Care must be taken when moving the position of log_entry_t's fields (struct misalignment)

	Payload is organized as: CONTEXT | KEY | CMD_DATA	where CMD_DATA is the value of the key for that command
*/
#define SERVER_LOG_ENTRY_HDR_SIZE ( sizeof(server_log_entry_hdr_t) )	// Check if padding is required when using sizeof of the whole struct
// #define SERVER_LOG_ENTRY_HDR_SIZE ( sizeof(index_t) + sizeof(size_t) + sizeof(unsigned int) + sizeof(unsigned int) + sizeof(int) ) // without padding
#define SERVER_CTX_PAYLOAD_LEN(hdr) ( ntohl(((server_log_entry_hdr_t *)hdr)->clen) )
#define SERVER_KEY_PAYLOAD_LEN(hdr) ( ntohl((( server_log_entry_hdr_t *)hdr)->klen) )
#define SERVER_DATA_PAYLOAD_LEN(hdr) ( NTOHLL(((server_log_entry_hdr_t *)hdr)->dlen) )
#define SERVER_CTX_PAYLOAD_ADDR(hdr) ( (unsigned char *)hdr + SERVER_LOG_ENTRY_HDR_SIZE )
#define SERVER_KEY_PAYLOAD_ADDR(hdr) ( (unsigned char *)hdr + SERVER_LOG_ENTRY_HDR_SIZE + SERVER_CTX_PAYLOAD_LEN(hdr) )
#define SERVER_DATA_PAYLOAD_ADDR(hdr) ( (unsigned char *)hdr + SERVER_LOG_ENTRY_HDR_SIZE + SERVER_CTX_PAYLOAD_LEN(hdr) + SERVER_KEY_PAYLOAD_LEN(hdr) )

/*
	Defines a header to transport snapshots
	Note: on adding or removing fields, check SNAPSHOT_REQ_HDR_SIZE and its related macros, and serialize/deserialize functions
	DANGER:	keep this struct aligned to avoid serialize/deserialize wrong information,
			care must be taken when adding, removing, or moving a field from its position
*/
typedef struct req_snapshot_hdr {
	index_t last_log_index;		// index of the snapshot's last log entry
	size_t dlen;				// length of the snapshot data
	snapshot_type_e type;		// defines of the snapshot is of type either raft or server
	unsigned int items;			// number of context/key/value group items
	server_id_t server_id;
} req_snapshot_hdr_t;

/*
	Defines the header size of a snapshot message
*/
#define SNAPSHOT_REQ_HDR_SIZE ( sizeof(req_snapshot_hdr_t) )
#define SNAPSHOT_REQ_PAYLOAD_ADDR(hdr) ( (unsigned char *)hdr + SNAPSHOT_REQ_HDR_SIZE )

void appnd_entr_header_to_msg_cpy( appnd_entr_hdr_t *header, request_append_entries_t *msg );
void repl_req_header_to_msg_cpy( repl_req_hdr_t *header, replication_request_t *msg );
void server_snapshot_header_to_msg_cpy( req_snapshot_hdr_t *header, snapshot_request_t *msg );

#endif
