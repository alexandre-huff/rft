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
	Mnemonic:	queue.c
	Abstract:	Static implementation of a generic queue using doubly linked list

				This file should be included by all the sources which want to use this queue
				So, those static functions will for sure be inlined in the code

	Date:		11 November 2019
	Author:		Alexandre Huff

	Modified:	29 January 2020 - Changed to static implementation to use inline functions
*/

#ifndef _QUEUE_C
#define _QUEUE_c

#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "logger.h"

typedef struct node {
	struct node *next;
	struct node *prev;
	void *data;
} node_t;

typedef struct queue {
	size_t len;
	node_t *head;
	node_t *tail;
} queue_t;


/*
	Creates a new queue

	Returns a pointer to the queue
*/
static inline queue_t *new_queue( ) {
	queue_t *queue = malloc( sizeof( queue_t ) );
	if (queue == NULL )
		return NULL;

	queue->len = 0;
	queue->head = queue->tail = NULL;

	return queue;
}

/*
	Destroys the structure of an empty queue

	Returns 1 on success, 0 otherwise
*/
static inline int destroy_queue( queue_t *queue ) {
	if ( queue == NULL || queue->head != NULL )
		return 0;

	queue->head = queue->tail = NULL;

	free( queue );

	return 1;
}

/*
	Enqueues and new node with its data in the queue

	Returns 1 if succeeded, 0 otherwise

	NOTE: ensure that content of data is copied before enqueue, since RMR will overwrite the content
*/
static inline int enqueue( queue_t *queue, void *data ) {
	node_t *node = NULL;

	if( queue == NULL ) {
		logger_error( "queue cannot be null" );
		return 0;
	}
	if( data == NULL ) {
		logger_error( "data cannot be null" );
		return 0;
	}

	node = (node_t *) malloc( sizeof( node_t ) );
	if( node == NULL ) {
		logger_error( "unable to create a new queue node: %s", strerror( errno ) );
		return 0;
	}

	node->data = data;
	node->next = NULL;
	node->prev = queue->tail;

	if ( queue->tail != NULL )	// queue already has nodes
		queue->tail->next = node;
	else
		queue->head = node;		// there is no node

	queue->tail = node;

	queue->len++;

	return 1;
}

/*
	Removes a node from the queue and frees it

	Returns a pointer to the data held by the removed node (pop), NULL on empty queue

	Error value: Returns NULL and sets errno to EINVAL in case of incorrect params
*/
static inline void *dequeue( queue_t *queue ) {
	if( queue == NULL ) {
		logger_error( "queue cannot be null" );
		errno = EINVAL;
		return NULL;
	}
	if ( queue->head == NULL )
		return NULL;

	node_t *node = (node_t *) queue->head;
	void *data = node->data;

	queue->head = queue->head->next;

	if ( queue->head == NULL )	// queue is empty
		queue->tail = NULL;
	else
		queue->head->prev = NULL;

	free( node );

	queue->len--;

	return data;
}

#endif
