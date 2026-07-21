#include <ethernet_manager.h>
#include "W5500.h"
#include "W5500_spi.h"
#include "wizchip_conf.h"
#include "socket.h"
#include "DHCP/dhcp.h"
#include "DNS/dns.h"
#include <stdbool.h>
#include <stdio.h>
#include "stm32f4xx_hal.h"

//#include "SSLInterface.h"
#define SOCKET_ID_FOR_DHCP 7
#define SOCKET_ID_FOR_DNS 6
static bool dhcp_success=false;
#define DHCP_BUFF_SIZE	2048
static char remote_host[] = "test.iotence.com";

static uint8_t dhcp_buff[DHCP_BUFF_SIZE];
static uint8_t dns_buff[MAX_DNS_BUF_SIZE];

static uint8_t dns_ip[4]={8,8,8,8};
static wiz_NetInfo gWIZNETINFO = {
		.mac = { 0x80, 0x34, 0x28, 0x74, 0xA5, 0xCB },//MSB - LSB
		.ip ={ 192, 168, 0, 112 },
		.sn = { 255, 255, 255, 0 },
		.gw ={ 192, 168, 0, 1 },
		.dns = { 8, 8, 8, 8 },
		.dhcp = NETINFO_STATIC};

static void DisplayNetworkConfigurations()
{
	ctlnetwork(CN_GET_NETINFO, (void*) &gWIZNETINFO);

	printf(" MAC : %02X:%02X:%02X:%02X:%02X:%02X\r\n", gWIZNETINFO.mac[0], gWIZNETINFO.mac[1], gWIZNETINFO.mac[2], gWIZNETINFO.mac[3], gWIZNETINFO.mac[4], gWIZNETINFO.mac[5]);
	printf(" IP : %d.%d.%d.%d\r\n", gWIZNETINFO.ip[0], gWIZNETINFO.ip[1], gWIZNETINFO.ip[2], gWIZNETINFO.ip[3]);
	printf(" GW : %d.%d.%d.%d\r\n", gWIZNETINFO.gw[0], gWIZNETINFO.gw[1], gWIZNETINFO.gw[2], gWIZNETINFO.gw[3]);
	printf(" SN : %d.%d.%d.%d\r\n", gWIZNETINFO.sn[0], gWIZNETINFO.sn[1], gWIZNETINFO.sn[2], gWIZNETINFO.sn[3]);
	printf("=======================================\r\n");
}

static void OnDHCPIPAssigned(void)
{
	printf("IP Obtained: \r\n");

	getIPfromDHCP(gWIZNETINFO.ip);
	getGWfromDHCP(gWIZNETINFO.gw);
	getSNfromDHCP(gWIZNETINFO.sn);
	getDNSfromDHCP(gWIZNETINFO.dns);

	// Validate and fix subnet mask if incorrect
	// For class C private networks (192.168.x.x), SN should be 255.255.255.0
	if (gWIZNETINFO.ip[0] == 192 && gWIZNETINFO.ip[1] == 168)
	{
		if (gWIZNETINFO.sn[0] != 255 || gWIZNETINFO.sn[1] != 255 || 
		    gWIZNETINFO.sn[2] != 255 || gWIZNETINFO.sn[3] != 0)
		{
			printf("WARNING: Invalid subnet mask detected from DHCP!\r\n");
			printf("Correcting SN from %d.%d.%d.%d to 255.255.255.0\r\n", 
			       gWIZNETINFO.sn[0], gWIZNETINFO.sn[1], gWIZNETINFO.sn[2], gWIZNETINFO.sn[3]);
			gWIZNETINFO.sn[0] = 255;
			gWIZNETINFO.sn[1] = 255;
			gWIZNETINFO.sn[2] = 255;
			gWIZNETINFO.sn[3] = 0;
		}
	}

	gWIZNETINFO.dhcp = NETINFO_DHCP;
	ctlnetwork(CN_SET_NETINFO, (void*) &gWIZNETINFO);

	dhcp_success = true;
	DisplayNetworkConfigurations();
}


// static void PHYStatusCheck(void)
// {
//   printf("PHYStatusCheck: not implemented\r\n");
// }

static bool PHYStatusCheck(void)
{
	uint8_t tmp;
	uint32_t start_ms = HAL_GetTick();

	printf("\r\nChecking Ethernet Cable Presence ...");
	while (1)
	{
		ctlwizchip(CW_GET_PHYLINK, (void*) &tmp);

		if (tmp != PHY_LINK_OFF)
		{
			printf("Good! Cable got connected!\r\n");
			return true;
		}

		if ((HAL_GetTick() - start_ms) > 10000u)
		{
			printf("NO Cable Connected or link not established!\r\n");
			return false;
		}

		printf("NO Cable Connected!");
		HAL_Delay(500);
	}
}

// static void PrintPHYConf(void)
// {
//   printf("PrintPHYConf: not implemented\r\n");
// }


static void PrintPHYConf(void)
{
	wiz_PhyConf phyconf;

	ctlwizchip(CW_GET_PHYCONF, (void*) &phyconf);

	if(phyconf.by==PHY_CONFBY_HW)
	{
		printf("\r\nPHY Configured by Hardware Pins");
	}
	else
	{
		printf("\r\nPHY Configured by Registers");
	}

	if(phyconf.mode==PHY_MODE_AUTONEGO)
	{
		printf("\r\nAutonegotiation Enabled");
	}
	else
	{
		printf("\r\nAutonegotiation NOT Enabled");
	}

	if(phyconf.duplex==PHY_DUPLEX_FULL)
	{
		printf("\r\nDuplex Mode: Full");
	}
	else
	{
		printf("\r\nDuplex Mode: Half");
	}

	if(phyconf.speed==PHY_SPEED_10)
	{
		printf("\r\nSpeed: 10Mbps\r\n");
	}
	else
	{
		printf("\r\nSpeed: 100Mbps\r\n");
	}
}


EthernetManagerError ethernet_init(){
  W5500_Init();
  printf("ethernet init successful\n");
   return EthernetManagerOK;
  // printf("free heap : %d", a.fordblks);

}
EthernetManagerError ethernet_connect(){

  //Configure PHY by software
  wiz_PhyConf phyconf;
  phyconf.by=PHY_CONFBY_SW;
  phyconf.duplex=PHY_DUPLEX_FULL;
  phyconf.speed=PHY_SPEED_100;
  phyconf.mode=PHY_MODE_AUTONEGO;//best to go with auto-negotiation

  ctlwizchip(CW_SET_PHYCONF, (void*) &phyconf);
  //*** End Phy Configuration
  if (!PHYStatusCheck())
  {
    return EthernetManagerPHYError;
  }
  PrintPHYConf();

  // Initialize DHCP with callback BEFORE init
  dhcp_success = false;
  reg_dhcp_cbfunc(OnDHCPIPAssigned, NULL, NULL);
  DHCP_init(SOCKET_ID_FOR_DHCP, dhcp_buff);

  // Initialize DNS only after W5500 and PHY are ready
  DNS_init(SOCKET_ID_FOR_DNS, dns_buff);

  printf("Obtaining IP address ...\r\n");

  uint32_t start_ms = HAL_GetTick();
  uint32_t last_dhcp_tick_ms = start_ms;
  while(!dhcp_success)
  {
    uint8_t dhcp_status = DHCP_run();

    if ((HAL_GetTick() - last_dhcp_tick_ms) >= 1000u)
    {
      DHCP_time_handler();
      last_dhcp_tick_ms = HAL_GetTick();
    }

    if (dhcp_status == DHCP_FAILED)
    {
      printf("DHCP failed - retrying...\r\n");
      DHCP_init(SOCKET_ID_FOR_DHCP, dhcp_buff);
      start_ms = HAL_GetTick();
      last_dhcp_tick_ms = start_ms;
    }

    if ((HAL_GetTick() - start_ms) > 20000u)
    {
      printf("DHCP timeout - retrying...\r\n");
      start_ms = HAL_GetTick();
      last_dhcp_tick_ms = start_ms;
      DHCP_init(SOCKET_ID_FOR_DHCP, dhcp_buff);
    }

    HAL_Delay(10);
  }
  printf("DHCP IP assigned successfully\r\n");
  DisplayNetworkConfigurations();
  return EthernetManagerOK;
}

bool ethernet_dns_resolve(const char* hostname, uint8_t ip_out[4])
{
    int8_t ret;

    if (!hostname || !ip_out) {
        return false;
    }

    /* DNS_run blocks until resolution completes or times out. */
    ret = DNS_run(dns_ip, (uint8_t*)hostname, ip_out);
    if (ret == 1) {
        printf("DNS resolved %s -> %d.%d.%d.%d\r\n",
               hostname, ip_out[0], ip_out[1], ip_out[2], ip_out[3]);
        return true;
    }

    printf("DNS resolution failed for %s\r\n", hostname);
    return false;
}