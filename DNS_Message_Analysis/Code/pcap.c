// Source: https://www.devdungeon.com/content/using-libpcap-c

#include <pcap.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netinet/udp.h>

#define MAX_NAME_LEN 280

struct dns_q {
    __be16 type;
    __be16 class;  // 1 for Internet
};

struct dns_header {
    __be16 id;
    __be16 flags;
    __be16 num_ques; // Number of questions
    __be16 num_ans;  // Number of answers RRs
    __be16 num_auth; // Number of authority RRs
    __be16 num_add;  // Number of additional RRs
};

/*
 * print_helper:
 *  - Decodes a possibly compressed DNS name at `start`, using `msg_start` as the beginning of the DNS message.
 *  - Writes the human-readable name (with trailing '.') into `name` (len `len`).
 *  - Returns a pointer to the first byte AFTER the on-wire name bytes (for uncompressed),
 *    or (for compressed) the original position + 2 (since a compression pointer is 2 bytes).
 */
unsigned char* print_helper(unsigned char *start, unsigned char *msg_start, char *name, size_t len)
{
    int jumped = 0;                // Whether we followed a compression pointer
    size_t name_len = 0;           // Output name buffer index
    unsigned char *orig_start = start; // Where the caller should continue (skipping wire bytes)

    while (1) {
        unsigned char byte = *start;

        if (byte == 0) { // End of labels
            if (!jumped) start++; // consume the terminating 0 only if we never jumped
            break;
        }

        // Compression pointer? top two bits set: 11xxxxxx
        if ((byte & 0xC0) == 0xC0) {
            int offset = ((byte & 0x3F) << 8) | *(start + 1);
            start = msg_start + offset; // jump to the target
            if (!jumped) {
                // For the caller, the name field on the wire was just a 2-byte pointer
                orig_start += 2;
                jumped = 1;
            }
            continue;
        }

        // Normal label
        int label_len = byte;
        start++;

        if (name_len > 0 && name_len < len - 1) {
            name[name_len++] = '.';
        }

        for (int i = 0; i < label_len; i++) {
            if (name_len < len - 1) {
                name[name_len++] = start[i];
            }
        }
        start += label_len;
    }

    // Ensure trailing dot
    if (name_len > 0 && name[name_len - 1] != '.' && name_len < len - 1) {
        name[name_len++] = '.';
    }
    name[name_len] = '\0';

    return jumped ? orig_start : start;
}

unsigned char* print_name(unsigned char *start, unsigned char *msg_start)
{
    char name[MAX_NAME_LEN];
    unsigned char *ret = print_helper(start, msg_start, name, MAX_NAME_LEN);
    printf("%s ", name);
    return ret;
}

/* Safe helpers to read big-endian values from possibly unaligned memory */
static inline uint16_t rd16(const unsigned char *p) {
    uint16_t v;
    memcpy(&v, p, sizeof(v));
    return ntohs(v);
}
static inline uint32_t rd32(const unsigned char *p) {
    uint32_t v;
    memcpy(&v, p, sizeof(v));
    return ntohl(v);
}

/*
 * Parse one Resource Record (RR) starting at `start`.
 * - Prints: "type: XX class: YY ttl: ZZZZ len: WW" (hex for type/class/len, hex for ttl to match sample)
 * - Returns pointer to the next RR (i.e., past rdata).
 * Layout on wire after NAME:
 *   2 bytes type, 2 bytes class, 4 bytes ttl, 2 bytes rlen, then rlen bytes rdata.
 */
unsigned char* parse_rr(unsigned char *start, unsigned char *msg_start)
{
    // 1) NAME
    unsigned char *p = print_name(start, msg_start);

    // 2) TYPE, CLASS, TTL, RDLEN
    uint16_t type = rd16(p);
    uint16_t class_ = rd16(p + 2);
    uint32_t ttl = rd32(p + 4);
    uint16_t rlen = rd16(p + 8);

    // Print in hex to mirror sample outputs
    printf("type: %hx class: %hx ttl: %x len: %hx\n", type, class_, ttl, rlen);

    // 3) Skip RDATA
    unsigned char *rdata = p + 10;
    return rdata + rlen;
}

void packet_handler(u_char *args, const struct pcap_pkthdr *header, const u_char *packet)
{
    struct ethhdr *eth = (struct ethhdr *) packet;
    if (ntohs(eth->h_proto) != ETHERTYPE_IP) {
        return;
    }

    struct iphdr *iph = (struct iphdr*)((const unsigned char*)eth + sizeof(struct ethhdr));
    if (iph->protocol != IPPROTO_UDP) {
        return; // not UDP
    }

    struct udphdr *udp = (struct udphdr*)((const unsigned char*)iph + (iph->ihl * 4));

    if (udp->source != htons(53) && udp->dest != htons(53)) {
        return; // not DNS
    }

    unsigned char *msg_start = (unsigned char*)udp + sizeof(struct udphdr);
    struct dns_header *dnshdr = (struct dns_header*)(msg_start);

    int id = (int)ntohs(dnshdr->id);
    int flags = (int)ntohs(dnshdr->flags);
    int num_ques = (int)ntohs(dnshdr->num_ques);
    int num_ans = (int)ntohs(dnshdr->num_ans);
    int num_auth = (int)ntohs(dnshdr->num_auth);
    int num_add = (int)ntohs(dnshdr->num_add);

    printf("\n-------------------- START -----------------------\n");
    printf("\nid: %d flags: %d num_questions: %d num_answers: %d num_authority: %d num_additional: %d\n",
           id, flags, num_ques, num_ans, num_auth, num_add);

    int i;
    unsigned char *start = ((unsigned char*)dnshdr + sizeof(struct dns_header));

    // Question Section
    printf("\nprinting question section\n");
    for (i = 0; i < num_ques; i++) {
        unsigned char *end = print_name(start, msg_start);
        struct dns_q *dq = (struct dns_q*)(end);
        printf("type: %hx class: %hx\n", rd16((unsigned char*)&dq->type), rd16((unsigned char*)&dq->class));
        start = end + sizeof(struct dns_q);
    }

    // Answer Section
    printf("\nprinting answer section\n");
    for (i = 0; i < num_ans; i++) {
        start = parse_rr(start, msg_start);
    }

    // Authority Section
    printf("\nprinting authority section\n");
    for (i = 0; i < num_auth; i++) {
        start = parse_rr(start, msg_start);
    }

    // Additional Section
    printf("\nprinting additional section\n");
    for (i = 0; i < num_add; i++) {
        start = parse_rr(start, msg_start);
    }

    printf("\n-------------------- END -----------------------\n");
}

int main(int argc, char *argv[])
{
    pcap_t *handle;                 /* Session handle */
    char errbuf[PCAP_ERRBUF_SIZE];  /* Error string */
    pcap_if_t *alldevs;
    pcap_if_t *d;

    // Find all devices
    if (pcap_findalldevs(&alldevs, errbuf) == -1) {
        fprintf(stderr, "Error finding devices: %s\n", errbuf);
        return 1;
    }

    if (alldevs == NULL) {
        fprintf(stderr, "no available device\n");
        return 1;
    }

    // Print the list
    printf("Available network interfaces:\n");
    for (d = alldevs; d != NULL; d = d->next) {
        printf("%s", d->name);
        if (d->description) {
            printf(" - %s\n", d->description);
        } else {
            printf(" - No description available\n");
        }
    }

    /* Define the device */
    handle = pcap_open_live(alldevs->name, BUFSIZ, 1, 1000, errbuf);
    if (handle == NULL) {
        fprintf(stderr, "Couldn't open default device: %s\n", alldevs->name);
        pcap_freealldevs(alldevs);
        return 2;
    }
    printf("Could open: %s\n", alldevs->name);

    pcap_loop(handle, 0, packet_handler, NULL);

    /* And close the session */
    pcap_close(handle);
    pcap_freealldevs(alldevs);
    return 0;
}
