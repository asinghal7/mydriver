#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include<linux/slab.h>                 //kmalloc()
#include<linux/uaccess.h>              //copy_to/from_user()
#include<linux/sysfs.h>
#include<linux/kobject.h>
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Akshat");

static int my_mode = 0;
static int my_bmode = 0;
static int my_bsize = 0;
static int mydriver_major = 0;
static int mydriver_minor = 0;
static int my_nr_devs = 2;

static DEFINE_MUTEX(sys_mutex);

static struct class *dev_class;
static struct kobject *kobj_ref;

struct my_dev {
        void *data;
        unsigned long size;
        struct mutex mutex;
        struct cdev cdev;
};
static struct my_dev *my_devs;

static int __init my_driver_init(void);
static void __exit my_driver_exit(void);

/*************** Driver Fuctions **********************/
static int my_open(struct inode *inode, struct file *file);
static int my_release(struct inode *inode, struct file *file);
static ssize_t my_read(struct file *filp, char __user *user_buffer,
                                        size_t size, loff_t *offset);
static ssize_t my_write(struct file *filp, const char *user_buffer,
                                        size_t size, loff_t *offset);
 
/*************** Sysfs Fuctions **********************/
static ssize_t sysfs_show(struct kobject *kobj,
                struct kobj_attribute *attr, char *buf);
static ssize_t sysfs_store(struct kobject *kobj, 
                struct kobj_attribute *attr,const char *buf, size_t count);
static struct kobj_attribute my_attr_mode =  __ATTR(my_mode, 0660,
                                                        sysfs_show, sysfs_store);
static struct kobj_attribute my_attr_bmode = __ATTR(my_bmode, 0660,
                                                        sysfs_show, sysfs_store);
static struct kobj_attribute my_attr_bsize = __ATTR(my_bsize, 0660,
                                                        sysfs_show, sysfs_store);

static struct attribute *attrs[] = {
        &my_attr_mode.attr,
        &my_attr_bmode.attr,
        &my_attr_bsize.attr,
        NULL,
};

static struct attribute_group attr_group = {
    .attrs = attrs,
};

static struct file_operations my_fops =
{
        .owner          = THIS_MODULE,
        .read           = my_read,
        .write          = my_write,
        .open           = my_open,
        .release        = my_release,
};

static ssize_t sysfs_show(struct kobject *kobj, 
                struct kobj_attribute *attr, char *buf)
{       
        //printk(KERN_INFO "Sysfs - Read!\n");
        if (strcmp(attr->attr.name, "my_mode") == 0){
                return sprintf(buf, "%d\n", my_mode);
        } else if (strcmp(attr->attr.name, "my_bmode") == 0){
                return sprintf(buf, "%d\n", my_bmode);
        } else if (strcmp(attr->attr.name, "my_bsize") == 0){
                return sprintf(buf, "%d\n", my_bsize);
        }
        return 0;
}

/*
 * In case the program is at evaluation the of condition for the 
 * else if case for mode '1' and another process changes my_mode 
 * from '2' to '0', the program will fall through to the dummy value. 
 * This is undesirable and hence calls for the use of a lock.
 */
static ssize_t sysfs_store(struct kobject *kobj,
                struct kobj_attribute *attr, const char *buf, size_t count)
{
        //printk(KERN_INFO "Sysfs - Write!\n");
        /*
         * Mode 0: Text in user input format 
         * Mode 1: Converting user input to uppercase
         * Mode 2: ROT13 encryption/decryption
         */
        mutex_lock(&sys_mutex);
        if (strcmp(attr->attr.name, "my_mode") == 0) {
            sscanf(buf, "%d\n", &my_mode);
        } else if (strcmp(attr->attr.name, "my_bmode") == 0) {
            sscanf(buf, "%d\n", &my_bmode);
        } else if (strcmp(attr->attr.name, "my_bsize") == 0) {
            sscanf(buf, "%d\n", &my_bsize);
        }
        mutex_unlock(&sys_mutex);
        return count;
}

int my_trim(struct my_dev *dev)
{
        /* "dev" is not-null */
        kfree(dev->data);
        dev->size = 0;
        dev->data = NULL;
        return 0;
}


static int my_open(struct inode *inode, struct file *file)
{       

        struct my_dev *dev = container_of(inode->i_cdev, struct my_dev, cdev);
        /* validate access to device */
        file->private_data = dev;
        if ((file->f_flags & O_ACCMODE) == O_WRONLY) {
                if (mutex_lock_interruptible(&dev->mutex))
                        return -ERESTARTSYS;    
                my_trim(dev); /* Ignore errors. */
                mutex_unlock(&dev->mutex);
        
        }
        printk(KERN_DEBUG "process %i (%s) success open minor(%u) file\n", current->pid, current->comm, iminor(inode));
        return 0;
}

static int my_release(struct inode *inode, struct file *file)
{
        printk(KERN_DEBUG "process %i (%s) success release minor(%u) file\n", current->pid, current->comm, iminor(inode));
        return 0;
}

static ssize_t my_read(struct file *filp, 
                char __user *user_buffer, size_t size, loff_t *offset)
{
        struct my_dev *dev = (struct my_dev *) filp->private_data;
        ssize_t len = min_t(ssize_t, dev->size - *offset, size);
        
        if (mutex_lock_interruptible(&dev->mutex))
                return -ERESTARTSYS;
        if (len <= 0){
                mutex_unlock(&dev->mutex);
                return 0;
        }
        /* read data from my_data->buffer to user buffer */
        if (copy_to_user(user_buffer, dev->data + *offset, len))
                return -EFAULT;

        *offset += len;
        mutex_unlock(&dev->mutex);
        return len;
}
static ssize_t my_write(struct file *filp,
                const char __user *user_buffer, size_t size, loff_t *offset)
{
        ssize_t fullsize = 1000;
        
        struct my_dev *dev = (struct my_dev *) filp->private_data;
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
        /* read data from user buffer to my_data->buffer */
        if (copy_from_user(dev->data + *offset, user_buffer, len)){
                retval = -EFAULT;
                return retval;
        }

        *offset += len;
        dev -> size += len;
        retval = len;
        out:
        mutex_unlock(&dev->mutex);
        return retval;
}

/*
 * Set up the char_dev structure for this device.
 */
static int my_setup_cdev(struct my_dev *dev, int index)
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
        return err;
}

static int __init my_driver_init(void)
{
        int result, i;
        dev_t dev = 0;

        /*Allocating Major number*/
        if ((alloc_chrdev_region(&dev, mydriver_minor, my_nr_devs, "mydriver")) <0) {
                printk(KERN_INFO "Cannot allocate major number\n");
                return -1;
        }
        mydriver_minor = MINOR(dev);
        mydriver_major = MAJOR(dev);
        printk(KERN_INFO "Major = %d Minor = %d \n", mydriver_major, mydriver_minor);
        
        my_devs = kmalloc(my_nr_devs * sizeof(struct my_dev), GFP_KERNEL);
        if (!my_devs) {
                result = -ENOMEM;
                goto r_class;
        }
        memset(my_devs, 0, my_nr_devs * sizeof(struct my_dev));        /* Initialize each device. */
        for (i = 0; i < my_nr_devs; i++) {
                mutex_init(&my_devs[i].mutex);
                if (my_setup_cdev(&my_devs[i], i)<0)
                        goto r_class;
        }

        /*Creating struct class*/
        if ((dev_class = class_create(THIS_MODULE, "my_class")) == NULL) {
            printk(KERN_INFO "Cannot create the struct class\n");
            goto r_class;
        }

        /*Creating device*/
        if ((device_create(dev_class, NULL, dev, NULL, "my_device")) == NULL) {
            printk(KERN_INFO "Cannot create the Device 1\n");
            goto r_device;
        }

        /*Creating a directory in /sys/kernel/ */
        kobj_ref = kobject_create_and_add("my_sysfs", kernel_kobj);
        /*Creating sysfs files*/     
        if (sysfs_create_group(kobj_ref, &attr_group)) {
                printk(KERN_INFO"Cannot create sysfs files...\n");
                goto r_sysfs;
        }
        printk(KERN_INFO "my_driver: intialization success\n");

        return 0;

r_sysfs:
        kobject_put(kobj_ref); 
        sysfs_remove_group(kernel_kobj, &attr_group);

r_device:
        class_destroy(dev_class);
r_class:
        if (my_devs) {
                for (i = 0; i < my_nr_devs; i++) {
                        my_trim(my_devs + i);
                        cdev_del(&my_devs[i].cdev);
                }
                kfree(my_devs);
        }    /* cleanup_module is never called if registering failed. */
        unregister_chrdev_region(dev, my_nr_devs);
        return -1;
}

void __exit my_driver_exit(void)
{       
        int i;
        dev_t devno = MKDEV(mydriver_major, mydriver_minor);    /* Get rid of our char dev entries. */

        kobject_put(kobj_ref);
        sysfs_remove_group(kernel_kobj, &attr_group);
        device_destroy(dev_class, devno);
        class_destroy(dev_class);
        if (my_devs) {
                for (i = 0; i < my_nr_devs; i++) {
                        my_trim(my_devs + i);
                        cdev_del(&my_devs[i].cdev);
                }
                kfree(my_devs);
        }    /* cleanup_module is never called if registering failed. */
        unregister_chrdev_region(devno, my_nr_devs);
        printk(KERN_INFO "my_driver: cleanup success\n");
}

module_init(my_driver_init);
module_exit(my_driver_exit);