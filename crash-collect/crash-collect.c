/*
 * ---------------------------------------------------------------------------
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 * ---------------------------------------------------------------------------
 */

#include "crash-collect.h"

// max number of directories can monitor
#define DIR_QUEUE_LEN 5

// IPQ server config. currently configured for local host
#define SERVER_IPQ_PORT   49999
#define QBLK_SIZE         1024
#define LOG_TIMESTAMP     "/data/logTimeStamp"
#define INIT_BYTES        8
#define SBUFF_LEN         50
#define RECONNECT_SLEEP_TIMER    10

struct monitor_dir {
	int fwd;
	int file_type;
	char dir[30];
};

struct message{
	int type;
	char file_name_buff[50];
};

struct monitor_dir monitor_dir_in[5];
char qtstamp_buff[50] = {'\0'};
FILE *pfd = NULL;

//send msg to IPQ server with ACK
int logMsgSend(int server_fd, struct message msg, int msgLen) {
	struct stat qfile;
	int fd, ret;
	char ipqAck[10];
	if(send(server_fd, &msg, msgLen, 0) == -1) {
		perror("send");
		return -1;
	}
	int ackSize = recv(server_fd, ipqAck, sizeof(ipqAck), 0);
	if(ackSize == -1) {
		perror("recv");
		return -1;
	} else {
		int len = strlen(msg.file_name_buff);
		msg.file_name_buff[len-1] = '\0';
		//printf("len :%d file:%s\n ", len, msg.file_name_buff);
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
		printf("fripntf return :%d\n", ret);
		memset(qtstamp_buff, 0, sizeof(qtstamp_buff));
	}
	ipqAck[ackSize] = '\0';
	printf("IPQ Ack received : %s ack len : %ld\n", ipqAck, strlen(ipqAck));
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
		logMsgSend(server_fd, msg_buff, sizeof(msg_buff));
	} else {
		printf("raw partition is empty\n");
	}

        return 0;
}

int notify_full_crash(int server_fd, const char *dir_sdcard) {
	struct message dir_buff;
	struct dirent *entry;

	int emmc_flag = system("cat /sys/kernel/dload/emmc_dload");
	if(emmc_flag != 1) {
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
				int ret = logMsgSend(server_fd, dir_buff, sizeof(dir_buff));
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
        const char *mhi_int = "rmnet_mhi0";
        struct ifaddrs *address = if_list;

        while(address != NULL)
        {
                if(address->ifa_addr != NULL)
			family = address->ifa_addr->sa_family;
                if (family == AF_INET && strcmp(address->ifa_name,mhi_int) == 0)
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

int main(void)
{
	int i, fd, wd,len;
	struct stat qfile;
	struct inotify_event *event;
	char buff[CBUFF_LEN] __attribute__ ((aligned(__alignof__(struct inotify_event))));
	struct message sdx_msg;
	int ipq_connect_count = 0;

	pfd = fopen(LOG_TIMESTAMP, "a+");
	if (pfd == NULL){
		perror("fopen");
		return -1;
	}

	// checking interface ip address
	const char* ipq_server_ip = getipq_serversocket();
	while(ipq_server_ip == NULL) {
		printf("interface not available\n");
		sleep(RECONNECT_SLEEP_TIMER);
		ipq_server_ip = getipq_serversocket();
		ipq_connect_count++;
		if(ipq_connect_count == 5){
			printf("interface not available\n");
			exit(EXIT_SUCCESS);
		}
	}

	printf("ipq server address : %s\n", ipq_server_ip);

	// IPQ server config
	int ipq_server_port = SERVER_IPQ_PORT;
	int ipq_server_fd;
	struct sockaddr_in ipq_server_addr;

	ipq_server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if(ipq_server_fd == -1) {
		perror("socket :");
		exit(EXIT_FAILURE);
	}
	ipq_server_addr.sin_family = AF_INET;
	ipq_server_addr.sin_port = htons(ipq_server_port);
	inet_pton(AF_INET, ipq_server_ip, &ipq_server_addr.sin_addr);

	int ret = connect(ipq_server_fd, (struct sockaddr*)&ipq_server_addr, sizeof(ipq_server_addr));
	if(ret == -1) {
		perror("connect :");
		exit(EXIT_FAILURE);
	}

	fd = inotify_init();
	if (fd == -1) {
		perror("inotify_init :");
		exit(EXIT_FAILURE);
	}

	// check if data/crash bin files present after full crash(emmc/sd card)
	// if present, send to IPQ
	notify_full_crash(ipq_server_fd, PATH_FULL_DUMP_SD);

	int fwd1 = inotify_add_watch(fd, PATH_SSR_DUMP, IN_ACCESS | IN_CLOSE_WRITE|IN_CLOSE);
	printf("file %s, fd : %d\n",PATH_SSR_DUMP, fwd1);
	if (fwd1 == -1) {
		perror("inotify_add_watch fwd1 :");
	}
	monitor_dir_in[0].fwd = fwd1;
	monitor_dir_in[0].file_type = 1;
	strlcpy(monitor_dir_in[0].dir, PATH_SSR_DUMP, sizeof(PATH_SSR_DUMP));

	while (1) {
		len = read(fd, buff, CBUFF_LEN);
		printf("checking events : %d\n", len);
		if (len == -1) {
			perror("read");
			return -1;
		}
		i = 0;
		while (i < len) {
			struct inotify_event *ssr_event = (struct inotify_event *)&buff[i];
			printf("File : %s %ld len:%d i:%d\n", ssr_event->name, strlen(ssr_event->name), len, i);
			if (strlen(ssr_event->name) <= 1) {
				i += CRASH_EVENT_SIZE + ssr_event->len ;
				continue ;
			}
			const char *dir_name = getDir(ssr_event->wd);
			int logFileType = qLogfileType(ssr_event->wd);
			printf("type %d File : %s%s ",logFileType, dir_name, ssr_event->name);

			if (ssr_event->mask & IN_CREATE)
				printf(" IN_CREATE");

			if (ssr_event->mask & IN_CLOSE_WRITE)
				printf(" IN_CLOSE_WRITE");

			if (ssr_event->mask & IN_CLOSE)
				printf(" IN_CLOSE");

			printf("%s%s\n", dir_name, ssr_event->name);
			memset(&sdx_msg, 0, sizeof(sdx_msg));
			snprintf(sdx_msg.file_name_buff, sizeof(sdx_msg.file_name_buff), "%s%s ", dir_name, ssr_event->name);
			sdx_msg.type = logFileType;
			int ret = logMsgSend(ipq_server_fd, sdx_msg, sizeof(sdx_msg));

			i += CRASH_EVENT_SIZE + ssr_event->len ;
		}
	}
	inotify_rm_watch(fd, fwd1);
	close(fd);
	free(ipq_server_ip);
	return 0;
}
