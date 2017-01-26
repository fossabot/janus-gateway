/*! \file    dtls.c
 * \author   Lorenzo Miniero <lorenzo@meetecho.com>
 * \copyright GNU General Public License v3
 * \brief    DTLS/SRTP processing
 * \details  Implementation (based on OpenSSL and libsrtp) of the DTLS/SRTP
 * transport. The code takes care of the DTLS handshake between peers and
 * the gateway, and sets the proper SRTP and SRTCP context up accordingly.
 * A DTLS alert from a peer is notified to the plugin handling him/her
 * by means of the hangup_media callback.
 *
 * \ingroup protocols
 * \ref protocols
 */

#include "janus.h"
#include "debug.h"
#include "dtls.h"
#include "rtcp.h"
#include "events.h"

#include <openssl/err.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/asn1.h>


/* SRTP stuff (http://tools.ietf.org/html/rfc3711) */
static const char *janus_srtp_error[] =
{
#ifdef HAVE_SRTP_2
	"srtp_err_status_ok",
	"srtp_err_status_fail",
	"srtp_err_status_bad_param",
	"srtp_err_status_alloc_fail",
	"srtp_err_status_dealloc_fail",
	"srtp_err_status_init_fail",
	"srtp_err_status_terminus",
	"srtp_err_status_auth_fail",
	"srtp_err_status_cipher_fail",
	"srtp_err_status_replay_fail",
	"srtp_err_status_replay_old",
	"srtp_err_status_algo_fail",
	"srtp_err_status_no_such_op",
	"srtp_err_status_no_ctx",
	"srtp_err_status_cant_check",
	"srtp_err_status_key_expired",
	"srtp_err_status_socket_err",
	"srtp_err_status_signal_err",
	"srtp_err_status_nonce_bad",
	"srtp_err_status_read_fail",
	"srtp_err_status_write_fail",
	"srtp_err_status_parse_err",
	"srtp_err_status_encode_err",
	"srtp_err_status_semaphore_err",
	"srtp_err_status_pfkey_err",
#else
	"err_status_ok",
	"err_status_fail",
	"err_status_bad_param",
	"err_status_alloc_fail",
	"err_status_dealloc_fail",
	"err_status_init_fail",
	"err_status_terminus",
	"err_status_auth_fail",
	"err_status_cipher_fail",
	"err_status_replay_fail",
	"err_status_replay_old",
	"err_status_algo_fail",
	"err_status_no_such_op",
	"err_status_no_ctx",
	"err_status_cant_check",
	"err_status_key_expired",
	"err_status_socket_err",
	"err_status_signal_err",
	"err_status_nonce_bad",
	"err_status_read_fail",
	"err_status_write_fail",
	"err_status_parse_err",
	"err_status_encode_err",
	"err_status_semaphore_err",
	"err_status_pfkey_err",
#endif
};
const gchar *janus_get_srtp_error(int error) {
	if(error < 0 || error > 24)
		return NULL;
	return janus_srtp_error[error];
}

const gchar *janus_get_dtls_srtp_state(janus_dtls_state state) {
	switch(state) {
		case JANUS_DTLS_STATE_CREATED:
			return "created";
		case JANUS_DTLS_STATE_TRYING:
			return "trying";
		case JANUS_DTLS_STATE_CONNECTED:
			return "connected";
		case JANUS_DTLS_STATE_FAILED:
			return "failed";
		default:
			return NULL;
	}
	return NULL;
}

const gchar *janus_get_dtls_srtp_role(janus_dtls_role role) {
	switch(role) {
		case JANUS_DTLS_ROLE_ACTPASS:
			return "actpass";
		case JANUS_DTLS_ROLE_SERVER:
			return "passive";
		case JANUS_DTLS_ROLE_CLIENT:
			return "active";
		default:
			return NULL;
	}
	return NULL;
}


/* Helper to notify DTLS state changes to the event handlers */
static void janus_dtls_notify_state_change(janus_dtls_srtp *dtls) {
	if(!janus_events_is_enabled())
		return;
	if(dtls == NULL)
		return;
	janus_ice_component *component = (janus_ice_component *)dtls->component;
	if(component == NULL)
		return;
	janus_ice_stream *stream = component->stream;
	if(stream == NULL)
		return;
	janus_ice_handle *handle = stream->handle;
	if(handle == NULL)
		return;
	janus_session *session = (janus_session *)handle->session;
	if(session == NULL)
		return;
	json_t *info = json_object();
	json_object_set_new(info, "dtls", json_string(janus_get_dtls_srtp_state(dtls->dtls_state)));
	json_object_set_new(info, "stream_id", json_integer(stream->stream_id));
	json_object_set_new(info, "component_id", json_integer(component->component_id));
	janus_events_notify_handlers(JANUS_EVENT_TYPE_WEBRTC, session->session_id, handle->handle_id, info);
}


/* DTLS stuff */
#define DTLS_CIPHERS	"ALL:NULL:eNULL:aNULL"
/* Duration for the self-generated certs: 1 year */
#define DTLS_AUTOCERT_DURATION	60*60*24*365

/* SRTP stuff (http://tools.ietf.org/html/rfc3711) */
#define SRTP_MASTER_KEY_LENGTH	16
#define SRTP_MASTER_SALT_LENGTH	14
#define SRTP_MASTER_LENGTH (SRTP_MASTER_KEY_LENGTH + SRTP_MASTER_SALT_LENGTH)


static SSL_CTX *ssl_ctx = NULL;
static X509* ssl_cert = NULL;
static EVP_PKEY* ssl_key = NULL;

static gchar local_fingerprint[160];
gchar *janus_dtls_get_local_fingerprint(void) {
	return (gchar *)local_fingerprint;
}


#ifdef HAVE_SCTP
/* Helper thread to create a SCTP association that will use this DTLS stack */
void *janus_dtls_sctp_setup_thread(void *data);
#endif


#if OPENSSL_VERSION_NUMBER < 0x10100000L
/*
 * DTLS locking stuff to make OpenSSL thread safe (not needed for 1.1.0)
 *
 * Note: this is an attempt to fix the infamous issue #316:
 * 		https://github.com/meetecho/janus-gateway/issues/316
 * that is the "tlsv1 alert decrypt error" randomly happening when
 * doing handshakes that force Janus to be restarted (issue affecting
 * OpenSSL but NOT BoringSSL, apparently). The cause might be related
 * to race conditions, and in fact OpenSSL docs state that:
 *
 * 		"OpenSSL can safely be used in multi-threaded applications
 * 		provided that at least two callback functions are set,
 * 		locking_function and threadid_func."
 *
 * See here for the whole docs:
 * 		https://www.openssl.org/docs/manmaster/crypto/threads.html
 *
 * The fix proposed here is heavily derived from a discussion related to
 * RTPEngine:
 * 		http://lists.sip-router.org/pipermail/sr-dev/2015-January/026860.html
 * where it was mentioned the issue was fixed in this commit:
 * 		https://github.com/sipwise/rtpengine/commit/935487b66363c9932684d8085f47450d65a8c37e
 * which does indeed implement the callbacks the OpenSSL docs suggest.
 *
 */
static janus_mutex *janus_dtls_locks;

static void janus_dtls_cb_openssl_threadid(CRYPTO_THREADID *tid) {
	/* FIXME Assuming pthread, which is fine as GLib wraps pthread and
	 * so that's what we use anyway? */
	pthread_t me = pthread_self();

	if(sizeof(me) == sizeof(void *)) {
		CRYPTO_THREADID_set_pointer(tid, (void *) me);
	} else {
		CRYPTO_THREADID_set_numeric(tid, (unsigned long) me);
	}
}

static void janus_dtls_cb_openssl_lock(int mode, int type, const char *file, int line) {
	if((mode & CRYPTO_LOCK)) {
		janus_mutex_lock(&janus_dtls_locks[type]);
	} else {
		janus_mutex_unlock(&janus_dtls_locks[type]);
	}
}
#endif


static int janus_dtls_generate_keys(X509** certificate, EVP_PKEY** private_key) {
	static const int num_bits = 2048;
	BIGNUM* bne = NULL;
	RSA* rsa_key = NULL;
	X509_NAME* cert_name = NULL;

	JANUS_LOG(LOG_VERB, "Generating DTLS key / cert\n");

	/* Create a big number object. */
	bne = BN_new();
	if (!bne) {
		JANUS_LOG(LOG_FATAL, "BN_new() failed\n");
		goto error;
	}

	if (!BN_set_word(bne, RSA_F4)) {  /* RSA_F4 == 65537 */
		JANUS_LOG(LOG_FATAL, "BN_set_word() failed\n");
		goto error;
	}

	/* Generate a RSA key. */
	rsa_key = RSA_new();
	if (!rsa_key) {
		JANUS_LOG(LOG_FATAL, "RSA_new() failed\n");
		goto error;
	}

	/* This takes some time. */
	if (!RSA_generate_key_ex(rsa_key, num_bits, bne, NULL)) {
		JANUS_LOG(LOG_FATAL, "RSA_generate_key_ex() failed\n");
		goto error;
	}

	/* Create a private key object (needed to hold the RSA key). */
	*private_key = EVP_PKEY_new();
	if (!*private_key) {
		JANUS_LOG(LOG_FATAL, "EVP_PKEY_new() failed\n");
		goto error;
	}

	if (!EVP_PKEY_assign_RSA(*private_key, rsa_key)) {
		JANUS_LOG(LOG_FATAL, "EVP_PKEY_assign_RSA() failed\n");
		goto error;
	}
	/* The RSA key now belongs to the private key, so don't clean it up separately. */
	rsa_key = NULL;

	/* Create the X509 certificate. */
	*certificate = X509_new();
	if (!*certificate) {
		JANUS_LOG(LOG_FATAL, "X509_new() failed\n");
		goto error;
	}

	/* Set version 3 (note that 0 means version 1). */
	X509_set_version(*certificate, 2);

	/* Set serial number. */
	ASN1_INTEGER_set(X509_get_serialNumber(*certificate), (long)g_random_int());

	/* Set valid period. */
	X509_gmtime_adj(X509_get_notBefore(*certificate), -1 * DTLS_AUTOCERT_DURATION);  /* -1 year */
	X509_gmtime_adj(X509_get_notAfter(*certificate), DTLS_AUTOCERT_DURATION);  /* 1 year */

	/* Set the public key for the certificate using the key. */
	if (!X509_set_pubkey(*certificate, *private_key)) {
		JANUS_LOG(LOG_FATAL, "X509_set_pubkey() failed\n");
		goto error;
	}

	/* Set certificate fields. */
	cert_name = X509_get_subject_name(*certificate);
	if (!cert_name) {
		JANUS_LOG(LOG_FATAL, "X509_get_subject_name() failed\n");
		goto error;
	}
	X509_NAME_add_entry_by_txt(cert_name, "O", MBSTRING_ASC, (const unsigned char*)"Janus", -1, -1, 0);
	X509_NAME_add_entry_by_txt(cert_name, "CN", MBSTRING_ASC, (const unsigned char*)"Janus", -1, -1, 0);

	/* It is self-signed so set the issuer name to be the same as the subject. */
	if (!X509_set_issuer_name(*certificate, cert_name)) {
		JANUS_LOG(LOG_FATAL, "X509_set_issuer_name() failed\n");
		goto error;
	}

	/* Sign the certificate with the private key. */
	if (!X509_sign(*certificate, *private_key, EVP_sha1())) {
		JANUS_LOG(LOG_FATAL, "X509_sign() failed\n");
		goto error;
	}

	/* Free stuff and resurn. */
	BN_free(bne);
	return 0;

error:
	if (bne)
		BN_free(bne);
	if (rsa_key && !*private_key)
		RSA_free(rsa_key);
	if (*private_key)
		EVP_PKEY_free(*private_key);  /* This also frees the RSA key. */
	if (*certificate)
		X509_free(*certificate);
	return -1;
}


static int janus_dtls_load_keys(const char* server_pem, const char* server_key, X509** certificate, EVP_PKEY** private_key) {
	FILE* f = NULL;

	f = fopen(server_pem, "r");
	if (!f) {
		JANUS_LOG(LOG_FATAL, "Error opening certificate file\n");
		goto error;
	}
	*certificate = PEM_read_X509(f, NULL, NULL, NULL);
	if (!*certificate) {
		JANUS_LOG(LOG_FATAL, "PEM_read_X509 failed\n");
		goto error;
	}
	fclose(f);

	f = fopen(server_key, "r");
	if (!f) {
		JANUS_LOG(LOG_FATAL, "Error opening key file\n");
		goto error;
	}
	*private_key = PEM_read_PrivateKey(f, NULL, NULL, NULL);
	if (!*private_key) {
		JANUS_LOG(LOG_FATAL, "PEM_read_PrivateKey failed\n");
		goto error;
	}
	fclose(f);

	return 0;

error:
	if (*certificate) {
		X509_free(*certificate);
		*certificate = NULL;
	}
	if (*private_key) {
		EVP_PKEY_free(*private_key);
		*private_key = NULL;
	}
	return -1;
}


/* DTLS-SRTP initialization */
gint janus_dtls_srtp_init(const char* server_pem, const char* server_key) {
#if OPENSSL_VERSION_NUMBER < 0x10100000L
	JANUS_LOG(LOG_WARN, "OpenSSL pre-1.1.0\n");
	/* First of all make OpenSSL thread safe (see note above on issue #316) */
	janus_dtls_locks = g_malloc0(sizeof(*janus_dtls_locks) * CRYPTO_num_locks());
	int l=0;
	for(l = 0; l < CRYPTO_num_locks(); l++) {
		janus_mutex_init(&janus_dtls_locks[l]);
	}
	CRYPTO_THREADID_set_callback(janus_dtls_cb_openssl_threadid);
	CRYPTO_set_locking_callback(janus_dtls_cb_openssl_lock);
#else
	JANUS_LOG(LOG_WARN, "OpenSSL >= 1.1.0\n");
#endif

	/* Go on and create the DTLS context */
#if OPENSSL_VERSION_NUMBER < 0x10100000L
	ssl_ctx = SSL_CTX_new(DTLSv1_method());
#else
	ssl_ctx = SSL_CTX_new(DTLS_method());
#endif
	if(!ssl_ctx) {
		JANUS_LOG(LOG_FATAL, "Ops, error creating DTLS context?\n");
		return -1;
	}
	SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, janus_dtls_verify_callback);
	SSL_CTX_set_tlsext_use_srtp(ssl_ctx, "SRTP_AES128_CM_SHA1_80");	/* FIXME Should we support something else as well? */

	if (!server_pem && !server_key) {
		JANUS_LOG(LOG_WARN, "No cert/key specified, autogenerating some...\n");
		if (janus_dtls_generate_keys(&ssl_cert, &ssl_key) != 0) {
			JANUS_LOG(LOG_FATAL, "Error generating DTLS key/certificate\n");
			return -2;
		}
	} else if (!server_pem || !server_key) {
		JANUS_LOG(LOG_FATAL, "DTLS certificate and key must be specified\n");
		return -2;
	} else if (janus_dtls_load_keys(server_pem, server_key, &ssl_cert, &ssl_key) != 0) {
		return -3;
	}

	if(!SSL_CTX_use_certificate(ssl_ctx, ssl_cert)) {
		JANUS_LOG(LOG_FATAL, "Certificate error (%s)\n", ERR_reason_error_string(ERR_get_error()));
		return -4;
	}
	if(!SSL_CTX_use_PrivateKey(ssl_ctx, ssl_key)) {
		JANUS_LOG(LOG_FATAL, "Certificate key error (%s)\n", ERR_reason_error_string(ERR_get_error()));
		return -5;
	}
	if(!SSL_CTX_check_private_key(ssl_ctx)) {
		JANUS_LOG(LOG_FATAL, "Certificate check error (%s)\n", ERR_reason_error_string(ERR_get_error()));
		return -6;
	}
	SSL_CTX_set_read_ahead(ssl_ctx,1);

	unsigned int size;
	unsigned char fingerprint[EVP_MAX_MD_SIZE];
	if(X509_digest(ssl_cert, EVP_sha256(), (unsigned char *)fingerprint, &size) == 0) {
		JANUS_LOG(LOG_FATAL, "Error converting X509 structure (%s)\n", ERR_reason_error_string(ERR_get_error()));
		return -7;
	}
	char *lfp = (char *)&local_fingerprint;
	unsigned int i = 0;
	for(i = 0; i < size; i++) {
		g_snprintf(lfp, 4, "%.2X:", fingerprint[i]);
		lfp += 3;
	}
	*(lfp-1) = 0;
	JANUS_LOG(LOG_INFO, "Fingerprint of our certificate: %s\n", local_fingerprint);
	SSL_CTX_set_cipher_list(ssl_ctx, DTLS_CIPHERS);

	if(janus_dtls_bio_filter_init() < 0) {
		JANUS_LOG(LOG_FATAL, "Error initializing BIO filter\n");
		return -8;
	}

	/* Initialize libsrtp */
	if(srtp_init() != srtp_err_status_ok) {
		JANUS_LOG(LOG_FATAL, "Ops, error setting up libsrtp?\n");
		return 5;
	}
	return 0;
}


void janus_dtls_srtp_cleanup(void) {
	if (ssl_cert != NULL) {
		X509_free(ssl_cert);
		ssl_cert = NULL;
	}
	if (ssl_key != NULL) {
		EVP_PKEY_free(ssl_key);
		ssl_key = NULL;
	}
	if (ssl_ctx != NULL) {
		SSL_CTX_free(ssl_ctx);
		ssl_ctx = NULL;
	}
}


janus_dtls_srtp *janus_dtls_srtp_create(void *ice_component, janus_dtls_role role) {
	janus_ice_component *component = (janus_ice_component *)ice_component;
	if(component == NULL) {
		JANUS_LOG(LOG_ERR, "No component, no DTLS...\n");
		return NULL;
	}
	janus_ice_stream *stream = component->stream;
	if(!stream) {
		JANUS_LOG(LOG_ERR, "No stream, no DTLS...\n");
		return NULL;
	}
	janus_ice_handle *handle = stream->handle;
	if(!handle || !handle->agent) {
		JANUS_LOG(LOG_ERR, "No handle/agent, no DTLS...\n");
		return NULL;
	}
	janus_dtls_srtp *dtls = g_malloc0(sizeof(janus_dtls_srtp));
	if(dtls == NULL) {
		JANUS_LOG(LOG_FATAL, "Memory error!\n");
		return NULL;
	}
	/* Create SSL context, at last */
	dtls->srtp_valid = 0;
	dtls->ssl = SSL_new(ssl_ctx);
	if(!dtls->ssl) {
		JANUS_LOG(LOG_ERR, "[%"SCNu64"]     Error creating DTLS session! (%s)\n",
			handle->handle_id, ERR_reason_error_string(ERR_get_error()));
		janus_dtls_srtp_destroy(dtls);
		return NULL;
	}
	SSL_set_ex_data(dtls->ssl, 0, dtls);
	SSL_set_info_callback(dtls->ssl, janus_dtls_callback);
	dtls->read_bio = BIO_new(BIO_s_mem());
	if(!dtls->read_bio) {
		JANUS_LOG(LOG_ERR, "[%"SCNu64"]   Error creating read BIO! (%s)\n",
			handle->handle_id, ERR_reason_error_string(ERR_get_error()));
		janus_dtls_srtp_destroy(dtls);
		return NULL;
	}
	BIO_set_mem_eof_return(dtls->read_bio, -1);
	dtls->write_bio = BIO_new(BIO_s_mem());
	if(!dtls->write_bio) {
		JANUS_LOG(LOG_ERR, "[%"SCNu64"]   Error creating write BIO! (%s)\n",
			handle->handle_id, ERR_reason_error_string(ERR_get_error()));
		janus_dtls_srtp_destroy(dtls);
		return NULL;
	}
	BIO_set_mem_eof_return(dtls->write_bio, -1);
	/* The write BIO needs our custom filter, or fragmentation won't work */
	dtls->filter_bio = BIO_new(BIO_janus_dtls_filter());
	if(!dtls->filter_bio) {
		JANUS_LOG(LOG_ERR, "[%"SCNu64"]   Error creating filter BIO! (%s)\n",
			handle->handle_id, ERR_reason_error_string(ERR_get_error()));
		janus_dtls_srtp_destroy(dtls);
		return NULL;
	}
	/* Chain filter and write BIOs */
	BIO_push(dtls->filter_bio, dtls->write_bio);
	/* Set the filter as the BIO to use for outgoing data */
	SSL_set_bio(dtls->ssl, dtls->read_bio, dtls->filter_bio);
	dtls->dtls_role = role;
	if(dtls->dtls_role == JANUS_DTLS_ROLE_CLIENT) {
		JANUS_LOG(LOG_VERB, "[%"SCNu64"]   Setting connect state (DTLS client)\n", handle->handle_id);
		SSL_set_connect_state(dtls->ssl);
	} else {
		JANUS_LOG(LOG_VERB, "[%"SCNu64"]   Setting accept state (DTLS server)\n", handle->handle_id);
		SSL_set_accept_state(dtls->ssl);
	}
	/* https://code.google.com/p/chromium/issues/detail?id=406458
	 * Specify an ECDH group for ECDHE ciphers, otherwise they cannot be
	 * negotiated when acting as the server. Use NIST's P-256 which is
	 * commonly supported.
	 */
	EC_KEY* ecdh = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
	if(ecdh == NULL) {
		JANUS_LOG(LOG_ERR, "[%"SCNu64"]   Error creating ECDH group! (%s)\n",
			handle->handle_id, ERR_reason_error_string(ERR_get_error()));
		janus_dtls_srtp_destroy(dtls);
		return NULL;
	}
	SSL_set_options(dtls->ssl, SSL_OP_SINGLE_ECDH_USE);
	SSL_set_tmp_ecdh(dtls->ssl, ecdh);
	EC_KEY_free(ecdh);
#ifdef HAVE_DTLS_SETTIMEOUT
	guint ms = 100;
	JANUS_LOG(LOG_VERB, "[%"SCNu64"]   Setting DTLS initial timeout: %u\n", handle->handle_id, ms);
	DTLSv1_set_initial_timeout_duration(dtls->ssl, ms);
#endif
	dtls->ready = 0;
#ifdef HAVE_SCTP
	dtls->sctp = NULL;
#endif
	janus_mutex_init(&dtls->srtp_mutex);
	/* Done */
	dtls->dtls_connected = 0;
	dtls->component = component;
	return dtls;
}

void janus_dtls_srtp_handshake(janus_dtls_srtp *dtls) {
	if(dtls == NULL || dtls->ssl == NULL)
		return;
	if(dtls->dtls_state == JANUS_DTLS_STATE_CREATED)
		dtls->dtls_state = JANUS_DTLS_STATE_TRYING;
	SSL_do_handshake(dtls->ssl);
	janus_dtls_fd_bridge(dtls);

	/* Notify event handlers */
	janus_dtls_notify_state_change(dtls);
}

void janus_dtls_srtp_incoming_msg(janus_dtls_srtp *dtls, char *buf, uint16_t len) {
	if(dtls == NULL) {
		JANUS_LOG(LOG_ERR, "No DTLS-SRTP stack, no incoming message...\n");
		return;
	}
	janus_ice_component *component = (janus_ice_component *)dtls->component;
	if(component == NULL) {
		JANUS_LOG(LOG_ERR, "No component, no DTLS...\n");
		return;
	}
	janus_ice_stream *stream = component->stream;
	if(!stream) {
		JANUS_LOG(LOG_ERR, "No stream, no DTLS...\n");
		return;
	}
	janus_ice_handle *handle = stream->handle;
	if(!handle || !handle->agent) {
		JANUS_LOG(LOG_ERR, "No handle/agent, no DTLS...\n");
		return;
	}
	if(janus_flags_is_set(&handle->webrtc_flags, JANUS_ICE_HANDLE_WEBRTC_ALERT)) {
		JANUS_LOG(LOG_WARN, "[%"SCNu64"] Alert already triggered, clearing up...\n", handle->handle_id);
		return;
	}
	if(!dtls->ssl || !dtls->read_bio) {
		JANUS_LOG(LOG_ERR, "[%"SCNu64"] No DTLS stuff for component %d in stream %d??\n", handle->handle_id, component->component_id, stream->stream_id);
		return;
	}
	janus_dtls_fd_bridge(dtls);
	int written = BIO_write(dtls->read_bio, buf, len);
	if(written != len) {
		JANUS_LOG(LOG_WARN, "[%"SCNu64"]     Only written %d/%d of those bytes on the read BIO...\n", handle->handle_id, written, len);
	} else {
		JANUS_LOG(LOG_HUGE, "[%"SCNu64"]     Written %d bytes on the read BIO...\n", handle->handle_id, written);
	}
	janus_dtls_fd_bridge(dtls);
	/* Try to read data */
	char data[1500];	/* FIXME */
	memset(&data, 0, 1500);
	int read = SSL_read(dtls->ssl, &data, 1500);
	JANUS_LOG(LOG_HUGE, "[%"SCNu64"]     ... and read %d of them from SSL...\n", handle->handle_id, read);
	if(read < 0) {
		unsigned long err = SSL_get_error(dtls->ssl, read);
		if(err == SSL_ERROR_SSL) {
			/* Ops, something went wrong with the DTLS handshake */
			char error[200];
			ERR_error_string_n(ERR_get_error(), error, 200);
			JANUS_LOG(LOG_ERR, "[%"SCNu64"] Handshake error: %s\n", handle->handle_id, error);
			return;
		}
	}
	janus_dtls_fd_bridge(dtls);
	if(janus_flags_is_set(&handle->webrtc_flags, JANUS_ICE_HANDLE_WEBRTC_STOP) || janus_is_stopping()) {
		/* DTLS alert triggered, we should end it here */
		JANUS_LOG(LOG_VERB, "[%"SCNu64"] Forced to stop it here...\n", handle->handle_id);
		return;
	}
	if(!SSL_is_init_finished(dtls->ssl)) {
		/* Nothing else to do for now */
		JANUS_LOG(LOG_HUGE, "[%"SCNu64"] Initialization not finished yet...\n", handle->handle_id);
		return;
	}
	if(dtls->ready) {
		/* There's data to be read? */
		JANUS_LOG(LOG_HUGE, "[%"SCNu64"] Any data available?\n", handle->handle_id);
#ifdef HAVE_SCTP
		if(dtls->sctp != NULL && read > 0) {
			JANUS_LOG(LOG_HUGE, "[%"SCNu64"] Sending data (%d bytes) to the SCTP stack...\n", handle->handle_id, read);
			janus_sctp_data_from_dtls(dtls->sctp, data, read);
		}
#else
		if(read > 0) {
			JANUS_LOG(LOG_WARN, "[%"SCNu64"] Data available but Data Channels support disabled...\n", handle->handle_id);
		}
#endif
	} else {
		JANUS_LOG(LOG_VERB, "[%"SCNu64"] DTLS established, yay!\n", handle->handle_id);
		/* Check the remote fingerprint */
		X509 *rcert = SSL_get_peer_certificate(dtls->ssl);
		if(!rcert) {
			JANUS_LOG(LOG_ERR, "[%"SCNu64"] No remote certificate?? (%s)\n",
				handle->handle_id, ERR_reason_error_string(ERR_get_error()));
		} else {
			unsigned int rsize;
			unsigned char rfingerprint[EVP_MAX_MD_SIZE];
			char remote_fingerprint[160];
			char *rfp = (char *)&remote_fingerprint;
			if(stream->remote_hashing && !strcasecmp(stream->remote_hashing, "sha-1")) {
				JANUS_LOG(LOG_VERB, "[%"SCNu64"] Computing sha-1 fingerprint of remote certificate...\n", handle->handle_id);
				X509_digest(rcert, EVP_sha1(), (unsigned char *)rfingerprint, &rsize);
			} else {
				JANUS_LOG(LOG_VERB, "[%"SCNu64"] Computing sha-256 fingerprint of remote certificate...\n", handle->handle_id);
				X509_digest(rcert, EVP_sha256(), (unsigned char *)rfingerprint, &rsize);
			}
			X509_free(rcert);
			rcert = NULL;
			unsigned int i = 0;
			for(i = 0; i < rsize; i++) {
				g_snprintf(rfp, 4, "%.2X:", rfingerprint[i]);
				rfp += 3;
			}
			*(rfp-1) = 0;
			JANUS_LOG(LOG_VERB, "[%"SCNu64"] Remote fingerprint (%s) of the client is %s\n",
				handle->handle_id, stream->remote_hashing ? stream->remote_hashing : "sha-256", remote_fingerprint);
			if(!strcasecmp(remote_fingerprint, stream->remote_fingerprint ? stream->remote_fingerprint : "(none)")) {
				JANUS_LOG(LOG_VERB, "[%"SCNu64"]  Fingerprint is a match!\n", handle->handle_id);
				dtls->dtls_state = JANUS_DTLS_STATE_CONNECTED;
				dtls->dtls_connected = janus_get_monotonic_time();
				/* Notify event handlers */
				janus_dtls_notify_state_change(dtls);
			} else {
				/* FIXME NOT a match! MITM? */
				JANUS_LOG(LOG_ERR, "[%"SCNu64"]  Fingerprint is NOT a match! got %s, expected %s\n", handle->handle_id, remote_fingerprint, stream->remote_fingerprint);
				dtls->dtls_state = JANUS_DTLS_STATE_FAILED;
				/* Notify event handlers */
				janus_dtls_notify_state_change(dtls);
				goto done;
			}
			if(dtls->dtls_state == JANUS_DTLS_STATE_CONNECTED) {
				if(component->stream_id == handle->audio_id || component->stream_id == handle->video_id) {
					/* Complete with SRTP setup */
					unsigned char material[SRTP_MASTER_LENGTH*2];
					unsigned char *local_key, *local_salt, *remote_key, *remote_salt;
					/* Export keying material for SRTP */
					if (!SSL_export_keying_material(dtls->ssl, material, SRTP_MASTER_LENGTH*2, "EXTRACTOR-dtls_srtp", 19, NULL, 0, 0)) {
						/* Oops... */
						JANUS_LOG(LOG_ERR, "[%"SCNu64"] Oops, couldn't extract SRTP keying material for component %d in stream %d?? (%s)\n",
							handle->handle_id, component->component_id, stream->stream_id, ERR_reason_error_string(ERR_get_error()));
						goto done;
					}
					/* Key derivation (http://tools.ietf.org/html/rfc5764#section-4.2) */
					if(dtls->dtls_role == JANUS_DTLS_ROLE_CLIENT) {
						local_key = material;
						remote_key = local_key + SRTP_MASTER_KEY_LENGTH;
						local_salt = remote_key + SRTP_MASTER_KEY_LENGTH;
						remote_salt = local_salt + SRTP_MASTER_SALT_LENGTH;
					} else {
						remote_key = material;
						local_key = remote_key + SRTP_MASTER_KEY_LENGTH;
						remote_salt = local_key + SRTP_MASTER_KEY_LENGTH;
						local_salt = remote_salt + SRTP_MASTER_SALT_LENGTH;
					}
					/* Build master keys and set SRTP policies */
						/* Remote (inbound) */
					srtp_crypto_policy_set_rtp_default(&(dtls->remote_policy.rtp));
					srtp_crypto_policy_set_rtcp_default(&(dtls->remote_policy.rtcp));
					dtls->remote_policy.ssrc.type = ssrc_any_inbound;
					unsigned char remote_policy_key[SRTP_MASTER_LENGTH];
					dtls->remote_policy.key = (unsigned char *)&remote_policy_key;
					memcpy(dtls->remote_policy.key, remote_key, SRTP_MASTER_KEY_LENGTH);
					memcpy(dtls->remote_policy.key + SRTP_MASTER_KEY_LENGTH, remote_salt, SRTP_MASTER_SALT_LENGTH);
#if HAS_DTLS_WINDOW_SIZE
					dtls->remote_policy.window_size = 128;
					dtls->remote_policy.allow_repeat_tx = 0;
#endif
					dtls->remote_policy.next = NULL;
						/* Local (outbound) */
					srtp_crypto_policy_set_rtp_default(&(dtls->local_policy.rtp));
					srtp_crypto_policy_set_rtcp_default(&(dtls->local_policy.rtcp));
					dtls->local_policy.ssrc.type = ssrc_any_outbound;
					unsigned char local_policy_key[SRTP_MASTER_LENGTH];
					dtls->local_policy.key = (unsigned char *)&local_policy_key;
					memcpy(dtls->local_policy.key, local_key, SRTP_MASTER_KEY_LENGTH);
					memcpy(dtls->local_policy.key + SRTP_MASTER_KEY_LENGTH, local_salt, SRTP_MASTER_SALT_LENGTH);
#if HAS_DTLS_WINDOW_SIZE
					dtls->local_policy.window_size = 128;
					dtls->local_policy.allow_repeat_tx = 0;
#endif
					dtls->local_policy.next = NULL;
					/* Create SRTP sessions */
					srtp_err_status_t res = srtp_create(&(dtls->srtp_in), &(dtls->remote_policy));
					if(res != srtp_err_status_ok) {
						/* Something went wrong... */
						JANUS_LOG(LOG_ERR, "[%"SCNu64"] Oops, error creating inbound SRTP session for component %d in stream %d??\n", handle->handle_id, component->component_id, stream->stream_id);
						JANUS_LOG(LOG_ERR, "[%"SCNu64"]  -- %d (%s)\n", handle->handle_id, res, janus_get_srtp_error(res));
						goto done;
					}
					JANUS_LOG(LOG_VERB, "[%"SCNu64"] Created inbound SRTP session for component %d in stream %d\n", handle->handle_id, component->component_id, stream->stream_id);
					res = srtp_create(&(dtls->srtp_out), &(dtls->local_policy));
					if(res != srtp_err_status_ok) {
						/* Something went wrong... */
						JANUS_LOG(LOG_ERR, "[%"SCNu64"] Oops, error creating outbound SRTP session for component %d in stream %d??\n", handle->handle_id, component->component_id, stream->stream_id);
						JANUS_LOG(LOG_ERR, "[%"SCNu64"]  -- %d (%s)\n", handle->handle_id, res, janus_get_srtp_error(res));
						goto done;
					}
					dtls->srtp_valid = 1;
					JANUS_LOG(LOG_VERB, "[%"SCNu64"] Created outbound SRTP session for component %d in stream %d\n", handle->handle_id, component->component_id, stream->stream_id);
				}
#ifdef HAVE_SCTP
				if(component->stream_id == handle->data_id ||
						(janus_flags_is_set(&handle->webrtc_flags, JANUS_ICE_HANDLE_WEBRTC_BUNDLE) &&
						janus_flags_is_set(&handle->webrtc_flags, JANUS_ICE_HANDLE_WEBRTC_DATA_CHANNELS))) {
					/* FIXME Create SCTP association as well (5000 should be dynamic, from the SDP...) */
					dtls->sctp = janus_sctp_association_create(dtls, handle->handle_id, 5000);
					if(dtls->sctp != NULL) {
						/* FIXME We need to start it in a thread, though, since it has blocking accept/connect stuff */
						GError *error = NULL;
						char tname[16];
						g_snprintf(tname, sizeof(tname), "sctpinit %"SCNu64, handle->handle_id);
						g_thread_try_new(tname, janus_dtls_sctp_setup_thread, dtls, &error);
						if(error != NULL) {
							/* Something went wrong... */
							JANUS_LOG(LOG_ERR, "[%"SCNu64"] Got error %d (%s) trying to launch the DTLS-SCTP thread...\n", handle->handle_id, error->code, error->message ? error->message : "??");
						}
						dtls->srtp_valid = 1;
					}
				}
#endif
				dtls->ready = 1;
			}
done:
			if(!janus_flags_is_set(&handle->webrtc_flags, JANUS_ICE_HANDLE_WEBRTC_ALERT) && dtls->srtp_valid) {
				/* Handshake successfully completed */
				janus_ice_dtls_handshake_done(handle, component);
			} else {
				/* Something went wrong in either DTLS or SRTP... tell the plugin about it */
				janus_dtls_callback(dtls->ssl, SSL_CB_ALERT, 0);
				janus_flags_set(&handle->webrtc_flags, JANUS_ICE_HANDLE_WEBRTC_CLEANING);
			}
		}
	}
}

void janus_dtls_srtp_send_alert(janus_dtls_srtp *dtls) {
	/* Send alert */
	if(dtls != NULL && dtls->ssl != NULL) {
		SSL_shutdown(dtls->ssl);
		janus_dtls_fd_bridge(dtls);
	}
}

void janus_dtls_srtp_destroy(janus_dtls_srtp *dtls) {
	if(dtls == NULL)
		return;
	dtls->ready = 0;
#ifdef HAVE_SCTP
	/* Destroy the SCTP association if this is a DataChannel */
	if(dtls->sctp != NULL) {
		janus_sctp_association_destroy(dtls->sctp);
		dtls->sctp = NULL;
	}
#endif
	/* Destroy DTLS stack and free resources */
	dtls->component = NULL;
	if(dtls->ssl != NULL) {
		SSL_free(dtls->ssl);
		dtls->ssl = NULL;
	}
	/* BIOs are destroyed by SSL_free */
	dtls->read_bio = NULL;
	dtls->write_bio = NULL;
	dtls->filter_bio = NULL;
	if(dtls->srtp_valid) {
		if(dtls->srtp_in) {
			srtp_dealloc(dtls->srtp_in);
			dtls->srtp_in = NULL;
		}
		if(dtls->srtp_out) {
			srtp_dealloc(dtls->srtp_out);
			dtls->srtp_out = NULL;
		}
		/* FIXME What about dtls->remote_policy and dtls->local_policy? */
	}
	g_free(dtls);
	dtls = NULL;
}

/* DTLS alert callback */
void janus_dtls_callback(const SSL *ssl, int where, int ret) {
	/* We only care about alerts */
	if (!(where & SSL_CB_ALERT)) {
		return;
	}
	janus_dtls_srtp *dtls = SSL_get_ex_data(ssl, 0);
	if(!dtls) {
		JANUS_LOG(LOG_ERR, "No DTLS session related to this alert...\n");
		return;
	}
	janus_ice_component *component = dtls->component;
	if(component == NULL) {
		JANUS_LOG(LOG_ERR, "No ICE component related to this alert...\n");
		return;
	}
	janus_ice_stream *stream = component->stream;
	if(!stream) {
		JANUS_LOG(LOG_ERR, "No ICE stream related to this alert...\n");
		return;
	}
	janus_ice_handle *handle = stream->handle;
	if(!handle) {
		JANUS_LOG(LOG_ERR, "No ICE handle related to this alert...\n");
		return;
	}
	JANUS_LOG(LOG_VERB, "[%"SCNu64"] DTLS alert triggered on stream %"SCNu16" (component %"SCNu16"), closing...\n", handle->handle_id, stream->stream_id, component->component_id);
	janus_flags_set(&handle->webrtc_flags, JANUS_ICE_HANDLE_WEBRTC_CLEANING);
	if(!janus_flags_is_set(&handle->webrtc_flags, JANUS_ICE_HANDLE_WEBRTC_ALERT)) {
		janus_flags_set(&handle->webrtc_flags, JANUS_ICE_HANDLE_WEBRTC_ALERT);
		if(handle->iceloop)
			g_main_loop_quit(handle->iceloop);
		janus_plugin *plugin = (janus_plugin *)handle->app;
		if(plugin != NULL) {
			JANUS_LOG(LOG_VERB, "[%"SCNu64"] Telling the plugin about it (%s)\n", handle->handle_id, plugin->get_name());
			if(plugin && plugin->hangup_media)
				plugin->hangup_media(handle->app_handle);
			janus_ice_notify_hangup(handle, "DTLS alert");
		}
	}
}

/* DTLS certificate verification callback */
int janus_dtls_verify_callback(int preverify_ok, X509_STORE_CTX *ctx) {
	/* We just use the verify_callback to request a certificate from the client */
	return 1;
}

/* DTLS BIOs to/from socket bridge */
void janus_dtls_fd_bridge(janus_dtls_srtp *dtls) {
	if(dtls == NULL) {
		JANUS_LOG(LOG_ERR, "No DTLS-SRTP stack, no DTLS bridge...\n");
		return;
	}
	janus_ice_component *component = (janus_ice_component *)dtls->component;
	if(component == NULL) {
		JANUS_LOG(LOG_ERR, "No component, no DTLS bridge...\n");
		return;
	}
	janus_ice_stream *stream = component->stream;
	if(!stream) {
		JANUS_LOG(LOG_ERR, "No stream, no DTLS bridge...\n");
		return;
	}
	janus_ice_handle *handle = stream->handle;
	if(!handle || !handle->agent || !dtls->write_bio) {
		JANUS_LOG(LOG_ERR, "No handle/agent/bio, no DTLS bridge...\n");
		return;
	}
	int pending = BIO_ctrl_pending(dtls->filter_bio);
	while(pending > 0) {
		JANUS_LOG(LOG_HUGE, "[%"SCNu64"] >> Going to send DTLS data: %d bytes\n", handle->handle_id, pending);
		char outgoing[pending];
		int out = BIO_read(dtls->write_bio, outgoing, sizeof(outgoing));
		JANUS_LOG(LOG_HUGE, "[%"SCNu64"] >> >> Read %d bytes from the write_BIO...\n", handle->handle_id, out);
		if(out > 1500) {
			/* FIXME Just a warning for now, this will need to be solved with proper fragmentation */
			JANUS_LOG(LOG_WARN, "[%"SCNu64"] The DTLS stack is trying to send a packet of %d bytes, this may be larger than the MTU and get dropped!\n", handle->handle_id, out);
		}
		int bytes = nice_agent_send(handle->agent, component->stream_id, component->component_id, out, outgoing);
		if(bytes < out) {
			JANUS_LOG(LOG_ERR, "[%"SCNu64"] Error sending DTLS message on component %d of stream %d (%d)\n", handle->handle_id, component->component_id, stream->stream_id, bytes);
		} else {
			JANUS_LOG(LOG_HUGE, "[%"SCNu64"] >> >> ... and sent %d of those bytes on the socket\n", handle->handle_id, bytes);
		}
		/* Update stats (TODO Do the same for the last second window as well)
		 * FIXME: the Data stats includes the bytes used for the handshake */
		if(bytes > 0) {
			component->out_stats.data_packets++;
			component->out_stats.data_bytes += bytes;
		}
		/* Check if there's anything left to send (e.g., fragmented packets) */
		pending = BIO_ctrl_pending(dtls->filter_bio);
	}
}

#ifdef HAVE_SCTP
void janus_dtls_wrap_sctp_data(janus_dtls_srtp *dtls, char *buf, int len) {
	if(dtls == NULL || !dtls->ready || dtls->sctp == NULL || buf == NULL || len < 1)
		return;
	janus_sctp_send_data(dtls->sctp, buf, len);
}

int janus_dtls_send_sctp_data(janus_dtls_srtp *dtls, char *buf, int len) {
	if(dtls == NULL || !dtls->ready || buf == NULL || len < 1)
		return -1;
	int res = SSL_write(dtls->ssl, buf, len);
	if(res <= 0) {
		unsigned long err = SSL_get_error(dtls->ssl, res);
		JANUS_LOG(LOG_ERR, "Error sending data: %s\n", ERR_reason_error_string(err));
	} else {
		janus_dtls_fd_bridge(dtls);
	}
	return res;
}

void janus_dtls_notify_data(janus_dtls_srtp *dtls, char *buf, int len) {
	if(dtls == NULL || buf == NULL || len < 1)
		return;
	janus_ice_component *component = (janus_ice_component *)dtls->component;
	if(component == NULL) {
		JANUS_LOG(LOG_ERR, "No component...\n");
		return;
	}
	janus_ice_stream *stream = component->stream;
	if(!stream) {
		JANUS_LOG(LOG_ERR, "No stream...\n");
		return;
	}
	janus_ice_handle *handle = stream->handle;
	if(!handle || !handle->agent || !dtls->write_bio) {
		JANUS_LOG(LOG_ERR, "No handle...\n");
		return;
	}
	janus_ice_incoming_data(handle, buf, len);
}
#endif

gboolean janus_dtls_retry(gpointer stack) {
	janus_dtls_srtp *dtls = (janus_dtls_srtp *)stack;
	if(dtls == NULL)
		return FALSE;
	janus_ice_component *component = (janus_ice_component *)dtls->component;
	if(component == NULL)
		return FALSE;
	janus_ice_stream *stream = component->stream;
	if(!stream)
		return FALSE;
	janus_ice_handle *handle = stream->handle;
	if(!handle)
		return FALSE;
	if(janus_flags_is_set(&handle->webrtc_flags, JANUS_ICE_HANDLE_WEBRTC_STOP))
		return FALSE;
	if(dtls->dtls_state == JANUS_DTLS_STATE_CONNECTED) {
		JANUS_LOG(LOG_VERB, "[%"SCNu64"]  DTLS already set up, disabling retransmission timer!\n", handle->handle_id);
		if(component->source != NULL) {
			g_source_destroy(component->source);
			g_source_unref(component->source);
			component->source = NULL;
		}
		return FALSE;
	}
	struct timeval timeout = {0};
	DTLSv1_get_timeout(dtls->ssl, &timeout);
	guint64 timeout_value = timeout.tv_sec*1000 + timeout.tv_usec/1000;
	JANUS_LOG(LOG_HUGE, "[%"SCNu64"] DTLSv1_get_timeout: %"SCNu64"\n", handle->handle_id, timeout_value);
	if(timeout_value == 0) {
		JANUS_LOG(LOG_VERB, "[%"SCNu64"] DTLS timeout on component %d of stream %d, retransmitting\n", handle->handle_id, component->component_id, stream->stream_id);
		DTLSv1_handle_timeout(dtls->ssl);
		janus_dtls_fd_bridge(dtls);
	}
	return TRUE;
}


#ifdef HAVE_SCTP
/* Helper thread to create a SCTP association that will use this DTLS stack */
void *janus_dtls_sctp_setup_thread(void *data) {
	if(data == NULL) {
		JANUS_LOG(LOG_ERR, "No DTLS stack??\n");
		g_thread_unref(g_thread_self());
		return NULL;
	}
	janus_dtls_srtp *dtls = (janus_dtls_srtp *)data;
	if(dtls->sctp == NULL) {
		JANUS_LOG(LOG_ERR, "No SCTP stack??\n");
		g_thread_unref(g_thread_self());
		return NULL;
	}
	janus_sctp_association *sctp = (janus_sctp_association *)dtls->sctp;
	/* Do the accept/connect stuff now */
	JANUS_LOG(LOG_VERB, "[%"SCNu64"] Started thread: setup of the SCTP association\n", sctp->handle_id);
	janus_sctp_association_setup(sctp);
	g_thread_unref(g_thread_self());
	return NULL;
}
#endif
