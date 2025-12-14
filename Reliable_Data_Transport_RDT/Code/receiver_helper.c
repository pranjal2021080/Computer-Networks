/* receiver_helper.c
   Paste/replace the entire file with this content.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>   /* for uint64_t */
#include <stddef.h>   /* for size_t */
#include "config.h"
#include "receiver.h"

/* The assignment provides these functions (do not redefine):
 *   uint64_t get_seqno(const unsigned char *pkt);
 *   const unsigned char *get_data(const unsigned char *pkt);
 *   void send_ack(uint64_t ackno);
 *   void notify_app(void);
 *
 * We implement rdt_recv() and app_recv() below.
 */

/* -------- RECEIVER: globals and helper types -------- */

typedef struct InSeg {
    uint64_t seq;       /* sequence number of first byte */
    int data_len;       /* data bytes (len minus header) */
    unsigned char *data; /* malloc'd copy of data portion */
    struct InSeg *next;
} InSeg;

static InSeg *in_head = NULL;      /* list of out-of-order segments, sorted by seq */
static uint64_t recv_base = 0;     /* next expected byte sequence number */
static unsigned char *reasm_buf = NULL;  /* buffer of reassembled contiguous data */
static size_t reasm_len = 0;       /* number of bytes ready for app */
static size_t reasm_cap = 0;       /* current allocated capacity */
static uint64_t app_delivered_upto = 0;  /* absolute seq of bytes already given to app */

/* -------- Helper functions -------- */

/* Insert a segment into sorted list; ignore duplicates */
static void in_insert_segment(uint64_t seq, const unsigned char *data, int data_len) {
    if (seq + (uint64_t)data_len <= recv_base) return; /* already delivered */

    InSeg *s = (InSeg*) malloc(sizeof(InSeg));
    if (!s) return;
    s->seq = seq;
    s->data_len = data_len;
    s->data = (unsigned char*) malloc((size_t)data_len);
    if (!s->data) {
        free(s);
        return;
    }
    memcpy(s->data, data, (size_t)data_len);
    s->next = NULL;

    if (!in_head || seq < in_head->seq) {
        s->next = in_head;
        in_head = s;
        return;
    }
    InSeg *p = in_head;
    while (p->next && p->next->seq < seq) p = p->next;
    if (p->next && p->next->seq == seq) {
        /* duplicate segment */
        free(s->data);
        free(s);
        return;
    }
    s->next = p->next;
    p->next = s;
}

/* Ensure reassembly buffer has enough space */
static void ensure_reasm_capacity(size_t need) {
    if (reasm_cap >= need) return;
    size_t newcap = reasm_cap ? reasm_cap * 2 : 4096;
    while (newcap < need) newcap *= 2;
    reasm_buf = (unsigned char*) realloc(reasm_buf, newcap);
    reasm_cap = newcap;
}

/* Move contiguous segments starting at recv_base into reassembly buffer */
static size_t advance_reassembly(void) {
    size_t added = 0;
    while (in_head && in_head->seq == recv_base) {
        ensure_reasm_capacity(reasm_len + (size_t)in_head->data_len);
        memcpy(reasm_buf + reasm_len, in_head->data, (size_t)in_head->data_len);
        reasm_len += (size_t)in_head->data_len;
        added += (size_t)in_head->data_len;
        recv_base += (uint64_t)in_head->data_len;

        InSeg *tmp = in_head;
        in_head = in_head->next;
        free(tmp->data);
        free(tmp);
    }
    return added;
}

/* -------- Main receiver functions -------- */

/* Called when a packet arrives at receiver (pkt includes header) */
void rdt_recv(const unsigned char *pkt, size_t len) {
    uint64_t seq = get_seqno(pkt);
    const unsigned char *data_ptr = get_data(pkt);
    int data_len = (int)len - PACKET_HEADER_LEN;

    if (data_len <= 0) {
        send_ack(recv_base);
        return;
    }

    if (seq + (uint64_t)data_len > app_delivered_upto) {
        in_insert_segment(seq, data_ptr, data_len);
    }

    size_t newly_added = advance_reassembly();

    send_ack(recv_base);

    if (newly_added > 0) {
        notify_app();
    }
}

/* Called by application to fetch up to len bytes into buf; returns number of bytes copied */
size_t app_recv(unsigned char *buf, size_t len) {
    if (reasm_len == 0) return 0;

    size_t tocpy = len < reasm_len ? len : reasm_len;
    memcpy(buf, reasm_buf, tocpy);

    /* shift remaining bytes to front */
    if (tocpy < reasm_len) {
        memmove(reasm_buf, reasm_buf + tocpy, reasm_len - tocpy);
    }
    reasm_len -= tocpy;
    app_delivered_upto += (uint64_t)tocpy;

    return tocpy;
}
