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


static char *mysys_value;
static int mysys_mode = 0;
static int mysys_bmode = 0;
static int mysys_bsize = 0;
static int mysys_driver_major = 0;
static int mysys_driver_minor = 0;
static DEFINE_MUTEX(sys_mutex);

static dev_t dev = 0;
static struct class *dev_class;
static struct cdev mysys_cdev;
static struct kobject *kobj_ref;

static int __init mysys_driver_init(void);
static void __exit mysys_driver_exit(void);

/*************** Driver Fuctions **********************/
static int mysys_open(struct inode *inode, struct file *file);
static int mysys_release(struct inode *inode, struct file *file);
static ssize_t mysys_read(struct file *filp, char __user *buf,
                                        size_t len, loff_t *off);
static ssize_t mysys_write(struct file *filp, const char *buf,
                                        size_t len, loff_t *off);
 
/*************** Sysfs Fuctions **********************/
static ssize_t sysfs_show(struct kobject *kobj,
                struct kobj_attribute *attr, char *buf);
static ssize_t sysfs_store(struct kobject *kobj, 
                struct kobj_attribute *attr,const char *buf, size_t count);
static struct kobj_attribute mysys_attr_val = __ATTR(mysys_value, 0660,
                                                        sysfs_show, sysfs_store);
static struct kobj_attribute mysys_attr_mode =  __ATTR(mysys_mode, 0660,
                                                        sysfs_show, sysfs_store);
static struct kobj_attribute mysys_attr_bmode = __ATTR(mysys_bmode, 0660,
                                                        sysfs_show, sysfs_store);
static struct kobj_attribute mysys_attr_bsize = __ATTR(mysys_bsize, 0660,
                                                        sysfs_show, sysfs_store);

static struct attribute *attrs[] = {
        &mysys_attr_val.attr,
        &mysys_attr_mode.attr,
        &mysys_attr_bmode.attr,
        &mysys_attr_bsize.attr,
        NULL,
};

static struct attribute_group attr_group = {
    .attrs = attrs,
};

static struct file_operations fops =
{
        .owner          = THIS_MODULE,
        .read           = mysys_read,
        .write          = mysys_write,
        .open           = mysys_open,
        .release        = mysys_release,
};

static ssize_t sysfs_show(struct kobject *kobj, 
                struct kobj_attribute *attr, char *buf)
{       
        size_t i;
        //printk(KERN_INFO "Sysfs - Read!\n");
        if (strcmp(attr->attr.name, "mysys_value") == 0){
                if (mysys_bmode == 0){
                        return sprintf(buf, "%s\n", mysys_value);
                } else if (mysys_bmode == 1){
                        for (i = 0; i < (sizeof(mysys_value)/sizeof(char *))/mysys_bsize; i++)
                                return scnprintf(buf, mysys_bsize+1, "%s\n", mysys_value);
                }
        } else if (strcmp(attr->attr.name, "mysys_mode") == 0){
                return sprintf(buf, "%d\n", mysys_mode);
        } else if (strcmp(attr->attr.name, "mysys_bmode") == 0){
                return sprintf(buf, "%d\n", mysys_bmode);
        } else if (strcmp(attr->attr.name, "mysys_bsize") == 0){
                return sprintf(buf, "%d\n", mysys_bsize);
        }
        return 0;
}

/*
 * In case the program is at evaluation the of condition for the 
 * else if case for mode '1' and another process changes mysys_mode 
 * from '2' to '0', the program will fall through to the dummy value. 
 * This is undesirable and hence calls for the use of a lock.
 */
static ssize_t sysfs_store(struct kobject *kobj,
                struct kobj_attribute *attr, const char *buf, size_t count)
{
        //printk(KERN_INFO "Sysfs - Write!\n");
        size_t  i;
        
        /*
         * Mode 0: Text in user input format 
         * Mode 1: Converting user input to uppercase
         * Mode 2: ROT13 encryption/decryption
         */
        mutex_lock(&sys_mutex);
        if (strcmp(attr->attr.name, "mysys_value") == 0) {
                if (mysys_mode == 0 && mysys_bmode == 1) {
                        kfree(mysys_value);
                        mysys_value = kmalloc(count * sizeof(char *), GFP_KERNEL);
                        for(i = 0; i <= count/mysys_bsize ; i++){
                                snprintf(mysys_value + i*mysys_bsize, mysys_bsize , "%s\n", buf + i*mysys_bsize);
                                if (i == (count/mysys_bsize - 1) && count%mysys_bsize == 0)
                                        break;
                        }
                } else if (mysys_mode == 0 && mysys_bmode == 0) {
                                sscanf(buf, "%s\n", mysys_value);
                } else if (mysys_mode == 1) {
                        sscanf(buf, "%s\n", mysys_value);
                        for (i = 0; i < (count-1); i++)
                                *(mysys_value+i) += ('A' - 'a');
                } else if (mysys_mode == 2) {
                        sscanf(buf, "%s\n", mysys_value);
                        for (i = 0; i < (count-1); i++) {
                                if (strcmp(mysys_value+i, "m") > 0)
                                        *(mysys_value+i) -= ('n' - 'a');
                                else
                                        *(mysys_value+i) += ('n' - 'a');
                        }
                } else {
                        sscanf("dummy", "%s\n", mysys_value);
                }
        } else if (strcmp(attr->attr.name, "mysys_mode") == 0) {
            sscanf(buf, "%d\n", &mysys_mode);
        } else if (strcmp(attr->attr.name, "mysys_bmode") == 0) {
            sscanf(buf, "%d\n", &mysys_bmode);
        } else if (strcmp(attr->attr.name, "mysys_bsize") == 0) {
            sscanf(buf, "%d\n", &mysys_bsize);
        }
        mutex_unlock(&sys_mutex);
        return count;
}

static int mysys_open(struct inode *inode, struct file *file)
{
        printk(KERN_INFO "mysys_driver: opened successfully\n");
        return 0;
}

static int mysys_release(struct inode *inode, struct file *file)
{
        //printk(KERN_INFO "Device File Closed...!!!\n");
        return 0;
}

static ssize_t mysys_read(struct file *filp, 
                char __user *buf, size_t len, loff_t *off)
{
        //printk(KERN_INFO "Read function\n");
        return 0;
}
static ssize_t mysys_write(struct file *filp,
                const char __user *buf, size_t len, loff_t *off)
{
        //printk(KERN_INFO "Write Function\n");
        return 0;
}

static int __init mysys_driver_init(void)
{
        /*Allocating Major number*/
        if ((alloc_chrdev_region(&dev, mysys_driver_minor, 1, "mysys_Dev")) <0) {
                printk(KERN_INFO "Cannot allocate major number\n");
                return -1;
        }
        mysys_driver_minor = MINOR(dev);
        mysys_driver_major = MAJOR(dev);
        printk(KERN_INFO "Major = %d Minor = %d \n", mysys_driver_major, mysys_driver_minor);
 
        /*Creating cdev structure*/
        cdev_init(&mysys_cdev, &fops);

        /*Adding character device to the system*/
        if ((cdev_add(&mysys_cdev, dev, 1)) < 0) {
            printk(KERN_INFO "Cannot add the device to the system\n");
            goto r_class;
        }

        /*Creating struct class*/
        if ((dev_class = class_create(THIS_MODULE, "mysys_class")) == NULL) {
            printk(KERN_INFO "Cannot create the struct class\n");
            goto r_class;
        }

        /*Creating device*/
        if ((device_create(dev_class, NULL, dev, NULL, "mysys_device")) == NULL) {
            printk(KERN_INFO "Cannot create the Device 1\n");
            goto r_device;
        }

        /*Creating a directory in /sys/kernel/ */
        kobj_ref = kobject_create_and_add("mysys_sysfs", kernel_kobj);
        mysys_value = kmalloc(1000 * sizeof(char *), GFP_KERNEL);
        /*Creating sysfs file for mysys_value*/     
        if (sysfs_create_group(kobj_ref, &attr_group)) {
                printk(KERN_INFO"Cannot create sysfs files...\n");
                goto r_sysfs;
        }
        printk(KERN_INFO "mysys_driver: intialization success\n");

        return 0;

r_sysfs:
        kobject_put(kobj_ref); 
        sysfs_remove_group(kernel_kobj, &attr_group);

r_device:
        class_destroy(dev_class);
r_class:
        unregister_chrdev_region(dev,1);
        cdev_del(&mysys_cdev);
        return -1;
}

void __exit mysys_driver_exit(void)
{
        kobject_put(kobj_ref);
        sysfs_remove_group(kernel_kobj, &attr_group);
        device_destroy(dev_class,dev);
        class_destroy(dev_class);
        cdev_del(&mysys_cdev);
        unregister_chrdev_region(dev, 1);
        printk(KERN_INFO "mysys_driver: cleanup success\n");
        kfree(mysys_value);
}

module_init(mysys_driver_init);
module_exit(mysys_driver_exit);