#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "http_io.h"

static const char *base = "https://example.org/CaseSensitive/";
static z80_http_url_t requested;
static unsigned starts, cancels;
static uint8_t output[100000];
static size_t output_length;

void environment_io_init(const char *path) { (void)path; }
void environment_io_close(void) {}
uint8_t environment_input(uint8_t address) { (void)address; return 0; }
bool environment_io_get(const char *key, char *value, size_t capacity) {
  assert(!strcmp(key, "HTTP_BASE_URL"));
  if (!base) return false;
  assert(strlen(base) < capacity);
  strcpy(value, base);
  return true;
}
static bool start(const z80_http_url_t *url) { requested = *url; ++starts; return true; }
static void cancel(void) { ++cancels; }
static void service(void) { z80_http_io_core1_service(); }
static uint8_t status(void) { return z80_http_io_in(HTTP_COMMAND_PORT); }
static void command(uint8_t byte) { z80_http_io_out(HTTP_COMMAND_PORT, byte); }
static void filename(const char *name) {
  command(HTTP_SET_FILENAME);
  while (*name) z80_http_io_out(HTTP_DATA_PORT, (uint8_t)*name++);
  z80_http_io_out(HTTP_DATA_PORT, 0);
  assert(status() == HTTP_IDLE);
}
static void get(void) { command(HTTP_REQUEST_CHUNK); service(); }
static void drain(void) {
  service();
  if (status() != HTTP_DATA_READY) return;
  size_t n = z80_http_io_in(HTTP_DATA_PORT);
  if (!n) n = 256;
  assert(status() == HTTP_DATA_READY);
  for (size_t i = 0; i < n; ++i) {
    assert(output_length < sizeof(output));
    output[output_length++] = z80_http_io_in(HTTP_DATA_PORT);
  }
  if (status() != HTTP_EOF) get();
}
static void feed(const void *bytes, size_t n) {
  const uint8_t *p = bytes;
  size_t offset = 0;
  while (offset < n) {
    size_t used = z80_http_io_receive(p + offset, n - offset);
    offset += used;
    drain();
    if (!z80_http_io_active()) { assert(offset == n || status() == HTTP_ERROR); break; }
  }
}
static void wire(const char *text) { feed(text, strlen(text)); }
static void finish(void) {
  for (unsigned i = 0; status() != HTTP_EOF && i < 1000; ++i) drain();
  assert(status() == HTTP_EOF);
}
static void begin(const char *name) { output_length = 0; filename(name); get(); }

int main(void) {
  z80_http_io_init();
  z80_http_io_backend(start, cancel);
  z80_http_io_network_state(true, 1);
  begin("LOVE.BAS");
  assert(starts == 1 && requested.https && requested.port == 443);
  assert(!strcmp(requested.path, "/CaseSensitive/LOVE.BAS"));
  /* Binary data, fragmentation through every header boundary, full count=0,
   * a partial final chunk, and DATA before EOF until the last byte is read. */
  const char *header = "HTTP/1.1 200 OK\r\nContent-Length: 513\r\n\r\n";
  for (const char *p = header; *p; ++p) feed(p, 1);
  uint8_t payload[513];
  for (size_t i = 0; i < sizeof(payload); ++i) payload[i] = (uint8_t)i;
  feed(payload, sizeof(payload)); finish();
  assert(output_length == sizeof(payload) && !memcmp(output, payload, sizeof(payload)));

  begin("http://127.0.0.1:8000/test.bin");
  assert(!requested.https && requested.port == 8000);
  wire("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3;ext=yes\r\na");
  const uint8_t binary[] = {0, 255};
  feed(binary, sizeof(binary));
  wire("\r\n2\r\nbc\r\n0\r\nX-Trailer: yes\r\n\r\n"); finish();
  assert(output_length == 5 && !memcmp(output, "a\0\377bc", 5));

  begin("LOVE.BAS"); wire("HTTP/1.0 200 OK\r\n\r\nhello");
  z80_http_io_remote_close(); finish();
  assert(output_length == 5 && !memcmp(output, "hello", 5));
  begin("EMPTY.BIN"); wire("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n"); finish();
  assert(output_length == 0);

  /* Larger than 64 KiB, no file-size allocation and bounded consumer ring. */
  begin("BIG.BIN"); wire("HTTP/1.1 200 OK\r\nContent-Length: 70000\r\n\r\n");
  for (unsigned i = 0; i < 70000; ++i) { uint8_t b = (uint8_t)i; feed(&b, 1); }
  finish(); assert(output_length == 70000);
  for (unsigned i = 0; i < 70000; ++i) assert(output[i] == (uint8_t)i);

  /* Redirect bodies never become file content. Retain URL case. */
  begin("LOVE.BAS"); unsigned before = starts;
  wire("HTTP/1.1 302 Found\r\nLocation: /Other/LOVE.BAS\r\nContent-Length: 4\r\n\r\n");
  service(); assert(starts == before + 1 && !strcmp(requested.path, "/Other/LOVE.BAS"));
  wire("HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\nx"); finish();
  assert(output_length == 1 && output[0] == 'x');
  begin("LOVE.BAS"); wire("HTTP/1.1 302 Found\r\nLocation: http://example.org/x\r\n\r\n");
  service(); assert(status() == HTTP_ERROR);

  const char *bad[] = {
    "HTTP/1.1 404 Missing\r\nContent-Length: 1\r\n\r\n",
    "HTTP/1.1 200 OK\r\nContent-Length: 1\r\nContent-Length: 1\r\n\r\n",
    "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nTransfer-Encoding: chunked\r\n\r\n",
    "HTTP/1.1 200 OK\r\nContent-Length: 4294967296\r\n\r\n",
    "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\n\r\n",
    "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nx\r\n",
  };
  for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
    begin("BAD.BIN"); wire(bad[i]); service(); assert(status() == HTTP_ERROR);
  }
  begin("SHORT.BIN"); wire("HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nx");
  z80_http_io_remote_close(); service(); assert(status() == HTTP_ERROR);

  /* Cancel while a request is owned by core 1, then immediately reopen. The
   * stale response must be discarded without overwriting the new filename. */
  filename("OLD.BIN"); command(HTTP_REQUEST_CHUNK);
  filename("NEW.BIN"); command(HTTP_REQUEST_CHUNK);
  for (unsigned i = 0; i < 4; ++i) { service(); (void)status(); }
  assert(status() == HTTP_BUSY && !strcmp(requested.path, "/CaseSensitive/NEW.BIN"));
  wire("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n"); finish();
  begin("CANCEL.BIN"); wire("HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\nabc");
  command(HTTP_CLOSE); service(); assert(status() == HTTP_IDLE && !z80_http_io_active());

  command(HTTP_SET_FILENAME);
  for (unsigned i = 0; i < Z80_HTTP_URL_MAX + 2; ++i) z80_http_io_out(HTTP_DATA_PORT, 'a');
  z80_http_io_out(HTTP_DATA_PORT, 0); get(); assert(status() == HTTP_ERROR);
  base = NULL; begin("LOVE.BAS"); assert(status() == HTTP_MISSING_BASE_URL);
  base = ""; begin("LOVE.BAS"); assert(status() == HTTP_MISSING_BASE_URL);
  base = "https://example.org/CaseSensitive/";
  z80_http_io_network_state(false, 2); begin("https://example.org/x"); assert(status() == HTTP_ERROR);

  z80_http_url_t url;
  assert(z80_http_url_parse("HTTP://example.org?x=1", &url) && !strcmp(url.path, "/?x=1"));
  assert(!z80_http_url_parse("http://example.org:65536/x", &url));
  assert(!z80_http_url_parse("http://example.org/x\r\nHeader:bad", &url));
  assert(!z80_http_url_parse("ftp://example.org/x", &url));
  assert(!z80_http_url_parse("https://user@example.org/x", &url));
  assert(cancels > 0);
  puts("PASS: HTTP(S) FT ports, framing, binary data, large streams, cancellation and errors");
}
