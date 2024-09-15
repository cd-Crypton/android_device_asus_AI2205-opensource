#ifndef BTRFTEST_BLUEDROID
#define BTRFTEST_BLUEDROID
//#define BTRFTEST_SOCKET "btrftestd"
#define BTRFTEST_SOCKET "/sdcard/btrftestd"
#define CHIP_SOLUTION "WCN3990"

typedef enum {
   TEST_OP_ENABLE,
   TEST_OP_DISABLE,
   TEST_OP_ENABLE_TEST_MODE,
   TEST_OP_DISABLE_TEST_MODE,
   TEST_OP_RESET_BT,
   TEST_OP_TX_TEST_MODE,
   TEST_OP_RX_TEST_MODE,
   TEST_OP_RX_SINGLE_SLOT_BER,
   TEST_OP_LE_RX_TEST_MODE,
   TEST_OP_LE_TX_TEST_MODE,
   TEST_OP_CW_TEST_MODE,
   TEST_OP_AFH_TEST_MODE,
   TEST_OP_DISABLE_LE_TEST_MODE,
   Test_OP_LE_Enhance_RX_TEST_MODE,
   Test_OP_LE_Enhance_TX_TEST_MODE,
} btrf_test_op_t;

enum {
   OPTION_LE_RX,
   OPTION_LE_TX,
   OPTION_LE_END,
   OPTION_LE_Enhance_RX,
   OPTION_LE_Enhance_TX,
};

#define ENABLE_BT_OP 255
#define ASK_PACKET_TYPE 254
#define CLOSE_BT_OP 253

#define PARA1_KILL -1
#define PARA1_BT_ONOFF 0
#define PARA1_BT_TESTMODE 1
#define PARA1_RESET_BT 2
#define PARA1_BT_TX_TESTMODE 3
#define PARA1_BT_RX_TESTMODE 4
#define PARA1_GET_BT_RX_SINGLE 5
#define PARA1_EXIT_TX_TESTMODE 6
#define PARA1_BT_CHIP_SOLUTION 7
#define PARA1_LE_TESTMODE 10
#define PARA1_BT_SVC_Service 11
#define PARA1_BT_CW_TESTMODE 12
#define PARA1_BT_AFH_TESTMODE 13
#define PARA1_LE_Enhance_TESTMODE 14


#define PARA2_BT_ON 0
#define PARA2_BT_OFF 1
#define PARA2_ENTER_LE_RX_TESTMODE 0
#define PARA2_ENTER_LE_TX_TESTMODE 1
#define PARA2_EXIT_LE_TESTMODE 2
#define PARA2_ENTER_LE_Enhance_RX_TESTMODE 0
#define PARA2_ENTER_LE_Enhance_TX_TESTMODE 1

#define BT_disable 0
#define BT_Enable 1
#define BT_isEnable 2

#define OP_SUCCESS 0
#define OP_FAIL -1
#define OP_COMPLETE 1

//#define BT_STATE_OFF 0
//#define BT_STATE_ON 1

#define MAX_RECONNECT 3

//// Packet type
#define DH1_PACKET 4
#define HV1_PACKET 5
#define DH3_PACKET 11
#define DH5_PACKET 15
#define DH1_PACKET_2 36
#define DH3_PACKET_2 42
#define DH5_PACKET_2 46
#define DH1_PACKET_3 40
#define DH3_PACKET_3 43
#define DH5_PACKET_3 47

#ifndef msg
#define msg(format) printf(format); \
                    ALOGE(format);
#endif

#ifndef msg2
#define msg2(format1,format2) printf(format1,format2);	\
                              ALOGE(format1,format2);
#endif

#endif
