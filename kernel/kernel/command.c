#include <types.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <timer.h>
#include <memory.h>
#include "command.h"
#include "fcntl.h"
#include "sys/dirent.h"
#include "vfs.h"

#define CMD_MAX_CMD_NUM 50
#define CMD_MAXARGS 10

extern cmd_table *ct_list[];
int run_command(char *cmd);



void view_hex(char *data, int len){
	for (int i = 0; i < (len + 15) / 16; i++) {
		/* 每行打印16个数据 */
		for (int j = 0; j < 16; j++) {
			/* 先打印数值 */
			unsigned char c = data[i * 16 + j];
			if ((i * 16 + j) < len)
				printf("%02x ", c);
			else
				printf("   ");
		}

		printf("   ; ");

		for (int j = 0; j < 16; j++) {
			/* 后打印字符 */
			unsigned char c = data[i * 16 + j];
			if ((i * 16 + j) < len){
				if (c < 0x20 || c > 0x7e)  /* 不可视字符 */
					putchar('.');
				else
					putchar(c);
			}
				
		}
		printf("\n");
	}
}

CMD_DEFINE(mmtest, "mmtest", "mmtest") {
	UINT16 *data, *data2;
	data = kmalloc(7680, 0);
	printf("kamlloc size = %d addr = %X\n", 7680, data);
	kfree(data);
	data2 = kmalloc(7680, 0);
	printf("kamlloc size = %d addr = %X\n", 7680, data2);
	kfree(data);
	
	return 0;
}

CMD_DEFINE(help, "help", "help") {
	for (int i = 0; ct_list[i] != NULL; i++) {
		printf("%-20s:\t-%s\n", ct_list[i]->name, ct_list[i]->usage);
	}
	return 0;
}
#define CMD_ENTRY(x) & ct_##x
cmd_table *ct_list[] = {
	CMD_ENTRY(help),
	CMD_ENTRY(mmtest),
	NULL
};
cmd_table *search_cmd(char *name) {
	for (int i = 0; ct_list[i] != NULL; i++) {
		if (strcmp(ct_list[i]->name, name) == 0) {
			return ct_list[i];
		}
	}
	return NULL;
}
int run_command(char *cmd) {
	char str[256] = {
		[255] = 0
	};
	strncpy(str, cmd, 255);

	char *argv[CMD_MAXARGS + 1] = {0};	/* NULL terminated	*/
	int argc = 0;
	int cmdlen = strlen(cmd);

	for (int i = 0; i < cmdlen; i++) {
		if (str[i] != ' ' && i != 0) {
			continue;
		} else {
			while (str[i] == ' ') {
				str[i] = '\0';
				i++;
			}
			if (i < cmdlen) {
				argv[argc] = &str[i];
				argc++;
				if (argc == CMD_MAXARGS + 1)
					return -1;
			} else
				break;
		}
	}
	cmd_table *pct = search_cmd(argv[0]);
	if (pct) {
		pct->cmd(pct, argc, argv);
	} else {
		printf("%s:command not found\n", argv[0]);
		return 0;
	}
	return 1;
}
static int get_str(char *buf, int len) {
	int i;
	for (i = 0; i < len - 1; i++) {
		char c = getc();
		if (c == '\r') {
			if (i == 0) {
				return -1;
			} else {
				printf("\n");
				buf[i] = '\0';
				break;
			}
		} else if (c == '\b') {
			if (i > 0) { //前面有字符
				putc(c);
				i = i - 2;
			} else { //前面没有字符
				i = i - 1;
			}
		} else {
			putc(c);
			buf[i] = c;
		}
	}
	return 1;
}
int cmd_loop() {
	char buf[100] = {0};
	while (1) {
		printf("\nOS>");
		if (get_str(buf, 100) == -1)
			continue;
		run_command(buf);
	}
	return 0;
}
