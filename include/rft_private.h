// :vim ts=4 sw=4 noet:
/*
==================================================================================
	Copyright (c) 2019-2020 AT&T Intellectual Property.

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

void *send_append_entries( void *raft_server );
void handle_membership_request( membership_request_t *membership_msg, char *rmr_src );
server_state_t *get_me( );
server_id_t *get_myself_id( );
void set_mrc( void *_mrc );


#endif
