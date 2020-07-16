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


char *mysys_value;
volatile int mysys_mode = 0;
int mysys_driver_major = 0;
int mysys_driver_minor = 0; 
 

dev_t dev = 0;
static struct class *dev_class;
static struct cdev mysys_cdev;
struct kobject *kobj_ref;
 
static int __init mysys_driver_init(void);
static void __exit mysys_driver_exit(void);
 
/*************** Driver Fuctions **********************/
static int mysys_open(struct inode *inode, struct file *file);
static int mysys_release(struct inode *inode, struct file *file);
static ssize_t mysys_read(struct file *filp, 
                char __user *buf, size_t len,loff_t * off);
static ssize_t mysys_write(struct file *filp, 
                const char *buf, size_t len, loff_t * off);
 
/*************** Sysfs Fuctions **********************/
static ssize_t sysfs_show(struct kobject *kobj, 
                struct kobj_attribute *attr, char *buf);
static ssize_t sysfs_store(struct kobject *kobj, 
                struct kobj_attribute *attr,const char *buf, size_t count);
 
static struct kobj_attribute mysys_attr_val = __ATTR(mysys_value, 0660, sysfs_show, sysfs_store);
static struct kobj_attribute mysys_attr_mode =  __ATTR(mysys_mode, 0660, sysfs_show, sysfs_store);

static struct attribute *attrs[] = {
    &mysys_attr_val.attr,
    &mysys_attr_mode.attr,
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
        //printk(KERN_INFO "Sysfs - Read!\n");
    if(strcmp(attr->attr.name, "mysys_value") == 0)
        return sprintf(buf, "%s\n", mysys_value);
    else if (strcmp(attr->attr.name, "mysys_mode") == 0)
        return sprintf(buf, "%d\n", mysys_mode);
    else
        return 0;
}
 
static ssize_t sysfs_store(struct kobject *kobj, 
                struct kobj_attribute *attr,const char *buf, size_t count)
{
        //printk(KERN_INFO "Sysfs - Write!\n");
    char to_upper;
    size_t  i;
    if(strcmp(attr->attr.name, "mysys_value") == 0){
        if(mysys_mode == 0){
            sscanf(buf,"%s\n",mysys_value);
            for(i=0;i<count;i++){
                if(i<(count-1)){   
                    to_upper = 'A' + *(mysys_value+i) - 'a';// convert to uppercase
                    *(mysys_value+i) = to_upper;
                }
            }
        }
    }
    else if(strcmp(attr->attr.name, "mysys_attr_mode") == 0){
            sscanf(buf,"%d\n",&mysys_mode);
    }
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
        if((alloc_chrdev_region(&dev, mysys_driver_minor, 1, "mysys_Dev")) <0){
                printk(KERN_INFO "Cannot allocate major number\n");
                return -1;
        }
        printk(KERN_INFO "Major = %d Minor = %d \n",MAJOR(dev), MINOR(dev));
 
        /*Creating cdev structure*/
        cdev_init(&mysys_cdev,&fops);
 
        /*Adding character device to the system*/
        if((cdev_add(&mysys_cdev,dev,1)) < 0){
            printk(KERN_INFO "Cannot add the device to the system\n");
            goto r_class;
        }
 
        /*Creating struct class*/
        if((dev_class = class_create(THIS_MODULE,"mysys_class")) == NULL){
            printk(KERN_INFO "Cannot create the struct class\n");
            goto r_class;
        }
 
        /*Creating device*/
        if((device_create(dev_class,NULL,dev,NULL,"mysys_device")) == NULL){
            printk(KERN_INFO "Cannot create the Device 1\n");
            goto r_device;
        }
 
        /*Creating a directory in /sys/kernel/ */
        kobj_ref = kobject_create_and_add("mysys_sysfs",kernel_kobj);
        mysys_value = kmalloc(1000 * sizeof(char *), GFP_KERNEL);
        /*Creating sysfs file for mysys_value*/     
        if(sysfs_create_group(kobj_ref,&attr_group)){
                printk(KERN_INFO"Cannot create sysfs files...\n");
                goto r_sysfs;
        }
        printk(KERN_INFO "Device Driver Insert...Done!!!\n");
    
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