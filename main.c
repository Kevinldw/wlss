#include <stdlib.h>
#include <stdio.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include <time.h>
#include <mosquitto.h>
#include "pipe.h"
#include "cJSON.h"
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <netinet/in.h>
#include <net/if.h>
#include <sys/socket.h> 
#include <arpa/inet.h>
#include <sys/ioctl.h>
//#include <json.h>
#include "wlss.h"

#define TAG ""
#define s_device_path "/dev/ttyO2"
#ifdef __ARM__
#define LOCAL_NET_FILE_PATH			"/media/mmcblk0p1/wls.net"
#define SENSOR_STATUS_FILE_PATH	"/media/mmcblk0p1/wls.stas"
#define SENSOR_DBTMP_FILE_PATH	"/media/mmcblk0p1/wls_tmp.db"
#define SENSOR_DB_FILE_PATH			"/media/mmcblk0p1/wls.db"
#define SENSOR_JSON_FILE_PATH		"/media/mmcblk0p1/wls.cfg"
#else
#define LOCAL_NET_FILE_PATH			"/tmp/wls.net"
#define SENSOR_STATUS_FILE_PATH	"/tmp/sensors.stas"
#define SENSOR_DBTMP_FILE_PATH	"/tmp/wls_tmp.db"
#define SENSOR_DB_FILE_PATH			"/tmp/wls.db"
#define SENSOR_JSON_FILE_PATH		"/tmp/wls.cfg"
#endif
#define WAKEUP_TIMEOUT									(3*60)
#define SUBG_FAKE_TIMESTAMP_REQUIRE			0x2F
#define SUBG_REQUIRE_HOST_TIMESTAMP		0x30
#define SUBG_COMMAND_CONFIRM					0xAC
#define HOST_SET_SUBG_TIMESTAMP				0x27
#define HOST_SET_SUBG_RFINFO						0x2A
#define HOST_INQUIRE_SENSOR_NETCFG			0x26
#define HOST_INQUIRE_SENSOR_DATA				0x21
#define HOST_SET_SENSOR_SAMPLE_ARGS		0x22
#define HOST_INQUIRE_SENSOR_TDATA				0x2C
#define SUBG_REPORT_SENSOR_DATA				0x33
#define SUBG_REPORT_SENSOR_INFO				0x28
#define HOST_INQUIRE_SUBG_INFO					0x29

#define INFO_LEN													16
struct _active_slot_st{
    guint start;
    guint end;
    //struct list_head node;
};
struct _limit_st{
    gfloat low;
    gfloat high;
    //struct list_head node;
};

struct _confirm_st{
    guint times;
    guint raw;
    guint shadow;
};

struct _alarm_st{
    guint type;
    guint src;
    GSList *limit_list;
    
    //struct list_head node;
};

struct _argument_st{
    guint channel;
    guint freq;
    guint nums;
    GSList *alarm_list;
    struct _confirm_st *confirm;
    //struct list_head node;
};

struct _sensor_st{
    guint id;
    guint group;
    guint aquire_cycle;
    guint triger;
    guint raw;
    GSList *active_list;
    GSList *argument_list;
    //struct list_head node;
};

struct _cmd_list{
    guint id;// 传感器ID
    guint cmd;
    guint delay;
    void* data;// 附加数据
    guint len;
    // 响应事件
    //guint event;
    guint resend;
    gboolean notify;
    GCond *cond;
    GMutex *mutex;
};

struct _end_packet {
    guchar  hed[4];
    gushort sn;
    guchar  len;
    guchar  cmd;
    guchar  flag;
    guchar  res[3];
};

struct _rf_st{
	guint channel;
	guint pan;
	guint power;
};

struct _cache_st {
    guint id;
    guint ts;
    guint idx;
    guint crc;
    guint volt;
    guint rssi;
    gfloat coef;
    gfloat value;
    guint channel;
    guint freq;
    guint nums;
    guint address[3];
};

struct _check_alarm {
	guint id;
	guint idx;
	gfloat *pfdat;
	guint *pidat;
	GSList *alarm_list;
	struct _confirm_st *confirm;
};
struct _local_st {
	gchar ip[32];
	guint port;
	gboolean dhcp;
};
struct _server_st {
	gchar ip[32];
	guint port;
};
struct _mqtt_st {
	gchar ip[32];
	guint port;
	gchar user[32];
	gchar passw[32];
	guint keepalive;
};
struct _ntp_st {
	gchar host[32];
    guint updateTime;
    gboolean isUpdate;
    gboolean check;
};
struct _net_parameter_st {
	struct _local_st _local;
	struct _server_st _server;
	struct _mqtt_st _mqtt;
	struct _ntp_st _ntp;
	struct _rf_st _rf;
};

gint s_fd = -1;
pipe_t* s_pipe = NULL;
pipe_producer_t* s_p = NULL;
pipe_consumer_t* s_c = NULL;
GSList *s_sensor_list = NULL;
GAsyncQueue *s_asyncQueue = NULL;
GCond s_cond;
GMutex s_lock;
GCond s_dat_cond;
GMutex s_dat_lock;
G_LOCK_DEFINE(db_protect);
gint s_wakeup_timeout = 0;
guint s_to_res = 0;
guint s_to_rec_res = 0;	// wait respond watch dog
struct mosquitto *s_mosq;
gint s_mid = 1;
guint s_status = 0;
guint s_failure = 0;

struct _cache_st *s_cache = NULL;
gchar *s_localip = "127.0.0.1";
gchar *s_locamac = "00:00:00:00:00:00";
gchar *s_gid = NULL;
struct _net_parameter_st s_net_para;

struct _cache_st *findIdCache(guint id);
int sensor_volt_info_to_db(const gchar *db, guint sid, guint ts, guint volt, guint rssi);
int platform_info_from_db(const gchar *db, guint *channel, guint *pan, guint *power);
int sensor_data_info_insert_db(const gchar *db, guint id, gfloat *pfval, guint *pival);
int sensor_data_info_form_db(const gchar *db, guint **pdat);
int sensor_data_info_update_db(const gchar *db, guint id);
int misc_info_insert_db(const char *db, char *gid);
int misc_info_from_db(const char *db, char **gid);

void dumpmem(guchar *mem, guint len)
{
    printf("\n");
    for(int i=0; i<len; i++) {
        if(i!=0 && i%16==0)printf("\n");
        printf("%02X ", mem[i]);
    }
    printf("\n");
}

gint uart_init()
{
    gint fd = -1;
    
    fd = open(s_device_path, O_RDWR);
    if(fd > 0){
        gint flags;
        struct termios  ios;
        tcgetattr( fd, &ios );
        ios.c_iflag |= (IGNBRK | IGNPAR);
        ios.c_iflag &= ~(BRKINT|ICRNL|INPCK|ISTRIP|IXON);   
        ios.c_oflag &= ~OPOST;
        ios.c_oflag      = 0;
        ios.c_lflag &= ~(ECHO|ECHONL|ISIG|IEXTEN|ICANON);
        ios.c_lflag = 0;
        ios.c_cflag  |= CS8 | CREAD;
        ios.c_cflag  &= ~PARENB;
        ios.c_cflag |= CSTOPB;
        ios.c_cc[VMIN]   = 1;
        ios.c_cc[VTIME]  = 0;
        cfsetispeed(&ios, B921600);
        cfsetospeed(&ios, B921600);
        tcflush(fd,TCIOFLUSH);
        tcsetattr( fd, TCSANOW, &ios );

        flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
    return fd;
}
int speed_arr[] = { 
    B921600, B460800, B230400, B115200, B57600, B38400, B19200, 
    B9600, B4800, B2400, B1200, B300, 
};

int name_arr[] = {
    921600, 460800, 230400, 115200, 57600, 38400,  19200,  
    9600,  4800,  2400,  1200,  300,  
};
void set_speed(int fd, int speed)
{
    int   i;
    int   status;
    struct termios   Opt;
    tcgetattr(fd, &Opt);

    for ( i= 0;  i < sizeof(speed_arr) / sizeof(int);  i++) {
        if  (speed == name_arr[i])    {
            tcflush(fd, TCIOFLUSH);
            cfsetispeed(&Opt, speed_arr[i]);
            cfsetospeed(&Opt, speed_arr[i]);
            status = tcsetattr(fd, TCSANOW, &Opt);
            if  (status != 0)
                perror("tcsetattr fd1");
            return;
        }
        tcflush(fd,TCIOFLUSH);
    }
}
int set_Parity(int fd,int databits,int stopbits,int parity)
{
    struct termios options;
    memset(&options, 0, sizeof(struct termios));
    if  ( tcgetattr( fd,&options)  !=  0) {
        perror("SetupSerial 1");
        return(FALSE);
    }
    options.c_cflag &= ~CSIZE ;
    switch (databits) /*设置数据位数*/ {
    case 7:
        options.c_cflag |= CS7;
        break;
    case 8:
        options.c_cflag |= CS8;
        break;
    default:
        fprintf(stderr,"Unsupported data size\n");
        return (FALSE);
    }

    switch (parity) {
    case 'n':
    case 'N':
        options.c_cflag &= ~PARENB;   /* Clear parity enable */
        options.c_iflag &= ~INPCK;     /* Enable parity checking */
        break;
    case 'o':
    case 'O':
        options.c_cflag |= (PARODD | PARENB);  /* 设置为奇效验*/
        options.c_iflag |= INPCK;             /* Disnable parity checking */
        break;
    case 'e':
    case 'E':
        options.c_cflag |= PARENB;     /* Enable parity */
        options.c_cflag &= ~PARODD;   /* 转换为偶效验*/ 
        options.c_iflag |= INPCK;       /* Disnable parity checking */
        break;
    case 'S':    
    case 's':  /*as no parity*/
        options.c_cflag &= ~PARENB;
        options.c_cflag &= ~CSTOPB;
        break;
    default:
        fprintf(stderr,"Unsupported parity\n");
        return (FALSE);
    }
    /* 设置停止位*/  
    switch (stopbits) {
    case 1:
        options.c_cflag &= ~CSTOPB;
        break;
    case 2:
        options.c_cflag |= CSTOPB;
        break;
    default:
        fprintf(stderr,"Unsupported stop bits\n");
        return (FALSE);
    }
    /* Set input parity option */
    if (parity != 'n')
        options.c_iflag |= INPCK;
    options.c_cc[VTIME] = 1; // 150 seconds
    options.c_cc[VMIN] = 1;

    options.c_lflag &= ~(ECHO | ICANON);

    options.c_cflag &= ~CRTSCTS;

    tcflush(fd,TCIFLUSH); /* Update the options and do it NOW */
    if (tcsetattr(fd,TCSANOW,&options) != 0) {
        perror("SetupSerial 3");
        return (FALSE);
    }

    //gint flags;
    //flags = fcntl(fd, F_GETFL, 0);
    //fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    return (TRUE);
}

//
gboolean serial_write_data(guchar *buf, guint len) {
    guint writelen = 0;

#ifdef __ARM__
    while(len > 0) {
        writelen = write(s_fd, buf + writelen, len);
        if(writelen < 0) {
            g_log(TAG, G_LOG_LEVEL_DEBUG, "wirte data to serial fail errno=%d", errno);
            return FALSE;
        }
        len -= writelen;
    }
#endif
    return (len == 0);
}
//
static gpointer serial_read_thread(gpointer user_data) {
    int fd = *(int *) user_data;
    guchar buf[64*1024];
    gint readlen = 0;
    gint curlen = 0;
    g_log (TAG, G_LOG_LEVEL_DEBUG, "star ttyO2(%d) read loop", fd);
    for(;;) {
        while(1){
            readlen = read(fd, buf+curlen, 1024);
            if(readlen < 0)break;
            //g_log (TAG, G_LOG_LEVEL_DEBUG, "readl %d bytes", readlen);
            curlen += readlen;
            g_usleep(2*1000);
        }

        if(curlen > 0) {
            //dumpmem(buf, curlen);
            //g_log (TAG, G_LOG_LEVEL_DEBUG, "push %d bytes to fifo", curlen);
            pipe_push(s_p, buf, curlen);
        }
        g_usleep(10*1000);
        curlen = 0;
    }
    
    return GINT_TO_POINTER(-1);
}

static inline void build_command(guint cmd, guchar *data, guint len, guint delay)
{
    struct _cmd_list *pcmd = (struct _cmd_list *)
                g_malloc0(sizeof(struct _cmd_list));
    g_async_queue_lock(s_asyncQueue);
    pcmd->cmd = cmd;
    pcmd->data = (void *)g_malloc0(len);
    memcpy(pcmd->data, data, len);
    pcmd->len = len;
    pcmd->delay = delay;
    g_async_queue_push_unlocked(s_asyncQueue, pcmd);
    g_async_queue_unlock(s_asyncQueue);
}
static inline void build_sensor_command(guint id, guint cmd, guchar *data, guint len, guint delay, gboolean notify, GCond *cond, GMutex *mutex)
{
    struct _cmd_list *pcmd = (struct _cmd_list *)
                g_malloc0(sizeof(struct _cmd_list));

    g_async_queue_lock(s_asyncQueue);
    pcmd->id = id;
    pcmd->cmd = cmd;
    pcmd->data = (void *)g_malloc0(len);
    memcpy(pcmd->data, data, len);
    pcmd->len = len;
    pcmd->delay = delay;
    pcmd->notify = notify;
    pcmd->cond = cond;
    pcmd->mutex = mutex;
    //printf("pcmd->cond=%p,  pcmd->mutex=%p\n", pcmd->cond, pcmd->mutex);
    g_async_queue_push_unlocked(s_asyncQueue, pcmd);
    g_async_queue_unlock(s_asyncQueue);
}

#define NTP_TIMESTAMP_DELTA 2208988800ull
#define LI(packet)   (uint8_t) ((packet.li_vn_mode & 0xC0) >> 6) // (li   & 11 000 000) >> 6
#define VN(packet)   (uint8_t) ((packet.li_vn_mode & 0x38) >> 3) // (vn   & 00 111 000) >> 3
#define MODE(packet) (uint8_t) ((packet.li_vn_mode & 0x07) >> 0) // (mode & 00 000 111) >> 0
gboolean require_internet_datetime(char *host_name)
{
    int sockfd, n; // Socket file descriptor and the n return result from writing/reading from the socket.
    int portno = 123; // NTP UDP port number.
    //char* host_name = "us.pool.ntp.org"; // NTP server host-name.

    // Structure that defines the 48 byte NTP packet protocol.
    typedef guchar uint8_t;
    typedef guint uint32_t;
    typedef struct
    {
        uint8_t li_vn_mode;      // Eight bits. li, vn, and mode.
                     // li.   Two bits.   Leap indicator.
                     // vn.   Three bits. Version number of the protocol.
                     // mode. Three bits. Client will pick mode 3 for client.

        uint8_t stratum;         // Eight bits. Stratum level of the local clock.
        uint8_t poll;            // Eight bits. Maximum interval between successive messages.
        uint8_t precision;       // Eight bits. Precision of the local clock.

        uint32_t rootDelay;      // 32 bits. Total round trip delay time.
        uint32_t rootDispersion; // 32 bits. Max error aloud from primary clock source.
        uint32_t refId;          // 32 bits. Reference clock identifier.

        uint32_t refTm_s;        // 32 bits. Reference time-stamp seconds.
        uint32_t refTm_f;        // 32 bits. Reference time-stamp fraction of a second.

        uint32_t origTm_s;       // 32 bits. Originate time-stamp seconds.
        uint32_t origTm_f;       // 32 bits. Originate time-stamp fraction of a second.

        uint32_t rxTm_s;         // 32 bits. Received time-stamp seconds.
        uint32_t rxTm_f;         // 32 bits. Received time-stamp fraction of a second.

        uint32_t txTm_s;         // 32 bits and the most important field the client cares about. Transmit time-stamp seconds.
        uint32_t txTm_f;         // 32 bits. Transmit time-stamp fraction of a second.

    } ntp_packet;              // Total: 384 bits or 48 bytes.

    // Create and zero out the packet. All 48 bytes worth.
    ntp_packet packet = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    memset( &packet, 0, sizeof( ntp_packet ) );

    // Set the first byte's bits to 00,011,011 for li = 0, vn = 3, and mode = 3. The rest will be left set to zero.
    *( ( char * ) &packet + 0 ) = 0x1b; // Represents 27 in base 10 or 00011011 in base 2.

    // Create a UDP socket, convert the host-name to an IP address, set the port number,
    // connect to the server, send the packet, and then read in the return packet.
    struct sockaddr_in serv_addr; // Server address data structure.
    struct hostent *server;      // Server data structure.

    sockfd = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP ); // Create a UDP socket.
    if ( sockfd < 0 ) {
        printf( "ERROR opening socket" );
        return FALSE;
    }

    server = gethostbyname( host_name ); // Convert URL to IP.
    if ( server == NULL ) {
        printf( "ERROR, no such host" );
        return FALSE;
    }

    // Zero out the server address structure.
    bzero( ( char* ) &serv_addr, sizeof( serv_addr ) );
    serv_addr.sin_family = AF_INET;

    // Copy the server's IP address to the server address structure.
    bcopy( ( char* )server->h_addr, ( char* ) &serv_addr.sin_addr.s_addr, server->h_length );

    // Convert the port number integer to network big-endian style and save it to the server address structure.
    serv_addr.sin_port = htons( portno );

    // Call up the server using its IP address and port number.
    if ( connect( sockfd, ( struct sockaddr * ) &serv_addr, sizeof( serv_addr) ) < 0 ) {
        printf( "ERROR connecting" );
        return FALSE;
    }

    // Send it the NTP packet it wants. If n == -1, it failed.
    n = write( sockfd, ( char* ) &packet, sizeof( ntp_packet ) );
    if ( n < 0 ) {
        printf( "ERROR writing to socket" );
        return FALSE;
    }

    // Wait and receive the packet back from the server. If n == -1, it failed.
    n = read( sockfd, ( char* ) &packet, sizeof( ntp_packet ) );
    if ( n < 0 ) {
        printf( "ERROR reading from socket" );
        return FALSE;
    }

    // These two fields contain the time-stamp seconds as the packet left the NTP server.
    // The number of seconds correspond to the seconds passed since 1900.
    // ntohl() converts the bit/byte order from the network's to host's "endianness".
    packet.txTm_s = ntohl( packet.txTm_s ); // Time-stamp seconds.
    packet.txTm_f = ntohl( packet.txTm_f ); // Time-stamp fraction of a second.

    // Extract the 32 bits that represent the time-stamp seconds (since NTP epoch) from when the packet left the server.
    // Subtract 70 years worth of seconds from the seconds since 1900.
    // This leaves the seconds since the UNIX epoch of 1970.
    // (1900)------------------(1970)**************************************(Time Packet Left the Server)
    time_t txTm = ( time_t ) ( packet.txTm_s - NTP_TIMESTAMP_DELTA );

    // Print the time we got from the server, accounting for local timezone and conversion from UTC time.
    printf( "Time: %s %u:%u:%u\n", ctime( ( const time_t* ) &txTm ), (uint32_t)txTm, (uint32_t)packet.txTm_s, (uint32_t)packet.txTm_f);
    
    struct timeval  tv;
    tv.tv_sec = txTm;
    tv.tv_usec= packet.txTm_f / 1000;
    settimeofday(&tv, NULL);
    s_net_para._ntp.isUpdate = TRUE;
    s_net_para._ntp.updateTime = txTm;
    return TRUE;
}

//
static void inline signalCommander() {
    g_log (TAG, G_LOG_LEVEL_DEBUG, "signalCommander");
    g_mutex_lock (&s_lock);
    g_cond_signal (&s_cond);
    g_mutex_unlock (&s_lock);

}
gboolean seconds_rec_timeout(gpointer user_data) {
    g_log (TAG, G_LOG_LEVEL_DEBUG, "rec data timeout %p", user_data);
    if(user_data != NULL) {
        if(((struct _cmd_list *)user_data)->resend > 0) {
            struct _cmd_list * pcmd = (struct _cmd_list *)g_malloc0(sizeof(struct _cmd_list));
            memcpy(pcmd, user_data, sizeof(struct _cmd_list));
            pcmd->resend --;
            g_log (TAG, G_LOG_LEVEL_DEBUG, "push the id(%d) back to queue:%d", pcmd->id, pcmd->resend);
            // push the command back to queue
            g_async_queue_lock(s_asyncQueue);
            g_async_queue_push_unlocked(s_asyncQueue, pcmd);
            g_async_queue_unlock(s_asyncQueue);
        }
    }
    signalCommander();
    s_failure ++;
    s_to_rec_res = 0;
    return FALSE;
}

// save some sensor info for future
void save_volt_to_db(guint id, guint ts, guint volt, guint rssi) {
    
}

//
void recovery_alarm_confirm_shadow(GSList  *argument_list) {
	guint len2 = g_slist_length(argument_list);
	for(int j=0; j<len2; j++) {
		struct _argument_st *p2 = g_slist_nth_data (argument_list, j);
		if(p2->confirm != NULL) {
			p2->confirm->shadow = p2->confirm->times;
			printf("shadow:%d times:%d\n", p2->confirm->shadow , p2->confirm->times);
		}
	}
}
guchar host_channel_to_subG(guint channel)
{
    if(channel == 1)return 102;
    else if(channel == 2)return 101;
    else if(channel == 4)return 103;
    else if(channel == 8)return 119;
    else if(channel == 16)return 116;
    else return 102;
}
guchar subG_channel_to_host(guint channel) {
	if(channel == 102) return 1;
	else if(channel == 101) return 2;
	else if(channel == 103) return 4;
	else if(channel == 116) return 16;
	else if(channel == 119) return 8;
	else 1;
}
guchar host_freq_to_subG(guint maxfreq)
{
    if(maxfreq == 100)return 6;
    else if(maxfreq == 200)return 7;
    else if(maxfreq == 500)return 8;
    else if(maxfreq == 1000)return 9;
    else if(maxfreq == 2000)return 10;
    else if(maxfreq == 5000)return 11;
    else if(maxfreq == 10000)return 12;
    else if(maxfreq == 20000)return 13;
    else return 9;
}
guchar host_nums_to_subG(guint nums)
{
    if(nums == 256)return 8;
    else if(nums == 512)return 9;
    else if(nums == 1024)return 10;
    else if(nums == 2048)return 11;
    else if(nums == 4096)return 12;
    else return 10;
}

struct _sensor_info_st {
	guint sid;
	guint ver;
	guint volt;
	guint rssi;
};
gboolean CreateSensorJsonInfo(void *pData, char **pjson ) {
/*{
    "gid":"11-22-33-44",
	"message":"sensorinfo",
	"id":100,
	"version":20170704,
	"volt":330,
	"rssi":64
}*/
	cJSON * pJsonRoot = NULL;
	struct _sensor_info_st *_dat = (struct _sensor_info_st*) pData;
	gboolean ret = FALSE;
	pJsonRoot = cJSON_CreateObject();
    if(NULL == pJsonRoot){
        goto json_err;
    }
    cJSON_AddStringToObject(pJsonRoot, "gid", s_gid);
    cJSON_AddStringToObject(pJsonRoot, "message", "sensorinfo");
    cJSON_AddNumberToObject(pJsonRoot, "sid", _dat->sid);
    char *ver = NULL;
    asprintf(&ver, "%X", _dat->ver);
    cJSON_AddStringToObject(pJsonRoot, "version", ver);
    free(ver);
    cJSON_AddNumberToObject(pJsonRoot, "volt", _dat->volt);
    cJSON_AddNumberToObject(pJsonRoot, "rssi", _dat->rssi);
    
    char *json = cJSON_Print(pJsonRoot);
    if(json == NULL){
        goto json_err;
    }
    asprintf(pjson, "%s", json);
    cJSON_free(json);
    ret = TRUE;
json_err:
	cJSON_Delete(pJsonRoot);
    //printf("%s\n", pjson);
	return ret;
}
#if 0
gboolean CreateDataJsonInfo(void *pData, char **pjson) {
	gboolean ret = FALSE;
	struct __data_st *_dat = (struct __data_st *)pData;

	json_object *my_object = json_object_new_object();
	json_object_object_add(my_object, "gid", json_object_new_string(s_gid));
	json_object_object_add(my_object, "message", json_object_new_string("data"));
	json_object_object_add(my_object, "sid", json_object_new_int(_dat->sid));
	json_object_object_add(my_object, "timestamp", json_object_new_int(_dat->ts));
	json_object *batt_object = json_object_new_object();
	json_object_object_add(batt_object, "volt", json_object_new_int(_dat->volt));
	json_object_object_add(batt_object, "alarm", json_object_new_int(0));
	json_object_object_add(my_object, "batt", batt_object);
	
	json_object *my_array = json_object_new_array();
	for(int i=0; i<_dat->nums; i++) {
		char *sd = NULL;
		asprintf(&sd, "%.4f", _dat->data[i]);
		json_object_array_add(my_array, json_object_new_double_s(_dat->data[i], sd));
		free(sd);
	}
	json_object_object_add(my_object, "data", my_array);
	printf("my_object.to_string()=%s\n", json_object_to_json_string(my_object));
	return ret;
}
#endif
gboolean sensor_data_info_to_mqtt(void *pData) {
	//0:sid 1:ts 2:volt 3:rssi: 4:type 5:channel 6:freq 7:nums 8:aflag 9:p-value 10:pp-value 11:rms 
	cJSON * pJsonRoot = NULL;
	cJSON *node;
	struct __data_st *_dat = (struct __data_st *)pData;
    struct cJSON_NumOpts opts = {
		.decimal_places = 4,
		.format = NULL,
	};
	gboolean ret = FALSE;
	pJsonRoot = cJSON_CreateObject();
    if(NULL == pJsonRoot){
        goto json_err;
    }
    cJSON_AddStringToObject(pJsonRoot, "gid", s_gid);
    cJSON_AddStringToObject(pJsonRoot, "message", "data");
    cJSON_AddNumberToObject(pJsonRoot, "sid", _dat->sid);
    cJSON_AddNumberToObject(pJsonRoot, "timestamp", _dat->ts);
    cJSON *pNodeBatt = cJSON_CreateObject();
		cJSON_AddNumberToObject(pNodeBatt, "volt", _dat->volt);
		cJSON_AddNumberToObject(pNodeBatt, "alarm", 0);
		cJSON_AddItemToObject(pJsonRoot, "batt", pNodeBatt);
	cJSON_AddNumberToObject(pJsonRoot, "rssi", _dat->rssi);
	
	cJSON *pNodeData = cJSON_CreateObject();
		cJSON_AddNumberToObject(pNodeData, "type", _dat->type);
		cJSON *pJsonArgs = cJSON_CreateObject();
			cJSON_AddNumberToObject(pJsonArgs, "channel", _dat->channel);
			cJSON_AddNumberToObject(pJsonArgs, "freq", _dat->freq);
			cJSON_AddNumberToObject(pJsonArgs, "nums", _dat->nums);
		cJSON_AddItemToObject(pNodeData, "argument", pJsonArgs);
		
		cJSON *pJsonValue = cJSON_CreateObject();
			cJSON * pJsonArray = cJSON_CreateArray();
#ifdef __ARM__
			guint *pflag = (guint*)GUINT_TO_POINTER(_dat->aflag);
#else
			guint pflag[4]={0x10001, 0x10002, 0x10004, 0x10000};
#endif		
			for(guint i=0; i<_dat->alen; i++) {
				cJSON *pJsonAlarm = cJSON_CreateObject();
				cJSON_AddNumberToObject(pJsonAlarm, "src", pflag[i] >> 16);
				cJSON_AddNumberToObject(pJsonAlarm, "flag", pflag[i]  & 0xFFFF);
				cJSON_AddItemToArray(pJsonArray, pJsonAlarm);
			}
			cJSON_AddItemToObject(pJsonValue, "alarm", pJsonArray);
		
			//node = cJSON_CreateIntArray((int*)GUINT_TO_POINTER(_dat->aflag), _dat->alen);
			//cJSON_AddItemToObject(pJsonValue, "alarm", node);
			
			node = cJSON_CreateNumber(_dat->temp);
			opts.decimal_places = 1;
			cJSON_SetOpts(node, &opts);
			cJSON_AddItemToObject(pJsonValue, "temperature", node);
			node = cJSON_CreateNumber(_dat->p_val);
			opts.decimal_places = 4;
			cJSON_SetOpts(node, &opts);
			cJSON_AddItemToObject(pJsonValue, "p_value", node);
			node = cJSON_CreateNumber(_dat->pp_val);
			cJSON_SetOpts(node, &opts);
			cJSON_AddItemToObject(pJsonValue, "pp_val", node);
			node = cJSON_CreateNumber(_dat->rms_val);
			cJSON_SetOpts(node, &opts);
			cJSON_AddItemToObject(pJsonValue, "rms_val", node);
			node = cJSON_CreateFloatArray(_dat->data, _dat->nums, 4);
			cJSON_AddItemToObject(pJsonValue, "raw", node);
		cJSON_AddItemToObject(pNodeData, "value", pJsonValue);
	cJSON_AddItemToObject(pJsonRoot, "data", pNodeData);

    char *json = cJSON_Print(pJsonRoot);
    if(json == NULL){
        goto json_err;
    }
	//printf(json);
	if(mosquitto_publish(s_mosq, &s_mid, "/sensors", strlen(json), json, 2, false)) {
		ret = TRUE;
	}
	cJSON_free(json);
json_err:
    cJSON_Delete(pJsonRoot);
	return ret;
}

static gpointer send_data_thread(gpointer user_data) {
	guint *pdat = NULL;
	g_cond_init(&s_dat_cond);
	g_mutex_init (&s_dat_lock);
	while(1) {
		g_mutex_lock (&s_dat_lock);
		gint64 end_time = g_get_monotonic_time () + 2 * G_TIME_SPAN_SECOND;
		if(!g_cond_wait_until(&s_dat_cond, &s_dat_lock, end_time)) {
			//g_log (TAG, G_LOG_LEVEL_DEBUG, "%s: wait signal timeout", __func__);
		}
		g_mutex_unlock (&s_dat_lock);
		G_LOCK(db_protect);
		int id = sensor_data_info_form_db(SENSOR_DBTMP_FILE_PATH, &pdat);
		G_UNLOCK(db_protect);
		printf("id = %d\n", id);
		if(id > 0) {
			//for(int j=0; j<32; j++) {
			//	printf("%X ", pdat[12+j]);
			//}
			if(pdat != NULL) {
				// send out data & delete & save to local
				if(sensor_data_info_to_mqtt(pdat)) 
				{
					//sensor_data_info_update_db(SENSOR_DBTMP_FILE_PATH, id);
				}
#ifdef __ARM__
				struct __data_st *_data = (struct __data_st *) pdat;
				g_free(GUINT_TO_POINTER(_data->aflag));
#endif				
				g_free(pdat);
			}
		}
	}
	
	return GINT_TO_POINTER(-1);
}

#define INNER_WIN	1
#define OUTER_WIN	2
#define HIGH_LIMIT	4
#define LOW_LIMIT	8
static gpointer handle_sensor_data_thread(gpointer user_data) {
	struct _check_alarm  *pca = (struct _check_alarm  *)user_data;
	guint *alarm = NULL;
#define PCA_P_VALUE pca->pfdat[0]
#define PCA_PP_VALUE pca->pfdat[1]
#define PCA_RMS_VALUE pca->pfdat[2]
#define PCA_TMP_VALUE pca->pfdat[11]
	printf( "%s %f %f %f\n", __func__, 
				pca->pfdat[0], pca->pfdat[1], pca->pfdat[2]);
	printf( "%s %d %d %d %d\n\n", __func__, 
				pca->pidat[3], pca->pidat[4], pca->pidat[5], pca->pidat[6]);

	guint len3 = g_slist_length(pca->alarm_list);
	alarm = (guint*) g_malloc0(len3 * 4);
	//printf("alarm: %p\n",alarm);
	for(int i=0; i<len3; i++) {
		struct _alarm_st *p3 = g_slist_nth_data (pca->alarm_list, i);
		g_log (TAG, G_LOG_LEVEL_DEBUG, "alarm type: %d src:%d", p3->type, p3->src);
		
		//src=1 p-value, src=2 pp-value, src=4 rms, src=8 temp
		// alarm[] b24~b31 type b16~b23 src, b0~b3:al1 b4~b7:al2 b8~11:al3 b12~b15:al4
		// 即一种报警类型，最多只能设置4组边界值
		gfloat value = (p3->src == 1)?PCA_P_VALUE:(p3->src == 2?PCA_PP_VALUE:(p3->src == 4?PCA_RMS_VALUE:PCA_TMP_VALUE));
		printf("value = %f\n", value);
		switch(p3->type) { // alarm type
			case INNER_WIN: {
				guint len4 = g_slist_length (p3->limit_list);
				for(int j=0; j<len4; j++) {
					struct _limit_st *p4 = g_slist_nth_data (p3->limit_list, j);
					printf("low=%f high=%f\n", p4->low, p4->high);
					if(value > p4->low && value < p4->high){
						alarm[i] |= (1 << (4*j)) | (p3->src << 16) | (p3->type << 24);
						printf("alarm %x\n", alarm[i]);
					}
				}
				break;
			}
			case HIGH_LIMIT: {
				guint len4 = g_slist_length (p3->limit_list);
				for(int j=0; j<len4; j++) {
					struct _limit_st *p4 = g_slist_nth_data (p3->limit_list, j);
					// we only care about higher, if large than it, alrm !
					if(value > p4->high) {
						alarm[i] |= (1 << (4*j)) | (p3->src << 16) | (p3->type << 24);
					}
				}
				break;
			}
			case LOW_LIMIT: {
				guint len4 = g_slist_length (p3->limit_list);
				for(int j=0; j<len4; j++) {
					struct _limit_st *p4 = g_slist_nth_data (p3->limit_list, j);
					// we only care about low, if lower than it, alrm !
					if(value < p4->low) {
						alarm[i] |= (1 << (4*j)) | (p3->src << 16) | (p3->type << 24);
					}
				}
				break;
			}
			case OUTER_WIN: {
				break;
			}
		}
		pca->pidat[7] = GPOINTER_TO_UINT(alarm);	// save the alarm flag
		pca->pidat[12] = len3;										// alarm length
		//printf("pca->pidat[7] %p\n", (void *)pca->pidat[7]);
		if(alarm > 0 && pca->confirm != NULL) {
			if(pca->confirm->shadow > 0) {
				// CHECK: need to recovery confirm.shadow from confirm.times
				pca->confirm->shadow --;
				printf("shadow = %x\n",pca->confirm->shadow);
				// TODO: add temporary sample command to list
				//发送临时采集命令，参数使用当前参数
				//有个问题，数据回来后，如何再次判断是否报警？即数据应该有对应的信息。
				//一般临时采集命令应该有两种情况：1,不带参数，但是表明本次采用使用现有的哪一组参数，
				//回传数据时idx表明是临时数据及参数组，这种情况一般用于周期采集过程的报警采样确认;
				//2,带参数，可由用户指定或默认参数，一般用于用户临时采样
				//guchar data[1];
				//data[0] = pca->idx
				//build_sensor_command(pca->id, HOST_INQUIRE_SENSOR_TDATA, data, 1, 6, FALSE, NULL, NULL);
			}
		}
	}
	
	// CHECK：may I need a lock??
	G_LOCK(db_protect);
	sensor_data_info_insert_db(SENSOR_DBTMP_FILE_PATH, pca->id, pca->pfdat, pca->pidat);
	G_UNLOCK(db_protect);
	g_free(alarm);
	// 
	g_log (TAG, G_LOG_LEVEL_DEBUG, "%s:signal data thread to do the job", __func__);
	g_mutex_lock (&s_dat_lock);
	g_cond_signal(&s_dat_cond);
	g_mutex_unlock (&s_dat_lock);
	g_free(pca);
	return GINT_TO_POINTER(-1);
}
//0:p-value 1:pp-value 2:rms 3:channel 4:freq 5:nums 6:ts
void handle_sensor_data(struct _cache_st  *pCache) {
	struct _check_alarm *pca = g_malloc0(sizeof(struct _check_alarm));
	
	pca->id = pCache->id;
	pca->idx = pCache->idx ;
	pca->pfdat = GUINT_TO_POINTER(pCache->address[pca->idx & 0x0F]);
	pca->pidat = GUINT_TO_POINTER(pCache->address[pca->idx & 0x0F]);
	//dumpmem((guchar *)pca->pdat, 28);
	//g_log (TAG, G_LOG_LEVEL_DEBUG, "%s:idx=%d pdat=%p", __func__, pCache->idx, pca->pdat);
	// get idx corresponding parameter
	guint len = g_slist_length(s_sensor_list);
	for(int i=0; i<len; i++) {
            struct _sensor_st *p = g_slist_nth_data (s_sensor_list, i);
            if(pCache->id == p->id) {
				if(pca->idx == 0) {
					break;					// temporary sample, not need alarm check
				}else if(pca->idx & 0xF0) {
					pca->idx >>= 4;	// alarm temporary sample, idx room at high 4bits
				}
				//printf("-----%d\n", pca->idx);
				struct _argument_st *p2 = g_slist_nth_data (p->argument_list, pca->idx-1); //idx start form 1
				pca->alarm_list = p2->alarm_list;
				pca->confirm = p2->confirm;
				//printf("-----%p %p\n", p2->confirm, p2->alarm_list);
				break;
			}
	}
	GError *error = NULL;
	GThread * pthread;
	pthread = g_thread_try_new ("handle sensor data", handle_sensor_data_thread, pca, &error);
	if(pthread != NULL) {
		g_thread_unref(pthread);
	}else {
		if(error != NULL) {
			printf("g_thread_try_new fail cause by:%s\n", error->message);
		}
	}
}

#define HEAD            1
#define BODY            2
#define PLEN_POS    8
#define PCMD_POS  12
#define H2L               5
#define BATT_POS    9
#define INDEX
guint s_length = 0;
static gpointer reader_loop_thread(gpointer user_data) {
    guchar buf[1024];
    size_t c = 1, r;
    gint i = 0;
    gint status = HEAD;
    gboolean unknow_cmd = FALSE;
#define PK_LEN buf[8]
#define PK_VOLT (buf[9]+250)
#define PK_RSSI buf[14]
#define PK_ID ((buf[5] << 8) | buf[4])
#define PK_CMD buf[PCMD_POS]
#define PK_INDEX ((buf[11] << 8) | buf[10])
#define PK_FLAG buf[13]
#define MAKE_SHORT(pos) (p[pos]|(p[pos+1]<<8))
#define MAKE_LONG(pos) (p[pos]|(p[pos+1]<<8)|(p[pos+2]<<16)|(p[pos+3]<<24))
    while((r=pipe_pop(s_c, &buf[i], c))){
        //g_log (TAG, G_LOG_LEVEL_DEBUG, "%d %d %d %X", status, (gint)r, i, buf[i]);
        // receive head struct, 16bytes
        switch(status) {
			case HEAD: {
				if(i==0 && buf[i] == 0x55){i++; continue;}
				else if(i==1 && buf[i] == 0xAA){i++; continue;}
				else if(i==2 && buf[i] == 0x99){i++; continue;}
				else if(i==3 && buf[i] == 0x66){i++; c=H2L; continue;}
				else {
					if(i < 4) {
						if(buf[i] == 0x55){
							buf[0] = buf[i];
							i = 1;
							c = 1;
							break;
						}
					} else {
						i = BATT_POS;                 //
						c = buf[PLEN_POS] + 7;  // buf[8] packet length
						status = BODY;
						break;
					}
					i = 0;
					c = 1;
				}
				break;
			}
			case BODY: {
				unknow_cmd = FALSE;
				switch(PK_CMD) {
					case HOST_SET_SENSOR_SAMPLE_ARGS: {
						g_log (TAG, G_LOG_LEVEL_DEBUG, "subG report setting sensor parameter successful");
						dumpmem(&buf[16], buf[PLEN_POS]);
						break;
					}
					case HOST_INQUIRE_SENSOR_TDATA: {
						g_log (TAG, G_LOG_LEVEL_DEBUG, "subG report inquire sensor temporary successful");
						dumpmem(&buf[16], buf[PLEN_POS]);
						break;
					}
					case HOST_INQUIRE_SENSOR_NETCFG: {
						g_log (TAG, G_LOG_LEVEL_DEBUG, "subG report inquire sensor rf info successful");
						//00 00 1E 00 30 27 05 1E 17 B7 D1 38 01 00 01 00 
						dumpmem(&buf[16], buf[PLEN_POS]);
						break;
					}
					case SUBG_COMMAND_CONFIRM: {
						g_log (TAG, G_LOG_LEVEL_DEBUG, "subG confirm that it got host command");
						break;
					}
					case HOST_INQUIRE_SUBG_INFO: {
						g_log (TAG, G_LOG_LEVEL_DEBUG, "subG report inquire its rf info successful");
						dumpmem(&buf[16], buf[PLEN_POS]); //actually less 4bytes
						break;
					}
					case HOST_SET_SUBG_RFINFO: {
						//guint delay = 30*1000;
						g_log (TAG, G_LOG_LEVEL_DEBUG, "subG report setting rf info successful");
						//build_command(HOST_INQUIRE_SENSOR_NETCFG, (guchar *)&delay, 4, 5);
						build_command(HOST_INQUIRE_SUBG_INFO, NULL, 0, 3);
						s_status = 1;
						break;
					}
					case HOST_SET_SUBG_TIMESTAMP: {
						struct _rf_st info = {
							.channel = (s_net_para._rf.channel-769)*10,
							.pan = s_net_para._rf.pan,
							.power = s_net_para._rf.power
						};
						g_log (TAG, G_LOG_LEVEL_DEBUG, "subG report time adjust successful");
						g_log (TAG, G_LOG_LEVEL_DEBUG, "host set subG rf:%d %d %d", s_net_para._rf.channel, s_net_para._rf.pan, s_net_para._rf.power);
						//platform_info_from_db(SENSOR_DB_FILE_PATH, &info.channel, &info.pan, &info.power);
						if((s_status & 0x1000) == 0) {		//not fake time requier
							build_command(HOST_SET_SUBG_RFINFO, (guchar *)&info, sizeof(struct _rf_st), 3);
						}
						break;
					}
					case SUBG_FAKE_TIMESTAMP_REQUIRE:
						s_status |= 0x1000;
					case SUBG_REQUIRE_HOST_TIMESTAMP: {
						guint timestamp;
						if(!s_net_para._ntp.isUpdate) {
							require_internet_datetime("ntp1.aliyun.com");
						}
						timestamp = (guint)(g_get_real_time() / 1000000);
						g_log (TAG, G_LOG_LEVEL_DEBUG, "subG time adjust require");
						build_command(HOST_SET_SUBG_TIMESTAMP, (guchar*)&timestamp, 4, 3);
						break;
					}
					case SUBG_REPORT_SENSOR_INFO: {
						g_log (TAG, G_LOG_LEVEL_DEBUG, "subG report sensor's(%d) info", PK_ID);
						//04 07 17 20 00 00 A0 05 02 00 01 00 66 01 09 0B 
						//00 00 00 00 00 00 00 00 00 00 00 00
						struct _sensor_info_st _si;
						guint *p = (guint *)&buf[16];
						_si.sid = PK_ID;
						_si.ver = p[0];
						_si.volt = PK_VOLT;
						_si.rssi = PK_RSSI;
						char *pjson = NULL;
						if(CreateSensorJsonInfo((void *)&_si, &pjson)) {
							int ret = mosquitto_publish(s_mosq, NULL, "/sensors", strlen(pjson), pjson, 2, false);
							if(ret == MOSQ_ERR_SUCCESS) {
							}
							free(pjson);
						}
						break;
					}
					case SUBG_REPORT_SENSOR_DATA: {
						//g_log (TAG, G_LOG_LEVEL_DEBUG, "subG report sensor's(%d) data(%d) %d", PK_ID, PK_INDEX, PK_VOLT);
						struct _cache_st  *pCache = findIdCache(PK_ID);
						if(pCache == NULL)break;
						if(s_to_rec_res > 0) {
							g_source_remove(s_to_rec_res);
							s_to_rec_res = 0;
							// maybe sensor has more data send to us, delay some time when send out the SIGNAL
							// or maybe the transmission process is interrupted, we need a way SIGAL commander
							s_to_rec_res = g_timeout_add_seconds(1, seconds_rec_timeout, NULL);
						}
						if(PK_INDEX == 1) {
							s_length = 0;
							//g_log (TAG, G_LOG_LEVEL_DEBUG, "%d %d %d %d %d %d %s", p[0], p[1], (p[3]<<8|p[2]),  (p[5]<<8|p[4]), p[6], p[7], ctime( ( const time_t* ) &txTm ));
							guchar *p = &buf[16];
							pCache->channel = p[0];
							pCache->freq = MAKE_SHORT(2);
							pCache->nums = MAKE_SHORT(4);
							// FIXME:
							// idx 表明当前数据的参数组，低4位有效：0,临时采集 1,参数组1 2,参数组2
							// 高4位表示临时采集所用的通道，1,参数组1 2,参数组2
							pCache->idx = p[7];
							pCache->ts = MAKE_LONG(12);
							guint tmp = MAKE_LONG(16);
							pCache->coef = *(gfloat *)&tmp;
							tmp = MAKE_LONG(20);
							pCache->value = *(gfloat *)&tmp;
							pCache->crc = MAKE_LONG(24);
							guint *puint = (guint *)GUINT_TO_POINTER(pCache->address[pCache->idx & 0x0F]);
							puint[3] = subG_channel_to_host(pCache->channel);
							puint[4] = pCache->freq;
							puint[5] = pCache->nums;
							// TODO:check the timestamp if invalid
							puint[6] = pCache->ts;
							puint[8] = PK_VOLT;
							puint[9] = PK_RSSI;
							puint[10] = pCache->idx & 0x0F;
							
							g_log (TAG, G_LOG_LEVEL_DEBUG, "idx:%d volt:%d %X %f %f", pCache->idx, PK_VOLT, MAKE_LONG(20), pCache->coef, pCache->value);
							g_log (TAG, G_LOG_LEVEL_DEBUG, "%u %u %u %u", pCache->channel, pCache->freq, pCache->nums, pCache->ts);
							
						} else if(PK_INDEX == 2) {
							//dumpmem(&buf[16], PK_LEN);
							//0:p-value 1:pp-value 2:rms 3:channel 4:freq 5:nums 6:ts 7:aflat
							guchar *p = &buf[16];
							gfloat *pfloat = (gfloat *)GUINT_TO_POINTER(pCache->address[pCache->idx & 0x0F]);
							guint tmp = MAKE_LONG(0);
							pfloat[0] = *(gfloat *)&tmp;
							tmp = MAKE_LONG(4);
							pfloat[1] = *(gfloat *)&tmp;
							tmp = MAKE_LONG(12);
							pfloat[2] = *(gfloat *)&tmp;
							pfloat[11] = 25.1f;
						} else {
							gshort *psh = (gshort *)&buf[16];
							gfloat *pfloat = (gfloat *)GUINT_TO_POINTER(pCache->address[pCache->idx & 0x0F]);
							for(guchar i=0; i< (PK_LEN>>1); i++) {
								//printf("%d ", 24 * (PK_INDEX-3) + i);
								pfloat[INFO_LEN + 24 * (PK_INDEX-3) + i] =  pCache->coef * psh[i];
								//printf("%f ", pCache->coef * psh[i]);
							}
						}
						s_length += PK_LEN;
						
						if(PK_FLAG == 0){
							// end, save & send date
							g_log (TAG, G_LOG_LEVEL_DEBUG, "subG has reported sensor's(%d) data(%dB) @%s", PK_ID, s_length, ctime( ( const time_t* ) &pCache->ts ));

							handle_sensor_data(pCache);
							sensor_volt_info_to_db(SENSOR_DB_FILE_PATH, pCache->id, pCache->ts, PK_VOLT, PK_RSSI);
						}
						break;
					}
					default: {
						g_log (TAG, G_LOG_LEVEL_DEBUG, "unknow cmd :%x", PK_CMD);
						unknow_cmd = 1;
						break;
					}
				}
				i = 0;
				c = 1;
				status = HEAD;
				if(PK_CMD != SUBG_REPORT_SENSOR_DATA &&
					PK_CMD != SUBG_COMMAND_CONFIRM &&
					PK_CMD != SUBG_REQUIRE_HOST_TIMESTAMP &&
					PK_CMD != SUBG_FAKE_TIMESTAMP_REQUIRE && ! unknow_cmd) {
					g_log (TAG, G_LOG_LEVEL_DEBUG, "subG say cmd end:%x", PK_CMD);    
					if(s_to_rec_res > 0) {
						// clear the watch dog
						g_source_remove(s_to_rec_res);
						s_to_rec_res = 0;
					}
					s_failure = 0;
					signalCommander();
				}
				break;
			}
		}
    }
    return GINT_TO_POINTER(-1);
}

//
// parse sensor JSON format configure
int ParseSensorInfoFromJson(char *json, int length)
{
    int ret = -1;
    cJSON *pJsonRoot = NULL, *pJSensor, *_Jsensor, *pJnode;
    cJSON *pJSlot, *_Jslot;
    cJSON *pJArgs, *_Jargs;
    cJSON *pJAlarm, *_Jalarm;
    cJSON *pJLimit, *_Jlimit;
    cJSON *pJConfirm, *_Jconfirm;
    int iSize, i;
    int jSize, j;
    int kSize, k;
    int mSize, m;
    int lSize, l;
    int nSize, n;
    struct _sensor_st *pSensorTmp;
    struct _active_slot_st *pSlotTmp;
    struct _argument_st *pArgsTmp;
    struct _alarm_st *pAlarmTmp;
    struct _limit_st *pLimitTmp;
    struct _confirm_st *pConfirmTmp;
    
    if(json == NULL)goto outerr;
    
    pJsonRoot = cJSON_Parse(json);
    if(pJsonRoot == NULL){
		g_log (TAG, G_LOG_LEVEL_DEBUG, "is not a json file");
		goto outerr;
    }
    
    pJSensor = cJSON_GetObjectItem(pJsonRoot, "sensors");
    iSize = cJSON_GetArraySize(pJSensor);
    printf("---- %s\n", cJSON_Print(pJsonRoot));
    for(i = 0; i < iSize; i++){
        _Jsensor = cJSON_GetArrayItem(pJSensor, i);
        if(_Jsensor == NULL){
            continue;
        }
        // sensor info
        pSensorTmp = g_malloc0(sizeof(struct _sensor_st));
        if((pJnode = cJSON_GetObjectItem(_Jsensor, "id")) !=NULL)
			pSensorTmp->id = pJnode->valueint;
		if((pJnode = cJSON_GetObjectItem(_Jsensor, "group")) != NULL)
			pSensorTmp->group = pJnode->valueint;
		if((pJnode = cJSON_GetObjectItem(_Jsensor, "aquire_cycle")) != NULL)
			pSensorTmp->aquire_cycle = pJnode->valueint;
		if((pJnode = cJSON_GetObjectItem(_Jsensor, "triger")) != NULL)
			pSensorTmp->triger = pJnode->valueint;
		if((pJnode = cJSON_GetObjectItem(_Jsensor, "raw")) != NULL)
			pSensorTmp->raw = pJnode->valueint;
        //printf("---- %d\n", pSensorTmp->id);
        pJSlot = cJSON_GetObjectItem(_Jsensor, "active_slot");
        jSize = cJSON_GetArraySize(pJSlot);
        for(j = 0; j < jSize; j++){
            _Jslot = cJSON_GetArrayItem(pJSlot, j);
            if(_Jslot == NULL){
                continue;
            }
            
            pSlotTmp = g_malloc0(sizeof(struct _active_slot_st));
			if((pJnode = cJSON_GetObjectItem(_Jslot, "start")) != NULL)
				pSlotTmp->start = pJnode->valueint;
            if((pJnode = cJSON_GetObjectItem(_Jslot, "end")) != NULL)
				pSlotTmp->end = pJnode->valueint;
            pSensorTmp->active_list = g_slist_append(pSensorTmp->active_list, pSlotTmp);
            //printf("---- %d %d\n", pSlotTmp->start,  pSlotTmp->end);
        }

        pJArgs = cJSON_GetObjectItem(_Jsensor, "arguments");
        kSize = cJSON_GetArraySize(pJArgs);
        for(k = 0; k < kSize; k++){
            _Jargs = cJSON_GetArrayItem(pJArgs, k);
            if(_Jargs == NULL){
                continue;
            }
            pArgsTmp = g_malloc0(sizeof(struct _argument_st));
            if((pJnode = cJSON_GetObjectItem(_Jargs, "channel")) != NULL)
				pArgsTmp->channel = pJnode->valueint;
            if((pJnode = cJSON_GetObjectItem(_Jargs, "freq")) != NULL)
				pArgsTmp->freq = pJnode->valueint;
            if((pJnode = cJSON_GetObjectItem(_Jargs, "nums")) != NULL)
				pArgsTmp->nums = pJnode->valueint;
            //printf("---- %d %d %d\n", pArgsTmp->channel,  pArgsTmp->freq, pArgsTmp->nums);
            
            pJAlarm = cJSON_GetObjectItem(_Jargs, "alarm");
            mSize = cJSON_GetArraySize(pJAlarm);
            for(m=0; m<mSize; m++) {
                _Jalarm = cJSON_GetArrayItem(pJAlarm, m);
                if(_Jalarm == NULL){
                    continue;
                }
                pAlarmTmp = g_malloc0(sizeof(struct _alarm_st));
                if((pJnode = cJSON_GetObjectItem(_Jalarm, "type")) != NULL)
					pAlarmTmp->type = pJnode->valueint;
				if((pJnode = cJSON_GetObjectItem(_Jalarm, "source")) != NULL)
					pAlarmTmp->src = pJnode->valueint;
                //printf("---- %d %d\n", pAlarmTmp->type,  pAlarmTmp->src);
                
                pJLimit = cJSON_GetObjectItem(_Jalarm, "limit");
                lSize = cJSON_GetArraySize(pJLimit);
                if(lSize > 4) lSize = 4;
                for(l = 0; l < lSize; l++){
                    _Jlimit = cJSON_GetArrayItem(pJLimit, l);
                    if(_Jlimit == NULL){
                        continue;
                    }
                    pLimitTmp = g_malloc0(sizeof(struct _limit_st));
                    if((pJnode = cJSON_GetObjectItem(_Jlimit, "low")) != NULL)
						pLimitTmp->low = pJnode->valuedouble;
					if((pJnode = cJSON_GetObjectItem(_Jlimit, "high")) != NULL)
						pLimitTmp->high = pJnode->valuedouble;
                    //printf("---- %f %f\n", pLimitTmp->low ,  pLimitTmp->high);
                    pAlarmTmp->limit_list = g_slist_append(pAlarmTmp->limit_list, pLimitTmp);
                }
                pArgsTmp->alarm_list = g_slist_append(pArgsTmp->alarm_list, pAlarmTmp);
            }
            pJConfirm = cJSON_GetObjectItem(_Jargs, "confirm");
			if(pJConfirm != NULL) {
				pConfirmTmp = g_malloc0(sizeof(struct _confirm_st));
				if((pJnode = cJSON_GetObjectItem(pJConfirm, "times")) != NULL) {
					pConfirmTmp->times = pJnode->valueint;
					printf("times: %d\n", pConfirmTmp->times);
				}
				if((pJnode = cJSON_GetObjectItem(pJConfirm, "raw")) != NULL) {
					pConfirmTmp->raw = pJnode->valueint;
					printf("raw: %d\n", pConfirmTmp->raw);
				}
				pArgsTmp->confirm = pConfirmTmp;
			}
            pSensorTmp->argument_list = g_slist_append(pSensorTmp->argument_list, pArgsTmp);
        }
        s_sensor_list = g_slist_append(s_sensor_list, pSensorTmp);
    }
    // dump all sensor info
#if 1
    {
        guint len = g_slist_length(s_sensor_list);
        for(int i=0; i<len; i++) {
            struct _sensor_st *p = g_slist_nth_data (s_sensor_list, i);
            printf("id: %d\n", p->id);
            
            guint len1 = g_slist_length(p->active_list);
            for(int j=0; j<len1; j++) {
                struct _active_slot_st *p1 = g_slist_nth_data (p->active_list, j);
                printf("  start: %d end: %d\n", p1->start, p1->end);
            }
            
            guint len2 = g_slist_length(p->argument_list);
            for(int j=0; j<len2; j++) {
                struct _argument_st *p2 = g_slist_nth_data (p->argument_list, j);
                printf("  channel: %d freq: %d nums: %d\n", p2->channel, p2->freq, p2->nums);
                
                guint len3 = g_slist_length(p2->alarm_list);
                for(int k=0; k<len3; k++) {
                    struct _alarm_st *p3 = g_slist_nth_data (p2->alarm_list, k);
                    printf("    type: %d src: %d\n", p3->type, p3->src);
                    
                    guint len4 = g_slist_length(p3->limit_list);
                    //printf("    len4: %d\n", len4);
                    for(int n=0; n<len4; n++) {
						struct _limit_st *p4 = g_slist_nth_data(p3->limit_list, n);
						printf("    high: %f low: %f\n", p4->high, p4->low);
					}
                    
                }
                if(p2->confirm){
					printf("    times: %d raw: %d\n", p2->confirm->times, p2->confirm->raw);
				}
            }
        }
    }
#endif
    ret = 0;
outerr:
    cJSON_Delete(pJsonRoot);
    return ret;  
}

void RemoveSensorLIst()
{
	GSList *tmp;
	guint len = g_slist_length(s_sensor_list);
	for(int i=0; i<len; i++) {
		struct _sensor_st *p = g_slist_nth_data (s_sensor_list, i);
		guint len2 = g_slist_length(p->argument_list);
		for(int j=0; j<len2; j++) {
			struct _argument_st *p2 = g_slist_nth_data (p->argument_list, j);
			guint len3 = g_slist_length(p2->alarm_list);
			for(int k=0; k<len3; k++) {
				struct _alarm_st *p3 = g_slist_nth_data (p2->alarm_list, k);
				tmp = g_slist_remove_all(p3->limit_list, g_slist_nth_data(p3->limit_list, 0));
			}
			tmp = g_slist_remove_all(p2->alarm_list, g_slist_nth_data(p2->alarm_list, 0));
		}
		tmp = g_slist_remove_all(p->argument_list, g_slist_nth_data(p->argument_list, 0));
		tmp = g_slist_remove_all(p->active_list, g_slist_nth_data(p->active_list, 0));
	}
	tmp = g_slist_remove_all(s_sensor_list, g_slist_nth_data(s_sensor_list, 0));
	printf("kkkkkk %d\n", g_slist_length(s_sensor_list));
}
//
void initCache() {
    guint cnt = g_slist_length(s_sensor_list);
    s_cache = g_malloc0(sizeof(struct _cache_st) * cnt);
    for(guint i=0; i<cnt; i++) {
        struct _sensor_st *p = g_slist_nth_data (s_sensor_list, i);
        s_cache[i].id = p->id;
        guint cnt2 = g_slist_length(p->argument_list);
        for(int j=0; j<cnt2; j++) {
            struct _argument_st *p2 = g_slist_nth_data (p->argument_list, j);
            s_cache[i].channel = p2->channel;
            s_cache[i].address[1 + j] = GPOINTER_TO_UINT(g_malloc((INFO_LEN + p2->nums) * 4)); //pbuf[0] hold back for temporary, 16 for data info
            //printf("id: %d channel:%d buf:%p\n", s_cache[i].id, s_cache[i].channel, s_cache[i].pbuf[j]);
        }
        s_cache[i].address[0] = GPOINTER_TO_UINT(g_malloc((INFO_LEN + 4096) * 4)); // temporary sample MAX nums=4096
    }
}
//
void deinitCache() {
    guint cnt = g_slist_length(s_sensor_list);
    for(guint i=0; i<cnt; i++) {
        struct _sensor_st *p = g_slist_nth_data (s_sensor_list, i);
        guint cnt2 = g_slist_length(p->argument_list);
        for(int j=0; j<cnt2 + 1; j++) {
            g_free(GUINT_TO_POINTER(s_cache[i].address[j]));
        }
    }
    g_free(s_cache);
}

//
struct _cache_st *findIdCache(guint id) {
    guint cnt = g_slist_length(s_sensor_list);
    //g_log (TAG, G_LOG_LEVEL_DEBUG, "id=%d", id);
    for(guint i = 0; i < cnt; i++) {
        if(s_cache[i].id == id) {
            return &s_cache[i];
        }
    }
    return NULL;
}

//
// when application launch or a aquire cycle start, we need check if sensor should
// update argument or update firmware
gboolean checkSpecifySensorStatus(gint id, gchar *key){
    GError *error = NULL;
    GKeyFile *keyfile;
    gchar *group;
    gint value = 0;
    gboolean ret = FALSE;
    
    if(!g_file_test(SENSOR_STATUS_FILE_PATH, G_FILE_TEST_EXISTS)) {
        return ret;
    }
    asprintf(&group, "id_%d", id);
    keyfile = g_key_file_new();
    if(g_key_file_load_from_file(keyfile, SENSOR_STATUS_FILE_PATH, 0, &error)) {
        if(g_key_file_has_group(keyfile, group)) {
            value = g_key_file_get_integer(keyfile, group, key, &error);
            g_log (TAG, G_LOG_LEVEL_DEBUG, "id %d %s status is %d", id, key, value);
            if(error != NULL){
                g_error_free(error);
            } else {
                ret = (value == 1)?TRUE:FALSE;
            }
        }
    }else {
        g_log (TAG, G_LOG_LEVEL_DEBUG, "load status fail:%d %s", error->code, error->message);
        g_error_free(error);
    }
    g_key_file_free (keyfile);
    free(group);
    
    return ret;
}
//
// after got sensor configure form server, we need save the status
// 
void createSensorStatus() {
    GError *error = NULL;
    GKeyFile *keyfile;
    gchar *data;
    gchar *group;
    
    if(g_file_test(SENSOR_STATUS_FILE_PATH, G_FILE_TEST_EXISTS)) {
        g_remove(SENSOR_STATUS_FILE_PATH);
    }
    
    keyfile = g_key_file_new();
    guint len = g_slist_length(s_sensor_list);
    for(int i=0; i<len; i++) {
        struct _sensor_st *p = g_slist_nth_data (s_sensor_list, i);
        asprintf(&group, "id_%d", p->id);
        g_key_file_set_integer(keyfile, group, "id", p->id);
        g_key_file_set_integer(keyfile, group, "dlarg", 1);
        g_key_file_set_integer(keyfile, group, "dlfw", 0);
        free(group);
    }
    g_key_file_save_to_file(keyfile, SENSOR_STATUS_FILE_PATH, &error);
    if(error != NULL) {
        g_log (TAG, G_LOG_LEVEL_DEBUG, "save fail:%d %s", error->code, error->message);
        g_free(error);
    }
    
    g_key_file_free (keyfile);
}
//
//
gboolean recreateArgumentCommand(gboolean isAll)
{
    guchar *data;
    gboolean ret = FALSE;
    
    guint len = g_slist_length(s_sensor_list);
    for(int i=0; i<len; i++) {
        struct _sensor_st *p = g_slist_nth_data (s_sensor_list, i);
        if(!checkSpecifySensorStatus(p->id, "dlarg")){
            if(isAll)continue;
            else break;
        }
        data = g_malloc0(9);
        memcpy(data, &p->id, 2);
        {
            guint len2 = g_slist_length(p->argument_list);
            for(int j=0; j<len2; j++) {
                struct _argument_st *p2 = g_slist_nth_data (p->argument_list, j);
                g_log (TAG, G_LOG_LEVEL_DEBUG, 
                       "  channel = %d freq = %d, nums = %d",
                       p2->channel, p2->freq, p2->nums);
                data[2] |= p2->channel; // 1, 2, 4
                if(p2->channel & 0x01){
                    data[3] = p2->freq;
                    data[4] = p2->nums;
                }else if(p2->channel & 0x02){
                    data[5] = p2->freq;
                    data[6] = p2->nums;
                }else if(p2->channel & 0x04){
                    data[7] = p2->freq;
                    data[8] = p2->nums;
                }
                // where group??
                data[9] = p->group;
            }
        }
        struct _cmd_list *pcmd = (struct _cmd_list *)
                                g_malloc0(sizeof(struct _cmd_list));
        g_async_queue_lock(s_asyncQueue);
        pcmd->id = p->id;
        pcmd->cmd = 0x13; // config arg
        pcmd->data = data;
        g_async_queue_push_unlocked(s_asyncQueue, pcmd);
        g_async_queue_unlock(s_asyncQueue);
        
        ret = TRUE;
        if(!isAll)break;
    }  
    return ret;
}

//
// create sensor sample argument
int createArgumentCommand()
{
    guchar *data = NULL;
    guint len = g_slist_length(s_sensor_list);
    for(int i=0; i<len; i++) {
        struct _sensor_st *p = g_slist_nth_data (s_sensor_list, i);
        g_log (TAG, G_LOG_LEVEL_DEBUG, "id = %d",p->id);

        data = g_malloc0(9);
        memcpy(data, &p->id, 2);
        {
            guint len2 = g_slist_length(p->argument_list);
            for(int j=0; j<len2; j++) {
                struct _argument_st *p2 = g_slist_nth_data (p->argument_list, j);
                g_log (TAG, G_LOG_LEVEL_DEBUG, 
                       "  channel = %d freq = %d, nums = %d",p2->channel, p2->freq, p2->nums);
                data[2] |= p2->channel; // 1, 2, 4
                if(p2->channel & 0x01){
                    data[3] = p2->freq;
                    data[4] = p2->nums;
                }else if(p2->channel & 0x02){
                    data[5] = p2->freq;
                    data[6] = p2->nums;
                }else if(p2->channel & 0x04){
                    data[7] = p2->freq;
                    data[8] = p2->nums;
                }
                // where group??
            }
        }
        struct _cmd_list *pcmd = (struct _cmd_list *)
                                g_malloc0(sizeof(struct _cmd_list));
        g_async_queue_lock(s_asyncQueue);
        pcmd->id = p->id;
        pcmd->cmd = 0x13; // config arg
        pcmd->data = data;
        g_async_queue_push_unlocked(s_asyncQueue, pcmd);
        g_async_queue_unlock(s_asyncQueue);
    }  
    return 1;
}

//
// create aquire command according to cycle
int createAquireCommand(int minOfDay)
{
    struct _sensor_st *p;
    guchar *group = NULL;
    gboolean doAquire = FALSE;
    guchar cmd = 0x12;
    
    g_async_queue_lock(s_asyncQueue);
        
    guint len = g_slist_length(s_sensor_list);
    for(int i=0; i<len; i++) {
        struct _sensor_st *p = g_slist_nth_data (s_sensor_list, i);
        //g_log (TAG, G_LOG_LEVEL_DEBUG, "id = %d %d %d",p->id, minOfDay, p->aquire_cycle);
        if((minOfDay % p->aquire_cycle) != 0) {
            g_log (TAG, G_LOG_LEVEL_DEBUG, "id(%d) not at cycle", p->id);
            continue;
        }
        if(recreateArgumentCommand(FALSE)) {
            // update the sensor argument first
            continue;
        }
        cmd = p->raw == 1?0x1F:0x12; //0x12 value, 0x1F wave
        {
            guint len1 = g_slist_length(p->active_list);
            for(int j=0; j<len1; j++) {
                struct _active_slot_st *p1 = g_slist_nth_data (p->active_list, j);
                if((minOfDay >= p1->start) && (minOfDay <= p1->end)){
                    struct _cmd_list *pcmd = (struct _cmd_list *)
                                g_malloc0(sizeof(struct _cmd_list));
                    
                    recovery_alarm_confirm_shadow(p->argument_list);
                    
                    if(group == NULL) {
                        group = g_malloc0(16);
                    }
                    if(p->group > 16)p->group = 16;
                    group[p->group-1] = 1;
                    pcmd->id = p->id;
                    pcmd->cmd = HOST_INQUIRE_SENSOR_DATA;
                    pcmd->data = NULL;
                    pcmd->delay = 3*4;
                    pcmd->resend = 2; // TODO: find a batter way
                    g_log (TAG, G_LOG_LEVEL_DEBUG, "aquire id = %d cmd=%x", p->id, cmd);
                    g_async_queue_push_unlocked(s_asyncQueue, pcmd);
                    //doAquire = TRUE;
                }
            }
        }
    }
    if(doAquire) {
#ifdef __ARM__
// x86 glib don't support 'g_async_queue_push_front_unlocked'
        struct _cmd_list *pcmd = g_malloc0(sizeof(struct _cmd_list));
        pcmd->id = 0;
        pcmd->cmd = 0x10; //group
        pcmd->data = group;
        g_log (TAG, G_LOG_LEVEL_DEBUG, "aquire id = %d cmd=%x", p->id, pcmd->cmd);
        // start sample command go first
        g_async_queue_push_front_unlocked(s_asyncQueue, pcmd);
#endif
    }
    g_async_queue_unlock(s_asyncQueue);
    return 0;
}

void request_gid_JsonInfo() {
	cJSON * pJsonRoot = NULL;
	char *pjson = NULL;
	
	pJsonRoot = cJSON_CreateObject();
    if(NULL == pJsonRoot){
        goto json_err;
    }
    cJSON_AddStringToObject(pJsonRoot, "message", "req-gid");
    cJSON_AddStringToObject(pJsonRoot, "ip", s_localip);
    cJSON_AddStringToObject(pJsonRoot, "mac", s_locamac);
    
	pjson = cJSON_Print(pJsonRoot);
    if(pjson == NULL){
        goto json_err;
    }
    printf("%s\n", pjson);
    
	int ret = mosquitto_publish(s_mosq, &s_mid, "/gpub", strlen(pjson), pjson, 2, false);
    if(ret == MOSQ_ERR_SUCCESS) {
		
	}
	cJSON_free(pjson);
json_err:
	if(pJsonRoot != NULL)cJSON_Delete(pJsonRoot);
	return;
}
gboolean seconds_timeout(gpointer user_data) {
    if(g_date_time_get_second(g_date_time_new_now_local()) == 0) {
		if(s_gid == NULL || strlen(s_gid) == 0 ||
			strcmp(s_gid, "G00@default") == 0) {
			request_gid_JsonInfo();
		}
	}
	
    if(g_date_time_get_second(g_date_time_new_now_local()) == 30) {
		if(! s_net_para._ntp.isUpdate) {
			if(s_net_para._ntp.check && ! require_internet_datetime(s_net_para._ntp.host)) {
				g_log (TAG, G_LOG_LEVEL_DEBUG, "request internet datetime failure");
				return TRUE;
			}
		}
	}
	
	// sensor will sample at 0s, so we should wait 10s for sensor finish job
    if(g_date_time_get_second(g_date_time_new_now_local()) == 10) {
        g_log (TAG, G_LOG_LEVEL_DEBUG, "one min timeout arrive");
        gint h = g_date_time_get_hour(g_date_time_new_now_local());
        gint m = g_date_time_get_minute(g_date_time_new_now_local());
        gint minOfDay = 60*h + m;
        //if(s_status & 1) 
        {
            printf("check to resync timebase %u\n", (int)(g_get_real_time() / 1000000) - s_net_para._ntp.updateTime);
            if(!s_net_para._ntp.isUpdate || (((g_get_real_time() / 1000000) - s_net_para._ntp.updateTime) > (720*60))) {
                s_net_para._ntp.isUpdate = FALSE;
                // a fake time correction require
                guchar req[] = {0x55, 0xAA, 0x99, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x2F, 0x00, 0x00, 0x00};
                pipe_push(s_p, req, sizeof(req));
            }
            // test command
            if(0){
                GCond _cond;
                GMutex _mutex;
                g_cond_init(&_cond);
                g_mutex_init (&_mutex);
                //build_sensor_command(10033, SUBG_REPORT_SENSOR_INFO, NULL, 0, 9, TRUE, &_cond, &_mutex);
                //guchar info[4]={116,0,0,0};
                //build_sensor_command(10033, HOST_INQUIRE_SENSOR_TDATA, info, sizeof(info), 6, TRUE, &_cond, &_mutex);
                guchar info[]={0x00, 0x00, 
                    0xA0, 0x05, 
                    0x02, 0x00, 
                    0x01, 0x00, 
                    0x66, 0x01, 0x0A, 0x0C, 
                    0x00, 0x00, 0x00, 0x00, 
                    0x00, 0x00, 0x00, 0x00, 
                    0x00, 0x00, 0x00, 0x00 };
                build_sensor_command(10033, HOST_SET_SENSOR_SAMPLE_ARGS, info, sizeof(info), 6, TRUE, &_cond, &_mutex);
                g_mutex_lock (&_mutex);
                if(!g_cond_wait_until(&_cond, &_mutex, g_get_monotonic_time () + 9 * G_TIME_SPAN_SECOND)) {
                    g_log (TAG, G_LOG_LEVEL_DEBUG, "-----wait timeout-----");
                }
                g_mutex_unlock (&_mutex);
                g_log (TAG, G_LOG_LEVEL_DEBUG, "-----command ok-----");
                g_usleep(6000000);
                build_sensor_command(10033, HOST_INQUIRE_SENSOR_DATA, NULL, 0, 6, FALSE, NULL, NULL);
            }
			
            createAquireCommand(minOfDay);
        }
    }
    
    return TRUE;
}

gboolean wakeup_timeout(gpointer user_data) {
    g_log (TAG, G_LOG_LEVEL_DEBUG, "time left:%d", s_wakeup_timeout);
    if(s_wakeup_timeout > 0) {
        s_wakeup_timeout--;
        return TRUE;
    }
    
    s_to_res = 0;
    return FALSE;
}

//
// build a packet whitch send to end.
guint build_serial_packet(struct _cmd_list *pcmd, guchar **puser) {
#define ID_LEN 2
    guint len = sizeof(struct _end_packet)+ ID_LEN + pcmd->len ;
    guchar *user = g_malloc0(len);
    ((struct _end_packet *)user)->hed[0] = 0x55;
    ((struct _end_packet *)user)->hed[1] = 0xAA;
    ((struct _end_packet *)user)->hed[2] = 0x99;
    ((struct _end_packet *)user)->hed[3] = 0x66;
    ((struct _end_packet *)user)->sn = 1;
    ((struct _end_packet *)user)->len = pcmd->len + (pcmd->id==0?0:2);
    ((struct _end_packet *)user)->cmd = pcmd->cmd;
    if(pcmd->id > 0) {
        memcpy(user + sizeof(struct _end_packet), &pcmd->id, ID_LEN);
    }
    if(pcmd->len > 0) {
        memcpy(user + sizeof(struct _end_packet) + (pcmd->id==0?0:ID_LEN), pcmd->data, pcmd->len);
    }
    //for(int i=0; i<len-1; i++) {
        //user[len-1] ^= user[i];
    //}
    //dumpmem(user, len);
    *puser = user;
    return len;
}

//
static gpointer send_command_thread(gpointer user_data) {
    struct _cmd_list *pcmd;
    gint64 end_time = 0;
    guint fail_count = 0;
    g_mutex_init (&s_lock);
    g_cond_init (&s_cond);
    int i = 0;
    guint len = 0;
    guchar *puser = NULL;
    
    while(1){
        g_async_queue_lock(s_asyncQueue);
        pcmd = (struct _cmd_list *)g_async_queue_pop_unlocked(s_asyncQueue);
        g_async_queue_unlock(s_asyncQueue);
        if(pcmd != NULL) {
            printf("\r\n\r\n");
            g_log (TAG, G_LOG_LEVEL_DEBUG, 
                "%d got(%p) %d cmd %x data %p", i++, pcmd, pcmd->id, pcmd->cmd, pcmd->data);
            // check if we need wake up sensor
#if 0
            if(s_wakeup_timeout <= 10 && 
                        pcmd->cmd != HOST_SET_SUBG_TIMESTAMP && // set subG's time
                        pcmd->cmd != HOST_SET_SUBG_RFINFO &&        // set subG's RF info 
                        pcmd->cmd != HOST_INQUIRE_SUBG_INFO)
                // remove current timeout COUNTER
                if(s_to_rc > 0)g_source_remove(s_to_rc);
                
                // TODO: send out wakeup command
                g_log (TAG, G_LOG_LEVEL_DEBUG, "send wakeup command");
                
                s_wakeup_timeout = WAKEUP_TIMEOUT;
                s_to_rc = g_timeout_add_seconds(1, wakeup_timeout, NULL);
                g_log (TAG, G_LOG_LEVEL_DEBUG, "timeout add seconds rc=%d", s_to_rc);
            }
#endif
            // start a timeout watch dog, maybe subG is dead 
            g_log (TAG, G_LOG_LEVEL_DEBUG, "start timeout watch dog");
            if(s_to_rec_res > 0) {
				g_source_remove(s_to_rec_res);
				s_to_rec_res = 0;
			}
            s_to_rec_res = g_timeout_add_seconds(4, seconds_rec_timeout, pcmd);

            len = build_serial_packet(pcmd, &puser);
            if(!serial_write_data(puser, len)) {
				if(s_to_rec_res > 0) {
					g_source_remove(s_to_rec_res);
					s_to_rec_res = 0;
				}
                g_log(TAG, G_LOG_LEVEL_DEBUG, "serial write fail, errno=%d", errno);
                continue;
            }
#if 1
            // wait for command respond
            {
                g_mutex_lock (&s_lock);
                g_cond_wait(&s_cond, &s_lock); // wait forever
                g_mutex_unlock (&s_lock);
            }
#endif 
            g_log(TAG, G_LOG_LEVEL_DEBUG, "s_cond has signaled");
            // TODO: tell user command finish who would expect.
            // signal user, tell user we finish the job
            if(pcmd->notify) {
                //printf("pcmd->cond=%p,  pcmd->mutex=%p\n", pcmd->cond, pcmd->mutex);
                g_mutex_lock (pcmd->mutex);
                g_cond_signal (pcmd->cond);
                g_mutex_unlock (pcmd->mutex);
            }
            
            g_free(pcmd->data);
            g_free(pcmd);
        }
    }
    return GINT_TO_POINTER(-1);
}
/*
struct _cmd_list *pcmd = (struct _cmd_list *)
                                g_malloc0(sizeof(struct _cmd_list));
        g_async_queue_lock(s_asyncQueue);
        pcmd->id = 121;
        pcmd->cmd = 0x15; // config arg
        pcmd->data = NULL;
        pcmd->notify = TRUE;
        pcmd->cond = _cond;
        pcmd->mutex = _mutex;
        pcmd->delay = 10;
        g_async_queue_push_unlocked(s_asyncQueue, pcmd);
        g_async_queue_unlock(s_asyncQueue);
        g_log(TAG, G_LOG_LEVEL_DEBUG, "mqtt send a command");
        g_mutex_lock (&_mutex);
        end_time = g_get_monotonic_time () + pcmd->delay * G_TIME_SPAN_SECOND + 500000;
        if(!g_cond_wait_until (&_cond, &_mutex, end_time)) {
            g_log(TAG, G_LOG_LEVEL_DEBUG, "mqtt wait _cond signal timeout");
        }
        g_mutex_unlock (&_mutex);
 */
void on_connect(struct mosquitto *mosq, void *obj, int rc)
{
    g_log(TAG, G_LOG_LEVEL_DEBUG, "on_connect rc=%d", rc);
    if(rc == 0) {
		char *topic = NULL;
		asprintf(&topic, "/%s", s_gid);
		mosquitto_subscribe(mosq, NULL, topic, 2);
		if(topic != NULL)free(topic);
		
		// subcribe common message public by server
		mosquitto_subscribe(mosq, NULL, "/cpub", 2);
		
	}else if(rc == 1) {
		g_log(TAG, G_LOG_LEVEL_DEBUG, "connection refused (unacceptable protocol version)");
	}else if(rc == 2) {
		g_log(TAG, G_LOG_LEVEL_DEBUG, "connection refused (identifier rejected)");
	}else if(rc ==3) {
		g_log(TAG, G_LOG_LEVEL_DEBUG, "connection refused (broker unavailable)");
	}
}
void on_disconnect(struct mosquitto *mosq, void *obj, int rc)
{
    g_log(TAG, G_LOG_LEVEL_DEBUG, "on_disconnect");
}
void on_subscribe(struct mosquitto *mosq, void *obj, int mid, int qos_count, const int *granted_qos)
{
    g_log(TAG, G_LOG_LEVEL_DEBUG, "on_subscribe");
}
void on_publish(struct mosquitto *mosq, void *obj, int mid)
{
    //g_log(TAG, G_LOG_LEVEL_DEBUG, "on_publish, mid=%d", mid);
}

struct _ti_to {
	guint id;
	guint raw;
};
gboolean temporary_inquire_timeout(gpointer user_data){
	struct _ti_to *ti_to = (struct _ti_to *)user_data;
	ti_to->raw == 1?0x1F:0x12;
	
	build_sensor_command(ti_to->id, HOST_INQUIRE_SENSOR_DATA, NULL, 0, 6, FALSE, NULL, NULL);
	return FALSE;
}

void ParseTemporaryFromJson(gchar *json) {
/*
{
	"message": "temporary",
	"id": 100,
	"raw": 1,
	"arguments": {
		"channel": 1,
		"freq": 2000,
		"nums": 4096
	}
}
*/
	struct _ti_to ti_to;
	guint channel=0, freq=0, nums = 0, arggroup=0;
	cJSON *pjsonRoot, *pjsonArgs;
	
	if((pjsonRoot = cJSON_Parse(json)) == NULL) {
		g_log(TAG, G_LOG_LEVEL_DEBUG, "is not a json file");
		return;
	}
	ti_to.id = cJSON_GetObjectItem(pjsonRoot, "id")->valueint;
	ti_to.raw = cJSON_GetObjectItem(pjsonRoot, "raw")->valueint;
	if((pjsonArgs = cJSON_GetObjectItem(pjsonRoot, "arguments")) != NULL) {
		channel = cJSON_GetObjectItem(pjsonArgs, "channel")->valueint;
		freq = cJSON_GetObjectItem(pjsonArgs, "freq")->valueint;
		nums = cJSON_GetObjectItem(pjsonArgs, "nums")->valueint;
		printf("############# %d %d %d\n", channel, freq, nums);
	}
	if(cJSON_GetObjectItem(pjsonRoot, "arggroup") != NULL) {
		arggroup = cJSON_GetObjectItem(pjsonRoot, "arggroup")->valueint;
	}
	cJSON_Delete(pjsonRoot);
	if(arggroup > 0) {
		// TODO： check if arggroup exist
		guint cnt = g_slist_length(s_sensor_list);
		for(guint i=0; i<cnt; i++) {
			struct _sensor_st *p = g_slist_nth_data (s_sensor_list, i);
			if(p->id == ti_to.id) {
				guint cnt1 = g_slist_length(p->argument_list);
				if(arggroup > cnt1) {
					return;
				}
			}
		}
		guchar data[1] = {arggroup};
		build_sensor_command(ti_to.id, HOST_INQUIRE_SENSOR_TDATA, data, 1, 6, FALSE, NULL, NULL);
	}else {
		guchar data[4];
		data[0] = host_channel_to_subG(channel);
		data[1] = host_freq_to_subG(freq);
		data[2] = host_nums_to_subG(nums);
		build_sensor_command(ti_to.id, HOST_INQUIRE_SENSOR_TDATA, data, 1, 6, FALSE, NULL, NULL);
	}
	g_timeout_add_seconds(5, temporary_inquire_timeout, &ti_to);
}

void ParseSearchFromJson(gchar *json) {
/*
{
	"message":"search",
	"id":[100]
}
*/
	cJSON *pjsonRoot = cJSON_Parse(json);
	if(pjsonRoot == NULL) {
		g_log(TAG, G_LOG_LEVEL_DEBUG, "is not a json file");
		return;
	}
	int i = 0;
	cJSON *pJSensor = cJSON_GetObjectItem(pjsonRoot, "id");
	int iSize = cJSON_GetArraySize(pJSensor);
	guint *id = (guint *)g_malloc0(iSize * sizeof(guint));
	for(i = 0; i < iSize; i++){
        id[i] = cJSON_GetArrayItem(pJSensor, i)->valueint;
        printf("---- %d\n", id[i]);
    }
	cJSON_Delete(pjsonRoot);
	
	if(i==1 && id[0] == 0) {		// all sensors
		guint cnt = g_slist_length(s_sensor_list);
		for(guint i=0; i<cnt; i++) {
			struct _sensor_st *p = g_slist_nth_data (s_sensor_list, i);
			build_sensor_command(p->id, SUBG_REPORT_SENSOR_INFO, NULL, 0, 9, FALSE, NULL, NULL);
		}
	} else {								// indicate sensor
		for(int j=0; j<i; j++) {
			if(id[j] == 0) continue;
			build_sensor_command(id[j], SUBG_REPORT_SENSOR_INFO, NULL, 0, 9, FALSE, NULL, NULL);
		}
	}
}
void parse_request_parameter(gchar *json) {
	gchar *pjson = NULL;
	gsize length = 0;
	GError *error = NULL;
	cJSON *pjsonRoot = cJSON_Parse(json);
	if(pjsonRoot == NULL) {
		g_log(TAG, G_LOG_LEVEL_DEBUG, "is not a json file");
		return;
	}
	cJSON *pJSensor = cJSON_GetObjectItem(pjsonRoot, "id");
	if(pJSensor != NULL) {
		guint id = pJSensor->valueint;
		//send single sensor parameter
	} else {
		g_file_get_contents(SENSOR_JSON_FILE_PATH, &pjson, &length, &error);
		if(error != NULL) {
			g_log (TAG, G_LOG_LEVEL_DEBUG, "%s", error->message);
			g_error_free(error);
			return;
		}
	}
	int ret = mosquitto_publish(s_mosq, NULL, "/sensors", strlen(pjson), pjson, 2, false);
	if(ret == MOSQ_ERR_SUCCESS) {
	}
	g_free(pjson);
}

void do_bin_job(void *payload, int payloadlen) {
	
}
void parse_gid_jsonInfo(gchar *json) {
/*{
	"message": "gen-gid",
	"gid": "G01@192.168.1.6",
	"mac": "00:11:22:33:44:55"
}*/
	cJSON *pjsonRoot = cJSON_Parse(json);
	if(pjsonRoot == NULL) {
		g_log(TAG, G_LOG_LEVEL_DEBUG, "is not a json file");
		return;
	}
	
	char *mac = cJSON_GetObjectItem(pjsonRoot, "mac")->valuestring;
	if( strcmp(mac, s_locamac) == 0 ) {
		s_gid = cJSON_GetObjectItem(pjsonRoot, "gid")->valuestring;
		
		// save s_gid to db
		misc_info_insert_db(SENSOR_DB_FILE_PATH, s_gid);
		misc_info_from_db(SENSOR_DB_FILE_PATH, &s_gid);
	}
	cJSON_Delete(pjsonRoot);
}
void on_message(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg)
{
	// TODO: lock
    g_log(TAG, G_LOG_LEVEL_DEBUG, "on_message: %s", msg->topic);
	cJSON *pjsonRoot = cJSON_Parse((char *)msg->payload);
	if(pjsonRoot == NULL) {
		g_log(TAG, G_LOG_LEVEL_DEBUG, "is not a json file");
		do_bin_job(msg->payload, msg->payloadlen);
		return;
	}
	char *message = cJSON_GetObjectItem(pjsonRoot, "message")->valuestring;
	if(strcmp(message, "parameter") == 0) {
		GError *error = NULL;
		cJSON_Delete(pjsonRoot);
		
		g_log(TAG, G_LOG_LEVEL_DEBUG, "got parameter message");
		g_file_set_contents(SENSOR_JSON_FILE_PATH, msg->payload, msg->payloadlen, &error);
		if(error != NULL){
            g_log (TAG, G_LOG_LEVEL_DEBUG, "%s", error->message);
            g_error_free(error);
        }
        // TODO: send respond
        // restart app
        //ParseSensorInfoFromJson(msg->payload, msg->payloadlen);
        // TODO：check if we are sampling data，and wait finish
        exit(0);
	}
	else if(strcmp(message, "temporary") == 0) {
		g_log(TAG, G_LOG_LEVEL_DEBUG, "got temporary message");
		ParseTemporaryFromJson(msg->payload);
	}
	else if(strcmp(message, "search") == 0) {
		g_log(TAG, G_LOG_LEVEL_DEBUG, "got search message");
		ParseSearchFromJson(msg->payload);
	}
	else if(strcmp(message, "gen-gid") == 0) {
		g_log(TAG, G_LOG_LEVEL_DEBUG, "got gen-gid message");
		parse_gid_jsonInfo(msg->payload);
	}
	else if(strcmp(message, "request-parameter") == 0) {
		g_log(TAG, G_LOG_LEVEL_DEBUG, "got request-parameter message");
		parse_request_parameter(msg->payload);
	}
	else if(strcmp(message, "request-status") == 0) {
		g_log(TAG, G_LOG_LEVEL_DEBUG, "got status message");
	}
	else if(strcmp(message, "configstatus") == 0) {
		g_log(TAG, G_LOG_LEVEL_DEBUG, "got report_info message");
	}
	
	cJSON_Delete(pjsonRoot);
}

static gpointer mqtt_thread(gpointer user_data) {
    guint64 end_time;
    GCond _cond;
    GMutex _mutex;
    g_cond_init(&_cond);
    g_mutex_init (&_mutex);
    g_log(TAG, G_LOG_LEVEL_DEBUG, "mqtt_thread running");

    while(1)
    {
        int rc;
        char id[128]={'\0'};
        mosquitto_lib_init();
        sprintf(id, "wls@%s", s_locamac);
        s_mosq = mosquitto_new(id, false, NULL);
        mosquitto_threaded_set(s_mosq, true);
        mosquitto_connect_callback_set(s_mosq, on_connect);
        mosquitto_disconnect_callback_set(s_mosq, on_disconnect);
        mosquitto_subscribe_callback_set(s_mosq, on_subscribe);
        mosquitto_publish_callback_set(s_mosq, on_publish);
        mosquitto_message_callback_set(s_mosq, on_message);
        //mosquitto_username_pw_set(s_mosq, s_net_para._mqtt.user, s_net_para._mqtt.passw);
        rc = mosquitto_connect(s_mosq, s_net_para._mqtt.ip, s_net_para._mqtt.port, s_net_para._mqtt.keepalive);
        if(rc != MOSQ_ERR_SUCCESS){
            g_log("mqtt", G_LOG_LEVEL_DEBUG, "mosquitto_connect fail, connect later");
        }
        
        mosquitto_loop_forever(s_mosq, -1, 1);
        // will never get here unless you call mosquitto_disconnect(s_mosq);
        g_log("mqtt", G_LOG_LEVEL_DEBUG, "mosquitto bye bye");
        mosquitto_lib_cleanup();
    }
    return GINT_TO_POINTER(-1);
}

void subG_reset()
{
    char cmd[256];
    
    sprintf(cmd, "echo 0 > /sys/class/g780/device/rst_state");
    system(cmd);
    g_usleep(100000);
    sprintf(cmd, "echo 1 > /sys/class/g780/device/rst_state");
    system(cmd);
}
static gpointer info_request_thread(gpointer user_data) {
	
	return GINT_TO_POINTER(-1);
}
char* GetLocalIp()
{
    int sock_get_ip;
    char* ipaddr;
  
    struct   sockaddr_in *sin;
    struct   ifreq ifr_ip;
  
    if ((sock_get_ip=socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
         printf("socket create failse...GetLocalIp!\n");
         return NULL;  
    }
    memset(&ifr_ip, 0, sizeof(ifr_ip));
#ifdef __ARM__
	strncpy(ifr_ip.ifr_name, "eth0", sizeof(ifr_ip.ifr_name) - 1);
#else
    strncpy(ifr_ip.ifr_name, "wlan0", sizeof(ifr_ip.ifr_name) - 1);
#endif
    if( ioctl( sock_get_ip, SIOCGIFADDR, &ifr_ip) < 0 )
    {
        close( sock_get_ip );
        return NULL;
    }
    ipaddr = (char *)malloc(32);
    sin = (struct sockaddr_in *)&ifr_ip.ifr_addr;
    strcpy(ipaddr,inet_ntoa(sin->sin_addr));
    //printf("local ip:%x\n",sin->sin_addr);  
    printf("local ip:%s \n",ipaddr);
    close( sock_get_ip );
      
    return (char *)ipaddr;
}
char* GetLocalMac()
{
    int sock_mac;
      
    struct ifreq ifr_mac;
    char* macaddr = NULL;
      
    sock_mac = socket( AF_INET, SOCK_STREAM, 0 ); 
    if( sock_mac == -1)
    {
        perror("create socket falise...mac\n");
        return "";
    }
      
    memset(&ifr_mac,0,sizeof(ifr_mac));
    strncpy(ifr_mac.ifr_name, "eth0", sizeof(ifr_mac.ifr_name)-1);

    if( (ioctl( sock_mac, SIOCGIFHWADDR, &ifr_mac)) < 0)
    {
        perror("mac ioctl error\n");
        return "";
    }
    
    asprintf(&macaddr,"%02x:%02x:%02x:%02x:%02x:%02x",
            (unsigned char)ifr_mac.ifr_hwaddr.sa_data[0],
            (unsigned char)ifr_mac.ifr_hwaddr.sa_data[1],
            (unsigned char)ifr_mac.ifr_hwaddr.sa_data[2],
            (unsigned char)ifr_mac.ifr_hwaddr.sa_data[3],
            (unsigned char)ifr_mac.ifr_hwaddr.sa_data[4],
            (unsigned char)ifr_mac.ifr_hwaddr.sa_data[5]);
  
    printf("local mac:%s \n",macaddr);

    close( sock_mac );
    return macaddr;
    
    return 0;
}

void create_default_net_parameter() {
	GKeyFile *keyfile;
	GError *error = NULL;
	
	keyfile = g_key_file_new();
	//LOCAL
	g_key_file_set_string(keyfile, "LOCAL", "dhcp", "disable");
	g_key_file_set_string(keyfile, "LOCAL", "ip", "192.168.1.6");
	g_key_file_set_integer(keyfile, "LOCAL", "port", 40001);
	//SERVER
	g_key_file_set_string(keyfile, "SERVER", "ip", "192.168.1.6");
    g_key_file_set_integer(keyfile, "SERVER", "port", 40002);
    //MQTT
    g_key_file_set_string(keyfile, "MQTT", "ip", "127.0.0.1");
    g_key_file_set_integer(keyfile, "MQTT", "port", 1883);
    g_key_file_set_string(keyfile, "MQTT", "user", "admin");
    g_key_file_set_string(keyfile, "MQTT", "password", "password");
    g_key_file_set_integer(keyfile, "MQTT", "keepalive", 10);
    //NTP
    g_key_file_set_string(keyfile, "NTP", "host", "ntp1.aliyun.com");
    g_key_file_set_boolean(keyfile, "NTP", "check", FALSE);		// default we don't check ntp if is successful
    // RF
    g_key_file_set_integer(keyfile, "SUBG", "channel", 772);
    g_key_file_set_integer(keyfile, "SUBG", "pan", 30);
    g_key_file_set_integer(keyfile, "SUBG", "power", 0);
    
	g_key_file_save_to_file(keyfile, LOCAL_NET_FILE_PATH, &error);
    if(error != NULL) {
        g_log (TAG, G_LOG_LEVEL_DEBUG, "save fail:%d %s", error->code, error->message);
        g_free(error);
    }
    
    g_key_file_free (keyfile);
}
void load_net_parameter(struct _net_parameter_st *net) {
	GKeyFile *keyfile;
	GError *error = NULL;
	
	keyfile = g_key_file_new();
    if(g_key_file_load_from_file(keyfile, LOCAL_NET_FILE_PATH, 0, &error)) {
		sprintf(net->_local.ip, "%s", g_key_file_get_string(keyfile, "LOCAL", "ip", NULL));
		net->_local.port = g_key_file_get_integer(keyfile, "LOCAL", "port", NULL);
		char *dhcp = g_key_file_get_string(keyfile, "LOCAL", "dhcp", NULL);
		if(strcmp(dhcp, "disable") == 0)net->_local.dhcp = FALSE;
		else net->_local.dhcp = TRUE;
		
		sprintf(net->_server.ip, "%s", g_key_file_get_string(keyfile, "SERVER", "ip", NULL));
		net->_server.port = g_key_file_get_integer(keyfile, "SERVER", "port", NULL);
		
		sprintf(net->_mqtt.ip, "%s", g_key_file_get_string(keyfile, "MQTT", "ip", NULL));
		net->_mqtt.port = g_key_file_get_integer(keyfile, "MQTT", "port", NULL);
		sprintf(net->_mqtt.user, "%s", g_key_file_get_string(keyfile, "MQTT", "user", NULL));
		sprintf(net->_mqtt.passw, "%s", g_key_file_get_string(keyfile, "MQTT", "password", NULL));
		net->_mqtt.keepalive = g_key_file_get_integer(keyfile, "MQTT", "keepalive", NULL);
		
		sprintf(net->_ntp.host, "%s", g_key_file_get_string(keyfile, "NTP", "host", NULL));
		net->_ntp.check = g_key_file_get_boolean(keyfile, "NTP", "check", NULL);
		
		net->_rf.channel = g_key_file_get_integer(keyfile, "SUBG", "channel", NULL);
		net->_rf.pan = g_key_file_get_integer(keyfile, "SUBG", "pan", NULL);
		net->_rf.power = g_key_file_get_integer(keyfile, "SUBG", "power", NULL);
	} else {
		if(error != NULL) {
			g_log (TAG, G_LOG_LEVEL_DEBUG, "save fail:%d %s", error->code, error->message);
			g_free(error);
		}
    }
	g_key_file_free (keyfile);
}

// 
// 接受PHP传来的JSON格式信息，执行后，将JSON格式的响应回传给PHP
//
static gboolean client_handler (GThreadedSocketService *service,
         GSocketConnection      *connection,
         GSocketListener        *listener,
         gpointer                user_data) {
#define MESSAGE "Welcome to the echo service!\n"
	GOutputStream *out;
	GInputStream *in;
	char buffer[1024];
	gssize size;
	printf("someone connect in\n");	
	out = g_io_stream_get_output_stream (G_IO_STREAM (connection));
	in = g_io_stream_get_input_stream (G_IO_STREAM (connection));

	//g_output_stream_write_all (out, MESSAGE, strlen (MESSAGE),
    //                         NULL, NULL, NULL);

	while (0 < (size = g_input_stream_read (in, buffer,
                                          sizeof buffer, NULL, NULL))) {
		cJSON *pjsonRoot = NULL;
		buffer[size]='\0';
		printf("%s\n", buffer);	

		
		pjsonRoot = cJSON_Parse((char *)buffer);
		if(pjsonRoot != NULL) {
			char *msg = cJSON_GetObjectItem(pjsonRoot, "message")->valuestring;
			if(strcmp(msg, "restart_wls") == 0) {
				G_LOCK(db_protect);
				// 在重启AP时，我们需要等待数据库操作完毕
				G_UNLOCK(db_protect);
			} else if(strcmp(msg, "temporary") == 0) {
/*{
	"message": "temporary",
	"id": 100,
	"group": 1,
	"raw":1
}*/
				guint id = cJSON_GetObjectItem(pjsonRoot, "id")->valueint;
				guint group = cJSON_GetObjectItem(pjsonRoot, "group")->valueint;
				guint raw = cJSON_GetObjectItem(pjsonRoot, "raw")->valueint;
				
			} else if(strcmp(msg, "search") == 0) {
/*
{
	"message":"search",
	"id":[100]
}
*/
			} else if(strcmp(msg, "parameter") == 0) {
// 通过页面配置或更新某传感器参数，请求更新到传感器
// tbd:如何更新本地的json文件的某个传感器
				
			} else if(strcmp(msg, "report_sinfo") == 0) {
				// sensor info
/*{
	"message":"report_info",
	"sid":10033
}
{
	"respond":"report_info",
	"sid":10033,
	"version":"20170101"
}*/
			} else if(strcmp(msg, "report_ginfo") == 0) {
				// gate info
			} else if(strcmp(msg, "update_net") == 0) {
// 更新网络参数
/*{
	"message": "update_net",
	"ntp": {
		"host": "aliyun.ntp.com",
		"check": false
	},
	"mqtt": {
		"server": "202.96.209.5",
		"port": 1884,
		"user": "admin",
		"password": "admin"
	},
	"subG": {
		"channel": 769,
		"pan": 1,
		"power": 0
	}
}*/
				cJSON *node;
				node = cJSON_GetObjectItem(pjsonRoot, "ntp");
				if(node != NULL) {
					
				}
				node = cJSON_GetObjectItem(pjsonRoot, "mqtt");
				if(node != NULL) {
					
				}
				node = cJSON_GetObjectItem(pjsonRoot, "subG");
				if(node != NULL) {
					s_net_para._rf.channel = cJSON_GetObjectItem(node, "channel")->valueint;
					s_net_para._rf.pan = cJSON_GetObjectItem(node, "pan")->valueint;
					s_net_para._rf.power = cJSON_GetObjectItem(node, "power")->valueint;
					
					struct _rf_st info = {
							.channel = (s_net_para._rf.channel-769)*10,
							.pan = s_net_para._rf.pan,
							.power = s_net_para._rf.power
						};
					g_log (TAG, G_LOG_LEVEL_DEBUG, "php change gate rf");
					build_command(HOST_SET_SUBG_RFINFO, (guchar *)&info, sizeof(struct _rf_st), 3);
				}
				
				cJSON * pJsonNet = NULL;
				char *pjson = NULL;
				pJsonNet = cJSON_CreateObject();
				if(NULL != pJsonNet){
					cJSON_AddStringToObject(pJsonNet, "msg", "update_net");
					cJSON_AddStringToObject(pJsonNet, "respond", "successful");
					pjson = cJSON_Print(pJsonNet);
					printf("%s\n", pjson);
					
					g_output_stream_write (out, pjson, strlen(pjson), NULL, NULL);
					free(pjson);
					cJSON_Delete(pJsonNet);
				}
			}
			cJSON_Delete(pjsonRoot);
		}
		//g_output_stream_write (out, buffer, size, NULL, NULL);
	}
    printf("connect byb byb\n");		 
	return TRUE;
}
gpointer local_server_thread(gpointer user_data) {
	GSocketService *service;
	int port = 7777;
	GError *error = NULL;
	service = g_threaded_socket_service_new (10);
	if (!g_socket_listener_add_inet_port (G_SOCKET_LISTENER (service),
					port,
					NULL,
					&error))
	{
		g_printerr ("%s: %s\n", __func__, error->message);
		return GINT_TO_POINTER(-1);
	}

	g_print ("Echo service listening on port %d\n", port);

	g_signal_connect (service, "run", G_CALLBACK (client_handler), NULL);
	return GINT_TO_POINTER(-1);
}
void show_system_memery()
{
    FILE *fd;  
    char buff[1024];  
    int total = 0;  
    int free = 0;  
    float use=0;  
  
    fd = fopen ("/proc/meminfo", "r");  

    fgets (buff, sizeof(buff), fd);  
    sscanf (buff, "%*s %d %*s", &total);  

    fgets (buff, sizeof(buff), fd);  
    sscanf (buff, "%*s %d %*s", &free);  
    use=(float)((total-free) /(total * 0.01));//百分比  

    fclose(fd);      
    printf("mem use: %f free: %d\n", use, free);
}

int main(void) {
    GMainLoop *main_loop;
    main_loop = g_main_loop_new (NULL, FALSE);
    
    g_log_set_default_handler (g_log_default_handler, NULL);
    g_setenv ("G_MESSAGES_DEBUG", "all", TRUE);
    g_log (TAG, G_LOG_LEVEL_DEBUG, "start wls app");
    
    cJSON_Hooks hooks = {
		.malloc_fn = g_malloc0,
		.free_fn = g_free
	};
    cJSON_InitHooks( &hooks );
    
	if(!g_file_test(LOCAL_NET_FILE_PATH, G_FILE_TEST_EXISTS)) {
		create_default_net_parameter();
	}
	memset(&s_net_para, 0, sizeof(s_net_para));

    load_net_parameter(&s_net_para);
    printf("mqtt net parameter:\n\tip:%s\n\tport:%d\n\tuser:%s\n\tpass:%s\n", 
			s_net_para._mqtt.ip, s_net_para._mqtt.port, s_net_para._mqtt.user, s_net_para._mqtt.passw);

    if(!s_net_para._ntp.isUpdate) {
		//TODO:Check if host can't connect
        require_internet_datetime(s_net_para._ntp.host);
    }
    
	s_localip = GetLocalIp();
	s_locamac = GetLocalMac();
	misc_info_from_db(SENSOR_DB_FILE_PATH, &s_gid);
	printf("s_gid : %s\n", s_gid);
	s_pipe = pipe_new(sizeof(guchar), 0);
	s_p = pipe_producer_new(s_pipe);
	s_c = pipe_consumer_new(s_pipe);
	g_thread_unref(g_thread_new ("serial read thread", reader_loop_thread, NULL));
#ifdef __ARM__
	subG_reset();
	s_fd = uart_init();
	if(s_fd >= 0) {
		set_speed(s_fd, 921600);
		set_Parity(s_fd, 8, 1, 'N');
		g_log (TAG, G_LOG_LEVEL_DEBUG, "open device %s correctly", s_device_path);
		g_thread_unref(g_thread_new ("serial read thread", serial_read_thread, &s_fd));
	}
#endif

    if(!g_file_test(SENSOR_JSON_FILE_PATH, G_FILE_TEST_EXISTS)) {
        gchar *base64;
        GError *error = NULL;
        char *sSensor = "{\"message\":\"parameter\",\"buildtime\":\"2018-08-01\",\"sensors\":[{\"id\":10033,\"group\":1,\"aquire_cycle\":2,\"active_slot\":[{\"start\":0,\"end\":540},{\"start\":540,\"end\":1440}],\"triger\":0,\"raw\":0,\"arguments\":[{\"channel\":1,\"freq\":2000,\"nums\":4096,\"alarm\":[{\"type\":1,\"source\":1,\"limit\":[{\"low\":0,\"high\":10},{\"low\":20,\"high\":50}]}],\"confirm\":{\"times\":1,\"raw\":1}}]},{\"id\":222,\"group\":2,\"aquire_cycle\":2,\"active_slot\":[{\"start\":0,\"end\":1440}],\"triger\":0,\"raw\":0,\"arguments\":[{\"channel\":2,\"freq\":2000,\"nums\":2048,\"alarm\":[{\"type\":1,\"source\":0,\"limit\":[{\"low\":-10,\"high\":10}]}],\"confirm\":{\"times\":1,\"raw\":1}}]}]}";
        //base64 = g_base64_encode(sSensor, strlen(sSensor));
        g_file_set_contents(SENSOR_JSON_FILE_PATH, sSensor, strlen(sSensor), &error);
        if(error != NULL){
            g_log (TAG, G_LOG_LEVEL_DEBUG, "%s", error->message);
            g_error_free(error);
        }
        //g_free(base64);
    }
    //ParseSensorInfoFromJson("{\"message\":\"parameter\",\"buildtime\":\"2018-08-13\",\"sensors\":[{\"id\":10033,\"group\":0,\"aquire_cycle\":5,\"triger\":0,\"raw\":1,\"active_slot\":[{\"start\":0,\"end\":1440}],\"arguments\":[{\"channel\":1,\"freq\":2000,\"nums\":2018,\"alarm\":null,\"confirm\":null}]}]}", 0);
	//exit(0);
    {
        gchar *base64 = NULL;
        gsize length = 0;
        GError *error = NULL;
        g_file_get_contents(SENSOR_JSON_FILE_PATH, &base64, &length, &error);
        if(error != NULL) {
            g_log (TAG, G_LOG_LEVEL_DEBUG, "%s", error->message);
            g_error_free(error);
        }
        //base64 = g_base64_decode_inplace(base64, &length);
        ParseSensorInfoFromJson(base64, length);
    }
    
    initCache();
    //ParseTemporaryFromJson("{\"message\":\"temporary\",\"id\":100,\"raw\":1}");
	if(0){
		cJSON * pJsonRoot = NULL;
		cJSON * pJsonArray = NULL;
		cJSON * pJsonNode = NULL;
		pJsonRoot = cJSON_CreateObject();
		pJsonArray = cJSON_CreateArray();
		pJsonNode = cJSON_CreateObject();
		cJSON_AddNumberToObject(pJsonNode, "Width", 800);
		cJSON_AddNumberToObject(pJsonNode, "Height", 600);
		cJSON_AddItemToArray(pJsonArray, pJsonNode);
		cJSON_AddItemToObject(pJsonRoot, "screen", pJsonArray);
		printf("%s\n", cJSON_Print(pJsonRoot));
		exit(0);
	}
	if(0){
		struct _cache_st  *pCache = findIdCache(10033);
		pCache->idx = 1;
		gfloat *p1 = (gfloat *)GUINT_TO_POINTER(pCache->address[pCache->idx]);
		p1[0] = 0.0001f;
		p1[1] = 0.0001f;
		p1[2] = 0.1f;
		p1[11] = 25.7f;
		guint *p2 = (guint *)GUINT_TO_POINTER(pCache->address[pCache->idx]);
		p2[3] = 1;
		p2[4] = 1000;
		p2[5] = 8;
		p2[6] = 1532760949;
		p2[8] = 330;
		p2[9] = 64;
		p2[10] = 1;
		
		p1[16] = 0.1f;
		p1[17] = 0.2f;
		p1[18] = 0.3f;
		p1[19] = 0.4f;
		p1[20] = 0.1f;
		p1[21] = 0.2f;
		p1[22] = 0.3f;
		p1[23] = 0.4f;
		//dumpmem(GUINT_TO_POINTER(pCache->address[pCache->idx]) , 28);
		handle_sensor_data(pCache);
		sleep(10);
		exit(0);
	}
    s_asyncQueue = g_async_queue_new();
    g_timeout_add_seconds(1, seconds_timeout, NULL);
    g_thread_unref(g_thread_new ("send command thread", send_command_thread, NULL));
    g_thread_unref(g_thread_new ("mqtt thread", mqtt_thread, NULL));
    g_thread_unref(g_thread_new ("send data thread", send_data_thread, NULL));
    g_thread_unref(g_thread_new ("local socket thread", local_server_thread, NULL));
    g_main_loop_run (main_loop);
    g_main_loop_unref (main_loop);
    return -1;
}
