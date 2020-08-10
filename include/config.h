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
	Mnemonic:	config.c
	Abstract:	Header file for raft cluster configuration and membership

	Date:		24 November 2019
	Author:		Alexandre Huff
*/

#ifndef _RAFT_CONFIG_H
#define _RAFT_CONFIG_H

#include "types.h"

raft_config_t *raft_get_config( );
server_t *raft_config_add_server( server_id_t *server_id, char *target, index_t last_log_index );
void raft_config_remove_server( server_id_t *server_id );
server_t *raft_config_get_server( server_id_t *server_id );
int has_majority_of_votes( unsigned int rcv_votes );
void raft_config_reset_votes( );
int raft_config_set_new_vote( server_id_t *server_id );
int raft_config_set_server_status( server_id_t *server_id, raft_voting_member_e status );
void raft_config_set_all_server_indexes( index_t raft_last_log_index );
int has_majority_of_match_index( index_t match_index );
int is_server_caught_up( server_t *server, int *rounds, struct timespec *heartbeat_timeout, int *progress );
int set_configuration_changing( int is_changing );
int is_configuration_changing( );
void get_replica_servers( server_id_t *me_self_id, replicas_t *replicas, unsigned int n_replicas );
unsigned int raft_get_num_servers( );

#endif
