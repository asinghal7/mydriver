#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>   /* printk() */
#include <linux/slab.h>     /* kmalloc() */
#include <linux/fs.h>       /* everything... */
#include <linux/errno.h>    /* error codes */
#include <linux/types.h>    /* size_t */
#include <linux/fcntl.h>    /* O_ACCMODE */
#include <linux/cdev.h>
#include <asm/uaccess.h>    /* copy_*_user */
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Akshat");

int mydriver_major = 0;
int mydriver_minor = 0;
int my_nr_devs = 2;

struct my_dev {
    void *data;
    unsigned long size;
    struct mutex mutex;
    struct cdev cdev;
};
struct my_dev *my_devs;


int my_trim(struct my_dev *dev)
{
    /* "dev" is not-null */
    kfree(dev->data);
    dev->size = 0;
    //dev->quantum = scull_quantum;
    //dev->qset = scull_qset;
    dev->data = NULL;
    return 0;
}


void my_cleanup_module(void)
{
    int i;
    dev_t devno = MKDEV(mydriver_major, mydriver_minor);    /* Get rid of our char dev entries. */
    if (my_devs) {
        for (i = 0; i < my_nr_devs; i++) {
            my_trim(my_devs + i);
            cdev_del(&my_devs[i].cdev);
        }
        kfree(my_devs);
    }    /* cleanup_module is never called if registering failed. */
    unregister_chrdev_region(devno, my_nr_devs);
    printk(KERN_INFO "mydriver: cleanup success\n");
}




static int my_open(struct inode *inode, struct file *file)
{
    struct my_dev *dev = container_of(inode->i_cdev, struct my_dev, cdev);
    /* validate access to device */
    file->private_data = dev;
    if ( (file->f_flags & O_ACCMODE) == O_WRONLY) {
        if (mutex_lock_interruptible(&dev->mutex))
            return -ERESTARTSYS;    
        my_trim(dev); /* Ignore errors. */
        mutex_unlock(&dev->mutex);
        /*
         There might be an issue in case write is requested on the same device by separate processess. It might truncate the file (my_trim) while the second process is not finished writing.
         Thus, a locking mechanism might be required for the same. 
         */
    }
	printk(KERN_DEBUG "process %i (%s) success open minor(%u) file\n", current->pid, current->comm, iminor(inode));
    return 0;
}

int my_release(struct inode *inode, struct file *file)
{
    printk(KERN_DEBUG "process %i (%s) success release minor(%u) file\n", current->pid, current->comm, iminor(inode));
    return 0;
}

static ssize_t my_read(struct file *file, char __user *user_buffer,
                   size_t size, loff_t *offset)
{   
    struct my_dev *dev = (struct my_dev *) file->private_data;
    ssize_t len = min_t(ssize_t, dev->size - *offset, size);
    if (mutex_lock_interruptible(&dev->mutex))
        return -ERESTARTSYS;
    if (len <= 0)
        return 0;

    /* read data from my_data->buffer to user buffer */
    if (copy_to_user(user_buffer, dev->data + *offset, len))
        return -EFAULT;

    *offset += len;
    mutex_unlock(&dev->mutex);
    return len;
}


static ssize_t my_write(struct file *file, const char __user *user_buffer,
                    size_t size, loff_t * offset)
{	
	int i;
    ssize_t fullsize = 1000;
    struct my_dev *dev = (struct my_dev *) file->private_data;
    ssize_t len = min_t(ssize_t, fullsize - (dev->size - *offset), size);
    ssize_t retval = -ENOMEM;
    if (mutex_lock_interruptible(&dev->mutex))
        return -ERESTARTSYS;
    if (len <= 0)
        return 0;
    if (!dev->data) {
        dev->data = kmalloc(fullsize * sizeof(char *), GFP_KERNEL);
        if (!dev->data)
            goto out;
        memset(dev->data, 0, fullsize * sizeof(char *));
    }
    char *tmp = kmalloc(fullsize * sizeof(char *), GFP_KERNEL);
    for(i = 0; i<len; i++){
        char to_upper = tmp[i];
        to_upper = 'A' + to_upper - 'a';// convert to uppercase
        tmp[i] = to_upper;
        if(copy_from_user(tmp+i, user_buffer+i, 1)){
            retval = -EFAULT;
            return retval;
        }
    }
    dev->data = tmp;
    kfree(tmp);
    /* read data from user buffer to my_data->buffer */
    // if (copy_from_user(dev->data + *offset, user_buffer, len)){
    //     retval = -EFAULT;
    //     return retval;
    // }

    *offset += len;
    dev -> size += len;
    retval = len;
    out:
    mutex_unlock(&dev->mutex);
    return retval;
}

loff_t my_llseek(struct file *filp, loff_t off, int whence)
{
    struct my_dev *dev = filp->private_data;
    loff_t newpos;    switch(whence) {
      case 0: /* SEEK_SET */
        newpos = off;
        break;      case 1: /* SEEK_CUR */
        newpos = filp->f_pos + off;
        break;      case 2: /* SEEK_END */
        newpos = dev->size + off;
        break;      default: /* can't happen */
        return -EINVAL;
    }
    if (newpos < 0)
        return -EINVAL;
    filp->f_pos = newpos;
    return newpos;
}

const struct file_operations my_fops = {
    .owner = THIS_MODULE,
    .open = my_open,
    .read = my_read,
    .write = my_write,
    .release = my_release,
    .llseek =   my_llseek,
//    .unlocked_ioctl = my_ioctl
};

/*
 * Set up the char_dev structure for this device.
 */
static void my_setup_cdev(struct my_dev *dev, int index)
{
    int err, devno = MKDEV(mydriver_major, mydriver_minor + index);    
    cdev_init(&dev->cdev, &my_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &my_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    /* Fail gracefully if need be. */
    if (err)
        printk(KERN_NOTICE "Error %d adding mydriver%d", err, index);
    else
		printk(KERN_INFO "mydriver: %d add success\n", index);
}

int my_init_module(void)
{
    int result, i;
    dev_t dev = 0;    
    /*
     * Get a range of minor numbers to work with, asking for a dynamic major
     * unless directed otherwise at load time.
     */
    if (mydriver_major) {
        dev = MKDEV(mydriver_major, mydriver_minor);
        result = register_chrdev_region(dev, my_nr_devs, "mydriver");
    } 
    else {
        result = alloc_chrdev_region(&dev, mydriver_minor, my_nr_devs, "mydriver");
        mydriver_major = MAJOR(dev);
    }
    if (result < 0) {
        printk(KERN_WARNING "mydriver: can't get major %d\n", mydriver_major);
        return result;
    } 
    else {
        printk(KERN_INFO "mydriver: get major %d success\n", mydriver_major);
    }        /*
     * Allocate the devices. This must be dynamic as the device number can
     * be specified at load time.
     */
    my_devs = kmalloc(my_nr_devs * sizeof(struct my_dev), GFP_KERNEL);
    if (!my_devs) {
        result = -ENOMEM;
        goto fail;
    }
    memset(my_devs, 0, my_nr_devs * sizeof(struct my_dev));        /* Initialize each device. */
    for (i = 0; i < my_nr_devs; i++) {
        //scull_devices[i].quantum = scull_quantum;
        //scull_devices[i].qset = scull_qset;
        mutex_init(&my_devs[i].mutex);
        my_setup_cdev(&my_devs[i], i);
    }    
    return 0; /* succeed */  
    fail:
    my_cleanup_module();
    return result;
}


module_init(my_init_module);
module_exit(my_cleanup_module);
