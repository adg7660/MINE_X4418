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

#include <spinlock.h>
#include "vfs.h"
#include "lib.h"
#include "printk.h"
#include "sys/dirent.h"
#include "errno.h"
#include "memory.h"


struct super_block * root_sb = NULL;

struct dentry *d_alloc(struct dentry * parent, const char *name, int len)
{
	struct dentry *path = (struct dentry *)kmalloc(sizeof(struct dentry), 0);
	if(!path) return NULL;

	memset(path, 0, sizeof(struct dentry));
	
	char *temp_name = kmalloc(len + 1, 0);
	if(!temp_name){
		kfree(path);
		return NULL;
	}
	memset(temp_name, 0, len + 1);
	memcpy(temp_name, name, len);

	path->parent = parent;
	path->name = temp_name;
	path->name_length = len;
	return path;
}

struct dentry * d_lookup(struct dentry * parent, char *name, int len){
	struct list_head *i;

	if (!strncmp(name, ".", len)) {
		return parent;
	} else if(!strncmp(name, "..", len)) {
		return parent->parent;
	}

	list_for_each(i,&parent->subdirs_list){
		struct dentry *temp = container_of(i,struct dentry,child_node);
		if(!strncmp(name, temp->name, len)){
			return temp;
		}
	}
	return NULL;
}
struct dentry * path_walk(char * name, unsigned long flags) {
	char * tmpname = NULL;
	int tmpnamelen = 0;
	struct dentry * parent = root_sb->root;
	struct dentry * path = NULL;

	while (*name == '/')
		name++;

	if (!*name) {
		return parent;
	}

	for (;;) {
		tmpname = name;
		while (*name && (*name != '/'))
			name++;
		tmpnamelen = name - tmpname;

		path = d_lookup(parent, tmpname, tmpnamelen);

		if (path == NULL) {
			path = d_alloc(parent, tmpname, tmpnamelen);
			if (path == NULL)
				return NULL;
			if (parent->d_inode->inode_ops->lookup(parent->d_inode, path) == NULL) {
				color_printk(RED, WHITE, "can not find file or dir:%s\n", path->name);
				kfree(path->name);
				kfree(path);
				return NULL;
			}
		}

		init_list_head(&path->child_node);
		init_list_head(&path->subdirs_list);
		list_add(&path->child_node, &parent->subdirs_list);

		if (!*name)
			goto last_component;
		while (*name == '/')
			name++;
		if (!*name)
			goto last_slash;

		parent = path;
	}

last_slash:
last_component:

	if (flags & 1) {
		return parent;
	}

	return path;
}

int fill_dentry(void *buf, char *name, long namelen, long type, long offset) {
	struct dirent* dent = (struct dirent*)buf;
	int reclen = ALIGN(sizeof(struct dirent) + namelen + 1, sizeof(int));////TODO:可能多加一点，万一宽字符

	if ((unsigned long)buf < TASK_SIZE && !verify_area(buf, sizeof(struct dirent) + namelen))
		return -EFAULT;

	memcpy(dent->d_name, name, reclen);
	dent->d_reclen = reclen;
	dent->d_type = type;
	dent->d_off = offset;
	return sizeof(struct dirent) + reclen;
}


static struct list_head __filesystem_list = {
	.next = &__filesystem_list,
	.prev = &__filesystem_list,
};
static spinlock_t __filesystem_lock = SPIN_LOCK_INIT();

static struct kobj_t * search_class_filesystem_kobj(void)
{
	struct kobj_t * kclass = kobj_search_directory_with_create(kobj_get_root(), "class");
	return kobj_search_directory_with_create(kclass, "filesystem");
}

struct filesystem_t * search_filesystem(const char * name)
{
	struct filesystem_t * pos, * n;

	if(!name)
		return NULL;

	list_for_each_entry_safe(pos, n, &__filesystem_list, list)
	{
		if(strcmp(pos->name, name) == 0)
			return pos;
	}
	return NULL;
}

bool_t mount_fs(char * path, char *dev, char * name) {
	struct block_t * bdev;
	struct filesystem_t * p = search_filesystem(name);
	if (p) {
		if(dev == NULL)
			return FALSE;
		while(1){
			if(bdev = search_block(dev))
				break;	
		}
		struct super_block *sb = p->read_superblock(bdev);
		if((!strcmp(path, "/")) && root_sb == NULL)
			root_sb = sb;
		return TRUE;
	} else
		return FALSE;
}

bool_t register_filesystem(struct filesystem_t * fs)
{
	irq_flags_t flags;

	if(!fs || !fs->name)
		return FALSE;

	if(search_filesystem(fs->name))
		return FALSE;

	fs->kobj = kobj_alloc_directory(fs->name);
	kobj_add(search_class_filesystem_kobj(), fs->kobj);

	spin_lock_irqsave(&__filesystem_lock, flags);
	list_add_tail(&fs->list, &__filesystem_list);
	spin_unlock_irqrestore(&__filesystem_lock, flags);

	return TRUE;
}

bool_t unregister_filesystem(struct filesystem_t * fs)
{
	irq_flags_t flags;

	if(!fs || !fs->name)
		return FALSE;

	spin_lock_irqsave(&__filesystem_lock, flags);
	list_del(&fs->list);
	spin_unlock_irqrestore(&__filesystem_lock, flags);
	kobj_remove(search_class_filesystem_kobj(), fs->kobj);
	kobj_remove_self(fs->kobj);

	return TRUE;
}


