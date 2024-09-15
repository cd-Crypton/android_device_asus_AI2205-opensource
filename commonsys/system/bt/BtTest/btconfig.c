/*
 * Copyright (c) 2013,2016 The Linux Foundation. All rights reserved.
 */

//#define hcicmd_debug
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <dlfcn.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <signal.h>
#include <time.h>
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <cutils/sockets.h>
#include <linux/un.h>
#include <sys/time.h>
#include <linux/types.h>
#include <endian.h>
#include <byteswap.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdbool.h>
#include <utils/Log.h>

#ifdef ANDROID
#include <cutils/properties.h>
#include <termios.h>
#include "bt_vendor_lib.h"
#else
#include <sys/termios.h>
#include <sys/ioctl.h>
#include <limits.h>
#endif

#include "btconfig.h"
#define UNUSED(x) (x=x)

#define PRONTO_SOC TRUE
#define QCA_DEBUG FALSE
#define Inquiry_Complete_Event  0x01

#define WDS_SOCK "wdssock"
#define INIT_SOCKET_NAME "initsock"


//static char prop[100] = {0};
char soc_type[100] = {0};
bool is_qca_transport_uart = false;
//static bool is_unified_hci = false;

#ifdef ANDROID
//static bt_vendor_interface_t * p_btf=NULL;

#endif
void baswap(bdaddr_t *dst, const bdaddr_t *src)
{
    register unsigned char *d = (unsigned char *) dst;
    register const unsigned char *s = (const unsigned char *) src;
    register int i;

    for (i = 0; i < 6; i++)
        d[i] = s[5-i];
}

int bachk(const char *str)
{
    if (!str)
        return -1;

    if (strlen(str) != 17)
        return -1;

    while (*str) {
        if (!isxdigit(*str++))
            return -1;

        if (!isxdigit(*str++))
            return -1;

        if (*str == 0)
            break;

        if (*str++ != ':')
            return -1;
    }

    return 0;
}

int ba2str(const bdaddr_t *ba, char *str)
{
    return snprintf(str, 18,"%2.2X:%2.2X:%2.2X:%2.2X:%2.2X:%2.2X",
            ba->b[5], ba->b[4], ba->b[3], ba->b[2], ba->b[1], ba->b[0]);
}

int str2ba(const char *str, bdaddr_t *ba)
{
    bdaddr_t b;
    int i;

    if (bachk(str) < 0) {
        memset(ba, 0, sizeof(*ba));
        return -1;
    }

    for (i = 0; i < 6; i++, str += 3)
        b.b[i] = strtol(str, NULL, 16);

    baswap(ba, &b);

    return 0;
}

/* Redefine a small buffer for our simple text config files */
#undef BUFSIZ
#define BUFSIZ 128

ssize_t
getline(char ** __restrict buf, size_t * __restrict buflen,
        FILE * __restrict fp)
{
    size_t bytes, newlen;
    char *newbuf, *p;

    if (buf == NULL || buflen == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (*buf == NULL)
        *buflen = 0;

    bytes = 0;
    do {
        if (feof(fp))
            break;
        if (*buf == NULL || bytes != 0) {
            newlen = *buflen + BUFSIZ;
            newbuf = realloc(*buf, newlen);
            if (newbuf == NULL)
                return -1;
            *buf = newbuf;
            *buflen = newlen;
        }
        p = *buf + bytes;
        memset(p, 0, BUFSIZ);
        if (fgets(p, BUFSIZ, fp) == NULL)
            break;
        bytes += strlen(p);
    } while (bytes == 0 || *(*buf + (bytes - 1)) != '\n');
    if (bytes == 0)
        return -1;
    return bytes;
}
#ifdef hcicmd_debug
#ifdef QCA_DEBUG
static int qca_debug_dump(uint8_t *cmd, int size)
{
    int i;

    if(!cmd || size <= 0)
      return 0;

    ALOGE("dump : ");
    for (i = 0; i < size; i++)
        ALOGE(" %02x", cmd[i]);
    ALOGE("\n");

    return 0;
}
#endif
#endif
/* Global Variables */
//static int Patch_Count = 0;
// static BOOL CtrlCBreak = FALSE;
bdaddr_t BdAddr;
/* Function Declarations */
// static int LoadPSHeader(UCHAR *HCI_PS_Command,UCHAR opcode,int length,int index);
// static BOOL SU_LERxTest(int uart_fd, UCHAR channel);
// static BOOL SU_LETxTest(int uart_fd, UCHAR channel, UCHAR length, UCHAR payload);
//static void usage(void);
//static int writeHciCommand(int uart_fd, uint16_t ogf, uint16_t ocf, uint8_t plen, UCHAR *buf);
// static int MemBlkRead(int uart_fd, UINT32 Address,UCHAR *pBuffer, UINT32 Length);
static int Dut(int uart_fd);
//static int ReadAudioStats(int uart_fd);
//static int ReadGlobalDMAStats(int uart_fd);
//static int ResetGlobalDMAStats(int uart_fd);
//static int ReadTpcTable(int uart_fd);
//static int ReadHostInterest(int uart_fd,tBtHostInterest *pHostInt);
//static int ReadMemoryBlock(int uart_fd, int StartAddress,UCHAR *pBufToWrite, int Length );
//static int WriteMemoryBlock(int uart_fd, int StartAddress,UCHAR *pBufToWrite, int Length );
//static int write_otpRaw(int uart_fd, int address, int length, UCHAR *data);
// static int read_otpRaw(int uart_fd, int address, int length, UCHAR *data);
// static void dumpHex(UCHAR *buf, int length, int col);
//static void sig_term(int sig);


int read_hci_event(int fd, unsigned char* buf, int size);
int set_speed(int fd, struct termios *ti, int speed);

void close_ui_bluetooth(){
    char state[PROPERTY_VALUE_MAX];
    property_get("vendor.bluetooth.status", state, "off");
    while(strcmp(state,"off")!=0){
        system("svc bluetooth disable > /dev/null 2>&1");
        sleep(1);
        property_get("vendor.bluetooth.status", state, "");
    }
}

int trigger_init(){
    close_ui_bluetooth();
    struct sockaddr_un serv_addr;
    int fd = -1;
    int addr_len = 0;
    fd = socket(AF_LOCAL, SOCK_STREAM, 0);
    int ret = -1;
    int reconnect = 0;
    if(fd<0){
         ALOGE("%s, client socket creation failed: %s\n", __func__, strerror(errno));
         return -1;
    }
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sun_family = AF_LOCAL;
    strlcpy(&serv_addr.sun_path[1], INIT_SOCKET_NAME, strlen(INIT_SOCKET_NAME) + 1);
    addr_len =  strlen(INIT_SOCKET_NAME) + 1;
    addr_len += sizeof(serv_addr.sun_family);
    do{
        ret = connect(fd, (struct sockaddr*)&serv_addr, addr_len);
        reconnect++;
    }while(ret<0 && reconnect<3);
    int trigger = 1;
    int result = 0;
    write(fd, &trigger, 4);
    recv(fd, &result, 4, MSG_WAITALL);
    close(fd);
    return result;
}



int connect_to_wds_server()
{
    struct sockaddr_un serv_addr;
    int sock, ret = -1, /*i,*/ addr_len;
    int reconnect = 0;
    sock = socket(AF_LOCAL, SOCK_STREAM, 0);
    if (sock < 0) {
        ALOGE("%s, client socket creation failed: %s\n", __func__, strerror(errno));
        return -1;
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sun_family = AF_LOCAL;
    strlcpy(&serv_addr.sun_path[1], WDS_SOCK, strlen(WDS_SOCK) + 1);
    addr_len =  strlen(WDS_SOCK) + 1;
    addr_len += sizeof(serv_addr.sun_family);
    do{
        ret = connect(sock, (struct sockaddr*)&serv_addr, addr_len);
        reconnect++;
    }while(ret<0 && reconnect<3);
    
    if (ret < 0) {
        ALOGE("%s, failed to connect to WDS server: %s\n", __func__, strerror(errno));
        close(sock);
        return -1;
    }
#ifdef hcicmd_debug
    ALOGE("%s, Connected to WDS server, socket fd: %d\n", __func__, sock);
#endif
    return sock;
}



int hci_send_cmd(int uart_fd, uint16_t ogf, uint16_t ocf, uint8_t plen, void *param)
{
    uint8_t type = 0x01; //HCI_COMMAND_PKT
    uint8_t hci_command_buf[256] = {0};
    uint8_t * p_buf = &hci_command_buf[0];
    uint8_t head_len = 3;
    hci_command_hdr *ch;

    if( is_qca_transport_uart || (!strcasecmp(soc_type, "300x"))) {  // hastings/cherokee/moselle/rome/ar3002/qca3003 uart
        *p_buf++ = type;
        head_len ++;
    }

    ch = (void*)p_buf;
    ch->opcode = htobs(HCI_OPCODE_PACK(ogf, ocf));
    ch->plen = plen;
    p_buf += HCI_COMMAND_HEADER_SIZE;

    if(plen) {
        memcpy(p_buf, (uint8_t*) param, plen);
    }
#ifdef hcicmd_debug
#ifdef QCA_DEBUG
    ALOGE("SEND -> ");
    qca_debug_dump(hci_command_buf, plen+head_len);
#endif
#endif
    if(write(uart_fd, hci_command_buf, plen + head_len) < 0) {
        ALOGE("write wrong!");
        return -1;
    }
    return 0;
}





int read_incoming_events(int fd, unsigned char* buf, int to){
    int r,size;
    int count = 0;

    UNUSED(to);
    if(!buf)
      return 0;

    do{
        size = count = 0;
        memset(buf , 0, MAX_EVENT_SIZE);
        if( is_qca_transport_uart || (!strcasecmp(soc_type, "300x"))){
            // for cherokee/rome/ar3002/qca3003-uart, the 1st byte are packet type, should always be 4
            r = read(fd, buf, 1);
            if  (r<=0 || buf[0] != 4)  return -1;
                }
        /* The next two bytes are the event code and parameter total length. */
        while (count < 2) {
            r = read(fd, buf + count, 2 - count);
            if (r <= 0)
            {
                ALOGE("read error:%s\n", strerror(errno));
                return -1;
            }
            count += r;
        }


        if (buf[1] == 0)
        {
            ALOGE("Zero len , invalid \n");
            return -1;
        }
        size = buf[1];

        /* Now we read the parameters. */
        while (count  < size + 2 ) {
            r = read(fd, buf + count, size);
            if (r <= 0)
            {
                ALOGE("read error :size = %d\n",size);
                return -1;
            }
            count += r;
        }

        switch (buf[0])
        {
            int j=0;
            case 0x0f:
            ALOGE("Command Status Received\n");
            for (j=0 ; j< buf[1] + 2 ; j++)
                ALOGE("[%x]", buf[j]);
            ALOGE("\n");
            if(buf[2]) {
                ALOGE("Error code: %d\n", buf[2]);
                return 0;
            }
            break;

            case 0x02: // Inquiry result
            case 0x22: // Inquiry result with RSSI
            case 0x2f: // Extended inquiry result
            {
              static int devices = 0;
              int num, loop, i, cod_off = 0, itmlen = 0;
              unsigned char *result = (unsigned char *)&buf[3];
              char *prefix = "";

              num = buf[2];
              switch(buf[0]) {
                  case 0x02:
                      cod_off = 9;
                      itmlen = 14;
                      prefix = "B";
                      break;
                  case 0x22:
                      cod_off = 8;
                      itmlen = 14;
                      prefix = "R";
                      break;
                  case 0x2f:
                      cod_off = 8;
                      itmlen = 254;
                      prefix = "E";
                      break;
              }
              for (loop = 0; loop < num; loop++) {
                  devices ++;
                  ALOGE("\t%d\t(%s) mac: ", devices, prefix);
                  for (i=5; i>=0; i--) {
                      ALOGE("%02x%s", result[i], i==0 ? "  ": ":");
                  }
                  ALOGE("COD: 0x");
                  for(i=2; i>=0; i--) {
                      ALOGE("%02x%s", result[cod_off+i], i==0 ? "\n": "");
                  }
                  result += itmlen;
              }
            }
            break;
            case 0x01:
            ALOGE("INQ COMPLETE EVENT RECEIVED\n");
            return 0;
            case 0x03:
            if(buf[2] == 0x00)
                ALOGE("CONNECTION COMPLETE EVENT RECEIVED WITH HANDLE: 0x%02x%02x \n",buf[4],buf[3]);
            else
                ALOGE("CONNECTION COMPLETE EVENT RECEIVED WITH ERROR CODE 0x%x \n",buf[2]);
            for (j=0 ; j< buf[1] + 2 ; j++)
                ALOGE("[%x]", buf[j]);
            ALOGE("\n");
            return 0;
            case 0x05:
            ALOGE("DISCONNECTION COMPLETE EVENT RECEIVED WITH REASON CODE: 0x%x \n",buf[5]);
            for (j=0 ; j< buf[1] + 2 ; j++)
                ALOGE("[%x]", buf[j]);
            ALOGE("\n");
            return 0;
            default:
#ifdef hcicmd_debug
            ALOGE("Other event received, Breaking\n");
#endif
#ifdef hcicmd_debug
#ifdef QCA_DEBUG
            ALOGE("RECV <- ");
            qca_debug_dump(buf, buf[1] + 2);
#else
            for (j=0 ; j< buf[1] + 2 ; j++)
                ALOGE("[%x]", buf[j]);
            ALOGE("\n");
#endif
#endif
            return count;
        }

        /* buf[1] should be the event opcode
         * buf[2] shoudl be the parameter Len of the event opcode
         */
    }while (1);

    return count;
}

int writeHciCommand(int uart_fd, uint16_t ogf, uint16_t ocf, uint8_t plen, UCHAR *buf){
    int count;
    UCHAR resultBuf[MAX_EVENT_SIZE];
#ifdef hcicmd_debug
    ALOGE("HCI Command: ogf 0x%02x, ocf 0x%04x, plen %d\n", ogf, ocf, plen);
#endif
 count = hci_send_cmd(uart_fd, ogf, ocf, plen, buf);
#ifdef hcicmd_debug
 ALOGE("\nhci_send_cmd:%d\n",count);
#endif
 memset(&resultBuf,0,MAX_EVENT_SIZE);
 if (!count) {
  count = read_incoming_events(uart_fd, resultBuf, 0);
#ifdef hcicmd_debug
  ALOGE("\n");
  ALOGE("\nread event count:%d\n",count);
#endif
  memset(buf, 0, MAX_EVENT_SIZE);
  if(count > 0)
    memcpy(buf, resultBuf, count);
  }
    return count;
}


static int Dut(int uart_fd){
    UCHAR buf[MAX_EVENT_SIZE];
    int iRet;

    if (is_qca_transport_uart) {
        memset(&buf,0,MAX_EVENT_SIZE);
        buf[0] = 0; // Not required
        // OGF_HOST_CTL
        // OCF_WRITE_AUTHENTICATION_ENABLE
        iRet = writeHciCommand(uart_fd, 0x03, 0x0020, 1, buf);
        if(iRet <= 0 || buf[5] != 0){
            if (iRet <= 0)
                ALOGE("\nwrite hci command failed due to reason iRet <= 0\n");
            else
                ALOGE("\nWrite authentication enable command failed due to reason 0x%X\n",buf[5]);
            return FALSE;
        }

        memset(&buf,0,MAX_EVENT_SIZE);
        buf[0] = 0; // Not required
        // OGF_HOST_CTL
        // OCF_WRITE_ENCRYPTION_MODE
        iRet = writeHciCommand(uart_fd, 0x03, 0x0022, 1, buf);
        if(iRet <= 0 || buf[5] != 0){
            if (iRet <= 0)
                ALOGE("\nwrite hci command failed due to reason iRet <= 0\n");
            else
                ALOGE("\nWrite encryption mode command failed due to reason 0x%X\n",buf[5]);
            return FALSE;
        }

        memset(&buf,0,MAX_EVENT_SIZE);
        buf[0] = 0x02; // connection setup
                buf[1] = 0x00; // return responses from all devices during the inquiry process
        buf[2] = 0x02; // allow connections from a device with specific BD_ADDR
        // OGF_HOST_CTL
        // OCF_SET_EVENT_FILTER
        iRet = writeHciCommand(uart_fd, 0x03, 0x0005, 3, buf);
        if(iRet <= 0 || buf[5] != 0){
            if (iRet <= 0)
                ALOGE("\nwrite hci command failed due to reason iRet <= 0\n");
            else
                ALOGE("\nWrite set_event_filter command failed due to reason 0x%X\n",buf[5]);
            return FALSE;
        }
    }

    memset(&buf,0,MAX_EVENT_SIZE);
    buf[0] = 3; //All scan enabled
    // OGF_HOST_CTL
    // OCF_WRITE_SCAN_ENABLE
    iRet = writeHciCommand(uart_fd, 0x03, 0x001A, 1, buf);
    if(iRet <= 0 || buf[5] != 0){
        if (iRet <= 0)
            ALOGE("\nwrite hci command failed due to reason iRet <= 0\n");
        else
            ALOGE("\nWrite scan mode command failed due to reason 0x%X\n",buf[5]);
        return FALSE;
    }

    //sleep(1);

    memset(&buf,0,MAX_EVENT_SIZE);
    //OGF_TEST_CMD
    //OCF_ENABLE_DEVICE_UNDER_TEST_MODE
    iRet = writeHciCommand(uart_fd, 0x06, 0x0003, 0, buf);
    if(iRet <= 0 || buf[5] != 0){
        if (iRet <= 0)
            ALOGE("\nwrite hci command failed due to reason iRet <= 0\n");
        else
            ALOGE("\nDUT mode command failed due to reason 0x%X\n",buf[5]);
        return FALSE;
    }

#ifndef PRONTO_SOC
    memset(&buf,0,MAX_EVENT_SIZE);
    buf[0] = 0; //SEQN Track enable =0
    // HCI_VENDOR_CMD_OGF
    // OCF_TEST_MODE_SEQN_TRACKING
    iRet = writeHciCommand(uart_fd, 0x3F, 0x0018, 1, buf);
    if(iRet <= 0 || buf[6] != 0){
        if (iRet <= 0)
            ALOGE("\nwrite hci command failed due to reason iRet <= 0\n");
        else
            ALOGE("\nTest Mode seqn Tracking failed due to reason 0x%X\n",buf[6]);
        return FALSE;
    }
#endif
    return TRUE;
}



static const char *reset_help =
"Usage:\n"
"\n reset\n";

int cmd_reset(int uart_fd, int argc, char **argv){
    int Length = 0, iRet;
    UCHAR buf[MAX_EVENT_SIZE];

    //if(argv) UNUSED(argv);
    if(argc > 1) {
        ALOGE("\n%s\n",reset_help);
        return -1;
    }

    memset(&buf,0,sizeof(buf));
    Length = 0;

    // OGF_HOST_CTL 0x03
    // OCF_RESET 0x0003
    iRet = writeHciCommand(uart_fd, HCI_CMD_OGF_HOST_CTL, HCI_CMD_OCF_RESET, Length, buf);
    if(iRet <= 0 || buf[5] != 0){
        if (iRet <= 0)
            ALOGE("\nwrite hci command failed due to reason iRet <= 0\n");
        else
            ALOGE("\nError: HCI RESET failed due to reason 0x%X\n",buf[5]);
        return -1;
    }else{
#ifdef hcicmd_debug
        ALOGE("\nHCI Reset Pass");
#endif
        return 0;
    }
    ALOGE("\nReset Done\n");
}

static const char *edutm_help =
"Usage:\n"
"\n edutm\n";

int cmd_edutm(int uart_fd, int argc,const char **argv){
    if(argv) UNUSED(argv);
        if(argc > 1){
            ALOGE("\n%s\n",edutm_help);
                return -1;
    }
    /*
       Patch_Count = 20;
       for(i=0; i < Patch_Count; i++){
       RamPatch[i].Len = MAX_BYTE_LENGTH;
       memset(&RamPatch[i].Data,0,MAX_BYTE_LENGTH);
       }
       ALOGE("\nCMD DUT MODE\n");
     */
    if(!Dut(uart_fd)){
        return -1;
    }
#ifdef hcicmd_debug
    ALOGE("\nDevice is in test mode ...\n");
#endif
    return 0;
}



static const char *rawcmd_help =
"Usage:\n"
"\n rawcmd ogf ocf <bytes> \n";

void cmd_rawcmd(int uart_fd, int argc, const char **argv){
    int iRet,i;
#ifdef hcicmd_debug
    int j;
#endif
    UCHAR buf[MAX_EVENT_SIZE];
    UCHAR resultBuf[MAX_EVENT_SIZE];
    uint16_t ogf, ocf;
    unsigned long val32;
    unsigned char *pval8;

    if(argc < 2){
        ALOGE("\n%s\n",rawcmd_help);
        return;
    }

    memset(&buf,0,MAX_EVENT_SIZE);

    val32 = strtol((char*)argv[1], NULL, 16);
    pval8 = ((unsigned char*)&val32);
    ogf = (unsigned short)*pval8;

    val32 = strtol((char*)argv[2], NULL, 16);
    pval8 = ((unsigned char*)&val32);
    ocf = (unsigned short)*pval8;

    for (i = 3; i < argc; i++)
    {
        UCHAR *tmp;
        val32 = strtol ((char*)argv[i], NULL, 16);
        tmp = ((unsigned char*)&val32);
        buf[i-3] = *tmp;
    }
#ifdef hcicmd_debug
    ALOGE("RAW HCI command: ogf 0x%x ocf 0x%x\n Params: ", ogf, ocf);

    for (j = 0; j < i-3; j++) {
        ALOGE("0x%x ", buf[j]);
    }
    ALOGE("\n");
#endif
    iRet = hci_send_cmd( uart_fd, ogf, ocf, i-3, buf);

    memset(&resultBuf,0,MAX_EVENT_SIZE);
    // if (!iRet)
    //     read_incoming_events(uart_fd, resultBuf, 0);
}
