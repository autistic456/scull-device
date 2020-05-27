#ifndef _SCULL_H_ /*beginning*/
#define _SCULL_H_

#ifndef SCULL_MAJOR 
#define SCULL_MAJOR 0 
#endif

#ifndef SCULL_NR_DEVS
#define SCULL_NR_DEVS 4
#endif

#ifndef SCULL_QUANTUM
#define SCULL_QUANTUM 4000
#endif

#ifndef SCULL_QSET
#define SCULL_QSET 1000
#endif

/*representation of quantum*/
struct scull_qset {
	void **data;
	struct scull_qset *next;
};

/*main structure*/
struct scull_dev {
	struct scull_qset *data; /*quantum repre*/
	int quantum; /*in bytes*/
	int qset; /*array size*/
	unsigned long size; /*total bytes in device*/
	struct cdev cdev; /*Char device structure*/
};

/*global vars*/
extern int scull_major;
extern int scull_nr_devs;
extern int scull_quantum;
extern int scull_qset;

/*prototypes for shared functions*/
extern int scull_trim(struct scull_dev* dev);
extern ssize_t scull_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos);
extern ssize_t scull_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos);
extern int scull_open(struct inode *inode, struct file *filp);
extern int scull_release(struct inode *inode, struct file *filp);
extern int scull_init_module(void);
extern void scull_cleanup_module(void);

#endif /*_SCULL_H_*/

