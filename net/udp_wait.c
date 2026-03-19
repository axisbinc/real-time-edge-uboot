// SPDX-License-Identifier: GPL-2.0+
/*
 * UDP wait trigger - waits for a UDP packet to trigger boot
 */

#include <common.h>
#include <command.h>
#include <env.h>
#include <net.h>
#include <net/udp_wait.h>

/* Configurable parameters */
int udp_wait_port;
long udp_wait_timeout;

static int udp_wait_our_port;
static bool udp_wait_packet_received; /* Track if we've processed a packet */

static void udp_wait_send_ack(struct in_addr dest, int dport, int sport)
{
	uchar *pkt;
	const char *ack_msg = "ACK";
	int len = 3;

	pkt = net_tx_packet + net_eth_hdr_size() + IP_UDP_HDR_SIZE;
	memcpy(pkt, ack_msg, len);

	net_send_udp_packet(net_server_ethaddr, dest, dport, sport, len);
}

static void udp_wait_timeout_handler(void)
{
	puts("UDP wait: timeout\n");
	net_set_state(NETLOOP_FAIL);
}

/*
 * UDP wait packet handler
 * Expected payload format: "serverip:port:bootfile"
 * Example: "192.168.1.100:69:zImage"
 */
static void udp_wait_handler(uchar *pkt, unsigned dest, struct in_addr sip,
			     unsigned src, unsigned len)
{
	char buf[256];
	char *p, *handshake_str, *serverip_str, *port_str, *bootfile_str;
	char tmp[22];

	/* Check if packet is for our listening port */
	if (dest != udp_wait_our_port)
		return;

	/* Only process the FIRST valid packet */
	if (udp_wait_packet_received) {
		printf("UDP wait: already processed a packet, ignoring\n");
		return;
	}

	if (len == 0 || len >= sizeof(buf)) {
		puts("UDP wait: invalid packet length\n");
		return;
	}

	/* Copy payload to buffer for parsing */
	memcpy(buf, pkt, len);
	buf[len] = '\0';

	printf("UDP wait: received trigger from %pI4:%d\n", &sip, src);

	/* Parse payload: handshake:serverip:port:bootfile using strchr */
	/* Set handshake to "edcbrd01" for test purposes */
	handshake_str = buf;
	p = strchr(buf, ':');
	if (p) {
		*p = '\0';
		if(strcmp(handshake_str, HANDSHAKE_STR) != 0) {
			puts("UDP wait: invalid handshake\n");
			return;
		}
		serverip_str = p + 1;
		p = strchr(serverip_str, ':');
		if (p) {
			*p = '\0';
			port_str = p + 1;
			p = strchr(port_str, ':');
			if (p) {
				*p = '\0';
				bootfile_str = p + 1;
			} else {
				bootfile_str = NULL;
			}
		} else {
			port_str = NULL;
			bootfile_str = NULL;
		}
	}
	else {
		puts("UDP wait: No server IP provided\n");
		return;
	}

	/* Set serverip from packet source if not provided in payload */
	if (serverip_str && *serverip_str) {
		env_set("serverip", serverip_str);
	} else {
		ip_to_string(sip, tmp);
		env_set("serverip", tmp);
	}

	/* Set port if provided */
	if (port_str && *port_str)
		env_set("tftpport", port_str);

	/* Set bootfile if provided */
	if (bootfile_str && *bootfile_str)
		env_set("bootfile", bootfile_str);

	/* Store trigger source info */
	ip_to_string(sip, tmp);
	env_set("trigger_srcip", tmp);
	snprintf(tmp, sizeof(tmp), "%d", src);
	env_set("trigger_srcport", tmp);

	/* Send ACK packet back to sender */
	udp_wait_send_ack(sip, src, udp_wait_our_port);

	printf("UDP wait: trigger accepted, serverip=%s\n", env_get("serverip"));

	/* Mark that we've successfully processed a packet */
	udp_wait_packet_received = true;

	/* Cancel timeout handler - we got what we need, exit immediately */
	net_set_timeout_handler(0, NULL);

	net_set_state(NETLOOP_SUCCESS);
}

int udp_wait_prereq(void *data)
{
	if (udp_wait_port <= 0 || udp_wait_port > 65535) {
		puts("UDP wait: invalid port\n");
		return 1;
	}

	return 0;
}

int udp_wait_start(void *data)
{
	udp_wait_our_port = udp_wait_port;
	udp_wait_packet_received = false; /* Reset flag for new wait */

	printf("UDP wait: listening on port %d (timeout %lu ms)\n",
	       udp_wait_our_port, udp_wait_timeout);

	net_set_timeout_handler(udp_wait_timeout, udp_wait_timeout_handler);
	net_set_udp_handler(udp_wait_handler);
	memset(net_server_ethaddr, 0, sizeof(net_server_ethaddr));

	return 0;
}
