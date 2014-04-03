/*! \file   janus_voicemail.c
 * \author Lorenzo Miniero <lorenzo@meetecho.com>
 * \copyright GNU Affero General Public License v3
 * \brief  Janus VoiceMail plugin
 * \details  This is a plugin implementing a very simple VoiceMail service
 * for Janus, specifically recording Opus streams. This means that it replies
 * by providing in the SDP only support for Opus, and disabling video.
 * When a peer contacts the plugin, the plugin starts recording the audio
 * frames it receives and, after 10 seconds, it shuts the PeerConnection
 * down and returns an URL to the recorded file.
 * 
 * Since an URL is returned, the plugin allows you to configure where the
 * recordings whould be stored (e.g., a folder in your web server, writable
 * by the plugin) and the base path to use when returning URLs (e.g.,
 * /my/recordings/ or http://www.example.com/my/recordings).
 * 
 * By default the plugin saves the recordings in the \c html folder of
 * this project, meaning that it can work out of the box with the VoiceMail
 * demo we provide in the same folder.
 *
 * \ingroup plugins
 * \ref plugins
 */

#include "plugin.h"

#include <jansson.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <ogg/ogg.h>

#include "../config.h"
#include "../rtp.h"
#include "../utils.h"


/* Plugin information */
#define JANUS_VOICEMAIL_VERSION			1
#define JANUS_VOICEMAIL_VERSION_STRING	"0.0.1"
#define JANUS_VOICEMAIL_DESCRIPTION		"This is a plugin implementing a very simple VoiceMail service for Janus, recording Opus streams."
#define JANUS_VOICEMAIL_NAME				"JANUS VoiceMail plugin"
#define JANUS_VOICEMAIL_PACKAGE			"janus.plugin.voicemail"

/* Plugin methods */
janus_plugin *create(void);
int janus_voicemail_init(janus_callbacks *callback, const char *config_path);
void janus_voicemail_destroy(void);
int janus_voicemail_get_version(void);
const char *janus_voicemail_get_version_string(void);
const char *janus_voicemail_get_description(void);
const char *janus_voicemail_get_name(void);
const char *janus_voicemail_get_package(void);
void janus_voicemail_create_session(janus_plugin_session *handle, int *error);
void janus_voicemail_handle_message(janus_plugin_session *handle, char *transaction, char *message, char *sdp_type, char *sdp);
void janus_voicemail_setup_media(janus_plugin_session *handle);
void janus_voicemail_incoming_rtp(janus_plugin_session *handle, int video, char *buf, int len);
void janus_voicemail_incoming_rtcp(janus_plugin_session *handle, int video, char *buf, int len);
void janus_voicemail_hangup_media(janus_plugin_session *handle);
void janus_voicemail_destroy_session(janus_plugin_session *handle, int *error);

/* Plugin setup */
static janus_plugin janus_voicemail_plugin =
	{
		.init = janus_voicemail_init,
		.destroy = janus_voicemail_destroy,

		.get_version = janus_voicemail_get_version,
		.get_version_string = janus_voicemail_get_version_string,
		.get_description = janus_voicemail_get_description,
		.get_name = janus_voicemail_get_name,
		.get_package = janus_voicemail_get_package,
		
		.create_session = janus_voicemail_create_session,
		.handle_message = janus_voicemail_handle_message,
		.setup_media = janus_voicemail_setup_media,
		.incoming_rtp = janus_voicemail_incoming_rtp,
		.incoming_rtcp = janus_voicemail_incoming_rtcp,
		.hangup_media = janus_voicemail_hangup_media,
		.destroy_session = janus_voicemail_destroy_session,
	}; 

/* Plugin creator */
janus_plugin *create(void) {
	JANUS_PRINT("%s created!\n", JANUS_VOICEMAIL_NAME);
	return &janus_voicemail_plugin;
}


/* Useful stuff */
static int initialized = 0, stopping = 0;
static janus_callbacks *gateway = NULL;
static GThread *handler_thread;
static void *janus_voicemail_handler(void *data);

typedef struct janus_voicemail_message {
	janus_plugin_session *handle;
	char *transaction;
	char *message;
	char *sdp_type;
	char *sdp;
} janus_voicemail_message;
GQueue *messages;

typedef struct janus_voicemail_session {
	janus_plugin_session *handle;
	guint64 recording_id;
	gint64 start_time;
	char *filename;
	FILE *file;
	ogg_stream_state *stream;
	int seq;
	gboolean started;
	gboolean stopping;
	gboolean destroy;
} janus_voicemail_session;
GHashTable *sessions;

static char *recordings_path = NULL;
static char *recordings_base = NULL;

/* SDP offer/answer template */
static const char *sdp_template =
		"v=0\r\n"
		"o=- %"SCNu64" %"SCNu64" IN IP4 127.0.0.1\r\n"	/* We need current time here */
		"s=VoiceMail %"SCNu64"\r\n"						/* VoiceMail recording ID */
		"t=0 0\r\n"
		"m=audio 1 RTP/SAVPF %d\r\n"		/* Opus payload type */
		"c=IN IP4 1.1.1.1\r\n"
		"a=rtpmap:%d opus/48000/2\r\n"		/* Opus payload type */
		"a=mid:audio\r\n"
		"a=recvonly\r\n";					/* This plugin doesn't send any frames */


/* OGG/Opus helpers */
void le32(unsigned char *p, int v);
void le16(unsigned char *p, int v);
ogg_packet *op_opushead(void);
ogg_packet *op_opustags(void);
ogg_packet *op_from_pkt(const unsigned char *pkt, int len);
void op_free(ogg_packet *op);
int ogg_write(janus_voicemail_session *session);
int ogg_flush(janus_voicemail_session *session);


/* Plugin implementation */
int janus_voicemail_init(janus_callbacks *callback, const char *config_path) {
	if(stopping) {
		/* Still stopping from before */
		return -1;
	}
	if(callback == NULL || config_path == NULL) {
		/* Invalid arguments */
		return -1;
	}

	/* Read configuration */
	char filename[255];
	sprintf(filename, "%s/%s.cfg", config_path, JANUS_VOICEMAIL_PACKAGE);
	JANUS_PRINT("Configuration file: %s\n", filename);
	janus_config *config = janus_config_parse(filename);
	if(config != NULL)
		janus_config_print(config);
	
	sessions = g_hash_table_new(NULL, NULL);
	messages = g_queue_new();
	/* This is the callback we'll need to invoke to contact the gateway */
	gateway = callback;

	/* Parse configuration */
	if(config != NULL) {
		janus_config_item *path = janus_config_get_item_drilldown(config, "general", "path");
		if(path && path->value)
			recordings_path = g_strdup(path->value);
		janus_config_item *base = janus_config_get_item_drilldown(config, "general", "base");
		if(base && base->value)
			recordings_base = g_strdup(base->value);
		/* Done */
		janus_config_destroy(config);
		config = NULL;
	}
	if(recordings_path == NULL)
		recordings_path = "./html/recordings/";
	if(recordings_base == NULL)
		recordings_base = "/recordings/";
	JANUS_PRINT("Recordings path: %s\n", recordings_path);
	JANUS_PRINT("Recordings base: %s\n", recordings_base);
	/* Create the folder, if needed */
	struct stat st = {0};
	if(stat(recordings_path, &st) == -1) {
		int res = mkdir(recordings_path, 0755);
		JANUS_PRINT("Creating folder: %d\n", res);
		if(res < 0) {
			JANUS_DEBUG("%s", strerror(res));
			return -1;	/* No point going on... */
		}
	}
	
	initialized = 1;
	/* Launch the thread that will handle incoming messages */
	GError *error = NULL;
	handler_thread = g_thread_try_new("janus voicemail handler", janus_voicemail_handler, NULL, &error);
	if(error != NULL) {
		initialized = 0;
		/* Something went wrong... */
		JANUS_DEBUG("Got error %d (%s) trying to launch thread...\n", error->code, error->message ? error->message : "??");
		return -1;
	}
	JANUS_PRINT("%s initialized!\n", JANUS_VOICEMAIL_NAME);
	return 0;
}

void janus_voicemail_destroy() {
	if(!initialized)
		return;
	stopping = 1;
	if(handler_thread != NULL) {
		g_thread_join(handler_thread);
	}
	handler_thread = NULL;
	/* Actually clean up and remove ongoing sessions */
	g_hash_table_destroy(sessions);
	g_queue_free(messages);
	sessions = NULL;
	initialized = 0;
	JANUS_PRINT("%s destroyed!\n", JANUS_VOICEMAIL_NAME);
}

int janus_voicemail_get_version() {
	return JANUS_VOICEMAIL_VERSION;
}

const char *janus_voicemail_get_version_string() {
	return JANUS_VOICEMAIL_VERSION_STRING;
}

const char *janus_voicemail_get_description() {
	return JANUS_VOICEMAIL_DESCRIPTION;
}

const char *janus_voicemail_get_name() {
	return JANUS_VOICEMAIL_NAME;
}

const char *janus_voicemail_get_package() {
	return JANUS_VOICEMAIL_PACKAGE;
}

void janus_voicemail_create_session(janus_plugin_session *handle, int *error) {
	if(stopping || !initialized) {
		*error = -1;
		return;
	}	
	janus_voicemail_session *session = (janus_voicemail_session *)calloc(1, sizeof(janus_voicemail_session));
	if(session == NULL) {
		JANUS_DEBUG("Memory error!\n");
		*error = -2;
		return;
	}
	session->handle = handle;
	session->recording_id = g_random_int();
	session->start_time = 0;
	session->stream = NULL;
	char f[255];
	sprintf(f, "%s/janus-voicemail-%"SCNu64".opus", recordings_path, session->recording_id);
	session->filename = g_strdup(f);
	if(session->filename == NULL) {
		JANUS_DEBUG("Memory error!\n");
		*error = -2;
		return;
	}
	session->file = NULL;
	session->seq = 0;
	session->started = FALSE;
	session->stopping = FALSE;
	session->destroy = FALSE;
	handle->plugin_handle = session;
	g_hash_table_insert(sessions, handle, session);

	return;
}

void janus_voicemail_destroy_session(janus_plugin_session *handle, int *error) {
	if(stopping || !initialized) {
		*error = -1;
		return;
	}	
	janus_voicemail_session *session = (janus_voicemail_session *)handle->plugin_handle; 
	if(!session) {
		JANUS_DEBUG("No session associated with this handle...\n");
		*error = -2;
		return;
	}
	if(session->destroy) {
		JANUS_PRINT("Session already destroyed...\n");
		g_free(session);
		return;
	}
	JANUS_PRINT("Removing VoiceMail session...\n");
	g_hash_table_remove(sessions, handle);
	janus_voicemail_hangup_media(handle);
	session->destroy = TRUE;
	g_free(session);

	return;
}

void janus_voicemail_handle_message(janus_plugin_session *handle, char *transaction, char *message, char *sdp_type, char *sdp) {
	if(stopping || !initialized)
		return;
	JANUS_PRINT("%s\n", message);
	janus_voicemail_message *msg = calloc(1, sizeof(janus_voicemail_message));
	if(msg == NULL) {
		JANUS_DEBUG("Memory error!\n");
		return;
	}
	msg->handle = handle;
	msg->transaction = transaction ? g_strdup(transaction) : NULL;
	msg->message = message;
	msg->sdp_type = sdp_type;
	msg->sdp = sdp;
	g_queue_push_tail(messages, msg);
}

void janus_voicemail_setup_media(janus_plugin_session *handle) {
	JANUS_DEBUG("WebRTC media is now available\n");
	if(stopping || !initialized)
		return;
	janus_voicemail_session *session = (janus_voicemail_session *)handle->plugin_handle;	
	if(!session) {
		JANUS_DEBUG("No session associated with this handle...\n");
		return;
	}
	if(session->destroy)
		return;
	/* Only start recording this peer when we get this event */
	session->start_time = janus_get_monotonic_time();
	session->started = TRUE;
	/* Prepare JSON event */
	json_t *event = json_object();
	json_object_set(event, "voicemail", json_string("event"));
	json_object_set(event, "status", json_string("started"));
	char *event_text = json_dumps(event, JSON_INDENT(3));
	json_decref(event);
	JANUS_PRINT("Pushing event: %s\n", event_text);
	JANUS_PRINT("  >> %d\n", gateway->push_event(handle, &janus_voicemail_plugin, NULL, event_text, NULL, NULL));
}

void janus_voicemail_incoming_rtp(janus_plugin_session *handle, int video, char *buf, int len) {
	if(handle == NULL || handle->stopped || stopping || !initialized)
		return;
	janus_voicemail_session *session = (janus_voicemail_session *)handle->plugin_handle;	
	if(!session || session->destroy || session->stopping || !session->started || session->start_time == 0)
		return;
	gint64 now = janus_get_monotonic_time();
	/* Have 10 seconds passed? */
	if((now-session->start_time) >= 10*G_USEC_PER_SEC) {
		/* FIXME Simulate a "stop" coming from the browser */
		session->started = FALSE;
		janus_voicemail_message *msg = calloc(1, sizeof(janus_voicemail_message));
		if(msg == NULL) {
			JANUS_DEBUG("Memory error!\n");
			return;
		}
		msg->handle = handle;
		msg->message = "{\"request\":\"stop\"}";
		msg->transaction = NULL;
		msg->sdp_type = NULL;
		msg->sdp = NULL;
		g_queue_push_tail(messages, msg);
		return;
	}
	/* Save the frame */
	rtp_header *rtp = (rtp_header *)buf;
	uint16_t seq = ntohs(rtp->seq_number);
	if(session->seq == 0)
		session->seq = seq;
	ogg_packet *op = op_from_pkt((const unsigned char *)(buf+12), len-12);	/* TODO Check RTP extensions... */
	//~ JANUS_PRINT("\tWriting at position %d (%d)\n", seq-session->seq+1, 960*(seq-session->seq+1));
	op->granulepos = 960*(seq-session->seq+1); // FIXME: get this from the toc byte
	ogg_stream_packetin(session->stream, op);
	free(op);
	ogg_write(session);
}

void janus_voicemail_incoming_rtcp(janus_plugin_session *handle, int video, char *buf, int len) {
	if(handle == NULL || handle->stopped || stopping || !initialized)
		return;
	/* FIXME Should we care? */
}

void janus_voicemail_hangup_media(janus_plugin_session *handle) {
	JANUS_PRINT("No WebRTC media anymore\n");
	if(stopping || !initialized)
		return;
	janus_voicemail_session *session = (janus_voicemail_session *)handle->plugin_handle;
	if(!session) {
		JANUS_DEBUG("No session associated with this handle...\n");
		return;
	}
	if(session->destroy)
		return;
	session->started = FALSE;
	session->destroy = 1;
	/* Close and reset stuff */
	if(session->file)
		fclose(session->file);
	session->file = NULL;
	if(session->stream)
		ogg_stream_destroy(session->stream);
	session->stream = NULL;
}

/* Thread to handle incoming messages */
static void *janus_voicemail_handler(void *data) {
	JANUS_DEBUG("Joining thread\n");
	janus_voicemail_message *msg = NULL;
	char *error_cause = calloc(512, sizeof(char));	/* FIXME 512 should be enough, but anyway... */
	if(error_cause == NULL) {
		JANUS_DEBUG("Memory error!\n");
		return NULL;
	}
	while(initialized && !stopping) {
		if(!messages || (msg = g_queue_pop_head(messages)) == NULL) {
			usleep(50000);
			continue;
		}
		janus_voicemail_session *session = (janus_voicemail_session *)msg->handle->plugin_handle;	
		if(!session) {
			JANUS_DEBUG("No session associated with this handle...\n");
			continue;
		}
		if(session->destroy)
			continue;
		/* Handle request */
		JANUS_PRINT("Handling message: %s\n", msg->message);
		if(msg->message == NULL) {
			JANUS_DEBUG("No message??\n");
			sprintf(error_cause, "%s", "No message??");
			goto error;
		}
		json_error_t error;
		json_t *root = json_loads(msg->message, 0, &error);
		if(!root) {
			JANUS_DEBUG("JSON error: on line %d: %s\n", error.line, error.text);
			sprintf(error_cause, "JSON error: on line %d: %s", error.line, error.text);
			goto error;
		}
		if(!json_is_object(root)) {
			JANUS_DEBUG("JSON error: not an object\n");
			sprintf(error_cause, "JSON error: not an object");
			goto error;
		}
		/* Get the request first */
		json_t *request = json_object_get(root, "request");
		if(!request || !json_is_string(request)) {
			JANUS_DEBUG("JSON error: invalid element (request)\n");
			sprintf(error_cause, "JSON error: invalid element (request)");
			goto error;
		}
		const char *request_text = json_string_value(request);
		json_t *event = NULL;
		if(!strcasecmp(request_text, "record")) {
			JANUS_PRINT("Starting new recording\n");
			if(session->file != NULL) {
				JANUS_DEBUG("Already recording (%s)\n", session->filename ? session->filename : "??");
				sprintf(error_cause, "Already recording");
				goto error;
			}
			session->stream = malloc(sizeof(ogg_stream_state));
			if(session->stream == NULL) {
				JANUS_DEBUG("Couldn't allocate stream struct\n");
				sprintf(error_cause, "Couldn't allocate stream struct");
				goto error;
			}
			if(ogg_stream_init(session->stream, rand()) < 0) {
				JANUS_DEBUG("Couldn't initialize Ogg stream state\n");
				sprintf(error_cause, "Couldn't initialize Ogg stream state\n");
				goto error;
			}
			session->file = fopen(session->filename, "wb");
			if(session->file == NULL) {
				JANUS_DEBUG("Couldn't open output file\n");
				sprintf(error_cause, "Couldn't open output file");
				goto error;
			}
			session->seq = 0;
			/* Write stream headers */
			ogg_packet *op = op_opushead();
			ogg_stream_packetin(session->stream, op);
			op_free(op);
			op = op_opustags();
			ogg_stream_packetin(session->stream, op);
			op_free(op);
			ogg_flush(session);
			/* Done: now wait for the setup_media callback to be called */
			event = json_object();
			json_object_set(event, "voicemail", json_string("event"));
			json_object_set(event, "status", json_string(session->started ? "started" : "starting"));
		} else if(!strcasecmp(request_text, "stop")) {
			/* Stop the recording */
			session->started = FALSE;
			if(session->file)
				fclose(session->file);
			session->file = NULL;
			if(session->stream)
				ogg_stream_destroy(session->stream);
			session->stream = NULL;
			/* Done: now wait for the setup_media callback to be called */
			event = json_object();
			json_object_set(event, "voicemail", json_string("event"));
			json_object_set(event, "status", json_string("done"));
			char url[1024];
			sprintf(url, "%s/janus-voicemail-%"SCNu64".opus", recordings_base, session->recording_id);
			json_object_set(event, "recording", json_string(url));
		} else {
			JANUS_DEBUG("Unknown request '%s'\n", request_text);
			sprintf(error_cause, "Unknown request '%s'", request_text);
			goto error;
		}

		/* Prepare JSON event */
		JANUS_PRINT("Preparing JSON event as a reply\n");
		char *event_text = json_dumps(event, JSON_INDENT(3));
		json_decref(event);
		/* Any SDP to handle? */
		if(!msg->sdp) {
			JANUS_PRINT("  >> %d\n", gateway->push_event(msg->handle, &janus_voicemail_plugin, msg->transaction, event_text, NULL, NULL));
		} else {
			JANUS_PRINT("This is involving a negotiation (%s) as well:\n%s\n", msg->sdp_type, msg->sdp);
			char *type = NULL;
			if(!strcasecmp(msg->sdp_type, "offer"))
				type = "answer";
			if(!strcasecmp(msg->sdp_type, "answer"))
				type = "offer";
			/* Fill the SDP template and use that as our answer */
			char sdp[1024];
			/* What is the Opus payload type? */
			int opus_pt = 0;
			char *fmtp = strstr(msg->sdp, "opus/48000");
			if(fmtp != NULL) {
				fmtp -= 5;
				fmtp = strstr(fmtp, ":");
				if(fmtp)
					fmtp++;
				opus_pt = atoi(fmtp);
			}
			JANUS_PRINT("Opus payload type is %d\n", opus_pt);
			g_sprintf(sdp, sdp_template,
				janus_get_monotonic_time(),		/* We need current time here */
				janus_get_monotonic_time(),		/* We need current time here */
				session->recording_id,			/* Recording ID */
				opus_pt,						/* Opus payload type */
				opus_pt							/* Opus payload type */);
			/* Did the peer negotiate video? */
			if(strstr(msg->sdp, "m=video") != NULL) {
				/* If so, reject it */
				g_strlcat(sdp, "m=video 0 RTP/SAVPF 0\r\n", 1024);				
			}
			/* How long will the gateway take to push the event? */
			gint64 start = janus_get_monotonic_time();
			int res = gateway->push_event(msg->handle, &janus_voicemail_plugin, msg->transaction, event_text, type, sdp);
			JANUS_PRINT("  >> Pushing event: %d (took %"SCNu64" ms)\n", res, janus_get_monotonic_time()-start);
			if(res != JANUS_OK) {
				/* TODO Failed to negotiate? We should remove this participant */
			}
		}

		continue;
		
error:
		{
			if(root != NULL)
				json_decref(root);
			/* Prepare JSON error event */
			json_t *event = json_object();
			json_object_set(event, "voicemail", json_string("event"));
			json_object_set(event, "error", json_string(error_cause));
			char *event_text = json_dumps(event, JSON_INDENT(3));
			json_decref(event);
			JANUS_PRINT("Pushing event: %s\n", event_text);
			JANUS_PRINT("  >> %d\n", gateway->push_event(msg->handle, &janus_voicemail_plugin, msg->transaction, event_text, NULL, NULL));
		}
	}
	JANUS_DEBUG("Leaving thread\n");
	return NULL;
}


/* OGG/Opus helpers */
/* Write a little-endian 32 bit int to memory */
void le32(unsigned char *p, int v) {
	p[0] = v & 0xff;
	p[1] = (v >> 8) & 0xff;
	p[2] = (v >> 16) & 0xff;
	p[3] = (v >> 24) & 0xff;
}


/* Write a little-endian 16 bit int to memory */
void le16(unsigned char *p, int v) {
	p[0] = v & 0xff;
	p[1] = (v >> 8) & 0xff;
}

/* ;anufacture a generic OpusHead packet */
ogg_packet *op_opushead() {
	int size = 19;
	unsigned char *data = malloc(size);
	ogg_packet *op = malloc(sizeof(*op));

	if(!data) {
		JANUS_DEBUG("Couldn't allocate data buffer...\n");
		return NULL;
	}
	if(!op) {
		JANUS_DEBUG("Couldn't allocate Ogg packet...\n");
		return NULL;
	}

	memcpy(data, "OpusHead", 8);  /* identifier */
	data[8] = 1;                  /* version */
	data[9] = 2;                  /* channels */
	le16(data+10, 0);             /* pre-skip */
	le32(data + 12, 48000);       /* original sample rate */
	le16(data + 16, 0);           /* gain */
	data[18] = 0;                 /* channel mapping family */

	op->packet = data;
	op->bytes = size;
	op->b_o_s = 1;
	op->e_o_s = 0;
	op->granulepos = 0;
	op->packetno = 0;

	return op;
}

/* Manufacture a generic OpusTags packet */
ogg_packet *op_opustags() {
	char *identifier = "OpusTags";
	char *vendor = "opus rtp packet dump";
	int size = strlen(identifier) + 4 + strlen(vendor) + 4;
	unsigned char *data = malloc(size);
	ogg_packet *op = malloc(sizeof(*op));

	if(!data) {
		JANUS_DEBUG("Couldn't allocate data buffer...\n");
		return NULL;
	}
	if(!op) {
		JANUS_DEBUG("Couldn't allocate Ogg packet...\n");
		return NULL;
	}

	memcpy(data, identifier, 8);
	le32(data + 8, strlen(vendor));
	memcpy(data + 12, vendor, strlen(vendor));
	le32(data + 12 + strlen(vendor), 0);

	op->packet = data;
	op->bytes = size;
	op->b_o_s = 0;
	op->e_o_s = 0;
	op->granulepos = 0;
	op->packetno = 1;

	return op;
}

/* Allocate an ogg_packet */
ogg_packet *op_from_pkt(const unsigned char *pkt, int len) {
	ogg_packet *op = malloc(sizeof(*op));
	if(!op) {
		JANUS_DEBUG("Couldn't allocate Ogg packet.\n");
		return NULL;
	}

	op->packet = (unsigned char *)pkt;
	op->bytes = len;
	op->b_o_s = 0;
	op->e_o_s = 0;

	return op;
}

/* Free a packet and its contents */
void op_free(ogg_packet *op) {
	if(op) {
		if(op->packet) {
			free(op->packet);
		}
		free(op);
	}
}

/* Write out available ogg pages */
int ogg_write(janus_voicemail_session *session) {
	ogg_page page;
	size_t written;

	if(!session || !session->stream || !session->file) {
		return -1;
	}

	while (ogg_stream_pageout(session->stream, &page)) {
		written = fwrite(page.header, 1, page.header_len, session->file);
		if(written != (size_t)page.header_len) {
			JANUS_DEBUG("Error writing Ogg page header\n");
			return -2;
		}
		written = fwrite(page.body, 1, page.body_len, session->file);
		if(written != (size_t)page.body_len) {
			JANUS_DEBUG("Error writing Ogg page body\n");
			return -3;
		}
	}
	return 0;
}

/* Flush remaining ogg data */
int ogg_flush(janus_voicemail_session *session) {
	ogg_page page;
	size_t written;

	if(!session || !session->stream || !session->file) {
		return -1;
	}

	while (ogg_stream_flush(session->stream, &page)) {
		written = fwrite(page.header, 1, page.header_len, session->file);
		if(written != (size_t)page.header_len) {
			JANUS_DEBUG("Error writing Ogg page header\n");
			return -2;
		}
		written = fwrite(page.body, 1, page.body_len, session->file);
		if(written != (size_t)page.body_len) {
			JANUS_DEBUG("Error writing Ogg page body\n");
			return -3;
		}
	}
	return 0;
}
