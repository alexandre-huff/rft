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
	Mnemonic:	stub_rft.h
	Abstract:	Provides stub functions of the main module to build RFT tests

	Date:		2 September 2020
	Author:		Alexandre Huff
*/

#include <rmr/rmr.h>
#include "rft.h"
#include "types.h"

extern int rft_enqueue_msg( rmr_mbuf_t *msg ) {
    return 1;
}

server_id_t *get_myself_id( ) {
	static server_id_t server_id = { 'm', 'y', 's', 'e', 'l', 'f', '\0' };
	return &server_id;
}

void *send_append_entries( void *raft_server ) {
	return NULL;
}
