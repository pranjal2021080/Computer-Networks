// Source: https://www.devdungeon.com/content/using-libpcap-c

#include <pcap.h>
#include <stdio.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <netinet/tcp.h>
#include <string.h>
#include <assert.h>

#include <stdio.h>
#include <netinet/ip.h>
#include <arpa/inet.h> // Required for ntohl()


// options points to the first byte of TCP header options
// len is the length of the TCP header options
// print TCP SACK acknowledgements in this routine
void print_tcp_options(const u_char *options, int len) {
    int i = 0;
    // Loop through the entire options header, making sure not to go past its length
    while (i < len) {
        // The first byte is the 'kind'
        unsigned char kind = options[i];

        // Handle each kind differently
        if (kind == 0) {
            // Kind 0 means End of Option List. Stop parsing.
            // [cite: 6, 19]
            break;
        } else if (kind == 1) {
            // Kind 1 is a No-Operation (NOP) for padding. It's just one byte.
            // [cite: 7, 18]
            i++; // Skip this byte
            continue;
        } else {
            // For any other kind, the next byte is the length.
            // [cite: 9]
            // Be careful to check if there's enough space for the length byte.
            if (i + 1 >= len) break;
            unsigned char option_len = options[i + 1];

            // Safety check: ensure the reported length is valid
            if (option_len < 2) break; // Invalid length

            if (kind == 5) {
                // This is the SACK option!
                // [cite: 13]
                printf("SACK Ranges: ");

                // The number of ranges is (length - 2) / 8
                // [cite: 15]
                // Each range is 8 bytes (two 4-byte sequence numbers)
                // [cite: 14]
                int num_ranges = (option_len - 2) / 8;

                // Loop through each SACK range
                for (int j = 0; j < num_ranges; j++) {
                    // Calculate the starting position of the current range's data
                    // 2 bytes for kind and len, then j * 8 for previous ranges
                    const unsigned int* sack_block = (const unsigned int*)(options + i + 2 + (j * 8));
                    
                    // The sequence numbers are in Network Byte Order. Convert them to Host Byte Order for printing.
                    unsigned int left_edge = ntohl(sack_block[0]);
                    unsigned int right_edge = ntohl(sack_block[1]);

                    printf("[%u - %u] ", left_edge, right_edge);
                }
                printf("\n");
		fflush(stdout);
            }
            
            // To find the next kind, skip the current one entirely using its length.
            // [cite: 11]
            i += option_len;
        }
    }
}

void packet_handler(u_char *args, const struct pcap_pkthdr *header, const u_char *packet)
{
	struct ethhdr *eth;
	eth = (struct ethhdr *) packet;
	if (ntohs(eth->h_proto) != ETHERTYPE_IP) {
		return;
	}

	struct iphdr *iph = (struct iphdr*)((char*)eth + sizeof(struct ethhdr));

	if (iph->protocol != IPPROTO_TCP) {
		// not a TCP packet
		return;
	}
	struct tcphdr *tcp = (struct tcphdr*)((char*)iph + (iph->ihl * 4));
	int length = tcp->doff * 4;
	if (length > 20) {
		unsigned char *options = (((unsigned char*)tcp) + 20);
		print_tcp_options(options, length - 20);
	}
}

int main(int argc, char *argv[])
{
	pcap_t *handle;			/* Session handle */
	char errbuf[PCAP_ERRBUF_SIZE];	/* Error string */
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
		}
		else {
			printf(" - No description available\n");
		}
	}
	/* Define the device */
	handle = pcap_open_live(alldevs->name, BUFSIZ, 1, 1000, errbuf);
	if (handle == NULL) {
		fprintf(stderr, "Couldn't open default device: %s\n", alldevs->name);
		pcap_freealldevs(alldevs);
		return(2);
	}
	printf("Could open: %s\n", alldevs->name);

	pcap_loop(handle, 0, packet_handler, NULL);

	/* And close the session */
	pcap_close(handle);
	pcap_freealldevs(alldevs);
	return(0);
}
