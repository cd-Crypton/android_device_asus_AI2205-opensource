#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
//#include <hardware/bluetooth.h>
#include <pthread.h>
#include <sys/types.h>
#define LOG_TAG "BTRFTEST"
#include <utils/Log.h>
//#include "Log.h"

#include <cutils/properties.h>
#include <cutils/sockets.h>
#include "btrftest.h"
//#include <../../../../system/bt/include/hardware/bluetooth.h>
#include "btconfig.h"
#include "hardware/bluetooth.h"

pthread_t main_thread_id = 0;
int   s_fd = 0;
int argv1, argv2, argv3, argv4, argv5, argv6, argv7;
int missingcommand = -2;
const char* product_name = "ro.build.product";
//const char *current_antenna = "vendor.bluetooth.ftm_switch_antenna";
char current_product[PROPERTY_VALUE_MAX];
//char ftm_switch_antenna[PROPERTY_VALUE_MAX];
int rx_packet_type;

struct para{
    int t;
    pid_t pid;
};

void* timer(void *arg){
    struct para *p;
    p = (struct para *)arg;
    sleep(p->t);
    ALOGE("timeout fail!");
    printf("FAIL\n");
    fflush(stdout);
    system("killall wdsdaemon > /dev/null 2>&1");
    sleep(1);
    kill(p->pid,SIGKILL);
    return 0;
}

static int enable_bt() {
   ALOGE("enable_bt\n");
   sleep(1);
   int ret = OP_FAIL;
   int status = -1;
   uint8_t hci_command_buf[1] = {ENABLE_BT_OP};
   int fd = connect_to_wds_server();
   write(fd, hci_command_buf, 1);
   read(fd, &status, 4);
   read(fd, &ret, 4);
   close(fd);
   sleep(1);
   if(status!=0){
      ALOGE("BT has already opened, so do nothing\n");
   }
   if (ret == OP_SUCCESS) {
      return OP_SUCCESS;
   } else {
      return OP_FAIL;
   }
}

static int disable_bt() {
   ALOGE("disable_bt\n");
   uint8_t hci_command_buf[1] = {CLOSE_BT_OP};
   int ret = 1;
   int fd = connect_to_wds_server();
   write(fd, hci_command_buf, 1);
   read(fd, &ret, 4);
   close(fd);
   if (ret == OP_SUCCESS) {
      return OP_SUCCESS;
   } else {
      return OP_FAIL;
   }
}

static int do_enable_test(){
   ALOGE("do_enable_test\n");
   const char *cmd[1];
   cmd[0] = "edutm";
   int fd = connect_to_wds_server();
   int ret = cmd_edutm(fd, 1, cmd);
   close(fd);
   return ret;
}

static int do_reset_bt() {
   ALOGE("do_reset_bt\n");
   int fd = -1;
   int ret = OP_FAIL;
   fd = connect_to_wds_server();
   ret = cmd_reset(fd, 1, nullptr);
   close(fd);
   return ret;
}

static int do_disable_test() {
   ALOGE("do_disable_test\n");
   int ret = OP_FAIL;
   ret = do_reset_bt();
   return ret;
}

static int ftm_echo_command(char *tmp_command, char *tmp_get) {
    FILE *fp = NULL;
    int status = 0;
    char run_status[PATH_MAX];
    char run_command[PATH_MAX];

    memset(run_command, 0, PATH_MAX);
    memset(run_status, 0, PATH_MAX);

    memcpy(run_command, tmp_command, strlen(tmp_command));
    ALOGE("[DBG]: [btrftestd]: %s.\n", run_command);

    fp = popen(run_command, "r");
    if (fp == NULL) {
        return OP_FAIL;
    }

    /*sleep(1);*/
    while (fgets(run_status, PATH_MAX, fp) != NULL) {
        if (tmp_get != NULL) {
            strcpy(tmp_get, run_status);
            *(tmp_get + (strlen(tmp_get) - 1)) = 0;
            ALOGE("[DBG]: [btrftestd]: tmp_get=%s.\n", tmp_get);
            // break;
        } else {
            // LOGI("[DBG]: [%s]: run_status=%s", __FUNCTION__, run_status);
        }
    }

    status = pclose(fp);
    if (status == 0) {
        return OP_SUCCESS;
    } else {
        ALOGE("[DBG]: %s.\n", strerror(errno));
        return OP_FAIL;
    }
}

static int do_tx_testmode(int* plen, uint8_t param_buf[]) {
   ALOGE("do_tx_test_mode");
   uint16_t ogf = 0x3f;
   uint16_t ocf = 0x04;
   UCHAR packet_buf[MAX_EVENT_SIZE];
   param_buf[0] = (uint8_t)argv2;
   param_buf[1] = (uint8_t)argv3;
   param_buf[2] = (uint8_t)argv4;
   param_buf[3] = (uint8_t)argv5;
   param_buf[4] = (uint8_t)argv6;

   *plen = 4;
   int ret = OP_SUCCESS;
   /// Check antenna (AI2205)
   /*
   property_get(current_antenna, ftm_switch_antenna, "0");
   char target_antenna[10];
   sprintf(target_antenna, "%d", param_buf[3]);
   ALOGE(
       "enter_tx_test_mode check ftm_switch_antenna = %s target_antenna =  "
       "%s\n ",
       ftm_switch_antenna, target_antenna);
   if (strcmp(ftm_switch_antenna, target_antenna) != 0) {
       ALOGE(
           "current target = %s and future target antenna = %s  restart bt\n",
           ftm_switch_antenna, target_antenna);
       ret = disable_bt();
       property_set(current_antenna, target_antenna);
       ret = enable_bt();
   } else {
      ret = enable_bt();
   }
   if (ret != OP_SUCCESS) {
       ALOGE("enable_bt failed\n");
       ALOGE("result: %d\n", ret);
       return OP_FAIL;
   }
   */
   char ftm_set_sub_antenna_0[] =
       "echo 0 > "
       "/sys/devices/platform/soc/b0000000.qcom,cnss-qca-converged/"
       "do_wifi_antenna_switch";
   char ftm_set_sub_antenna_1[] =
       "echo 1 > "
       "/sys/devices/platform/soc/b0000000.qcom,cnss-qca-converged/"
       "do_wifi_antenna_switch";
   //// sub channel switching only for ch1 AI2205
   property_get(product_name, current_product, "default");
   if (strcmp(current_product, "AI2205") == 0 && param_buf[3] == 1) {
       ALOGE("do 2 chain Tx");
       //IBF Test (2 chain Tx now. It will be 2x PL10 for PL11 Tx and 2x PL9 for PL10)
       packet_buf[0] = 0x25;  // Sub Opcode
       packet_buf[1] = 0x03;
       packet_buf[2] = 0x03;
       uint8_t cmd_plen = 0x03;
       int fd = connect_to_wds_server();
       ret = writeHciCommand(fd, ogf, ocf, cmd_plen, packet_buf);
       close(fd);
       if (ret >= 0) {
           if (packet_buf[5] != 0) {
               ALOGE("\nerror code:0x%x\n", packet_buf[5]);
               return OP_FAIL;
           }
       } else {
           ALOGE("writeHciCommand error!");
           return OP_FAIL;
       }
       //switch Ant, 0=Ant6(Camera Mode), 1=Ant4
       if (param_buf[4] == 0) {
           ret = ftm_echo_command(ftm_set_sub_antenna_0, NULL);
       } else if (param_buf[4] == 1) {
           ret = ftm_echo_command(ftm_set_sub_antenna_1, NULL);
       }
       if (ret != OP_SUCCESS) {
           ALOGE("Change wifi antenna failed\n");
           ALOGE("result: %d\n", ret);
           return OP_FAIL;
       }
   }
   ALOGE("enter_tx_test_mode\n");
   ALOGE("Hop channel = %d\n", param_buf[0]);
   ALOGE("pcaket type = %d\n", param_buf[1]);
   ALOGE("modulation type = %d\n", param_buf[2]);
   ALOGE("select 2.4G chain %d\n", param_buf[3]);
   packet_buf[0] = 0x04;  // Sub Opcode
   if (param_buf[0] == 79) {
       ALOGE("Special use for Tx test mode: Enable Hopping\n");
       packet_buf[1] = 0x00;   // Hop channel
       packet_buf[2] = 0x00;   // Hop channel
       packet_buf[3] = 0x00;   // Hop channel
       packet_buf[4] = 0x00;   // Hop channel
       packet_buf[5] = 0x00;   // Hop channel
       packet_buf[17] = 0x01;  // hopping funtionality
   } else {
       packet_buf[1] = (uint8_t)param_buf[0];  // Hop channel
       packet_buf[2] = (uint8_t)param_buf[0];  // Hop channel
       packet_buf[3] = (uint8_t)param_buf[0];  // Hop channel
       packet_buf[4] = (uint8_t)param_buf[0];  // Hop channel
       packet_buf[5] = (uint8_t)param_buf[0];  // Hop channel
       packet_buf[17] = 0x00;                  // hopping funtionality
   }

   /*trandmitted pattern payload, independent of the master pattern*/
   // Modulation type
   if (param_buf[2] == 3)
       packet_buf[6] = 0x02;  // alternating bit = 10101010
   else if (param_buf[2] == 4)
       packet_buf[6] = 0x04;  // pseudo-random
   else if (param_buf[2] == 9)
       packet_buf[6] = 0x03;  // alternating nibble = 11110000
   else {
       ALOGE("modulation type error \n");
       return OP_FAIL;
   }
   /*packet type*/
   switch (param_buf[1]) {
       case DH1_PACKET:            // packet type: 4 payload bytes: 0-27
           packet_buf[7] = 0x04;   // packet type
           packet_buf[18] = 0x1B;  // payload length
           packet_buf[19] = 0x00;  // payload length
           break;
       case HV1_PACKET:
           packet_buf[7] = 0x05;   // packet type
           packet_buf[18] = 0x0A;  // payload length
           packet_buf[19] = 0x00;  // payload length
           break;
       case DH3_PACKET:            // packet type: 11 payload bytes: 0-183
           packet_buf[7] = 0x0B;   // packet type
           packet_buf[18] = 0xB7;  // payload length
           packet_buf[19] = 0x00;  // payload length
           break;
       case DH5_PACKET:            // packet type: 15 payload bytes: 0-339
           packet_buf[7] = 0x0F;   // packet type
           packet_buf[18] = 0x53;  // payload length
           packet_buf[19] = 0x01;  // payload length
           break;
       case DH1_PACKET_2:          // packet type: 36 payload bytes: 0-54
           packet_buf[7] = 0x24;   // packet type
           packet_buf[18] = 0x36;  // payload length
           packet_buf[19] = 0x00;  // payload length
           break;
       case DH3_PACKET_2:          // packet type: 42 payload bytes: 0-367
           packet_buf[7] = 0x2A;   // packet type
           packet_buf[18] = 0x16;  // payload length
           packet_buf[19] = 0x0F;  // payload length
           break;
       case DH5_PACKET_2:          // packet type: 46 payload bytes: 0-679
           packet_buf[7] = 0x2E;   // packet type
           packet_buf[18] = 0xA7;  // payload length
           packet_buf[19] = 0x02;  // payload length
           break;
       case DH1_PACKET_3:          // packet type: 40 payload bytes: 0-83
           packet_buf[7] = 0x28;   // packet type
           packet_buf[18] = 0x53;  // payload length
           packet_buf[19] = 0x00;  // payload length
           break;
       case DH3_PACKET_3:          // packet type: 43 payload bytes: 0-552
           packet_buf[7] = 0x2B;   // packet type
           packet_buf[18] = 0x28;  // payload length
           packet_buf[19] = 0x02;  // payload length
           break;
       case DH5_PACKET_3:          // packet type: 47 payload bytes: 0-1021
           packet_buf[7] = 0x2F;   // packet type
           packet_buf[18] = 0xFD;  // payload length
           packet_buf[19] = 0x03;  // payload length
           break;
       default:
           ALOGE("packet type error \n");
           return OP_FAIL;
           break;
   }
   packet_buf[8] = 0x00;   // data whitening off
   packet_buf[9] = 0x0b;   // transmit output power = highest power
   packet_buf[10] = 0x01;  // receiver gain = high
   packet_buf[11] = 0xee;  // bluetooth target device address 6
   packet_buf[12] = 0xff;  // bluetooth target device address 5
   packet_buf[13] = 0xc0;  // bluetooth target device address 4
   packet_buf[14] = 0x88;  // bluetooth target device address 3
   packet_buf[15] = 0x00;  // bluetooth target device address 2
   packet_buf[16] = 0x00;  // bluetooth target device address 1
   packet_buf[20] = 0x00;  // logical transport address
   uint8_t cmd_plen = 0x15;
   int fd = connect_to_wds_server();
   ret = writeHciCommand(fd, ogf, ocf, cmd_plen, packet_buf);
   close(fd);
   if (ret >= 0) {
      if ((packet_buf[5] | 0) == 0) {
         return OP_SUCCESS;
      }else{
         ALOGE("\nerror code:0x%x\n", packet_buf[5]);
         return OP_FAIL;
      }
   }else{
      ALOGE("writeHciCommand error!");
      return OP_FAIL;
   }
}

static int do_rx_testmode(int* plen, uint8_t param_buf[]) {
   ALOGE("do_rx_testmode");
   param_buf[0] = (uint8_t)argv2;
   param_buf[1] = (uint8_t)argv3;
   param_buf[2] = (uint8_t)argv4;
   param_buf[3] = (uint8_t)argv4;
   UCHAR packet_buf[MAX_EVENT_SIZE];
   uint16_t ogf = 0x3f;
   uint16_t ocf = 0x04;
   *plen = 4;
   uint8_t cmd_plen = 0x15;
   int ret = OP_FAIL;
   //property_get(current_antenna, ftm_switch_antenna, "0");
   //char target_antenna[10];
   //sprintf(target_antenna, "%d", param_buf[2]);
   //ALOGE(
   //    "enter_rx_test_mode check ftm_switch_antenna = %s target_antenna =  "
   //    "%s\n ",
   //    ftm_switch_antenna, target_antenna);
   ALOGE("enter_rx_test_mode\n");
   rx_packet_type = param_buf[1];
   ALOGE("pcaket type = %d\n", param_buf[1]);
   ALOGE("select Antenna = %d\n", param_buf[2]);
   packet_buf[0] = 0x06;                   // command opcode
   packet_buf[1] = (uint8_t)param_buf[0];  // Hop channel
   packet_buf[2] = (uint8_t)param_buf[0];  // Hop channel
   packet_buf[3] = (uint8_t)param_buf[0];  // Hop channel
   packet_buf[4] = (uint8_t)param_buf[0];  // Hop channel
   packet_buf[5] = (uint8_t)param_buf[0];  // Hop channel
   packet_buf[6] = 0x04;  // Tx pattern payload, independent of the master
                          // pattern, 0x04 = pseudo-random
   /*packet type*/
   switch (param_buf[1]) {
       case DH1_PACKET:            // packet type: 4 payload bytes: 0-27
           packet_buf[7] = 0x04;   // packet type
           packet_buf[18] = 0x1B;  // payload length
           packet_buf[19] = 0x00;  // payload length
           break;
       case HV1_PACKET:
           packet_buf[7] = 0x05;   // packet type
           packet_buf[18] = 0x0A;  // payload length
           packet_buf[19] = 0x00;  // payload length
           break;
       case DH3_PACKET:            // packet type: 11 payload bytes: 0-183
           packet_buf[7] = 0x0B;   // packet type
           packet_buf[18] = 0xB7;  // payload length
           packet_buf[19] = 0x00;  // payload length
           break;
       case DH5_PACKET:            // packet type: 15 payload bytes: 0-339
           packet_buf[7] = 0x0F;   // packet type
           packet_buf[18] = 0x53;  // payload length
           packet_buf[19] = 0x01;  // payload length
           break;
       case DH1_PACKET_2:          // packet type: 36 payload bytes: 0-54
           packet_buf[7] = 0x24;   // packet type
           packet_buf[18] = 0x36;  // payload length
           packet_buf[19] = 0x00;  // payload length
           break;
       case DH3_PACKET_2:          // packet type: 42 payload bytes: 0-367
           packet_buf[7] = 0x2A;   // packet type
           packet_buf[18] = 0x16;  // payload length
           packet_buf[19] = 0x0F;  // payload length
           break;
       case DH5_PACKET_2:          // packet type: 46 payload bytes: 0-679
           packet_buf[7] = 0x2E;   // packet type
           packet_buf[18] = 0xA7;  // payload length
           packet_buf[19] = 0x02;  // payload length
            break;
       case DH1_PACKET_3:          // packet type: 40 payload bytes: 0-83
           packet_buf[7] = 0x28;   // packet type
           packet_buf[18] = 0x53;  // payload length
           packet_buf[19] = 0x00;  // payload length
           break;
       case DH3_PACKET_3:          // packet type: 43 payload bytes: 0-552
           packet_buf[7] = 0x2B;   // packet type
           packet_buf[18] = 0x28;  // payload length
           packet_buf[19] = 0x02;  // payload length
           break;
       case DH5_PACKET_3:          // packet type: 47 payload bytes: 0-1021
           packet_buf[7] = 0x2F;   // packet type
           packet_buf[18] = 0xFD;  // payload length
           packet_buf[19] = 0x03;  // payload length
           break;
       default:
           ALOGE("packet type error \n");
           return OP_FAIL;
           break;
   }
   packet_buf[8] = 0x00;   // set data whitening off
   packet_buf[9] = 0x09;   // transmit output power = highest power
   packet_buf[10] = 0x01;  // receiver gain = set to high gain
   packet_buf[11] = 0xee;  // bluetooth target device address 6
   packet_buf[12] = 0xff;  // bluetooth target device address 5
   packet_buf[13] = 0xc0;  // bluetooth target device address 4
   packet_buf[14] = 0x88;  // bluetooth target device address 3
   packet_buf[15] = 0x00;  // bluetooth target device address 2
   packet_buf[16] = 0x00;  // bluetooth target device address 1
   packet_buf[17] = 0x00;  // hopping funtionality
   packet_buf[20] = 0x00;  // logical transport address
   int fd = connect_to_wds_server();
   ret = writeHciCommand(fd, ogf, ocf, cmd_plen, packet_buf);
   close(fd);
   if (ret >= 0) {
       if ((packet_buf[5] | 0) == 0) {
           return OP_SUCCESS;
       } else {
           ALOGE("\nerror code:0x%x\n", packet_buf[5]);
           return OP_FAIL;
       }
   } else {
       ALOGE("writeHciCommand error!");
       return OP_FAIL;
   }
}

static int do_rx_single_slot_ber() {
   ALOGE("do_rx_single_slot_ber");
   UCHAR packet_buf[MAX_EVENT_SIZE];
   packet_buf[0] = 0x02;
   uint8_t cmd_plen = 0x01;
   uint8_t ogf = 0x3f;
   uint8_t ocf = 0x04;
   int ret = OP_FAIL;
   int total_packet_bit_error = 0;
   int packet_receive = 0;
   int packet_access_error = 0;
   int total_recevied_packets = 0;
   int *ptr;
   double rx_ber = 0;
   int max_payload_data_len = 0;
   int fd = connect_to_wds_server();
   uint8_t hci_command_buf[1] = {ASK_PACKET_TYPE};
   write(fd, hci_command_buf, 1);
   read(fd, &rx_packet_type, 1);
   close(fd);
   fd = connect_to_wds_server();
   ret = writeHciCommand(fd, ogf, ocf, cmd_plen, packet_buf);
   if (ret >= 0) {
       if ((packet_buf[5] | 0) == 0) {
           ret = cmd_reset(fd, 1, nullptr);
           close(fd);
           if (ret == OP_SUCCESS) {
               for (int m = 0; m < 5; m++) {
                   ptr = (int *)(packet_buf + (25 + 32 * m));
                   total_packet_bit_error += (*ptr);
                   ptr = (int *)(packet_buf + (9 + 32 * m));
                   packet_receive += (*ptr);
                   ptr = (int *)(packet_buf + (13 + 32 * m));
                   packet_access_error += (*ptr);
               }
               if (rx_packet_type == 4)
                   max_payload_data_len = 27;  // DH1
               else if (rx_packet_type == 11)
                   max_payload_data_len = 183;  // DH3
               else if (rx_packet_type == 15)
                   max_payload_data_len = 339;  // DH5
               else if (rx_packet_type == 36)
                   max_payload_data_len = 54;  // 2DH1
               else if (rx_packet_type == 42)
                   max_payload_data_len = 367;  // 2DH3
               else if (rx_packet_type == 46)
                   max_payload_data_len = 679;  // 2DH5
               else if (rx_packet_type == 40)
                   max_payload_data_len = 83;  // 3DH1
               else if (rx_packet_type == 43)
                   max_payload_data_len = 552;  // 3DH3
               else if (rx_packet_type == 47)
                   max_payload_data_len = 1021;  // 3DH5
               total_recevied_packets = packet_receive - packet_access_error;
               // avoid div by zero
               if (total_recevied_packets == 0) {
                   rx_ber = (double)total_packet_bit_error /
                            (max_payload_data_len * 8 * 1) * 100;
               } else {
                   rx_ber =
                       (double)total_packet_bit_error /
                       (max_payload_data_len * 8 * total_recevied_packets) *
                       100;
               }
               printf("\nNumber of received packets=%d, BER=%.4lf\n",
                      total_recevied_packets, rx_ber);
               return OP_SUCCESS;
           } else {
               ALOGE("cmd_reset error");
               return OP_FAIL;
           }
       } else {
           ALOGE("\nerror code:0x%x\n", packet_buf[5]);
           return OP_FAIL;
       }
   } else {
       close(fd);
       ALOGE("writeHciCommand error!");
       return OP_FAIL;
   }
}

static int do_le_test(int option, int argc, char** argv, int* plen, uint8_t param_buf[]){
   ALOGE("do_le_test, option=%d\n", option);
   switch (option) {
        case OPTION_LE_RX:
            if (argc != 5) {
                ALOGE("Invalid number of parameters of LE Rx test");
                return 1;
            }
            break;
        case OPTION_LE_TX:
            if (argc != 7) {
                ALOGE("Invalid number of parameters of LE Tx test");
                return 1;
            }
            break;
        case OPTION_LE_END:
            break;
        case OPTION_LE_Enhance_RX:
            if (argc != 7) {
                ALOGE("Invalid number of parameters of LE Rx test");
                return 1;
            }
            break;
        case OPTION_LE_Enhance_TX:
            if (argc != 8) {
                ALOGE("Invalid number of parameters of LE Tx test");
                return 1;
            }
            break;
        default:
            ALOGE("Unknow LE test option: %d", option);
            return 1;
    }

    switch (option) {
        case OPTION_LE_RX:
            param_buf[0] = (uint8_t)atoi(argv[4]);
            *plen = 1;
            break;
        case OPTION_LE_TX:
            param_buf[0] = (uint8_t)atoi(argv[4]);
            param_buf[1] = (uint8_t)atoi(argv[5]);
            param_buf[2] = (uint8_t)atoi(argv[6]);
            *plen = 3;
            break;
        case OPTION_LE_Enhance_RX:
            param_buf[0] = (uint8_t)atoi(argv[4]);
            param_buf[1] = (uint8_t)atoi(argv[5]);
            param_buf[2] = (uint8_t)atoi(argv[6]);
            *plen = 3;
            break;
        case OPTION_LE_Enhance_TX:
            param_buf[0] = (uint8_t)atoi(argv[4]);
            param_buf[1] = (uint8_t)atoi(argv[5]);
            param_buf[2] = (uint8_t)atoi(argv[6]);
            param_buf[3] = (uint8_t)atoi(argv[7]);
            *plen = 4;
            break;
        default:
            *plen = 0;
            break;
    }
    int len;
    uint16_t ogf = 0x08;
    uint16_t ocf;
    int ret = OP_FAIL;
    switch (option) {
        case OPTION_LE_RX:
            ocf = 0x001D;
            len = 1;
            break;
        case OPTION_LE_TX:
            ocf = 0x001E;
            len = 3;
            break;
        case OPTION_LE_END:
            ocf = 0x001F;
            len = 0;
            break;
        case OPTION_LE_Enhance_RX:
            ocf = 0x0033;
            len = 3;
            break;
        case OPTION_LE_Enhance_TX:
            ocf = 0x0034;
            len = 4;
            break;
        default:
            ALOGE("unknown mode: %d\n", option);
            return -1;
    }
    int fd = connect_to_wds_server();
    ret = writeHciCommand(fd, ogf, ocf, len, param_buf);
    close(fd);
    if (ret >= 0) {
        if ((param_buf[5] | 0) == 0) {
            return OP_SUCCESS;
        } else {
            ALOGE("\nerror code:0x%x\n", param_buf[5]);
            return OP_FAIL;
        }
    } else {
        ALOGE("writeHciCommand error!");
        return OP_FAIL;
    }
}

static int do_cw_testmode(int* plen, uint8_t param_buf[]) {
   ALOGE("do_cw_testmode");
   int ret = OP_FAIL;
   UCHAR packet_buf[MAX_EVENT_SIZE];
   param_buf[0] = (uint8_t)argv2;
   *plen = 1;
   uint8_t ogf = 0x3f;
   uint8_t ocf = 0x04;
   uint8_t cmd_plen = 0x09;
   packet_buf[0] = 0x05;                   // command opcode
   packet_buf[1] = (uint8_t)param_buf[0];  // Hop channel
   packet_buf[2] = 0x09;                   /////  Transmit output power
   packet_buf[3] = 0x04;                   /////  Transmit type
   packet_buf[4] = 0x01;                   /////  Pattern length
   packet_buf[5] = 0x00;                   /////  Bit pattern
   packet_buf[6] = 0x00;                   /////  Bit pattern
   packet_buf[7] = 0x00;                   /////  Bit pattern
   packet_buf[8] = 0x00;                   /////  Bit pattern
   int fd = connect_to_wds_server();
   ret = writeHciCommand(fd, ogf, ocf, cmd_plen, packet_buf);
   close(fd);
   if (ret >= 0) {
       if ((packet_buf[5] | 0) == 0) {
           return OP_SUCCESS;
       } else {
           ALOGE("\nerror code:0x%x\n", packet_buf[5]);
           return OP_FAIL;
       }
   } else {
       ALOGE("writeHciCommand error!");
       return OP_FAIL;
   }
}

static int do_AFH_testmode(int* plen, uint8_t param_buf[]) {
   ALOGE("do_AFH_testmode");
   param_buf[0] = (uint8_t)argv2;
   param_buf[1] = (uint8_t)argv3;
   *plen = 2;
   unsigned int packet_buf_1[80];
   uint8_t packet_buf[10];
   int ret = OP_FAIL;
   uint16_t ogf = 0x03;
   uint16_t ocf = 0x3f;
   uint8_t cmd_len = 0x0A;
   ALOGE("From channel = %d to channel = %d\n", param_buf[0], param_buf[1]);
   //// Mapping the Channel Map
   for (int i = 0; i < 79; i++) {
       if ((i >= param_buf[0]) && (i <= param_buf[1])) {
           packet_buf_1[i] = 1;
       } else {
           packet_buf_1[i] = 0;
       }
   }
   packet_buf_1[79] = 0;
   //// Transfer type
   for (int i = 0; i < 10; i++) {
       packet_buf[i] =
           (packet_buf_1[i * 8] << 3 | packet_buf_1[i * 8 + 1] << 2 |
            packet_buf_1[i * 8 + 2] << 1 | packet_buf_1[i * 8 + 3] |
            packet_buf_1[i * 8 + 4] << 7 | packet_buf_1[i * 8 + 5] << 6 |
            packet_buf_1[i * 8 + 6] << 5 | packet_buf_1[i * 8 + 7] << 4);
   }
   ALOGE("Channel map =%s\n", packet_buf);
   int fd = connect_to_wds_server();
   ret = writeHciCommand(fd, ogf, ocf, cmd_len, packet_buf);
   close(fd);

   if (ret >= 0) {
       if ((packet_buf[5] | 0) == 0) {
           return OP_SUCCESS;
       } else {
           ALOGE("\nerror code:0x%x\n", packet_buf[5]);
           return OP_FAIL;
       }
   } else {
       ALOGE("writeHciCommand error!");
       return OP_FAIL;
   }
}

static void usage(int failreason) {
    msg2("Product %s:\n", current_product);

    if (failreason == missingcommand){
	msg("Missing or padding some parameters!!!\n\n");
    }
    else if(failreason != 0) {
	msg2("Para%d syntax error!!!\n\n", failreason);
    }

    msg("Usage:\n");
    msg("\tbtrftest  (Android S version)\n");
    msg("\tCommand ID 0 (Enable or disable BT chip)\n"); 
    msg("\t\tPara1: 0; Para2: 0 - Enable, 1 - Disable; Para3: Timeout value in decimal(sec)\n");
    msg("\tCommand ID 1 (Enter or exit BT test mode)\n");
    msg("\t\tPara1: 1; Para2: 0 - Enter test mode, 1 - Exit test mode; Para3: Timeout value in decimal(sec)\n");
    msg("\tCommand ID 2 (Reset BT chip)\n");
    msg("\t\tPara1: 2\n");
    msg("\tCommand ID 3 (Enter BT Tx test mode)\n");
    msg("\t\tPara1: 3; Para2: Channel (Ex: 0,1,...,78), Enable Hopping: 79;\n");
    msg("\t\tPara3: Packet Type \n");
    msg("\t\t(4=DH1, 5=HV1, 11=DH3, 15=DH5, 36=2DH1, 42=2DH3, 46=2DH5, 40=3DH1, 43=3DH3, 47=2DH5)\n");
    msg("\t\tPara4: Modulation type/bit pattern (3=0xAA/8-bit pattern, 4=PRBS9 pattern, 9=0xF0/8-bit pattern)\n");
    msg("\t\tPara5: Select Antenna(0=chain0, 1=chain1))\n");
    msg("\t\tAI2205 support sub-Antenna \n");
    msg("\t\tPara6: Select Sub Antenna(0=Ant6(Camera Mode), 1=Ant4))\n");
    msg("\tCommand ID 4 (BT Rx test mode)\n");
    msg("\t\tPara1: 4; Para2: Channel (Ex: 0,1,...,78);\n");
    msg("\t\tPara3: Packet Type \n");
    msg("\t\t(4=DH1, 5=HV1, 11=DH3, 15=DH5, 36=2DH1, 42=2DH3, 46=2DH5, 40=3DH1, 43=3DH3, 47=2DH5)\n");
    msg("\t\tPara4: Select Antenna(0=Main, 1=slave))\n");
    msg("\t\tPara5: Select Sub Antenna(0=chA, 1=chB[default])))\n");
    msg("\tCommand ID 5 (Get BT Rx single slot BER)\n");
    msg("\t\tPara1: 5\n");
    msg("\tCommand ID 6 (Exit BT TX test mode)\n");
    msg("\t\tPara1: 6\n");
    msg("\tCommand ID  (Show BT chip solution)\n");
    msg("\t\tPara1: 7\n");
    msg("\tCommand ID 10 (Enter BLE Tx/Rx test mode and Test End)\n");
    msg("\t\tPara1: 10; Para2: 0 - Rx Test Mode, 1 - Tx Test Mode, 2 - Test End; Para3: Timeout value in decimal(sec);\n");
    msg("\t\tRx Test Mode: Para4: Rx freq( 0-39) \n");
    msg("\t\tTx Test Mode: Para4: Tx freq( 0-39); Para5:  Len of payload (0-37) Para6:  payload type (0-7)\n");
    msg("\tCommand ID 11 (Using SVC Service )\n");
    msg("\t\tPara1: 11; Para2: 0 - Disable, 1 - Enable, 2 -isEnable\n");
    msg("\tCommand ID 12 (BT CW test mode)\n");
    msg("\t\tPara1: 12; Para2: Channel (Ex: 0,1,...,78);\n");
    msg("\tCommand ID 13 (Setting AFH Mode)\n");
    msg("\t\tPara1: 13; Para2: Start from the Channel (Ex: 0,1,...,78); \n");
    msg("\t\tPara3: End of the  Channel (Ex: 0,1,...,78, Para2 =< Para 3);\n");
    msg("Special usage for killing btrftestd: btrftest -1 -1\n");
    msg("\tCommand ID 14 (Enter BLE enhance Tx/Rx test mode)\n");
    msg("\t\tPara1: 14; Para2: 0 - Rx Enhance Test Mode, 1 - Tx Enhance Test Mode; Para3: Timeout value in decimal(sec);\n");
    msg("\t\tRx enhance Test Mode: Para4: Rx freq( 0-39) Para5:  PHY (1-3) Para6:  Modulation Index (0/1)\n");
    msg("\t\tTx enhanceTest Mode: Para4: Tx freq( 0-39); Para5:  Len of payload (0-255) Para6:  payload type (0-7) para7:  PHY (1-4) \n");

}

int bt_main_function(int argc, char **argv)
{
   int plen = 0;
   int ret = 0;
   uint8_t param_buf[5];
   int rmtTimeout = 0;
   pthread_t t;
   struct para parameter;
   parameter.pid = getpid();
   parameter.t = 3;
   ALOGE("bt_main_function enter\n");
   switch(argv1){
      case PARA1_BT_ONOFF:
         parameter.t = argv3;
         pthread_create(&t,NULL,timer,&parameter);
         trigger_init();
         if (argv2 == PARA2_BT_ON) ret = enable_bt();
	     else ret = disable_bt();
	     rmtTimeout = argv3;
	     break;
      case PARA1_BT_TESTMODE:
         parameter.t = argv3;
         pthread_create(&t,NULL,timer,&parameter);
         trigger_init();
         if (argv2 == 0) ret = do_enable_test();
         else ret = do_disable_test();
	     rmtTimeout = argv3;
         break;
      case PARA1_RESET_BT:
         parameter.t = 10;
         pthread_create(&t,NULL,timer,&parameter);
         trigger_init();
         ret = do_reset_bt();
         rmtTimeout = 10;
         break;
      case PARA1_BT_TX_TESTMODE:
         parameter.t = 10;
         pthread_create(&t,NULL,timer,&parameter);
         trigger_init();
         ret = do_tx_testmode(&plen, param_buf);
         rmtTimeout = 10;
         break;
      case PARA1_BT_RX_TESTMODE:
         parameter.t = 10;
         pthread_create(&t,NULL,timer,&parameter);
         trigger_init();
         ret = do_rx_testmode(&plen, param_buf);
         rmtTimeout = 10;
         break;
      case PARA1_GET_BT_RX_SINGLE:
         parameter.t = 10;
         pthread_create(&t,NULL,timer,&parameter);
         trigger_init();
         ret = do_rx_single_slot_ber();
         rmtTimeout = 10;
         break;
      case PARA1_BT_CHIP_SOLUTION:
         return OP_SUCCESS;
         break;
      case PARA1_LE_TESTMODE:
         parameter.t = argv3;
         pthread_create(&t,NULL,timer,&parameter);
         trigger_init();
         if (argv2 == PARA2_ENTER_LE_RX_TESTMODE)
            ret = do_le_test(OPTION_LE_RX, argc, argv, &plen, param_buf);
         else if (argv2 == PARA2_ENTER_LE_TX_TESTMODE)
            ret = do_le_test(OPTION_LE_TX, argc, argv, &plen, param_buf);
         else if (argv2 == PARA2_EXIT_LE_TESTMODE)
            ret = do_le_test(OPTION_LE_END, argc, argv, &plen, param_buf);
            rmtTimeout = argv3;
         break;
      case PARA1_BT_CW_TESTMODE:
         parameter.t = 10;
         pthread_create(&t,NULL,timer,&parameter);
         trigger_init();
         ret = do_cw_testmode(&plen, param_buf);
         rmtTimeout = 10;
         break;
      case PARA1_BT_AFH_TESTMODE:
         parameter.t = 10;
         pthread_create(&t,NULL,timer,&parameter);
         trigger_init();
         ret = do_AFH_testmode(&plen, param_buf);
         rmtTimeout = 10;
         break;
      case PARA1_EXIT_TX_TESTMODE:
         parameter.t = 10;
         pthread_create(&t,NULL,timer,&parameter);
         trigger_init();
         ret = do_reset_bt();
         rmtTimeout = 10;
         break;
      case PARA1_LE_Enhance_TESTMODE:
         parameter.t = argv3;
         pthread_create(&t,NULL,timer,&parameter);
         trigger_init();
         if (argv2 == PARA2_ENTER_LE_Enhance_RX_TESTMODE)
            ret = do_le_test(OPTION_LE_Enhance_RX, argc, argv, &plen, param_buf);
         else if (argv2 == PARA2_ENTER_LE_Enhance_TX_TESTMODE)
            ret = do_le_test(OPTION_LE_Enhance_TX, argc, argv, &plen, param_buf);
            rmtTimeout = argv3;
         break;
      case PARA1_BT_SVC_Service:
          parameter.t = 10;
          pthread_create(&t,NULL,timer,&parameter);
          if (argv2 == 0) {
             system("/system/bin/svc bluetooth disable");
          } else if (argv2 == 1) {
             system("/system/bin/svc bluetooth enable");
          } else if (argv2 == 2) {
             system("getprop vendor.bluetooth.status");
          }
          ret = OP_SUCCESS;
          break;
      default:
         break;
   }
   
   ALOGE("bt_main_function leave\n");
   return(ret);
}


int parse_parameter_valid(int argc, char **argv){
   if (argc < 2) {
      usage(0);
      return OP_FAIL;
   }
   
   argv1 = (int)atoi(argv[1]);
   switch (argv1){
      case PARA1_KILL:
         if (argc != 3) usage(missingcommand);
         else if ((argv2=(int)atoi(argv[2])) != -1) usage(2);
         else return OP_SUCCESS;
         break;
      case PARA1_BT_ONOFF:
         if (argc != 4) usage(missingcommand);
         else if ((argv2=(int)atoi(argv[2])) < 0 || argv2 > 1) usage(2);
         else if ((argv3=(int)atoi(argv[3])) < 0) usage(3);
         else return OP_SUCCESS;
         break;
      case PARA1_BT_TESTMODE:
         if (argc != 4) usage(missingcommand);
         else if ((argv2=(int)atoi(argv[2])) < 0 || argv2 > 1) usage(2);
         else if ((argv3=(int)atoi(argv[3])) < 0) usage(3);
         else return OP_SUCCESS;
         break;
      case PARA1_RESET_BT:
         if (argc != 2) usage(missingcommand);
         else return OP_SUCCESS;
         break;
      case PARA1_BT_TX_TESTMODE:
         if (argc <6 || argc > 7 ) usage(missingcommand);
         else if ((argv2=(int)atoi(argv[2])) < 0 || argv2 > 79) usage(2);
         else if ((argv3=(int)atoi(argv[3])) <0 || argv3 > 61) usage(3);
         else if ((argv4=(int)atoi(argv[4])) != 3 && argv4 != 4 && argv4 != 9) usage(4);
         else if ((argv5=(int)atoi(argv[5])) != 0 && argv5 != 1) usage(5);
         else if ((argv6=(int)atoi(argv[6])) != 0 && argv6 != 1) usage(6);
         else return OP_SUCCESS;
         break;
      case PARA1_BT_RX_TESTMODE:
         if (argc < 4 || argc > 6) usage(missingcommand);
         else if ((argv2=(int)atoi(argv[2])) < 0 || argv2 > 78) usage(2);
         else if ((argv3=(int)atoi(argv[3])) <0 || argv3 > 61) usage(3);
         else if (argc <4 && (argv4=(int)atoi(argv[4])) != 0 && argv4 != 1) usage(4);
		 else if (argc <5 && (argv5=(int)atoi(argv[5])) != 0 && argv5 != 1) usage(5);
         else return OP_SUCCESS;
         break;
      case PARA1_GET_BT_RX_SINGLE:
         if (argc != 2) usage(missingcommand);
         else return OP_SUCCESS;
         break;
      case PARA1_LE_TESTMODE:
         if ((argv2=(int)atoi(argv[2])) == 0){
            if (argc != 5) usage(missingcommand);
			else if ((argv3=(int)atoi(argv[3])) < 0) usage(3);
            else if ((argv4=(int)atoi(argv[4])) < 0 || argv4 > 39) usage(4);
            else return OP_SUCCESS;
         }
         if ((argv2=(int)atoi(argv[2])) == 1){
            if (argc != 7) usage(missingcommand);
            else if ((argv3=(int)atoi(argv[3])) < 0) usage(3);
			else if ((argv4=(int)atoi(argv[4])) < 0 || argv4 > 39) usage(4);
			else if ((argv5=(int)atoi(argv[5])) < 0 || argv5 > 255) usage(5);
			else if ((argv6=(int)atoi(argv[6])) < 0 || argv6 > 7) usage(6);
            else return OP_SUCCESS;
         }
         if ((argv2=(int)atoi(argv[2])) == 2){
            if (argc != 4) usage(missingcommand);
            else if ((argv3=(int)atoi(argv[3])) < 0) usage(3);
            else return OP_SUCCESS;
         }
         break;
      case PARA1_EXIT_TX_TESTMODE:
         if (argc != 2) usage(missingcommand);
         else return OP_SUCCESS;
         break;
      case PARA1_BT_CHIP_SOLUTION:
         return OP_SUCCESS;
         break;
      case PARA1_BT_SVC_Service:
         if (argc != 3) usage(missingcommand);
         else if ((argv2=(int)atoi(argv[2])) < 0 || argv2 > 2) usage(2);
         else return OP_SUCCESS;
         break;
      case PARA1_BT_CW_TESTMODE:
         if (argc != 3) usage(missingcommand);
	else if  ((argv2=(int)atoi(argv[2])) < 0 || argv2 > 78) usage(2);
         else return OP_SUCCESS;
         break;
      case PARA1_BT_AFH_TESTMODE:
         if (argc != 4) usage(missingcommand);
	else if  ((argv2=(int)atoi(argv[2])) < 0 || argv2 > 78) usage(2);
	else if  ((argv3=(int)atoi(argv[3])) < 0 || argv3 < argv2|| argv3 > 78) usage(3);
         else return OP_SUCCESS;
         break;
      case PARA1_LE_Enhance_TESTMODE:
         if ((argv2=(int)atoi(argv[2])) == 0){ // Rx
            if (argc != 7) usage(missingcommand);
	     else if ((argv3=(int)atoi(argv[3])) < 0) usage(3);
            else if ((argv4=(int)atoi(argv[4])) < 0 || argv4 > 39) usage(4);
	     else if ((argv5=(int)atoi(argv[5])) < 1 || argv5 > 3) usage(5);
	     else if ((argv6=(int)atoi(argv[6])) < 0 || argv6 > 1) usage(6);
            else return OP_SUCCESS;
	     break;
         }
         if ((argv2=(int)atoi(argv[2])) == 1){ //Tx
            if (argc != 8) usage(missingcommand);
	     else if ((argv3=(int)atoi(argv[3])) < 0) usage(3);
            else if ((argv4=(int)atoi(argv[4])) < 0 || argv4 > 39) usage(4);
	     else if ((argv5=(int)atoi(argv[5])) < 0 || argv5 > 255) usage(5);
	     else if ((argv6=(int)atoi(argv[6])) < 0 || argv6 > 7) usage(6);
	     else if ((argv7=(int)atoi(argv[7])) < 1 || argv7 > 4) usage(7);
            else return OP_SUCCESS;
	     break;
         }
         break;
      default:
         usage(1);
         break;
   }
      
   return OP_FAIL;
}


int main(int argc, char **argv) {
   int iret;
   int attempt;


   s_fd = 0; 

#ifdef ANDROID
   property_get("persist.vendor.qcom.bluetooth.soc", soc_type, "pronto");
#else
   strlcpy(soc_type, "pronto");
#endif
   if ((!strcasecmp(soc_type, "rome")) ||
       (!strcasecmp(soc_type, "cherokee")) ||
       (!strcasecmp(soc_type, "hastings")) ||
       (!strcasecmp(soc_type, "hamilton")) ||
       !strcasecmp(soc_type, "moselle")) {
       is_qca_transport_uart = true;
   }

   if (parse_parameter_valid(argc, argv) != OP_SUCCESS) {
       ALOGE("parameter invalid");
       return OP_FAIL;
   }

   //show input command
   char command[64];
   for (int i = 0; argc > 0; argc--) {
       strcat(command, argv[i]);
       strcat(command, " ");
       i++;
   }
   ALOGE("%s ", command);

   for (attempt = 5; attempt >0; attempt-- )
   {
      iret = bt_main_function(argc, argv);
      if (iret != OP_SUCCESS) {
         ALOGE("main function failed\n");
         sleep(1);
         continue; // RETRY 
      }
      else {
         break; // PASS
      }
   }

   if (attempt == 0)
   {
      if (argv1 == 10 && (argv2 == 0 || argv2 == 1)){
         msg("after you implement LE TX or LE RX command, you need to exit LE test mode\n");
     }
      msg("FAIL\n");//success
      system("killall wdsdaemon > /dev/null 2>&1");
      return OP_FAIL;
     } else if (argv1 == 7){
       msg2("%s\n",CHIP_SOLUTION);
     } else {
      msg("PASS\n");//success
     }
     
   return OP_SUCCESS;
}
