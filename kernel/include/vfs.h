/***************************************************
*		版权声明
*
*	本操作系统名为：MINE
*	该操作系统未经授权不得以盈利或非盈利为目的进行开发，
*	只允许个人学习以及公开交流使用
*
*	代码最终所有权及解释权归田宇所有；
*
*	本模块作者：	田宇
*	EMail:		345538255@qq.com
*
*
***************************************************/

#ifndef __VFS_H__
#define __VFS_H__

#include "lib.h"

struct Disk_Partition_Table_Entry {
	unsigned char flags;
	unsigned char start_head;
	unsigned short  start_sector	: 6,	//0~5
				start_cylinder	: 10;	//6~15
	unsigned char type;
	unsigned char end_head;
	unsigned short  end_sector	: 6,	//0~5
			  end_cylinder	: 10;	//6~15
	unsigned int start_LBA;
	unsigned int sectors_limit;
} __attribute__((packed));

struct Disk_Partition_Table {
	unsigned char BS_Reserved[446];
	struct Disk_Partition_Table_Entry DPTE[4];
	unsigned short BS_TrailSig;
} __attribute__((packed));

struct filesystem_t {
	struct kobj_t * kobj;
	struct list_head list;
	const char * name;
	int fs_flags;
	struct super_block *(*read_superblock)(struct Disk_Partition_Table_Entry *DPTE, void *buf);
};

struct super_block *mount_fs(char *name, struct Disk_Partition_Table_Entry *DPTE, void *buf);
bool_t register_filesystem(struct filesystem_t * fs);
bool_t unregister_filesystem(struct filesystem_t * fs);

struct super_block_operations;
struct inode_operations;
struct dentry_operations;
struct file_operations;

struct super_block {
	struct dentry *root;

	struct super_block_operations *sb_ops;

	void *private_sb_info;
};

struct inode {
	unsigned long file_size;
	unsigned long blocks;
	unsigned long attribute;

	struct super_block *sb;

	struct file_operations *f_ops;
	struct inode_operations *inode_ops;

	void *private_index_info;
};

#define FS_ATTR_FILE	(1UL << 0)
#define FS_ATTR_DIR		(1UL << 1)
#define	FS_ATTR_DEVICE	(1UL << 2)

struct dentry {
	char *name;
	int name_length;
	struct list_head child_node;
	struct list_head subdirs_list;

	struct inode *dir_inode;
	struct dentry *parent;

	struct dentry_operations *dir_ops;
};

struct file {
	long position;
	unsigned long mode;

	struct dentry *dentry;

	struct file_operations *f_ops;

	void *private_data;
};

struct super_block_operations {
	void (*write_superblock)(struct super_block *sb);
	void (*put_superblock)(struct super_block *sb);

	void (*write_inode)(struct inode *inode);
};

struct inode_operations {
	long (*create)(struct inode *inode, struct dentry *dentry, int mode);
	struct dentry *(*lookup)(struct inode *parent_inode, struct dentry *dest_dentry);
	long (*mkdir)(struct inode *inode, struct dentry *dentry, int mode);
	long (*rmdir)(struct inode *inode, struct dentry *dentry);
	long (*rename)(struct inode *old_inode, struct dentry *old_dentry, struct inode *new_inode, struct dentry *new_dentry);
	long (*getattr)(struct dentry *dentry, unsigned long *attr);
	long (*setattr)(struct dentry *dentry, unsigned long *attr);
};

struct dentry_operations {
	long (*compare)(struct dentry *parent_dentry, char *source_filename, char *destination_filename);
	long (*hash)(struct dentry *dentry, char *filename);
	long (*release)(struct dentry *dentry);
	long (*iput)(struct dentry *dentry, struct inode *inode);
};

typedef int (*filldir_t)(void *buf, char *name, long namelen, long type, long offset);

struct file_operations {
	long (*open)(struct inode *inode, struct file *filp);
	long (*close)(struct inode *inode, struct file *filp);
	long (*read)(struct file *filp, char *buf, unsigned long count, long *position);
	long (*write)(struct file *filp, char *buf, unsigned long count, long *position);
	long (*lseek)(struct file *filp, long offset, long origin);
	long (*ioctl)(struct inode *inode, struct file *filp, unsigned long cmd, unsigned long arg);

	long (*readdir)(struct file *filp, void *dirent, filldir_t filler);
};

struct dentry *path_walk(char *name, unsigned long flags);
int fill_dentry(void *buf, char *name, long namelen, long type, long offset);
extern struct super_block *root_sb;

#endif
