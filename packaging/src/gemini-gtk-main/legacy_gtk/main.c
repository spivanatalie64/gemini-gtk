#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#include <gtk/gtk.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include <sodium.h>

typedef struct {
    GtkWidget *window;
    GtkWidget *api_key_entry;
    GtkWidget *stack;
    GtkWidget *chat_view; /* GtkTextView */
    GtkWidget *chat_input_entry;
    GtkWidget *chat_send_button;
    GtkWidget *endpoint_entry;
} AppWidgets;

static const char *MAGIC = "GEMINIENC1";

static gchar *get_api_key_enc_path(void) {
    const gchar *config_dir = g_get_user_config_dir();
    return g_build_filename(config_dir, "gemini-gtk", "api_key.enc", NULL);
}

static gchar *get_api_key_plain_path(void) {
    const gchar *config_dir = g_get_user_config_dir();
    return g_build_filename(config_dir, "gemini-gtk", "api_key.txt", NULL);
}

/* Curl response */
struct CurlResponse { char *data; size_t len; };
static size_t curl_write_cb(void *ptr, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct CurlResponse *r = (struct CurlResponse*)userp;
    char *newp = realloc(r->data, r->len + realsize + 1);
    if (!newp) return 0;
    r->data = newp;
    memcpy(&(r->data[r->len]), ptr, realsize);
    r->len += realsize;
    r->data[r->len] = '\0';
    return realsize;
}

/* UI helpers */
typedef struct { AppWidgets *app; char *text; } UIMessage;
static gboolean idle_append_ui_message(gpointer data) {
    UIMessage *m = (UIMessage*)data;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->app->chat_view));
    GtkTextIter end_iter;
    gtk_text_buffer_get_end_iter(buffer, &end_iter);
    gtk_text_buffer_insert(buffer, &end_iter, m->text, -1);
    gtk_text_buffer_insert(buffer, &end_iter, "\n", -1);
    g_free(m->text);
    g_free(m);
    return G_SOURCE_REMOVE;
}

static void schedule_append(AppWidgets *app, const char *text_fmt, ...) {
    va_list ap;
    va_start(ap, text_fmt);
    gchar *msg = g_strdup_vprintf(text_fmt, ap);
    va_end(ap);
    UIMessage *m = g_new0(UIMessage, 1);
    m->app = app;
    m->text = msg;
    g_idle_add(idle_append_ui_message, m);
}

/* Passphrase prompt */
static char *prompt_passphrase(GtkWindow *parent, gboolean confirm) {
    GtkWidget *dialog = gtk_dialog_new_with_buttons(confirm ? "Enter passphrase (confirm)" : "Enter passphrase",
                                                    parent,
                                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    "_Cancel", GTK_RESPONSE_CANCEL,
                                                    "_OK", GTK_RESPONSE_OK,
                                                    NULL);
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *label = gtk_label_new(confirm ? "Passphrase (will be used to encrypt the API key). Type twice to confirm:" : "Passphrase to decrypt the API key:");
    gtk_box_pack_start(GTK_BOX(content), label, FALSE, FALSE, 6);
    GtkWidget *entry1 = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(entry1), FALSE);
    gtk_box_pack_start(GTK_BOX(content), entry1, FALSE, FALSE, 6);
    GtkWidget *entry2 = NULL;
    if (confirm) {
        entry2 = gtk_entry_new();
        gtk_entry_set_visibility(GTK_ENTRY(entry2), FALSE);
        gtk_box_pack_start(GTK_BOX(content), entry2, FALSE, FALSE, 6);
    }
    gtk_widget_show_all(content);
    char *result = NULL;
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        const char *p1 = gtk_entry_get_text(GTK_ENTRY(entry1));
        if (confirm) {
            const char *p2 = gtk_entry_get_text(GTK_ENTRY(entry2));
            if (strcmp(p1, p2) == 0 && strlen(p1) > 0) {
                result = g_strdup(p1);
            }
        } else {
            if (strlen(p1) > 0) result = g_strdup(p1);
        }
    }
    gtk_widget_destroy(dialog);
    return result;
}

/* Encryption storage */
static gboolean encrypt_and_store_api_key(AppWidgets *app, const char *api_key) {
    char *pass = prompt_passphrase(GTK_WINDOW(app->window), TRUE);
    if (!pass) return FALSE;

    unsigned char salt[crypto_pwhash_SALTBYTES];
    randombytes_buf(salt, sizeof(salt));

    unsigned char key[crypto_aead_xchacha20poly1305_ietf_KEYBYTES];
    if (crypto_pwhash(key, sizeof key, pass, strlen(pass), salt,
                      crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE,
                      crypto_pwhash_ALG_DEFAULT) != 0) {
        schedule_append(app, "Error deriving key from passphrase (out of memory)");
        sodium_memzero(pass, strlen(pass));
        g_free(pass);
        return FALSE;
    }

    const unsigned char *m = (const unsigned char*)api_key;
    unsigned long long mlen = strlen(api_key);
    unsigned char nonce[crypto_aead_xchacha20poly1305_ietf_NPUBBYTES];
    randombytes_buf(nonce, sizeof(nonce));
    unsigned long long clen = mlen + crypto_aead_xchacha20poly1305_ietf_ABYTES;
    unsigned char *cipher = malloc(clen);
    unsigned long long actual_clen = 0;

    crypto_aead_xchacha20poly1305_ietf_encrypt(cipher, &actual_clen,
                                               m, mlen,
                                               NULL, 0, NULL,
                                               nonce, key);

    gchar *path = get_api_key_enc_path();
    size_t magic_len = strlen(MAGIC);
    size_t total = magic_len + sizeof(salt) + sizeof(nonce) + actual_clen;
    unsigned char *buf = malloc(total);
    unsigned char *p = buf;
    memcpy(p, MAGIC, magic_len); p += magic_len;
    memcpy(p, salt, sizeof(salt)); p += sizeof(salt);
    memcpy(p, nonce, sizeof(nonce)); p += sizeof(nonce);
    memcpy(p, cipher, actual_clen);

    GError *error = NULL;
    gboolean ok = g_file_set_contents(path, (const char*)buf, total, &error);
    if (!ok) {
        schedule_append(app, "Failed to write encrypted key: %s", error ? error->message : "unknown");
        if (error) g_error_free(error);
    }

    sodium_memzero(key, sizeof(key));
    sodium_memzero(pass, strlen(pass));
    g_free(pass);
    free(buf);
    free(cipher);
    g_free(path);
    return ok;
}

static char *read_and_decrypt_api_key(AppWidgets *app) {
    gchar *enc_path = get_api_key_enc_path();
    char *data = NULL;
    gsize length = 0;
    GError *error = NULL;
    if (!g_file_get_contents(enc_path, &data, &length, &error)) {
        g_free(enc_path);
        if (error) g_error_free(error);
        return NULL;
    }

    size_t magic_len = strlen(MAGIC);
    if (length < magic_len) {
        g_free(enc_path);
        g_free(data);
        return NULL;
    }
    if (memcmp(data, MAGIC, magic_len) != 0) {
        char *plain = g_strdup(data);
        g_free(enc_path);
        g_free(data);
        return plain;
    }

    const unsigned char *p = (const unsigned char*)data + magic_len;
    const unsigned char *salt = p; p += crypto_pwhash_SALTBYTES;
    const unsigned char *nonce = p; p += crypto_aead_xchacha20poly1305_ietf_NPUBBYTES;
    const unsigned char *cipher = p;
    size_t cipherlen = length - (magic_len + crypto_pwhash_SALTBYTES + crypto_aead_xchacha20poly1305_ietf_NPUBBYTES);

    char *pass = prompt_passphrase(GTK_WINDOW(app->window), FALSE);
    if (!pass) {
        g_free(enc_path);
        g_free(data);
        return NULL;
    }

    unsigned char key[crypto_aead_xchacha20poly1305_ietf_KEYBYTES];
    if (crypto_pwhash(key, sizeof key, pass, strlen(pass), salt,
                      crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE,
                      crypto_pwhash_ALG_DEFAULT) != 0) {
        schedule_append(app, "Error deriving key from passphrase");
        sodium_memzero(pass, strlen(pass));
        g_free(pass);
        g_free(enc_path);
        g_free(data);
        return NULL;
    }

    unsigned long long mlen = 0;
    unsigned char *m = malloc(cipherlen + 1);
    if (crypto_aead_xchacha20poly1305_ietf_decrypt(m, &mlen,
                                                  NULL,
                                                  cipher, cipherlen,
                                                  NULL, 0,
                                                  nonce, key) != 0) {
        schedule_append(app, "Incorrect passphrase or corrupted file");
        sodium_memzero(key, sizeof(key));
        sodium_memzero(pass, strlen(pass));
        g_free(pass);
        free(m);
        g_free(enc_path);
        g_free(data);
        return NULL;
    }

    char *out = g_strndup((char*)m, (gsize)mlen);

    sodium_memzero(key, sizeof(key));
    sodium_memzero(pass, strlen(pass));
    g_free(pass);
    free(m);
    g_free(enc_path);
    g_free(data);
    return out;
}

/* Endpoint storage and testing */
static gchar *get_endpoint_path(void) {
    const gchar *config_dir = g_get_user_config_dir();
    return g_build_filename(config_dir, "gemini-gtk", "endpoint.txt", NULL);
}

static void save_endpoint_file(AppWidgets *app) {
    gchar *path = get_endpoint_path();
    gchar *dir = g_path_get_dirname(path);
    g_mkdir_with_parents(dir, 0700);
    const char *ep = gtk_entry_get_text(GTK_ENTRY(app->endpoint_entry));
    GError *error = NULL;
    if (!g_file_set_contents(path, ep, -1, &error)) {
        schedule_append(app, "Failed to save endpoint: %s", error ? error->message : "unknown");
        if (error) g_error_free(error);
    } else {
        schedule_append(app, "Saved endpoint: %s", ep);
    }
    g_free(dir);
    g_free(path);
}

static void load_endpoint_file(AppWidgets *app) {
    gchar *path = get_endpoint_path();
    char *content = NULL;
    gsize len = 0;
    GError *error = NULL;
    const char *env = getenv("GEMINI_ENDPOINT");
    if (g_file_get_contents(path, &content, &len, &error)) {
        gtk_entry_set_text(GTK_ENTRY(app->endpoint_entry), content);
        g_free(content);
    } else if (env && strlen(env) > 0) {
        gtk_entry_set_text(GTK_ENTRY(app->endpoint_entry), env);
    } else {
        gtk_entry_set_text(GTK_ENTRY(app->endpoint_entry), "https://generativelanguage.googleapis.com/v1beta2/models/text-bison-001:generate");
    }
    if (error) g_error_free(error);
    g_free(path);
}

typedef struct { AppWidgets *app; gchar *endpoint; } EndpointTestData;
static gpointer endpoint_test_thread(gpointer user_data) {
    EndpointTestData *td = (EndpointTestData*)user_data;
    AppWidgets *app = td->app;
    const char *url = td->endpoint;

    struct CurlResponse resp = { .data = malloc(1), .len = 0 };
    struct curl_slist *headers = NULL;
    CURL *curl = curl_easy_init();
    if (!curl) {
        schedule_append(app, "Test: failed to init curl");
        g_free(td->endpoint);
        g_free(td);
        return NULL;
    }

    headers = curl_slist_append(headers, "Content-Type: application/json");
    const char *payload = "{\"prompt\":{\"text\":\"test\"},\"temperature\":0.2,\"maxOutputTokens\":16}";
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);

    CURLcode cres = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    schedule_append(app, "[Test] Request URL: %s", url);
    schedule_append(app, "[Test] HTTP status: %ld", http_code);
    if (cres != CURLE_OK) {
        schedule_append(app, "[Test] Network error: %s", curl_easy_strerror(cres));
    } else if (http_code == 404) {
        schedule_append(app, "[Test] 404 Not Found: endpoint likely incorrect or API not enabled.");
        if (resp.len > 0) schedule_append(app, "%s", resp.data);
    } else {
        if (resp.len > 0) schedule_append(app, "[Test] Response: %s", resp.data);
        else schedule_append(app, "[Test] Empty response (check credentials/endpoint)");
    }

    free(resp.data);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    g_free(td->endpoint);
    g_free(td);
    return NULL;
}

static void on_save_endpoint_button_clicked(GtkButton *button, gpointer user_data) {
    AppWidgets *app = (AppWidgets*)user_data;
    save_endpoint_file(app);
}

static void on_test_endpoint_button_clicked(GtkButton *button, gpointer user_data) {
    AppWidgets *app = (AppWidgets*)user_data;
    const char *ep = gtk_entry_get_text(GTK_ENTRY(app->endpoint_entry));
    EndpointTestData *td = g_new0(EndpointTestData, 1);
    td->app = app;
    td->endpoint = g_strdup(ep);
    g_thread_new("endpoint-test", endpoint_test_thread, td);
}


/* Background request thread */
typedef struct { AppWidgets *app; char *message; } GeminiThreadData;
static gpointer gemini_request_thread(gpointer user_data) {
    GeminiThreadData *td = (GeminiThreadData*)user_data;
    AppWidgets *app = td->app;

    char *api_key = read_and_decrypt_api_key(app);
    if (!api_key) {
        gchar *plain_path = get_api_key_plain_path();
        gchar *content = NULL;
        gsize len = 0;
        GError *gerr = NULL;
        if (g_file_get_contents(plain_path, &content, &len, &gerr)) {
            api_key = content;
        }
        g_free(plain_path);
    }

    if (!api_key) {
        schedule_append(app, "No API key available. Please save one.");
        g_free(td->message);
        g_free(td);
        return NULL;
    }

    const char *env_endpoint = getenv("GEMINI_ENDPOINT");
    gchar *request_url = NULL;
    struct curl_slist *headers = NULL;
    CURL *curl = NULL;
    struct CurlResponse resp = { .data = malloc(1), .len = 0 };

    if (env_endpoint && strlen(env_endpoint) > 0) {
        /* Use the user-provided endpoint as-is. Prefer Authorization header for non-API-key credentials. */
        request_url = g_strdup(env_endpoint);
        if (api_key && strncmp(api_key, "AIza", 4) != 0) {
            gchar *auth = g_strdup_printf("Authorization: Bearer %s", api_key);
            headers = curl_slist_append(headers, "Content-Type: application/json");
            headers = curl_slist_append(headers, auth);
            g_free(auth);
        } else {
            /* API key: user may include it in the endpoint or we'll add it as a query param later */
            headers = curl_slist_append(headers, "Content-Type: application/json");
        }
    } else {
        /* Default endpoint -- if this returns 404 you may need to set GEMINI_ENDPOINT to the correct URL */
        const char *default_ep = "https://generativelanguage.googleapis.com/v1beta2/models/text-bison-001:generate";
        if (api_key && strncmp(api_key, "AIza", 4) == 0) {
            request_url = g_strdup_printf("%s?key=%s", default_ep, api_key);
        } else {
            request_url = g_strdup(default_ep);
            gchar *auth = g_strdup_printf("Authorization: Bearer %s", api_key);
            headers = curl_slist_append(headers, "Content-Type: application/json");
            headers = curl_slist_append(headers, auth);
            g_free(auth);
        }
    }

    curl = curl_easy_init();
    if (!curl) {
        schedule_append(app, "Error: failed to initialize curl");
        g_free(api_key);
        g_free(td->message);
        g_free(td);
        curl_slist_free_all(headers);
        return NULL;
    }

    json_object *jroot = json_object_new_object();
    json_object *prompt = json_object_new_object();
    json_object_object_add(prompt, "text", json_object_new_string(td->message));
    json_object_object_add(jroot, "prompt", prompt);
    json_object_object_add(jroot, "maxOutputTokens", json_object_new_int(512));
    json_object_object_add(jroot, "temperature", json_object_new_double(0.2));
    const char *payload = json_object_to_json_string(jroot);

    curl_easy_setopt(curl, CURLOPT_URL, request_url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);

    CURLcode cres = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    schedule_append(app, "Request URL: %s", request_url);
    schedule_append(app, "HTTP status: %ld", http_code);

    if (cres != CURLE_OK) {
        schedule_append(app, "Network error: %s", curl_easy_strerror(cres));
    } else if (http_code == 404) {
        schedule_append(app, "Error 404: endpoint not found. Try setting the GEMINI_ENDPOINT environment variable to the correct API URL.");
        if (resp.len > 0) schedule_append(app, "%s", resp.data);
    } else {
        gchar *out = NULL;
        if (resp.len > 0) {
            json_object *rjson = json_tokener_parse(resp.data);
            if (rjson) {
                json_object *candidate = NULL;
                if (json_object_object_get_ex(rjson, "candidates", &candidate)) {
                    if (json_object_get_type(candidate) == json_type_array) {
                        json_object *first = json_object_array_get_idx(candidate, 0);
                        if (first) {
                            if (json_object_get_type(first) == json_type_string) {
                                out = g_strdup(json_object_get_string(first));
                            } else if (json_object_get_type(first) == json_type_object) {
                                json_object *content = NULL;
                                if (json_object_object_get_ex(first, "content", &content)) {
                                    out = g_strdup(json_object_get_string(content));
                                } else if (json_object_object_get_ex(first, "text", &content)) {
                                    out = g_strdup(json_object_get_string(content));
                                }
                            }
                        }
                    }
                }
                if (!out && json_object_object_get_ex(rjson, "output", &candidate)) {
                    out = g_strdup(json_object_get_string(candidate));
                }
                if (!out && json_object_object_get_ex(rjson, "response", &candidate)) {
                    out = g_strdup(json_object_get_string(candidate));
                }
                if (!out) out = g_strdup(resp.data);
                json_object_put(rjson);
            } else {
                out = g_strdup(resp.data);
            }
        } else {
            out = g_strdup("(empty response)");
        }
        schedule_append(app, "Gemini: %s", out);
        g_free(out);
    }

    free(resp.data);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    g_free(request_url);
    json_object_put(jroot);
    g_free(api_key);
    g_free(td->message);
    g_free(td);
    return NULL;
}

/* UI callbacks */
static void on_open_key_button_clicked(GtkButton *button, gpointer user_data) {
    GError *error = NULL;
    const char *url = "https://aistudio.google.com/api-keys";
    if (!g_app_info_launch_default_for_uri(url, NULL, &error)) {
        g_warning("Failed to open URL: %s\n", error->message);
        g_error_free(error);
    }
}

static void on_save_key_button_clicked(GtkButton *button, gpointer user_data) {
    AppWidgets *app = (AppWidgets*)user_data;
    const char *api_key = gtk_entry_get_text(GTK_ENTRY(app->api_key_entry));

    gchar *enc_path = get_api_key_enc_path();
    gchar *dir_path = g_path_get_dirname(enc_path);

    if (g_mkdir_with_parents(dir_path, 0700) == -1) {
        g_warning("Failed to create directory %s: %s\n", dir_path, g_strerror(errno));
        g_free(dir_path);
        g_free(enc_path);
        return;
    }
    g_free(dir_path);

    if (encrypt_and_store_api_key(app, api_key)) {
        gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "chat_view");
        gtk_window_set_title(GTK_WINDOW(app->window), "Gemini Chat");
    }
    g_free(enc_path);
}

static void load_api_key(AppWidgets *app) {
    gchar *enc_path = get_api_key_enc_path();
    if (g_file_test(enc_path, G_FILE_TEST_EXISTS)) {
        char *dec = read_and_decrypt_api_key(app);
        if (dec) {
            gtk_entry_set_text(GTK_ENTRY(app->api_key_entry), dec);
            g_free(dec);
            gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "chat_view");
            gtk_window_set_title(GTK_WINDOW(app->window), "Gemini Chat");
            g_free(enc_path);
            return;
        }
    }
    g_free(enc_path);

    gchar *plain_path = get_api_key_plain_path();
    gchar *content = NULL;
    gsize length = 0;
    GError *error = NULL;
    if (g_file_get_contents(plain_path, &content, &length, &error)) {
        gtk_entry_set_text(GTK_ENTRY(app->api_key_entry), content);
        g_free(content);
        gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "chat_view");
        gtk_window_set_title(GTK_WINDOW(app->window), "Gemini Chat");
    } else {
        gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "key_input_view");
        gtk_window_set_title(GTK_WINDOW(app->window), "Gemini API Key Manager");
        if (error) g_error_free(error);
    }
    g_free(plain_path);
}

static void on_chat_send_button_clicked(GtkButton *button, gpointer user_data) {
    AppWidgets *app = (AppWidgets*)user_data;
    const char *message = gtk_entry_get_text(GTK_ENTRY(app->chat_input_entry));
    if (!message || strlen(message) == 0) return;

    schedule_append(app, "You: %s", message);

    GeminiThreadData *td = g_new0(GeminiThreadData, 1);
    td->app = app;
    td->message = g_strdup(message);
    g_thread_new("gemini-request", gemini_request_thread, td);

    gtk_entry_set_text(GTK_ENTRY(app->chat_input_entry), "");
}

static void activate(GtkApplication *app_instance, gpointer user_data) {
    AppWidgets *w = g_new0(AppWidgets, 1);
    w->window = gtk_application_window_new(app_instance);
    gtk_window_set_title(GTK_WINDOW(w->window), "Gemini API Key Manager");
    gtk_window_set_default_size(GTK_WINDOW(w->window), 600, 400);

    w->stack = gtk_stack_new();
    gtk_container_add(GTK_CONTAINER(w->window), w->stack);

    GtkWidget *api_key_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(api_key_vbox), 10);
    gtk_stack_add_named(GTK_STACK(w->stack), api_key_vbox, "key_input_view");

    GtkWidget *api_label = gtk_label_new("Google Gemini API Key:");
    gtk_box_pack_start(GTK_BOX(api_key_vbox), api_label, FALSE, FALSE, 0);

    w->api_key_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(w->api_key_entry), "Enter your API key here");
    gtk_box_pack_start(GTK_BOX(api_key_vbox), w->api_key_entry, FALSE, FALSE, 0);

    GtkWidget *open_button = gtk_button_new_with_label("Get API Key");
    g_signal_connect(open_button, "clicked", G_CALLBACK(on_open_key_button_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(api_key_vbox), open_button, FALSE, FALSE, 0);

    GtkWidget *save_button = gtk_button_new_with_label("Save API Key");
    g_signal_connect(save_button, "clicked", G_CALLBACK(on_save_key_button_clicked), w);
    gtk_box_pack_start(GTK_BOX(api_key_vbox), save_button, FALSE, FALSE, 0);

    GtkWidget *endpoint_label = gtk_label_new("API Endpoint (optional):");
    gtk_box_pack_start(GTK_BOX(api_key_vbox), endpoint_label, FALSE, FALSE, 0);
    w->endpoint_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(w->endpoint_entry), "https://generativelanguage.googleapis.com/....");
    gtk_box_pack_start(GTK_BOX(api_key_vbox), w->endpoint_entry, FALSE, FALSE, 0);

    GtkWidget *save_ep_button = gtk_button_new_with_label("Save Endpoint");
    g_signal_connect(save_ep_button, "clicked", G_CALLBACK(on_save_endpoint_button_clicked), w);
    gtk_box_pack_start(GTK_BOX(api_key_vbox), save_ep_button, FALSE, FALSE, 0);

    GtkWidget *test_ep_button = gtk_button_new_with_label("Test Endpoint");
    g_signal_connect(test_ep_button, "clicked", G_CALLBACK(on_test_endpoint_button_clicked), w);
    gtk_box_pack_start(GTK_BOX(api_key_vbox), test_ep_button, FALSE, FALSE, 0);

    GtkWidget *chat_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(chat_vbox), 10);
    gtk_stack_add_named(GTK_STACK(w->stack), chat_vbox, "chat_view");

    w->chat_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(w->chat_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(w->chat_view), GTK_WRAP_WORD);
    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scrolled), w->chat_view);
    gtk_box_pack_start(GTK_BOX(chat_vbox), scrolled, TRUE, TRUE, 0);

    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(chat_vbox), hbox, FALSE, FALSE, 0);
    w->chat_input_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(w->chat_input_entry), "Type your message here...");
    gtk_box_pack_start(GTK_BOX(hbox), w->chat_input_entry, TRUE, TRUE, 0);
    w->chat_send_button = gtk_button_new_with_label("Send");
    g_signal_connect(w->chat_send_button, "clicked", G_CALLBACK(on_chat_send_button_clicked), w);
    gtk_box_pack_start(GTK_BOX(hbox), w->chat_send_button, FALSE, FALSE, 0);

    load_api_key(w);
    load_endpoint_file(w);

    gtk_widget_show_all(w->window);
}

int main(int argc, char **argv) {
    if (sodium_init() < 0) {
        g_printerr("libsodium initialization failed\n");
        return 1;
    }
    curl_global_init(CURL_GLOBAL_DEFAULT);
    GtkApplication *app = gtk_application_new("com.example.GeminiApp", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
        curl_global_cleanup();
        return status;
}