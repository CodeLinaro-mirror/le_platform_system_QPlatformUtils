/*
 * ---------------------------------------------------------------------------
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 * ---------------------------------------------------------------------------
 */

#include "crash-collect.h"
#include "errno.h"

// max number of directories can monitor
#define DIR_QUEUE_LEN 5
#define IPQ_SERIP "10.10.10.7"
#define IPLEN 16
#define DEBUG 1

// IPQ server config. currently configured for local host
#define QBLK_SIZE         1024
#define INIT_BYTES        8
#define SBUFF_LEN         50
#define CONNECT_SLEEP_TIMER    200
#define RECONNECT_SLEEP_TIMER    2
#define RECONNECT_RETRY_COUNT    5

struct monitor_dir {
	int fwd;
	int file_type;
	char dir[30];
};

struct message{
	int type;
	char file_name_buff[100];
};

int status = 0;
struct sockaddr_in ipq_server_addr;
struct monitor_dir monitor_dir_in[5];
struct message last_msg;
static int server_fd;
char qtstamp_buff[50] = {'\0'};
FILE *pfd = NULL;

//Handling SIGPIPE received while sending notification to IPQ
void sigp_handle(int sig) {
	printf("SIGPIPE received\n");
}

//send msg to IPQ server with ACK
int logMsgSend(struct message msg, int msgLen) {
	struct stat qfile;
	int fd, ret, ipq_retry_count=0;
	int len_cur = 0, len_last = 0;

	int len = strlen(msg.file_name_buff);
	msg.file_name_buff[len-1] = '\0';

	len_cur = strlen(msg.file_name_buff);
	len_last = strlen(last_msg.file_name_buff);

	printf("Debug mode enable\n");
	printf("sending data to ipq\n");
	if((status == 1) && strcmp(&last_msg.file_name_buff, &msg.file_name_buff)== 0){
		printf("File already notified : Duplicate event\n");
		return 0;
	}
	if(send(server_fd, &msg, msgLen, 0) == -1){
		perror("send: disconnected ");
		//return -1;
		status = 0;
RECONNECT_IPQ:
		close(server_fd);
		server_fd = socket(AF_INET, SOCK_STREAM, 0);
	        if(server_fd == -1) {
        	        perror("socket :");
                	exit(EXIT_FAILURE);
        	}

	        printf("connecting to server\n");
        	ipq_retry_count = 0;
	        int ret = connect(server_fd, (struct sockaddr*)&ipq_server_addr, sizeof(ipq_server_addr));
        	while(ret == -1) {
                	sleep(RECONNECT_SLEEP_TIMER);
                	printf("connecting to server...%d\n", ipq_retry_count);
                	ret = connect(server_fd, (struct sockaddr*)&ipq_server_addr, sizeof(ipq_server_addr));
                	ipq_retry_count++;
        	}
		if((ret != -1) && strcmp(last_msg.file_name_buff, msg.file_name_buff)== 0){
			printf("File already notified\n");
			return 0;
		}
		if(&last_msg.file_name_buff != NULL)
			if(send(server_fd, &last_msg, msgLen, 0) == -1){
				goto RECONNECT_IPQ;
			}
		if(send(server_fd, &msg, msgLen, 0) == -1)
			goto RECONNECT_IPQ;
	}
#ifdef DEBUG
	printf("file notified to IPQ:%s\n",msg.file_name_buff);
#endif

	status = 1;
	memset(&last_msg, 0, sizeof(last_msg));
	strlcpy(&last_msg.file_name_buff, &msg.file_name_buff, strlen(msg.file_name_buff)+1);
	last_msg.type = msg.type;

		fd = open(msg.file_name_buff, O_RDONLY);
		if (fd == -1) {
			perror("open");
			return -1;
		}
		if (fstat(fd, &qfile) == -1) {
			perror("stat");
		}
		time_t last_mtime = qfile.st_mtime;
		struct tm *file_mtime = localtime(&last_mtime);
		snprintf(qtstamp_buff, sizeof(qtstamp_buff), "%04d/%02d/%02d %02d:%02d:%02d file: %s", file_mtime->tm_year+1900, \
				file_mtime->tm_mon+1, file_mtime->tm_mday, file_mtime->tm_hour, file_mtime->tm_min, file_mtime->tm_sec, msg.file_name_buff);
		printf("file string : %s\n", qtstamp_buff);
		fflush(pfd);
		ret = fprintf(pfd,"%s\n", qtstamp_buff);
		if(ret == -1) {
			perror("fprintf");
			return -1;
		}
		fflush(pfd);
		memset(qtstamp_buff, 0, sizeof(qtstamp_buff));
		close(fd);
	return 0;
}

int read_rawdump(int server_fd){
        struct message msg_buff;
	char rbuff[QBLK_SIZE];
	int i, isOne=0, isZero=0;
	int raw_partfd = open(PATH_FULL_DUMP, O_RDWR);
	if(raw_partfd == -1) {
		perror("open");
		return -1;
	}

	int bytRead = read(raw_partfd, rbuff, INIT_BYTES);
	if(bytRead == -1){
		perror("read");
		return -1;
	}

	for(i=0; i<INIT_BYTES; i++){
		if(rbuff[i] == '0') {
			isZero++ ;
		} else if (rbuff[i] == '1'){
			isOne++;
		}
	}

	if(isZero < INIT_BYTES || isOne < INIT_BYTES ) {
		lseek(raw_partfd, 0, SEEK_SET);
                snprintf(msg_buff.file_name_buff, sizeof(msg_buff.file_name_buff), "%s",PATH_FULL_DUMP);
                msg_buff.type = 3;
		logMsgSend(msg_buff, sizeof(msg_buff));
	} else {
		printf("raw partition is empty\n");
	}

        return 0;
}

int notify_full_crash(const char *dir_sdcard) {
	struct message dir_buff;
	struct dirent *entry;

	int emmc_flag = system("cat /sys/kernel/dload/emmc_dload");
	if(emmc_flag == 1) {
		read_rawdump(server_fd);
	} else {
		DIR *dir = opendir(dir_sdcard);

		if (dir == NULL) {
			perror("opendir");
			return -1;
		}

		printf("SD card path : %s\n", dir_sdcard);
		while((entry = readdir(dir)) != NULL) {
			if (entry->d_type == DT_DIR) {
				if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
					continue;
				}

				memset(&dir_buff, 0, sizeof(dir_buff));
				snprintf(dir_buff.file_name_buff, sizeof(dir_buff.file_name_buff), "%s%s",dir_sdcard, entry->d_name);
				dir_buff.type = 3;
				int ret = logMsgSend(dir_buff, sizeof(dir_buff));
			}
		}
	}
}

// to fetch directory log type
int qLogfileType(int fwd){
	for(int i=0; i < DIR_QUEUE_LEN; i++) {
		if(fwd == monitor_dir_in[i].fwd)
			return monitor_dir_in[i].file_type;
	}
	return 0;
}

// to fetch respective directory
const char* getDir(int fwd){
	for(int i=0; i < DIR_QUEUE_LEN; i++) {
		if(fwd == monitor_dir_in[i].fwd)
			return monitor_dir_in[i].dir;
	}
	return NULL;
}

const char* getipq_serversocket(void) {
        struct ifaddrs *if_list;
        char ipq_serip[50] ;
        int family, family_size = sizeof(struct sockaddr_in);

	memset(ipq_serip, 0, SBUFF_LEN);
	if (getifaddrs(&if_list) == -1)
        {
                printf("getifaddrs call failed\n");
                return NULL;
        }
        struct ifaddrs *address = if_list;

        while(address != NULL)
        {
                if(address->ifa_addr != NULL)
			family = address->ifa_addr->sa_family;
                if (family == AF_INET && strcmp(address->ifa_name, MHISW_INTFC) == 0)
                {
                        getnameinfo(address->ifa_addr,family_size, ipq_serip, sizeof(ipq_serip), 0, 0, NI_NUMERICHOST);
                        break;
                }
                address = address->ifa_next;
        }

        freeifaddrs(if_list);
	if(strlen(ipq_serip) == 0)
		return NULL;
        return strdup(ipq_serip);
}

/* Pass first argument as server address to connect with crash-collect */
int main(int argc, char *argv[])
{
	int i, fd, wd,len;
	struct stat qfile;
	struct inotify_event *event;
	char buff[CBUFF_LEN] __attribute__ ((aligned(__alignof__(struct inotify_event))));
	struct message sdx_msg;
	int ipq_connect_count = 0;

	/* Handling SIGPIPE single received from send() call */
	signal(SIGPIPE, sigp_handle);

	pfd = fopen(LOG_TIMESTAMP, "a+");
	if (pfd == NULL){
		perror("fopen");
		return -1;
	}
	/* connecting to ipq server ip */
	char* ipq_server_ip = (char *)malloc(24);
	if(argc > 1) {
		strlcpy(ipq_server_ip, argv[1], IPLEN);
	} else {
		strlcpy(ipq_server_ip, IPQ_SERIP, IPLEN);
	}
#ifdef DEBUG
	printf("argc:%d ipq_server_ip:%s\n", argc, ipq_server_ip);
	printf("ipq server address : %s\n", ipq_server_ip);
#endif

	// IPQ server config
	int ipq_server_port = SERVER_IPQ_PORT;

	server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if(server_fd == -1) {
		perror("socket :");
		exit(EXIT_FAILURE);
	}
	ipq_server_addr.sin_family = AF_INET;
	ipq_server_addr.sin_port = htons(ipq_server_port);
	inet_pton(AF_INET, ipq_server_ip, &ipq_server_addr.sin_addr);

	printf("connecting to server\n");
	ipq_connect_count = 0;
	int ret = connect(server_fd, (struct sockaddr*)&ipq_server_addr, sizeof(ipq_server_addr));
	while(ret == -1) {
                usleep(CONNECT_SLEEP_TIMER);
                printf("connecting to server...%d\n", ipq_connect_count);
		        ret = connect(server_fd, (struct sockaddr*)&ipq_server_addr, sizeof(ipq_server_addr));
                ipq_connect_count++;
		if(ipq_connect_count >= RECONNECT_RETRY_COUNT){
			perror("connect:");
			exit(EXIT_FAILURE);
		}
        }
	printf("connected to server\n");

	fd = inotify_init();
	if (fd == -1) {
		perror("inotify_init :");
		exit(EXIT_FAILURE);
	}

	// check if data/crash bin files present after full crash(emmc/sd card)
	// if present, send to IPQ
	notify_full_crash(PATH_FULL_DUMP_SD);

	int fwd1 = inotify_add_watch(fd, PATH_SSR_DUMP, IN_CLOSE_WRITE);
	if (fwd1 == -1) {
		perror("inotify_add_watch fwd1 :");
		exit(EXIT_FAILURE);
	}
#ifdef DEBUG
	printf("file %s, fd : %d\n",PATH_SSR_DUMP, fwd1);
#endif
	monitor_dir_in[0].fwd = fwd1;
	monitor_dir_in[0].file_type = 1;
	strlcpy(monitor_dir_in[0].dir, PATH_SSR_DUMP, sizeof(PATH_SSR_DUMP));

	while (1) {
		len = read(fd, buff, CBUFF_LEN);
#ifdef DEBUG
		printf("checking events : %d\n", len);
#endif
		if (len == -1) {
			perror("read");
			return -1;
		}
		i = 0;
		while (i < len) {

			struct inotify_event *ssr_event = (struct inotify_event *)&buff[i];
#ifdef DEBUG
			printf("File : %s %ld len:%d i:%d\n", ssr_event->name, strlen(ssr_event->name), len, i);
#endif
			if (strlen(ssr_event->name) <= 1) {
				i += CRASH_EVENT_SIZE + ssr_event->len ;
				continue ;
			}
			const char *dir_name = getDir(ssr_event->wd);
			int logFileType = qLogfileType(ssr_event->wd);
#ifdef DEBUG
			printf("type %d File : %s%s ",logFileType, dir_name, ssr_event->name);
			printf("%s%s\n", dir_name, ssr_event->name);
#endif
			memset(&sdx_msg, 0, sizeof(sdx_msg));
			snprintf(sdx_msg.file_name_buff, sizeof(sdx_msg.file_name_buff), "%s%s ", dir_name, ssr_event->name);
			sdx_msg.type = logFileType;
			int ret = logMsgSend(sdx_msg, sizeof(sdx_msg));

			i += CRASH_EVENT_SIZE + ssr_event->len ;
		}
	}
	inotify_rm_watch(fd, fwd1);
	close(fd);
	free(ipq_server_ip);
	return 0;
}
