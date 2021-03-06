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
	Mnemonic:	queue.h
	Abstract:	Defines header file of typedefs for a generic queue
				using doubly linked list

	Date:		12 May 2020
	Author:		Alexandre Huff
*/

#ifndef _QUEUE_H
#define _QUEUE_H

/*
	Defines a generic element of the queue
*/
typedef struct node {
	struct node *next;
	struct node *prev;
	void *data;
} node_t;

/*
	Defines the queue
*/
typedef struct queue {
	size_t len;
	node_t *head;
	node_t *tail;
} queue_t;

#endif
