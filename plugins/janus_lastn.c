/*! \file    janus-lastn.c
 * Implementation of last n speakers 
 */

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/types.h>
#include <jansson.h>

#include "utils.h"
#include "janus_lastn.h"
#include "debug.h"

/* ETHER
 * DS for maintaining lastN speaker queue
 * This is circular queue of unsigned long long type
 * Overwrite the least recent element when queue full
 * Did not try to make this generic since C does not support 
 * templates.Other options of typecasting and macros approach 
 * is not readable 
 */


void janus_lastn_init(lastn_queue *q)
{
    if (q) {
        q->tail  = 0;
        q->head  = 0;
        q->size  = 0;
        q->count = 0;
        q->data  = NULL;
        janus_mutex_init(&q->lastn_queue_mutex);
    }
}

int janus_lastn_alloc(guint32 size, lastn_queue *q)
{
    if (!q)
        return CIRCQ_ENULL;

    if (q->data != NULL)
        return CIRCQ_ENFREE;

    if (size <= 0)
        return CIRCQ_EINVAL;

    q->data = g_malloc(size * sizeof(guint64));

    if (q->data == NULL)
        return CIRCQ_ENOMEM;

    q->size = size;


    return 0;
}

void janus_lastn_destroy(lastn_queue *q)
{
    if (q) {
	   janus_mutex_destroy(&q->lastn_queue_mutex);
        free(q->data);
    }
}


int janus_lastn_empty(lastn_queue *q)
{
    return (q && q->count == 0);
}

int janus_lastn_full(lastn_queue *q)
{
    return (q && q->size == q->count);
}

int janus_lastn_insert(guint64 n, lastn_queue *q)
{
    if (!q)
        return CIRCQ_ENULL;

    if (q->data == NULL)
        return CIRCQ_EALLOC;

    janus_mutex_lock(&q->lastn_queue_mutex);
    q->data[q->head] = n;
    q->head = (q->head + 1) % q->size;
    if (janus_lastn_full(q)) {
        q->tail = q->head;
    }
    else {
       q->count++;
       if (q->count >= q->size)
           q->count = q->size;
    }
    janus_mutex_unlock(&q->lastn_queue_mutex);

    return 0;
}


int janus_lastn_delete(lastn_queue *q, guint64 *data)
{
    if (!q || !data)
        return CIRCQ_ENULL;

    if (q->data == NULL)
        return CIRCQ_EALLOC;

    if (janus_lastn_empty(q))
        return CIRCQ_EEMPTY;

    janus_mutex_lock(&q->lastn_queue_mutex);

    *data = q->data[q->tail];
    q->tail = (q->tail + 1) % q->size;
    q->count--;
    janus_mutex_unlock(&q->lastn_queue_mutex);

    return 0;
}

int janus_lastn_del_elem(lastn_queue *q, guint64 elem)
{
    if (!q)
        return CIRCQ_ENULL;

    if (janus_lastn_empty(q))
        return CIRCQ_EEMPTY;

    guint32 position;
    gboolean is_exists = janus_lastn_elem_position(q, elem, &position);
    if(is_exists) {
        if (position == q->tail) {
            guint64  data;
            janus_lastn_delete(q, &data);
        }
        else {
            janus_mutex_lock(&q->lastn_queue_mutex);
            lastn_queue last_n_copy;
            janus_lastn_init(&last_n_copy);
            janus_lastn_alloc(q->size, &last_n_copy);
            guint32 tail = q->tail;
            guint32 x;
            for (x=0; x < q->count ; x++) {
                if(q->data[tail] != elem) {
                    janus_lastn_insert(q->data[tail], &last_n_copy);
                }
                tail = (tail + 1 ) % q->size;
            }
            memcpy((void *)q, (void *)&last_n_copy, sizeof(last_n_copy));
            /* Do a deep copy for the element pointer*/
            memcpy(q->data, last_n_copy.data, last_n_copy.size * sizeof(guint64));
            janus_mutex_unlock(&q->lastn_queue_mutex);
        }
    }
    return 0;
}

int janus_lastn_peek_back(lastn_queue *q, guint64 *data)
{
    if (!q || !data)
        return CIRCQ_ENULL;

    if (janus_lastn_empty(q))
        return CIRCQ_EEMPTY;

    janus_mutex_lock(&q->lastn_queue_mutex);
    *data = q->data[q->tail];
    janus_mutex_unlock(&q->lastn_queue_mutex);

    return 0;
}

int janus_lastn_peek_front(lastn_queue *q, guint64 *data)
{
    if (!q || !data)
        return CIRCQ_ENULL;

    if (janus_lastn_empty(q))
        return CIRCQ_EEMPTY;

    janus_mutex_lock(&q->lastn_queue_mutex);
    if (q->head > 0)
        *data = q->data[q->head - 1];
    else
        *data = q->data[q->size - 1];
    janus_mutex_unlock(&q->lastn_queue_mutex);

    return 0;
}

gboolean janus_lastn_elem_position(lastn_queue *q, guint64 data, guint32 *pos)
{
    janus_mutex_lock(&q->lastn_queue_mutex);
    *pos = 0;
    gboolean exists = FALSE ; 
    guint32 i, t = q->tail;

    if (q) {
        if (!janus_lastn_empty(q)) {
            for (i = 0; i < q->count; i++) {
                if ( q->data[t] ==  data ) {
                    *pos = t;
                    exists = TRUE;
                    break;
                }
                t = (t + 1) % q->size;
            }
        }
    }
    janus_mutex_unlock(&q->lastn_queue_mutex);
    return exists;

}

/* ETHER 
 * Create JSON list for last n speakers 
 * with most recent one on the top
 * Hence start iterating from head
 * 
 */

void janus_lastn_get_json_list(lastn_queue *q, guint64 user_id, json_t **list, gboolean add_talking_event, gboolean talking)
{
    int i, head = q->head;
    janus_mutex_lock(&q->lastn_queue_mutex);
    if (!janus_lastn_empty(q)) {
        for ( i = q->count -1 ; i >=0 ; i--) {
            head = (head - 1) % q->size;
            if ( head < 0) 
            {
                head = q->size - 1; 
            }
            json_t *pl = json_object();
            json_object_set_new(pl, "id", json_integer(q->data[head]));
            if(add_talking_event) {
                if((user_id != 0) && (q->data[head] == user_id)) {
                    json_object_set_new(pl, "talk_event", json_string(talking ? "talking" : "stopped-talking"));
                }
                else {
                    json_object_set_new(pl, "talk_event", json_string("stopped-talking"));
                }
            }

            json_array_append_new(*list, pl); 
        }
    }
    janus_mutex_unlock(&q->lastn_queue_mutex);
}
