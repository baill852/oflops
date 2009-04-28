#include "config.h"
#include <assert.h>
#include <pcap.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <netinet/in.h>

#include <linux/if_arp.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>

#include "test_module.h"
#include "utils.h"


/***********************************************************************
 * hook for the test module to signal that the test is done
 **/

int oflops_end_test(struct oflops_context *ctx)
{
	ctx->should_end = 1;
	return 0;
}

/**********************************************************************
 * hook for the test module to get access to a raw file descriptor bound
 * 	to the data channel's device
 **/

int oflops_get_channel_raw_fd(struct oflops_context * ctx, oflops_channel_name ch)
{
	struct ifreq ifr;
	struct channel_info * ch_info;
	if(ch >= ctx->n_channels)
		return -1;	// no such channel
	ch_info = &ctx->channels[ch];
	if(ch_info->raw_sock != -1)	// already allocated?
		return ch_info->raw_sock;
	// else, setup raw socket
	ch_info->raw_sock = socket(AF_PACKET,SOCK_RAW, htons(ETH_P_ALL));
	if( ch_info->raw_sock == -1)
		perror_and_exit("raw socket(AF_PACKET,SOCK_RAW,htons(ETH_P_ALL))",1);
	// bind to a specific port
	strncpy(ifr.ifr_name,ch_info->dev,IFNAMSIZ);
	if( ioctl( ch_info->raw_sock, SIOCGIFINDEX, &ifr)  == -1 )
		perror_and_exit("ioctl() bind to dev",1);
	return ch_info->raw_sock;
}

/**********************************************************************
 * hook for the test module to get access to a udp file descriptor bound
 * 	to the data channel's device
 **/
int oflops_get_channel_fd(struct oflops_context * ctx, oflops_channel_name ch)
{
	struct ifreq ifr;
	struct channel_info * ch_info;
	if(ch >= ctx->n_channels)
		return -1;	// no such channel
	ch_info = &ctx->channels[ch];
	if(ch_info->sock != -1)	// already allocated?
		return ch_info->sock;
	// else, setup raw socket
	ch_info->sock = socket(AF_INET,SOCK_DGRAM,0);	// UDP socket
	if( ch_info->sock == -1)
		perror_and_exit("udp socket(AF_INET,SOCK_DGRAM,0)",1);
	// bind to a specific port
	strncpy(ifr.ifr_name,ch_info->dev,IFNAMSIZ);
	if( ioctl( ch_info->sock, SIOCGIFINDEX, &ifr)  == -1 )
		perror_and_exit("ioctl() bind to dev",1);
	return ch_info->sock;
}

/***************************************************************************
 * hook for the test module to schedule an timer_event to be called back into the module
 **/

int oflops_schedule_timer_event(struct oflops_context *ctx, struct timeval *tv, void * arg)
{
	return wc_event_add(ctx->timers, NULL, arg, *tv);
}


/*****************************************************************************
 * hook for the test module to send an openflow mesg across the control channel 
 * 	to the switch
 * 	FIXME: assert()'s that the message doesn't block -- if this is a problem
 * 	we need to implement some buffering and mod the select() call to open for
 * 	writing
 **/
int oflops_send_of_mesg(struct oflops_context *ctx, struct ofp_header *ofph)
{
	int len = ntohs(ofph->length);
	int err;

	err = write(ctx->control_fd, ofph, len);
	if(err<0)
		perror_and_exit("send_of_mesg: write()",1);
	if( err < len)
	{
		fprintf(stderr, "Short write on control channel (%d < %d) -- FIXME!", err, len);
		abort();
	}
	return err;
}

/********************************************************************************
 * hook for the test module to send a raw message out a certain data channel
 * 	here, "raw" means with ethernet header
 **/

int oflops_send_raw_mesg(struct oflops_context *ctx, oflops_channel_name ch, void * msg, int len)
{
	struct sockaddr_ll socket_address;
	int send_result = 0;
	int sock = oflops_get_channel_raw_fd(ctx,ch);

	bzero(&socket_address,sizeof(socket_address));
	socket_address.sll_family   = PF_PACKET;
	socket_address.sll_protocol = htons(ETH_P_ALL);
	/*index of the network device
	 *          * see full code later how to retrieve it*/
	socket_address.sll_ifindex  = ctx->channels[ch].ifindex;
	/********************* do we need any of this?
		socket_address.sll_hatype   = ARPHRD_ETHER; don't need?
		socket_address.sll_halen    = ETH_ALEN;
		socket_address.sll_pkttype  = PACKET_OTHERHOST;
		*/

	/*send the packet*/
	send_result = sendto(sock, msg, len, 0,
			(struct sockaddr*)&socket_address, sizeof(socket_address));
	if (send_result == -1)
		perror("oflops_send_raw_mesg(): sendto()");
	return send_result;
}

/**************************************************************************************
 * hook to get high accuracy pcap timestamp for this data
 * @return zero if not found, one if found
 **/
int oflops_get_timestamp(struct oflops_context * ctx, void * data, int len, struct pcap_pkthdr * hdr, oflops_channel_name ofc)
{
	channel_info * ch  = &ctx->channels[ofc];
	assert(ch->timestamps);
	return ptrack_lookup(ch->timestamps,data,len,hdr);
}

