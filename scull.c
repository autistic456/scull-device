#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/kernel.h> /* printk()  */
#include <linux/slab.h> /* kmalloc() */
#include <linux/fs.h> /* everything */
#include <linux/errno.h>
#include <linux/types.h> /* size_t */
#include <linux/fcntl.h> /* O_ACCMODE */
#include <linux/cdev.h>
#include <linux/uaccess.h> /* copy_*_user */

#include "scull.h"

int scull_major =   SCULL_MAJOR;
int scull_minor =   0;
int scull_nr_devs = SCULL_NR_DEVS;	/* number of bare scull devices */
int scull_quantum = SCULL_QUANTUM;
int scull_qset =    SCULL_QSET;

struct scull_dev *scull_devices; /*array of active devices */
static void scull_setup_cdev(struct scull_dev *, int); /*helper function (internal) */  

MODULE_LICENSE("Dual BSD/GPL");

static struct file_operations scull_fops = {
	.owner = THIS_MODULE,
	.open = scull_open,	
	.read = scull_read,
	.write = scull_write,
	.release = scull_release,
};

int scull_init_module(void) {
	int i;	
	dev_t dev = 0;	
	/* register devices */
	int result = 1;
	result = alloc_chrdev_region(&dev, scull_minor, scull_nr_devs, "scull"); /**comment later**/
	scull_major = MAJOR(dev);
	printk(KERN_ALERT"Passed %s %d \n in scull_init_module\n", __FUNCTION__, __LINE__);
	if(result < 0){
		printk(KERN_WARNING "scull: can't get major %d\n", scull_major);
		return result;
	}

	/* allocate space for devices */	
	scull_devices = kmalloc(sizeof(struct scull_dev) * scull_nr_devs, GFP_KERNEL);
	if(!scull_devices){
		result = -1;
		goto fail;
	}

	/*initilization of devices */
	for(i=0; i<scull_nr_devs;i++){
		scull_devices[i].quantum = scull_quantum;
		scull_devices[i].qset = scull_qset;
		scull_setup_cdev(&scull_devices[i], i);
	}

	return 0;
	fail:
		scull_cleanup_module();
		return result;
}

static void scull_setup_cdev(struct scull_dev *dev, int index){
	int err, devno=MKDEV(scull_major, scull_minor + index);
	
	cdev_init(&dev->cdev, &scull_fops);
	dev->cdev.owner = THIS_MODULE;
	dev->cdev.ops = &scull_fops; /**comment later**/
	err = cdev_add(&dev->cdev, devno, 1);	
	if (err) 
		printk(KERN_NOTICE "Error %d adding scull%d", err, index);
}

int scull_trim(struct scull_dev *dev) {
	struct scull_qset *next, *dptr; /* next for loop, dptr = data pointer (index in loop) */
	int qset = dev->qset; /* get size of arrat */
	int i; /*index for second loop for quantum bytes */

	for(dptr = dev->data /*struct scull_qset*/; dptr ; dptr = next){
		if (dptr->data /*array of quantum*/) {
			for(i=0; i<qset; i++){
				kfree(dptr->data[i]); /*free each byte of array data[i]*/
			}
			kfree(dptr->data); /*free array pointer itself*/
			dptr->data = NULL; /*set array pointer to null pointer to avoid garbage*/
		}
		next = dptr->next;
		kfree(dptr); /* free pointer itself */
	}
	//setting new attributes for cleared dev
	dev->size = 0;
	dev->quantum = scull_quantum;
	dev->qset = scull_qset;
	dev->data = NULL;
	
	return 0;
}

void scull_cleanup_module(void){
	int i;
	dev_t devno = MKDEV(scull_major, scull_minor);
	if(scull_devices){
		for(i=0; i<scull_nr_devs;i++){
			scull_trim(scull_devices+i);
			cdev_del(&scull_devices[i].cdev);
		}
		kfree(scull_devices);
	}
	unregister_chrdev_region(devno, scull_nr_devs);
}

int scull_open(struct inode *inode, struct file *filp){
	struct scull_dev *dev; /*for other information*/

	dev = container_of(inode->i_cdev, struct scull_dev, cdev);
	filp->private_data = dev; /* storing dev sructer obtained from inode structure */

	/*trim to 0 length of the device if open was write-only */
	if ( (filp->f_flags & O_ACCMODE) == O_WRONLY) {
		scull_trim(dev);
	}
	return 0;
}

int scull_release(struct inode *inode, struct file *filp) { return 0; } /* everything is already in scull_cleanup_module, no need to do anything else */

struct scull_qset *scull_follow(struct scull_dev *dev, int n){
	struct scull_qset *qs = dev->data;

	if(!qs){
		qs = dev->data = kmalloc(sizeof(struct scull_qset), GFP_KERNEL);
		if (!qs) 
			return NULL; /* no free page for kmalloc */
		else
			memset(qs->next, 0, sizeof(struct scull_qset)); /* make memory 0 for next struct */
	}

	while(n--) {
		if (!qs->next){
			qs->next = kmalloc(sizeof(struct scull_qset), GFP_KERNEL);
			if(!qs->next)
				return NULL; /*no free page for next scull_qset */
			memset(qs->next, 0, sizeof(struct scull_qset));
		}
		qs = qs->next;
		continue;
	}
	return qs;
}

ssize_t scull_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos) {
	struct scull_dev *dev = filp->private_data;
	struct scull_qset *dptr; /* the first listitme */
	int quantum = dev->quantum, qset = dev->qset;
	int itemsize = quantum*qset; /* total bytes in a listitem */
	int item, set_pos, q_pos, rest;
	ssize_t retval = 0;

	/* finding listitem, qset index and offset in the quantum */	
	item = (long)*f_pos / itemsize; 
	rest = (long)*f_pos % itemsize;
	set_pos = rest / quantum;
	q_pos = rest % quantum;

	dptr = scull_follow(dev, item); /*follow the list up to the right position */
	if(!dptr || !dptr->data || !dptr->data[set_pos])
		goto out; /* didn't get scull_qset, array of data, pointer to bytes */

	/* read only up to the end of quantum (content in array qset)*/	
	if (count > quantum - q_pos)
		count = quantum - q_pos;

	if (copy_to_user(buf, dptr->data[set_pos] + q_pos, count)) {
		retval = -1;
		goto out;
	}

	*f_pos += count;
	retval = count;

	out:
		return retval;
}

ssize_t scull_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos) {
	struct scull_dev *dev = filp->private_data;
	struct scull_qset *dp;
	int quantum = dev->quantum, qset = dev->qset;
	int itemsize = quantum * qset;
	int item, set_pos, q_pos, rest;
	ssize_t retval = -1; /*case that something went bad */

	item = (long)*f_pos / itemsize;
	rest = (long)*f_pos % itemsize;
	set_pos = rest/quantum, q_pos = rest%quantum;

	dp = scull_follow(dev, item);
	if(!dp) goto out; /* no data pointer (struct) */
	if(!dp->data) { /*no array of pointers (void **) */
		dp->data = kmalloc(sizeof(char *)*qset, GFP_KERNEL); /*try to kmalloc array of pointers */
		if (!dp->data) 
			goto out;
		memset(dp->data, 0, sizeof(char *) *qset);
	}
	if (!dp->data[set_pos]) { /*no array of bytes */
		dp->data[set_pos] = kmalloc(quantum, GFP_KERNEL); /* try to kmalloc array for bytes */
		if (!dp->data[set_pos])
			goto out;
	}

	/*write only up to the end of this quantum*/
	if(count > quantum - q_pos)
		count = quantum - q_pos;

	if (copy_from_user(dp->data[set_pos] + q_pos, buf, count)) {
		retval = -1;
		goto out;
	}
	
	*f_pos += count;
	retval = count;

	/*update the size*/
	if(dev->size < *f_pos)
		dev->size = *f_pos;

	out:
	return retval;
}

module_init(scull_init_module);
module_exit(scull_cleanup_module);
