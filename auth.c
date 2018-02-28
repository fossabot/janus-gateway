/*! \file    auth.c
 * \author   Lorenzo Miniero <lorenzo@meetecho.com>
 * \copyright GNU General Public License v3
 * \brief    Requests authentication
 * \details  Implementation of a simple mechanism for authenticating
 * requests. If enabled (it's disabled by default), the Janus admin API
 * can be used to specify valid tokens; each request must then contain
 * a valid token string, or otherwise the request is rejected with an
 * error. Whether tokens should be shared across users or not is
 * completely up to the controlling application: these tokens are
 * completely opaque to Janus, and treated as strings, which means
 * Janus will only check if the token exists or not when asked.
 * 
 * \ingroup core
 * \ref core
 */

#include "auth.h"
#include "debug.h"
#include "mutex.h"
#include "utils.h"

#include <string.h>
#include <openssl/hmac.h>

/* Hash table to contain the tokens to match */
static GHashTable *tokens = NULL, *allowed_plugins = NULL;
static gboolean auth_enabled = FALSE;
static janus_mutex mutex;
static char *auth_secret = NULL;

static void janus_auth_free_token(char *token) {
	g_free(token);
}

/* Setup */
void janus_auth_init(gboolean enabled, const char *secret) {
	if(enabled) {
		if(secret == NULL) {
			JANUS_LOG(LOG_INFO, "Stored-Token based authentication enabled\n");
			tokens = g_hash_table_new_full(g_str_hash, g_str_equal, (GDestroyNotify)janus_auth_free_token, NULL);
			allowed_plugins = g_hash_table_new_full(g_str_hash, g_str_equal, (GDestroyNotify)janus_auth_free_token, NULL);
			auth_enabled = TRUE;
		} else {
			JANUS_LOG(LOG_INFO, "Signed-Token based authentication enabled\n");
			auth_secret = g_strdup(secret);
			auth_enabled = TRUE;
		}
	} else {
		JANUS_LOG(LOG_WARN, "Token based authentication disabled\n");
	}
	janus_mutex_init(&mutex);
}

gboolean janus_auth_is_enabled(void) {
	return auth_enabled;
}

gboolean janus_auth_is_stored_mode(void) {
  return auth_enabled && tokens != NULL;
}

void janus_auth_deinit(void) {
	janus_mutex_lock(&mutex);
	if(tokens != NULL)
		g_hash_table_destroy(tokens);
	tokens = NULL;
	if(allowed_plugins != NULL)
		g_hash_table_destroy(allowed_plugins);
	allowed_plugins = NULL;
	if(auth_secret != NULL)
		g_free(auth_secret);
	auth_secret = NULL;
	janus_mutex_unlock(&mutex);
}

gboolean janus_auth_check_signature(const char *token, const char *realm) {
	if (!auth_enabled || auth_secret == NULL)
		return FALSE;
	gchar *token_signature = strchr(token, ':') + 1;
	gchar *token_metadata = g_strndup(token, token_signature - token - 1);
	gchar **list = g_strsplit(token_metadata, ",", 3);
	/* need at least an expiry timestamp and realm */
	if(!list[0] || !list[1])
		goto fail;
	/* verify timestamp */
	int expiry_time = atoi(list[0]);
	int real_time = janus_get_real_time() / 1000000;
	if(real_time > expiry_time)
		goto fail;
	/* verify realm */
	if(strcmp(list[1], realm))
		goto fail;
	/* verify HMAC-SHA1 */
	unsigned char signature[EVP_MAX_MD_SIZE];
	unsigned int len;
	HMAC(EVP_sha1(), auth_secret, strlen(auth_secret), token_metadata, strlen(token_metadata), signature, &len);
	gchar *base64 = g_base64_encode(signature, len);
	gboolean result = janus_strcmp_const_time(token_signature, base64);
	g_strfreev(list);
	g_free(base64);
	g_free(token_metadata);
	return result;

fail:
	g_strfreev(list);
	g_free(token_metadata);
	return FALSE;
}

gboolean janus_auth_check_signature_contains(const char *token, const char *realm, const char *desc) {
	if (!auth_enabled || auth_secret == NULL)
		return FALSE;
	gchar *token_signature = strchr(token, ':') + 1;
	gchar *token_metadata = g_strndup(token, token_signature - token - 1);
	gchar **list = g_strsplit(token_metadata, ",", 0);
	/* need at least an expiry timestamp and realm */
	if(!list[0] || !list[1])
		goto fail;
	/* verify timestamp */
	int expiry_time = atoi(list[0]);
	int real_time = janus_get_real_time() / 1000000;
	if(real_time > expiry_time)
		goto fail;
	/* verify realm */
	if(strcmp(list[1], realm))
		goto fail;
	/* find descriptor */
	gboolean result = FALSE;
	int i = 2;
	for(i = 2; list[i]; i++) {
		if (!strcmp(desc, list[i])) {
			result = TRUE;
			break;
		}
	}
	if (!result)
		goto fail;
	/* verify HMAC-SHA1 */
	unsigned char signature[EVP_MAX_MD_SIZE];
	unsigned int len;
	HMAC(EVP_sha1(), auth_secret, strlen(auth_secret), token_metadata, strlen(token_metadata), signature, &len);
	gchar *base64 = g_base64_encode(signature, len);
	result = janus_strcmp_const_time(token_signature, base64);
	g_strfreev(list);
	g_free(base64);
	g_free(token_metadata);
	return result;

fail:
	g_strfreev(list);
	g_free(token_metadata);
	return FALSE;
}

/* Tokens manipulation */
gboolean janus_auth_add_token(const char *token) {
	if(!auth_enabled || tokens == NULL) {
		JANUS_LOG(LOG_ERR, "Can't add token, stored-authentication mechanism is disabled\n");
		return FALSE;
	}
	if(token == NULL)
		return FALSE;
	janus_mutex_lock(&mutex);
	if(g_hash_table_lookup(tokens, token)) {
		JANUS_LOG(LOG_VERB, "Token already validated\n");
		janus_mutex_unlock(&mutex);
		return TRUE;
	}
	char *new_token = g_strdup(token);
	g_hash_table_insert(tokens, new_token, new_token);
	janus_mutex_unlock(&mutex);
	return TRUE;
}

gboolean janus_auth_check_token_exists(const char *token) {
	/* Always TRUE if the mechanism is disabled, of course */
	if(!auth_enabled || tokens == NULL)
		return TRUE;
	janus_mutex_lock(&mutex);
	if(token && g_hash_table_lookup(tokens, token)) {
		janus_mutex_unlock(&mutex);
		return TRUE;
	}
	janus_mutex_unlock(&mutex);
	return FALSE;
}

gboolean janus_auth_check_token(const char *token) {
	/* Always TRUE if the mechanism is disabled, of course */
	if(!auth_enabled)
		return TRUE;
	if (tokens == NULL)
		return janus_auth_check_signature(token, "janus");
	return janus_auth_check_token_exists(token);
}

GList *janus_auth_list_tokens(void) {
	/* Always NULL if the mechanism is disabled, of course */
	if(!auth_enabled || tokens == NULL)
		return NULL;
	janus_mutex_lock(&mutex);
	GList *list = NULL;
	if(g_hash_table_size(tokens) > 0) {
		GHashTableIter iter;
		gpointer value;
		g_hash_table_iter_init(&iter, tokens);
		while (g_hash_table_iter_next(&iter, NULL, &value)) {
			const char *token = value;
			list = g_list_append(list, g_strdup(token));
		}
	}
	janus_mutex_unlock(&mutex);
	return list;
}

gboolean janus_auth_remove_token(const char *token) {
	if(!auth_enabled || tokens == NULL) {
		JANUS_LOG(LOG_ERR, "Can't remove token, stored-authentication mechanism is disabled\n");
		return FALSE;
	}
	janus_mutex_lock(&mutex);
	gboolean ok = token && g_hash_table_remove(tokens, token);
	/* Also clear the allowed plugins mapping */
	GList *list = g_hash_table_lookup(allowed_plugins, token);
	g_hash_table_remove(allowed_plugins, token);
	if(list != NULL)
		g_list_free(list);
	/* Done */
	janus_mutex_unlock(&mutex);
	return ok;
}

/* Plugins access */
gboolean janus_auth_allow_plugin(const char *token, janus_plugin *plugin) {
	if(!auth_enabled || allowed_plugins == NULL) {
		JANUS_LOG(LOG_ERR, "Can't allow access to plugin, authentication mechanism is disabled\n");
		return FALSE;
	}
	if(token == NULL || plugin == NULL)
		return FALSE;
	janus_mutex_lock(&mutex);
	if(!g_hash_table_lookup(tokens, token)) {
		janus_mutex_unlock(&mutex);
		return FALSE;
	}
	GList *list = g_hash_table_lookup(allowed_plugins, token);
	if(list == NULL) {
		/* Add the new permission now */
		list = g_list_append(list, plugin);
		char *new_token = g_strdup(token);
		g_hash_table_insert(allowed_plugins, new_token, list);
		janus_mutex_unlock(&mutex);
		return TRUE;
	}
	/* We already have a list, update it if needed */
	if(g_list_find(list, plugin) != NULL) {
		JANUS_LOG(LOG_VERB, "Plugin access already allowed for token\n");
		janus_mutex_unlock(&mutex);
		return TRUE;
	}
	list = g_list_append(list, plugin);
	char *new_token = g_strdup(token);
	g_hash_table_insert(allowed_plugins, new_token, list);
	janus_mutex_unlock(&mutex);
	return TRUE;
}

gboolean janus_auth_check_plugin(const char *token, janus_plugin *plugin) {
	/* Always TRUE if the mechanism is disabled, of course */
	if(!auth_enabled)
		return TRUE;
	if (allowed_plugins == NULL)
		return janus_auth_check_signature_contains(token, "janus", plugin->get_package());
	janus_mutex_lock(&mutex);
	if(!g_hash_table_lookup(tokens, token)) {
		janus_mutex_unlock(&mutex);
		return FALSE;
	}
	GList *list = g_hash_table_lookup(allowed_plugins, token);
	if(g_list_find(list, plugin) == NULL) {
		janus_mutex_unlock(&mutex);
		return FALSE;
	}
	janus_mutex_unlock(&mutex);
	return TRUE;
}

GList *janus_auth_list_plugins(const char *token) {
	/* Always NULL if the mechanism is disabled, of course */
	if(!auth_enabled || allowed_plugins == NULL)
		return NULL;
	janus_mutex_lock(&mutex);
	if(!g_hash_table_lookup(tokens, token)) {
		janus_mutex_unlock(&mutex);
		return FALSE;
	}
	GList *list = NULL;
	GList *plugins_list = g_hash_table_lookup(allowed_plugins, token);
	if(plugins_list != NULL)
		list = g_list_copy(plugins_list);
	janus_mutex_unlock(&mutex);
	return list;
}

gboolean janus_auth_disallow_plugin(const char *token, janus_plugin *plugin) {
	if(!auth_enabled || allowed_plugins == NULL) {
		JANUS_LOG(LOG_ERR, "Can't disallow access to plugin, authentication mechanism is disabled\n");
		return FALSE;
	}
	janus_mutex_lock(&mutex);
	if(!g_hash_table_lookup(tokens, token)) {
		janus_mutex_unlock(&mutex);
		return FALSE;
	}
	GList *list = g_hash_table_lookup(allowed_plugins, token);
	if(list != NULL) {
		/* Update the list */
		list = g_list_remove_all(list, plugin);
		char *new_token = g_strdup(token);
		g_hash_table_insert(allowed_plugins, new_token, list);
	}
	janus_mutex_unlock(&mutex);
	return TRUE;
}
