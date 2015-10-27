#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <winsock.h>
#include <time.h>
#include "win-pcap.h"

#include "ibm.h"
#include "nethandler.h"

#define pcap_sendpacket(A,B,C)			PacketSendPacket(A,B,C)
#define pcap_close(A)					PacketClose(A)
#define pcap_freealldevs(A)				PacketFreealldevs(A)
#define pcap_open(A,B,C,D,E,F)			PacketOpen(A,B,C,D,E,F)
#define pcap_next_ex(A,B,C)				PacketNextEx(A,B,C)
#define pcap_findalldevs_ex(A,B,C,D)	PacketFindALlDevsEx(A,B,C,D)

int (*PacketSendPacket)(pcap_t *, const u_char *, int) = NULL;
void (*PacketClose)(pcap_t *) = NULL;
void (*PacketFreealldevs)(pcap_if_t *) = NULL;
pcap_t* (*PacketOpen)(char const *,int,int,int,struct pcap_rmtauth *,char *) = NULL;
int (*PacketNextEx)(pcap_t *, struct pcap_pkthdr **, const u_char **) = NULL;
int (*PacketFindALlDevsEx)(char *, struct pcap_rmtauth *, pcap_if_t **, char *) = NULL;

uint8_t ethif;
int inum;

static HINSTANCE pcapinst = NULL;

void closepcap();

void initpcap()
{
        pcap_if_t *alldevs;
        pcap_if_t *currentdev = NULL;
        char errbuf[PCAP_ERRBUF_SIZE];
        int load_success;

        load_success = 1;

		if(!network_card_current) {
			load_success = 0;
			return;
		}

		// init the library
		pcapinst = LoadLibrary("WPCAP.DLL");
		if(pcapinst==NULL) {
#ifndef RELEASE_BUILD
			pclog("WinPcap has to be installed for the NIC to work.\n");
#endif
			load_success = 0;
			return;
		}
		FARPROC psp;

		psp = GetProcAddress(pcapinst,"pcap_sendpacket");
		if(!PacketSendPacket) PacketSendPacket =
			(int (__cdecl *)(pcap_t *,const u_char *,int))psp;

		psp = GetProcAddress(pcapinst,"pcap_close");
		if(!PacketClose) PacketClose =
			(void (__cdecl *)(pcap_t *)) psp;

		psp = GetProcAddress(pcapinst,"pcap_freealldevs");
		if(!PacketFreealldevs) PacketFreealldevs =
			(void (__cdecl *)(pcap_if_t *)) psp;

		psp = GetProcAddress(pcapinst,"pcap_open");
		if(!PacketOpen) PacketOpen =
			(pcap_t* (__cdecl *)(char const *,int,int,int,struct pcap_rmtauth *,char *)) psp;

		psp = GetProcAddress(pcapinst,"pcap_next_ex");
		if(!PacketNextEx) PacketNextEx =
			(int (__cdecl *)(pcap_t *, struct pcap_pkthdr **, const u_char **)) psp;

		psp = GetProcAddress(pcapinst,"pcap_findalldevs_ex");
		if(!PacketFindALlDevsEx) PacketFindALlDevsEx =
			(int (__cdecl *)(char *, struct pcap_rmtauth *, pcap_if_t **, char *)) psp;

		if(PacketFindALlDevsEx==0 || PacketNextEx==0 || PacketOpen==0 ||
			PacketFreealldevs==0 || PacketClose==0 || PacketSendPacket==0) {
#ifndef RELEASE_BUILD
			pclog("Wrong WinPcap version or something\n");
#endif
			load_success = 0;
			return;
		}

#ifndef WIN32
		if (pcap_findalldevs_ex(PCAP_SRC_IF_STRING, NULL, &alldevs, errbuf) == -1)
#else
		if (pcap_findalldevs(&alldevs, errbuf) == -1)
#endif
 		{
#ifndef RELEASE_BUILD
			pclog("Cannot enumerate network interfaces: %s\n", errbuf);
#endif
			load_success = 0;
			return;
		}
			int i = 0;
			for(currentdev=alldevs; currentdev!=NULL; currentdev=currentdev->next) {
			i++;
			if (ethif==255) {
#ifndef RELEASE_BUILD
					pclog ("%d. %s", i, currentdev->name);
					if (currentdev->description) {
							pclog (" (%s)\n", currentdev->description);
						}
					else {
							pclog (" (No description available)\n");
						}
#endif
				}
		}

		if(i==0) {
#ifndef RELEASE_BUILD
			pclog("Unable to find network interface\n");
#endif
			load_success = 0;
			pcap_freealldevs(alldevs);
			return;
		}

#ifndef RELEASE_BUILD
	pclog ("\n");
#endif
	
	if (ethif==255) exit (0);
	else inum = ethif;
#ifndef RELEASE_BUILD
	pclog ("Using network interface %u.\n", ethif);
#endif
	
	if (inum < 1 || inum > i) {
#ifndef RELEASE_BUILD
			pclog ("\nInterface number out of range.\n");
#endif
			/* Free the device list */
			load_success = 0;
			pcap_freealldevs (alldevs);
			return;
		}

	/* Jump to the selected adapter */
	for (currentdev=alldevs, i=0; i< inum-1 ; currentdev=currentdev->next, i++);

		// attempt to open it
#ifndef WIN32
		if ( (adhandle= pcap_open(
			currentdev->name, // name of the device
            65536,            // portion of the packet to capture
                              // 65536 = whole packet
            PCAP_OPENFLAG_PROMISCUOUS,    // promiscuous mode
            -1,             // read timeout
            NULL,             // authentication on the remote machine
            errbuf            // error buffer
            ) ) == NULL)
#else
		/*pcap_t *pcap_open_live(const char *device, int snaplen,
               int promisc, int to_ms, char *errbuf)*/
		if ( (adhandle= pcap_open_live(
			currentdev->name, // name of the device
            65536,            // portion of the packet to capture
                              // 65536 = whole packet
            1,    // promiscuous mode
            -1,             // read timeout
            errbuf            // error buffer
            ) ) == NULL)

#endif
        {
#ifndef RELEASE_BUILD
            pclog("\nUnable to open the interface: %s.", errbuf);
#endif
        	pcap_freealldevs(alldevs);
			load_success = 0;
			return;
		}
#ifndef RELEASE_BUILD
        pclog ("\nEthernet bridge on %s...\n", currentdev->description);
#endif
		
		pcap_freealldevs(alldevs);
#ifndef WIN32
		pcap_setnonblock(adhandle,1,errbuf);
#endif
}

void closepcap()
{
		if(adhandle) pcap_close(adhandle);
		adhandle=0;
		FreeLibrary(pcapinst);
}
