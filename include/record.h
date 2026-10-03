#ifndef PIPEFLOW_RECORD_H
#define PIPEFLOW_RECORD_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define PF_RECORD_MAX_PAYLOAD 4096

// Return codes for record I/O operations
typedef enum {
    PF_REC_OK = 0,
    PF_REC_EOF = 1,
    PF_REC_EPIPE = 2,
    PF_REC_ERR = -1
} pf_rec_status_t;

// Frame-based record structure for stream processing
typedef struct {
    uint32_t seq;                              // Sequence number
    uint32_t length;                           // Payload byte length
    char payload[PF_RECORD_MAX_PAYLOAD];       // Payload data
} pf_record_t;

// Initialize a record with sequence number and string payload
void record_init(pf_record_t *rec, uint32_t seq, const char *payload);

// Write a framed record to fd, handling partial writes and EPIPE cleanly
pf_rec_status_t record_write(int fd, const pf_record_t *rec);

// Read a framed record from fd, returns PF_REC_EOF on stream close
pf_rec_status_t record_read(int fd, pf_record_t *rec);

// Read/write line-based records (for interoperability with standard Unix tools)
pf_rec_status_t record_read_line(int fd, char *buffer, size_t max_len);
pf_rec_status_t record_write_line(int fd, const char *buffer);

#endif // PIPEFLOW_RECORD_H
