//WinPCap//
#define HAVE_REMOTE
#include <pcap.h>

pcap_t *adhandle;
const u_char *pktdata;
struct pcap_pkthdr *hdr;
