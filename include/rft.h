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
	Mnemonic:	rft.h
	Abstract:	Header file for the RFT library

	Date:		22 October 2019
	Author:		Alexandre Huff
*/

#ifndef _RFT_H
#define _RFT_H


#define MAX_RMR_RETRIES			10		// defines the number of retry loops before giving up sending a message (RMR_ERR_RETRY)

/*
	RFT message types
*/
#define APPEND_ENTRIES_REQ		200		// Append Entries and Heartbeat request
#define APPEND_ENTRIES_REPLY	201		// Append Entries and Heartbeat reply
#define VOTE_REQ				202		// Vote Request
#define VOTE_REPLY				203
#define MEMBERSHIP_REQ			204		// Message sent from a server which is trying to join to the cluster
#define REPLICATION_REQ			205		// Append Entries used to replicate log entries for xApps commands
#define REPLICATION_REPLY		206		// Append Entries replication reply for xApps commands
#define XAPP_SNAPSHOT_REQ		207		// xApp snapshot request message
#define XAPP_SNAPSHOT_REPLY		208
#define RAFT_SNAPSHOT_REQ		209		// Raft snapshot request message
#define RAFT_SNAPSHOT_REPLY		210

#define HEARTBEAT_TIMETOUT		1000	// time (ms) that a leader waits before issue a heartbeat message

/*
	Time (ms) a follower waits to become candidate, and time a candidade waits to reissue a new leader election

	Election timeout is defined by the following range:
	Min time: ELECTION_TIMEOUT
	Max time: ELECTION_TIMEOUT * 2
	Tipically election timeout values are between 150 and 300 ms (raft paper and dissertation)
*/
#define ELECTION_TIMEOUT		2500

/*
	defines the max number of heartbeats without replies before removing the server from the cluster
	used to provide somewhat of fault detection and for sending notification of routing changes to the RIC Routing Manager
*/
#define MAX_HEARBEAT_TIMEOUTS	5

/*
	Defines the periodicity (in ms) the replication thread will be awaked for
	checking and replicating the xapp state
	This is the default replication interval, and it can be changed by an environment variable
	in the init function
*/
#define REPLICATION_INTERVAL	10

/*
	Defines the threshold (in Mbytes) to trigger the corresponding snapshot function
	Basically, it defines the maximum size a log can grow up to create a new snapshot
	This constant is used for both, RAFT and xApp log entries
*/
#define LOG_SIZE_THRESHOLD		10

/*
	Defines constants to access Redis database which is used to bootstrap the RFT cluster
	while the instance is in the INIT_SERVER state
*/
#define BOOTSTRAP_REDIS_HOST "service-ricplt-dbaas-tcp.ricplt"
#define BOOTSTRAP_REDIS_PORT 6379
#define BOOTSTRAP_REDIS_KEY "rft.leader"	// defines the default key RFT will set/get on bootstrapping and when becomes leader

/*
	Defines the role that an xApp replica plays for a given Managed Equipment
*/
typedef enum role {
	RFT_NONE = 0,
	RFT_PRIMARY,
	RFT_BACKUP
} role_e;

/* FSM apply callback function that xApps must implement */
typedef void (*apply_state_cb_t)(const int command, const char *context, const char *key, const unsigned char *value, const size_t len);

/*
	This callback must be implemented by the xApp to provide the snapshot of its state to the RFT
	The data param can and must be reallocked and the size of the snapshot copied into
	data must be returned
	The context param contains a list of contexts to take snapshot, and the
	nctx param holds the number of contexts to take the snapshot
	This function is thread-safe as it is called by a copy-on-write child process, thus
	it does not conflicts with the running parent process
*/
typedef size_t (*take_snapshot_cb_t)( char **contexts, int nctx, unsigned int *items, unsigned char **data );

typedef void (*install_snapshot_cb_t)( unsigned int items, const unsigned char *data );

extern void rft_init( void *_mrc, char *listen_port, int rmr_max_msg_size, apply_state_cb_t apply_state_cb,
								take_snapshot_cb_t take_snapshot_cb, install_snapshot_cb_t install_snapshot_cb );
extern int rft_enqueue_msg( rmr_mbuf_t *msg );
extern int rft_replicate( int command, const char *context, const char *key, unsigned char *value, size_t len, unsigned char *meid, role_e role );
extern role_e get_role( unsigned char *meid );
extern void rft_shutdown();

#endif
