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
	Defines metadata of the snapshot taken from the xapp
	This metadata is sent through the pipe before the serialized snapshot is sent through the pipe
	This is required since we need to know how much data will be read from the pipe
*/
typedef struct ctx_metabuf {
	index_t last_log_index;	// snapshot index of log entry immediately preceding new ones
	size_t dlen;			// data length of the snapshot
	unsigned int items;		// number of items in the snapshot data ( each item corresponds to: clen|context|klen|key|vlen|value )
} ctx_metabuf_t;


void take_xapp_snapshot( hashtable_t *ctxtable, take_snapshot_cb_t take_snapshot_cb );
snapshot_t *get_xapp_snapshot( );

#endif
