/*! \file    janus-lastn.h
 */
 
#ifndef _JANUS_LASTN_H
#define _JANUS_LASTN_H

#include <stdint.h>
#include <glib.h>
#include <jansson.h>
#include "../mutex.h"

#define CIRCQ_EFULL  1 /* Queue full */
#define CIRCQ_EEMPTY 2 /* Queue empty */
#define CIRCQ_EALLOC 3 /* No memory allocated for queue */
#define CIRCQ_ENOMEM 4 /* No system memory */
#define CIRCQ_ENULL  5 /* NULL pointer */
#define CIRCQ_ENFREE 6 /* Queue already allocated */
#define CIRCQ_EINVAL 7 /* Invalid argument passed */
#define DEFAULT_LASTN_SPEAKER_COUNT 4 /*Default value incase not provided via config*/
typedef struct lastn_queue {
    guint32 tail;
    guint32 head;
    guint32 size;
    guint32 count;
    guint64 *data;
    janus_mutex lastn_queue_mutex;
} lastn_queue;
 

void janus_lastn_init(lastn_queue *q);
int janus_lastn_alloc(guint32 size, lastn_queue *q);
void janus_lastn_destroy(lastn_queue *q);
int janus_lastn_empty(lastn_queue *q);
int janus_lastn_full(lastn_queue *q);
int janus_lastn_insert(guint64 n, lastn_queue *q);
int janus_lastn_delete(lastn_queue *q, guint64 *data);
int janus_lastn_peek_front(lastn_queue *q, guint64 *data);
int janus_lastn_peek_back(lastn_queue *q, guint64 *data);
gboolean janus_lastn_elem_position(lastn_queue *q, guint64 data, guint32 *pos);
int janus_lastn_del_elem(lastn_queue *q, guint64 n);
void janus_lastn_get_json_list(lastn_queue *q, guint64 id, json_t **list, gboolean add_talking_event, gboolean talking);

#endif
