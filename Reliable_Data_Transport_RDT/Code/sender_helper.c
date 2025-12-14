/* sender_helper.c
   Paste/replace the entire file with this content.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>   /* for uint64_t */
#include <stddef.h>   /* for size_t */
#include "config.h"
#include "sender.h"   /* contains the rdt_send prototype the framework expects */

/* The assignment provides these functions (do not redefine):
 *   void make_pkt(void *buf, uint64_t seqno);
 *   void udt_send(void *pkt, int len);
 *   void start_timer(void);
 *   void stop_timer(void);
 *
 * We implement rdt_send, rdt_recv_base, timeout below.
 */

/* -------- SENDER: globals and helper types -------- */

/* Outstanding (not-yet-acked) segment stored by sender */
typedef struct OutSeg {
    uint64_t seq;      /* sequence number of first data byte in pkt */
    int pkt_len;       /* full packet length (includes header) */
    char *pkt_copy;    /* malloc'd copy of full packet */
    struct OutSeg *next;
} OutSeg;

static OutSeg *out_head = NULL;  /* oldest unacked */
static OutSeg *out_tail = NULL;  /* newest outstanding */
static uint64_t send_base = 0;   /* next byte expected to be acked (cumulative ack value) */
static uint64_t next_seqno = 0;  /* seqno for first byte of next packet to send */
static int timer_running = 0;

/* helper: push a copy of pkt into out buffer */
static void out_push(uint64_t seq, const char *pkt, int pkt_len) {
    OutSeg *s = (OutSeg*) malloc(sizeof(OutSeg));
    if (!s) return; /* allocation failure unlikely, but be defensive */
    s->seq = seq;
    s->pkt_len = pkt_len;
    s->pkt_copy = (char*) malloc((size_t)pkt_len);
    if (!s->pkt_copy) {
        free(s);
        return;
    }
    memcpy(s->pkt_copy, pkt, (size_t)pkt_len);
    s->next = NULL;
    if (out_tail) out_tail->next = s;
    else out_head = s;
    out_tail = s;
}

/* helper: pop and free head node(s) whose data fully acked (ackno is cumulative) */
static void out_pop_acked(uint64_t ackno) {
    while (out_head) {
        int data_len = out_head->pkt_len - PACKET_HEADER_LEN;
        uint64_t seg_end = out_head->seq + (uint64_t)data_len; /* first byte after this seg */
        if (seg_end <= ackno) {
            OutSeg *tmp = out_head;
            out_head = out_head->next;
            if (!out_head) out_tail = NULL;
            free(tmp->pkt_copy);
            free(tmp);
        } else break;
    }
}

/* helper: find the segment that contains send_base (oldest unacked).
   If not found, return out_head */
static OutSeg* out_find_segment_for_sendbase() {
    OutSeg *p = out_head;
    while (p) {
        int dlen = p->pkt_len - PACKET_HEADER_LEN;
        uint64_t seg_end = p->seq + (uint64_t)dlen;
        if (p->seq <= send_base && send_base < seg_end) return p;
        p = p->next;
    }
    return out_head; /* fallback */
}

/* -------- SENDER: implement rdt_send, rdt_recv_base, timeout -------- */

/* The framework/sender.h expects this exact signature:
 *    void rdt_send(const void *msg, size_t len);
 * We make a local writable copy because make_pkt() embeds the seqno into the buffer.
 */
void rdt_send(const void *msg, size_t len) {
    if (len == 0) return;

    /* make sure len fits into int where project APIs expect int packet len */
    int pkt_len = (int) len;

    /* allocate a mutable copy of the provided packet buffer */
    char *pkt = (char*) malloc((size_t)pkt_len);
    if (!pkt) return;
    memcpy(pkt, msg, (size_t)pkt_len);

    /* sequence number for first byte in this packet */
    uint64_t seq = next_seqno;

    /* embed seqno into the packet */
    make_pkt(pkt, seq);

    /* store a copy for retransmission */
    out_push(seq, pkt, pkt_len);

    /* send over unreliable channel */
    udt_send(pkt, pkt_len);

    /* update next_seqno (per-byte numbering) */
    int data_len = pkt_len - PACKET_HEADER_LEN;
    if (data_len > 0) {
        next_seqno += (uint64_t)data_len;
    }

    /* start the timer if not running */
    if (!timer_running) {
        start_timer();
        timer_running = 1;
    }

    /* free the local copy we used for immediate send; out_push stored its own copy */
    free(pkt);
}

/* Called by lower layer when a cumulative ACK (ackno) arrives.
   ackno acknowledges all bytes with sequence number < ackno. */
void rdt_recv_ack(unsigned long long ackno) {
    if (ackno > send_base) {
        send_base = ackno;
        /* remove acknowledged segments from outstanding buffer */
        out_pop_acked(ackno);
    }
    /* manage timer */
    if (send_base == next_seqno) {
        /* everything acknowledged */
        if (timer_running) {
            stop_timer();
            timer_running = 0;
        }
    } else {
        /* still outstanding bytes; restart timer */
        if (timer_running) {
            stop_timer();
            start_timer();
        } else {
            start_timer();
            timer_running = 1;
        }
    }
}

/* Called by timer driver when timeout occurs. Retransmit only the segment containing send_base */
void timeout(void) {
    if (!out_head) return; /* nothing to retransmit */
    OutSeg *seg = out_find_segment_for_sendbase();
    if (!seg) seg = out_head;
    /* retransmit stored packet */
    udt_send(seg->pkt_copy, seg->pkt_len);
    /* restart timer */
    if (timer_running) stop_timer();
    start_timer();
    timer_running = 1;
}
