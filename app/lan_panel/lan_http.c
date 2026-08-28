/****************************************************************************
 * contest2026_113_veladuxingke/app/lan_panel/lan_http.c
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <unistd.h>

#include "lan_http.h"

#define LAN_HTTP_MAX_HEADERS 4096
#define LAN_HTTP_CHUNK 4096

/* An idle browser pre-connect must not stall the whole panel, so the first
 * byte of a request gets a short deadline while the remainder of a started
 * request gets a longer one.
 */

#define LAN_HTTP_IDLE_MS 1200
#define LAN_HTTP_BODY_MS 4000
#define LAN_HTTP_SEND_MS 4000
#define LAN_HTTP_DRAIN_MS 200

static int lan_http_wait(int fd, int events, int timeout_ms)
{
  struct pollfd pfd;
  int ret;

  pfd.fd = fd;
  pfd.events = events;
  do
    {
      pfd.revents = 0;
      ret = poll(&pfd, 1, timeout_ms);
    }
  while (ret < 0 && errno == EINTR);

  if (ret < 0)
    {
      return -errno;
    }
  return ret == 0 ? -ETIMEDOUT : 0;
}

static ssize_t lan_http_read_some(int fd, char *buffer, size_t size,
                                  int timeout_ms)
{
  ssize_t length;
  int ret;

  for (; ; )
    {
      ret = lan_http_wait(fd, POLLIN, timeout_ms);
      if (ret < 0)
        {
          return ret;
        }

      length = recv(fd, buffer, size, 0);
      if (length >= 0)
        {
          return length;
        }
      if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
        {
          return -errno;
        }
    }
}

static int lan_http_send_all(int fd, const char *data, size_t length)
{
  while (length > 0)
    {
      ssize_t sent = send(fd, data, length, 0);
      if (sent > 0)
        {
          data += sent;
          length -= sent;
          continue;
        }

      if (sent == 0)
        {
          return -EPIPE;
        }
      if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
        {
          return -errno;
        }

      /* A short write must never truncate the reply: wait for room and
       * resume from where the transfer stopped.
       */

      if (lan_http_wait(fd, POLLOUT, LAN_HTTP_SEND_MS) < 0)
        {
          return -ETIMEDOUT;
        }
    }

  return 0;
}

void lan_http_close(int fd)
{
  char discard[256];

  /* Closing with unread data queued makes the stack emit RST, which lets the
   * peer drop a reply it already received.  Half-close, drain briefly, then
   * close so the browser always sees an orderly end of response.
   */

  shutdown(fd, SHUT_WR);
  while (lan_http_wait(fd, POLLIN, LAN_HTTP_DRAIN_MS) == 0)
    {
      ssize_t length = recv(fd, discard, sizeof(discard), 0);
      if (length <= 0)
        {
          break;
        }
    }

  close(fd);
}

static char *lan_http_find_header_end(char *data, size_t length)
{
  size_t i;

  for (i = 3; i < length; i++)
    {
      if (data[i - 3] == '\r' && data[i - 2] == '\n' &&
          data[i - 1] == '\r' && data[i] == '\n')
        {
          return data + i + 1;
        }
    }

  return NULL;
}

static const char *lan_http_find_crlf(const char *cursor,
                                      const char *limit)
{
  while (cursor + 1 < limit)
    {
      if (cursor[0] == '\r' && cursor[1] == '\n')
        {
          return cursor;
        }

      cursor++;
    }

  return NULL;
}

static int lan_http_header_value(const char *headers,
                                 const char *headers_end,
                                 const char *name,
                                 char *value,
                                 size_t value_size)
{
  const char *line = headers;
  size_t name_length = strlen(name);

  while (line < headers_end)
    {
      const char *end = lan_http_find_crlf(line, headers_end);
      const char *colon;
      const char *value_start;
      const char *value_end;
      size_t length;

      if (end == NULL)
        {
          return -EINVAL;
        }

      if (end == line)
        {
          break;
        }

      colon = memchr(line, ':', (size_t)(end - line));
      if (colon != NULL && (size_t)(colon - line) == name_length &&
          strncasecmp(line, name, name_length) == 0)
        {
          value_start = colon + 1;
          while (value_start < end &&
                 (*value_start == ' ' || *value_start == '\t'))
            {
              value_start++;
            }

          value_end = end;
          while (value_end > value_start &&
                 (value_end[-1] == ' ' || value_end[-1] == '\t'))
            {
              value_end--;
            }

          length = (size_t)(value_end - value_start);
          if (length + 1 > value_size)
            {
              return -E2BIG;
            }

          memcpy(value, value_start, length);
          value[length] = '\0';
          return 0;
        }

      line = end + 2;
    }

  return -ENOENT;
}

int lan_http_read_request(int fd, struct lan_http_request_s *request)
{
  char buffer[LAN_HTTP_MAX_HEADERS + 1];
  char *header_end = NULL;
  char *line_end;
  char content_length_text[32];
  size_t used = 0;
  unsigned long content_length = 0;
  ssize_t length;
  int ret;

  if (request == NULL)
    {
      return -EINVAL;
    }
  memset(request, 0, sizeof(*request));

  while (used < LAN_HTTP_MAX_HEADERS)
    {
      length = lan_http_read_some(fd, buffer + used,
                                  LAN_HTTP_MAX_HEADERS - used,
                                  used == 0 ? LAN_HTTP_IDLE_MS :
                                              LAN_HTTP_BODY_MS);
      if (length == 0 || length == -ETIMEDOUT)
        {
          /* Browsers open speculative connections and send nothing on them.
           * Report that as an idle connection so the caller can drop it
           * silently instead of spending a reply on it.
           */

          return used == 0 ? -ENODATA : -ECONNRESET;
        }
      if (length < 0)
        {
          return (int)length;
        }
      used += length;
      buffer[used] = '\0';
      header_end = lan_http_find_header_end(buffer, used);
      if (header_end != NULL)
        {
          break;
        }
    }

  if (header_end == NULL)
    {
      return -E2BIG;
    }

  line_end = strstr(buffer, "\r\n");
  if (line_end == NULL)
    {
      return -EINVAL;
    }
  *line_end = '\0';
  if (sscanf(buffer, "%7s %255s HTTP/1.%*1[01]", request->method,
             request->path) != 2)
    {
      return -EINVAL;
    }

  ret = lan_http_header_value(line_end + 2, header_end, "Authorization",
                              request->authorization,
                              sizeof(request->authorization));
  if (ret < 0 && ret != -ENOENT)
    {
      return ret;
    }

  ret = lan_http_header_value(line_end + 2, header_end, "Content-Length",
                              content_length_text,
                              sizeof(content_length_text));
  if (ret == 0)
    {
      char *end;
      errno = 0;
      content_length = strtoul(content_length_text, &end, 10);
      if (errno != 0 || *end != '\0' || content_length > sizeof(request->body))
        {
          return -E2BIG;
        }
    }
  else if (ret != -ENOENT)
    {
      return ret;
    }

  if (content_length > 0)
    {
      size_t already = used - (size_t)(header_end - buffer);
      if (already > content_length)
        {
          return -EINVAL;
        }
      if (already > 0)
        {
          memcpy(request->body, header_end, already);
        }
      request->body_length = already;
      while (request->body_length < content_length)
        {
          length = lan_http_read_some(fd,
                                      request->body + request->body_length,
                                      content_length - request->body_length,
                                      LAN_HTTP_BODY_MS);
          if (length <= 0)
            {
              return length == 0 ? -ECONNRESET : (int)length;
            }
          request->body_length += length;
        }
    }

  return 0;
}

static const char *lan_http_reason(int status)
{
  switch (status)
    {
      case 200: return "OK";
      case 202: return "Accepted";
      case 204: return "No Content";
      case 400: return "Bad Request";
      case 401: return "Unauthorized";
      case 404: return "Not Found";
      case 405: return "Method Not Allowed";
      case 500: return "Internal Server Error";
      case 503: return "Service Unavailable";
      default:  return "OK";
    }
}

int lan_http_send_response(int fd, int status, const char *type,
                           const char *body, size_t length)
{
  char header[256];
  int header_length;

  header_length = snprintf(header, sizeof(header),
                           "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %lu\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n",
                           status, lan_http_reason(status),
                           type == NULL ? "text/plain" : type,
                           (unsigned long)length);
  if (header_length < 0 || (size_t)header_length >= sizeof(header))
    {
      return -E2BIG;
    }

  if (lan_http_send_all(fd, header, header_length) < 0)
    {
      return -EPIPE;
    }
  return body == NULL || length == 0 ? 0 : lan_http_send_all(fd, body, length);
}

int lan_http_send_file(int fd, const char *path, const char *type)
{
  char buffer[LAN_HTTP_CHUNK];
  struct stat info;
  size_t remaining;
  int file;
  int ret;
  ssize_t length;

  /* Open before measuring.  These files are replaced by rename() while the
   * panel serves them, so a stat()/open() pair can size the header from one
   * generation and stream another, leaving the reply short or long against
   * Content-Length.  fstat() on the open descriptor pins one generation.
   */

  file = open(path, O_RDONLY);
  if (file < 0)
    {
      return lan_http_send_response(fd, 404, "text/plain", "Not found\n", 10);
    }
  if (fstat(file, &info) < 0 || !S_ISREG(info.st_mode))
    {
      close(file);
      return lan_http_send_response(fd, 404, "text/plain", "Not found\n", 10);
    }
  remaining = (size_t)info.st_size;

  {
    char header[256];
    int header_length = snprintf(header, sizeof(header),
                                 "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %lu\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n",
                                 type == NULL ? "application/octet-stream" : type,
                                 (unsigned long)remaining);
    if (header_length < 0 || (size_t)header_length >= sizeof(header))
      {
        close(file);
        return -E2BIG;
      }
    ret = lan_http_send_all(fd, header, header_length);
    if (ret < 0)
      {
        close(file);
        return ret;
      }
  }

  do
    {
      size_t want = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
      if (want == 0)
        {
          break;
        }
      length = read(file, buffer, want);
      if (length > 0)
        {
          remaining -= (size_t)length;
          ret = lan_http_send_all(fd, buffer, length);
        }
      else
        {
          ret = length < 0 ? -errno : 0;
        }
    }
  while (ret == 0 && length > 0);

  close(file);
  if (ret == 0 && remaining > 0)
    {
      /* The header already promised more bytes than the file yielded; the
       * body cannot be completed, so let the caller drop the connection.
       */

      return -EIO;
    }
  return ret;
}
