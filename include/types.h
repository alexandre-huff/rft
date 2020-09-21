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
	Mnemonic:	types.h
	Abstract:	Header file for types of the RFT library

	Date:		15 November 2019
	Author:		Alexandre Huff
*/

#ifndef _RFT_TYPES_H
#define _RFT_TYPES_H

#include <pthread.h>
#include <rmr/rmr.h>

#include "logring.h"

/*
	Defines RFT server_id data type

	rft_init checks if sizeof server_id is at most (RMR_MAX_SRC - 1) bytes to allow including \0
*/
typedef char server_id_t[ RMR_MAX_SRC ];

typedef unsigned long term_t;	// defines the raft term's type

typedef unsigned long index_t;	// defines the raft index's type

/*
	Defines possible states of terms after comparing received msg's term and servers' current_term
*/
typedef enum term_match {
	E_OUTDATED_MSG = 0,		// if term < currentTerm
	E_TERMS_MATCH = 1,		// if terms match ( equal )
	E_OUTDATED_TERM = 2		// if term > currentTerm
} term_match_e;

/*
	Defines if a server can be accounted for the majority of votes
*/
typedef enum raft_voting_member {
	NON_VOTING_MEMBER,	// votes are not accounted for
	VOTING_MEMBER		// votes are accounted for
} raft_voting_member_e;

/*
	Defines if the log is either a RAFT log or a SERVER log
*/
typedef enum log_type {
	RAFT_LOG = 0,
	SERVER_LOG
} log_type_e;

/*
	Defines if a log entry is a raft configuration, raft regular command, or an xApp command
*/
typedef enum log_entry_type {
	RAFT_CONFIG,		// defines a raft cluster configuration command (add/remove)
	RAFT_COMMAND,		// defines a regular command replicated using the full sync raft algorithm
	/*
		defines a no-operation log type, used when a leader has to commit entries from previous terms
		issued when a new leader is elected in the cluster

		Raft never commits log entries from previous terms by counting replicas.
		Only log entries from the leader’s current term are committed by counting replicas.
		Section 5.4.2 and Fig. 8 from raft paper

		Once an entry from the current term has been committed in this way, then all prior
		entries are committed indirectly because of the Log Matching Property.
		Section 3.6.2 in raft dissertation
	*/
	RAFT_NOOP,
	SERVER_COMMAND		// defines a server (xApp) command replicated using our async algorithm
} log_entry_type_e;

typedef enum server_active_status {
	SHUTDOWN = 0,	// indicates that the server's thread is not initialized yet
	RUNNING			// indicates that the server's thread is up and running (normal operation)
} server_active_status_e;

/*
	Defines commands for the raft CONFIG log type to manage the raft cluster configuration
*/
typedef enum raft_config_cmd {
	ADD_MEMBER,		// defines the add member command for a log type CONFIG
	DEL_MEMBER		// defines the delete member command for a log type CONFIG
} raft_config_cmd_e;

/*
	Defines the type of replication based on the RFT_REPLICA_SERVERS environmet variable
*/
typedef enum replication_type {
	GLOBAL,		// replicates xApp's state to all servers in the raft cluster
	PARTIAL		// replicates xApp's state to a set of servers in the raft cluster
} replication_type_e;

/*
	Defines the type of the snapshot
*/
typedef enum snapshot_type {
	RAFT_SNAPSHOT,		// raft snapshot
	SERVER_SNAPSHOT		// xapp snapshot
} snapshot_type_e;

/*
	Defines the particular RAFT state for the current server instance (me)
*/
typedef struct raft_state {
	int	state;				// Role playing by this server (LEADER, CANDIDATE, FOLLOWER, INIT_SERVER)
	term_t current_term;	// Persistent latest term server has seen (initialized to 0 on first boot, increases monotonically)
	/*
		Total of votes received by the candidate in its current term (term sent in request message)
		Initialized to 0 at each term, incremented by the candidate
	*/
	unsigned int rcv_votes;		// total of votes received in current term
	server_id_t self_id;		// Persistent selfId to be used on message exchanges
	server_id_t voted_for;		// Persistent candidateId that received vote in current term, or null if none
	server_id_t current_leader;	// defines the server_id this server "guess" thinks is the current leader
	/*
		volatile index of highest log entry known to be committed (should mean that log entry was saved on stable storage
		and can be applied to the state machine)
	*/
	index_t commit_index;
	/*
		volatile index of highest log entry applied to state machine (entries must be committed before applying to state machine)
	*/
	index_t last_applied;
} raft_state_t;

/*
	Defines common information for all cluster members
*/
typedef struct server {
	server_id_t server_id;

	/* ============= RAFT-specific from here ============= */
	char target[RMR_MAX_SRC];		// the source:port address from that server used to send AppendEntries messages by wormholes
	raft_voting_member_e status;	// defines if votes of this server are accounted for the majority (changed when processed by FSM)
	/*
		Defines if that member has voted for me in the current term
		Used by the candidate to check if that server has already voted in current term
	*/
	int voted_for_me;
	index_t next_index;		// index of the next log entry to send to that server (initialized to leader last log index + 1)
	index_t match_index;	// index of highest log entry known to be replicated on that server (initialized to 0, increases monotonically)
	pthread_mutex_t index_lock;	// required when handling any of next_index, or match_index
	pthread_t th_id;		// identifies the thread that is sending AppendEntries to that server (this id is not used yet)
	pthread_cond_t *heartbeat_cond;	// pointer to the condition to send a specific append entries signal to wake up this thread
	pthread_cond_t *leader_cond;	// pointer to the condition to wake up that thread if not in leader state (upon removing a follower)
	server_active_status_e active;
	struct timespec replied_ts;		// identifies the replied timestamp used to determine if the NON_VOTING server is caught-up with the leader
	int hb_timeouts;		// counter of the number of heartbeat timeouts that a server did not reply for append entries (fault detection)
	/* ============= RAFT-specific up to here ============= */

	/*	============= added after version 0.1 for xapp state replication =============	*/

	/*
		index of highest xapp's log entry known to be replicated in this server (initialized to 0, increases monotonically)
		(it is the xapp's last log index stored in the backup replica i.e. receiver -- used by the backup)
	*/
	index_t replica_index;
	/*
		index of the xapp's last log entry sent to this server, initialized to 0
		(it is the xapp's last log index stored in the primary server i.e. sender -- used by the primary)
	*/
	index_t master_index;
	rmr_whid_t *whid;		// wormhole id used to send replication requests (initilized by the server's thread)
} server_t;

/*
	Defines the raft configuration, by holding the total of servers which are part of the cluster
	and also the total of voting members
*/
typedef struct config {
	unsigned int size;				// defines the total size of the cluster, including voting and non-voting members
	unsigned int voting_members;	// defines the total of voters in the cluster
	/*
		defines if a cluster configuration (add/rm) is changing (only one at a time is allowed)
		starts by appending entry to the log, and finishes when commit it (only then it is safe to start a new configuration)
	*/
	int is_changing;			 // defines if a configuration is on the way
	server_t **servers;			 // array of servers in the cluster
} raft_config_t;

/*
	Defines the command data for a log entry that adds/removes a server in the configuration
*/
typedef struct server_conf_cmd_data {
	server_id_t server_id;
	char target[RMR_MAX_SRC];
} server_conf_cmd_data_t;

/*
	Defines a log entry for the state machine
	Note: on adding or removing fields, check LOG_ENTRY_HDR_SIZE macro and serialize/deserialize functions
*/
typedef struct log_entry {
	term_t	term;				// term when entry was received by leader (first index term is 1)
	index_t index;				// index of log entry (populated be the append log entries according to its position in the log)
	size_t	dlen;				// length of the data in bytes
	unsigned int clen;			// length of the context in bytes
	unsigned int klen;			// length of the key in bytes
	log_entry_type_e type;		// defines if the log entry is a regular command or a raft configuration
	int	 command;				// command for state machine (user defined)
	char *context;				// only makes sense for xApp server replication
	char *key;					// only makes sense for xApp server replication
	unsigned char *data;		// holds the data executed by command
} log_entry_t;

/*
	Defines the in-memory log structure
*/
typedef struct log_entries {
	size_t memsize;				// memory size (in bytes) of all log entries stored in the *entries ring (data and metadata)
	size_t mthresh;				// defines the memory threshold (in bytes) to trigger the corresponding snapshot function
	index_t cthresh;			// defines the entries count threshold to trigger the corresponding snapshot function
	index_t first_log_index;	// the first index of the stored log entries (used for snapshotting)
	index_t last_log_index;		// the index of the last log saved in entries
	logring_t *entries;			// ring buffer pointing to the log entries
} log_entries_t;

/*
	Invoked by leader to replicate log entries, also used as heartbeat
	DANGER: on modifying this struct check the definition of apnd_entr_hdr_t in mtl.h
	Do NOT use this as a network transport layer, use mtl api instead
*/
typedef struct request_append_entries {
	term_t term;				// leader’s term
	server_id_t leader_id;		// so follower can redirect clients
	index_t prev_log_index;		// index of log entry immediately preceding new ones
	term_t prev_log_term;		// term of prevLogIndex entry
	index_t leader_commit;		// leader’s commitIndex
	unsigned int n_entries;		// defines the number of the entries carried by this message
	log_entry_t **entries;		// log entries (not needed for heartbeat; may send more than one entry for efficiency)
} request_append_entries_t;

typedef struct reply_append_entries {
	term_t term;			// currentTerm, for leader to update itself
	/*
		length of the follower log, allows the leader cap the follower's nextIndex accordingly, pg. 39 raft dissertation
	*/
	index_t last_log_index;
	int success;			// true if follower contained entry matching prevLogIndex and prevLogTerm
	server_id_t server_id;	// identifies the server which is replying this message
} reply_append_entries_t;

/*
	Defines the set of servers that will be used to replicate xApp state
*/
typedef struct replicas {
	unsigned int len;		// defines the number of replicas in the array (array may be greater than len)
	server_t **servers;
} replicas_t;

/*
	Replication msg of xapp's log entries
	Do NOT use it as a network transport layer, use mtl api instead
*/
typedef struct replication_request {
	index_t master_index;		// index of the log entry immediately preceding new ones
	server_id_t server_id;		// identified from which server this replication is comming
	unsigned int n_entries;		// defines the number of the entries carried by this message
	log_entry_t **entries;		// log entries deserialized
} replication_request_t;

typedef struct replication_reply {
	int success;				// true if server contained entry matching prev_log_index
	index_t replica_index;		// the highest replicated index in the server
	server_id_t server_id;		// identifies the server which is replying this message
} replication_reply_t;

/*
	Invoked by candidates to gather votes
*/
typedef struct request_vote {
	term_t term;				// candidate’s term
	index_t last_log_index;		// index of candidate’s last log commited entry
	term_t last_log_term;		// term of candidate’s last log commited entry
	server_id_t candidate_id;	// candidate requesting vote
} request_vote_t;

typedef struct reply_vote {
	term_t term;		// follower/candidate currentTerm, for candidate (another) to update itself
	int	granted;		// 1 means candidate received vote, 0 otherwise
	server_id_t voter;	// this actually is required to identify which server has voted (avoid duplicated counting in candidate)
} reply_vote_t;

/*
	Invoked by servers when in the init state to discover a leader for catching up log entries and join to the cluster
*/
typedef struct membership_request {
	/*
		identifies the last log replicated on that server
		allows the leader to cap the the follower's next_index quickly on catching up a new follower
		it avoids the problem of back-and-forth heartbeats up to the leader find the nextIndex accordingly
		last paragraph of section 4.2.1 in raft dissertation, pg 39
	*/
	index_t last_log_index;
	server_id_t server_id;
} membership_request_t;

/*
	Defines a generic snapshot structure
*/
typedef struct snapshot {
	index_t last_log_index;	// snapshot index of log entry immediately preceding new ones
	size_t dlen;			// data length of the snapshot
	unsigned int items;		// number of items in the snapshot data ( each item corresponds to: clen|context|klen|key|vlen|value )
	unsigned char *data;	// pointer to the state data
} snapshot_t;

typedef struct snapshot_request {
	snapshot_type_e type;		// RAFT or SERVER
	server_id_t server_id;
	snapshot_t snapshot;
} snapshot_request_t;

typedef struct snapshot_reply {
	snapshot_type_e type;		// RAFT or SERVER
	int success;				// true if server contained entry matching prev_log_index
	index_t last_log_index;		// the highest replicated index in the server
	server_id_t server_id;		// identifies the server which is replying this message
} snapshot_reply_t;

#endif
