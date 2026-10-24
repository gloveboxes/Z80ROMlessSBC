/* Native asynchronous transport for the production FT driver/parser. Emulator
 * I/O and polling share one thread; the Pico retains its separate core owners. */
#include "host_http_io.h"
#include "http_io.h"

#include <stdio.h>
#include <string.h>

#ifdef HAVE_LIBCURL
#include <curl/curl.h>

static CURLM *multi;
static CURL *client;
static unsigned char pending[CURL_MAX_WRITE_SIZE];
static size_t pending_length, pending_position;
static bool paused, closed;

static void cancel(void) {
  if (client) {
    curl_multi_remove_handle(multi, client);
    curl_easy_cleanup(client);
  }
  client = NULL;
  pending_length = pending_position = 0;
  paused = closed = false;
}

static size_t receive(char *data, size_t size, size_t count, void *context) {
  (void)context;
  size_t n = size * count;
  if (pending_length) {
    paused = true;
    return CURL_WRITEFUNC_PAUSE;
  }
  if (n > sizeof(pending)) return 0;
  memcpy(pending, data, n);
  pending_length = n;
  pending_position = 0;
  return n;
}

static bool start(const z80_http_url_t *url) {
  cancel();
  char text[1280];
  int n = snprintf(text, sizeof(text), "%s://%s%s", url->https ? "https" : "http",
                   url->authority, url->path);
  if (n <= 0 || (size_t)n >= sizeof(text) || !multi) return false;
  client = curl_easy_init();
  if (!client) return false;
  curl_easy_setopt(client, CURLOPT_URL, text);
  curl_easy_setopt(client, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
  curl_easy_setopt(client, CURLOPT_HEADER, 1L);
  curl_easy_setopt(client, CURLOPT_SUPPRESS_CONNECT_HEADERS, 1L);
  curl_easy_setopt(client, CURLOPT_WRITEFUNCTION, receive);
  /* The production parser, rather than curl, decodes chunking and redirects. */
  curl_easy_setopt(client, CURLOPT_HTTP_TRANSFER_DECODING, 0L);
  curl_easy_setopt(client, CURLOPT_HTTP_CONTENT_DECODING, 0L);
  curl_easy_setopt(client, CURLOPT_ACCEPT_ENCODING, "identity");
  curl_easy_setopt(client, CURLOPT_FOLLOWLOCATION, 0L);
  curl_easy_setopt(client, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(client, CURLOPT_SSL_VERIFYHOST, 2L);
  curl_easy_setopt(client, CURLOPT_CAINFO, Z80_HTTP_CA_FILE);
  curl_easy_setopt(client, CURLOPT_CONNECTTIMEOUT_MS, 30000L);
  curl_easy_setopt(client, CURLOPT_LOW_SPEED_LIMIT, 1L);
  curl_easy_setopt(client, CURLOPT_LOW_SPEED_TIME, 30L);
  curl_easy_setopt(client, CURLOPT_NOSIGNAL, 1L);
  return curl_multi_add_handle(multi, client) == CURLM_OK;
}
#endif

void host_http_io_init(void) {
  z80_http_io_init();
#ifdef HAVE_LIBCURL
  curl_global_init(CURL_GLOBAL_DEFAULT);
  multi = curl_multi_init();
  z80_http_io_backend(start, cancel);
  z80_http_io_network_state(multi != NULL, 1);
#endif
}

void host_http_io_poll(void) {
  z80_http_io_core1_service();
#ifdef HAVE_LIBCURL
  if (!client) return;
  if (!pending_length && paused) {
    paused = false;
    curl_easy_pause(client, CURLPAUSE_CONT);
  }
  int running;
  if (curl_multi_perform(multi, &running) != CURLM_OK)
    z80_http_io_error("Native HTTP poll failed");
  if (pending_length) {
    size_t n = pending_length - pending_position;
    if (n > 512) n = 512;
    pending_position += z80_http_io_receive(pending + pending_position, n);
    if (pending_position == pending_length) pending_length = pending_position = 0;
  }
  int messages;
  CURLMsg *message;
  while ((message = curl_multi_info_read(multi, &messages))) {
    if (message->msg != CURLMSG_DONE || message->easy_handle != client) continue;
    if (message->data.result != CURLE_OK)
      z80_http_io_error(curl_easy_strerror(message->data.result));
    else closed = true;
  }
  if (closed && !pending_length) z80_http_io_remote_close();
  z80_http_io_core1_service();
#endif
}

void host_http_io_close(void) {
  z80_http_io_out(HTTP_COMMAND_PORT, HTTP_CLOSE);
  z80_http_io_core1_service();
#ifdef HAVE_LIBCURL
  cancel();
  if (multi) curl_multi_cleanup(multi);
  multi = NULL;
  curl_global_cleanup();
#endif
}
