#define _POSIX_C_SOURCE 200809L
#include "record.h"
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>

void record_init(pf_record_t *rec, uint32_t seq, const char *payload)
{
    if (!rec) return;
    rec->seq = seq;
    if (payload) {
        size_t len = strlen(payload);
        if (len >= PF_RECORD_MAX_PAYLOAD) {
            len = PF_RECORD_MAX_PAYLOAD - 1;
        }
        memcpy(rec->payload, payload, len);
        rec->payload[len] = '\0';
        rec->length = (uint32_t)len;
    } else {
        rec->payload[0] = '\0';
        rec->length = 0;
    }
}

// Helper to write exact bytes with retry on EINTR and EPIPE detection
static pf_rec_status_t write_all(int fd, const void *buf, size_t total)
{
    const char *ptr = (const char *)buf;
    size_t left = total;

    while (left > 0) {
        ssize_t n = write(fd, ptr, left);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EPIPE) {
                return PF_REC_EPIPE;
            }
            return PF_REC_ERR;
        }
        if (n == 0) {
            return PF_REC_ERR;
        }
        left -= (size_t)n;
        ptr += n;
    }
    return PF_REC_OK;
}

// Helper to read exact bytes with retry on EINTR
static pf_rec_status_t read_all(int fd, void *buf, size_t total)
{
    char *ptr = (char *)buf;
    size_t left = total;

    while (left > 0) {
        ssize_t n = read(fd, ptr, left);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return PF_REC_ERR;
        }
        if (n == 0) {
            // End of stream if we haven't read anything yet
            return (left == total) ? PF_REC_EOF : PF_REC_ERR;
        }
        left -= (size_t)n;
        ptr += n;
    }
    return PF_REC_OK;
}

pf_rec_status_t record_write(int fd, const pf_record_t *rec)
{
    if (!rec || fd < 0) return PF_REC_ERR;

    // Write header: seq (4 bytes), length (4 bytes)
    uint32_t header[2] = { rec->seq, rec->length };
    pf_rec_status_t st = write_all(fd, header, sizeof(header));
    if (st != PF_REC_OK) return st;

    // Write payload
    if (rec->length > 0) {
        st = write_all(fd, rec->payload, rec->length);
        if (st != PF_REC_OK) return st;
    }

    return PF_REC_OK;
}

pf_rec_status_t record_read(int fd, pf_record_t *rec)
{
    if (!rec || fd < 0) return PF_REC_ERR;

    // Read header: seq, length
    uint32_t header[2];
    pf_rec_status_t st = read_all(fd, header, sizeof(header));
    if (st != PF_REC_OK) return st;

    rec->seq = header[0];
    rec->length = header[1];

    if (rec->length >= PF_RECORD_MAX_PAYLOAD) {
        return PF_REC_ERR; // Payload size exceeds maximum buffer
    }

    if (rec->length > 0) {
        st = read_all(fd, rec->payload, rec->length);
        if (st != PF_REC_OK) return st;
    }
    rec->payload[rec->length] = '\0';

    return PF_REC_OK;
}

pf_rec_status_t record_read_line(int fd, char *buffer, size_t max_len)
{
    if (!buffer || max_len == 0 || fd < 0) return PF_REC_ERR;

    size_t idx = 0;
    while (idx < max_len - 1) {
        char ch;
        ssize_t n = read(fd, &ch, 1);
        if (n < 0) {
            if (errno == EINTR) continue;
            return PF_REC_ERR;
        }
        if (n == 0) {
            if (idx == 0) return PF_REC_EOF;
            break;
        }
        buffer[idx++] = ch;
        if (ch == '\n') break;
    }
    buffer[idx] = '\0';
    return PF_REC_OK;
}

pf_rec_status_t record_write_line(int fd, const char *buffer)
{
    if (!buffer || fd < 0) return PF_REC_ERR;
    size_t len = strlen(buffer);
    return write_all(fd, buffer, len);
}
