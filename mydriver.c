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
#include <linux/crypto.h>
#include <crypto/aes.h>
#include <crypto/skcipher.h>
#include <linux/scatterlist.h>
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Akshat");

#define MY_KEY_SIZE     16
#define MY_KEY          "abcdefghabcdefgh"

static int my_mode = 0;
static int my_bmode = 0;
static char *my_passkey;
static size_t my_bsize = 0;
static int mydriver_major = 0;
static int mydriver_minor = 0;
static int my_nr_devs = 2;
static struct class *dev_class;

static DEFINE_MUTEX(sys_mutex);
static struct kobject *kobj_ref;

struct my_aes_ctx {
        struct crypto_cipher *tfm;
        u8 key[MY_KEY_SIZE];
};

struct my_dev {
        u8 *data;
        unsigned long size;
        struct mutex mutex;
        struct cdev cdev;
        struct my_aes_ctx my_ctx;
        u8 *buf;
};

static struct my_dev *my_devs;

/*************** Driver Fuctions **********************/
static int my_open(struct inode *inode, struct file *file);
static int my_release(struct inode *inode, struct file *file);
static ssize_t my_read(struct file *filp, char __user *user_buffer,
                                        size_t size, loff_t *offset);
static ssize_t my_write(struct file *filp, const char *user_buffer,
                                        size_t size, loff_t *offset);
 
/*************** Sysfs Fuctions **********************/

static ssize_t sysfs_show(struct kobject *kobj, 
                struct kobj_attribute *attr, char *buf)
{       
        //printk(KERN_INFO "Sysfs - Read!\n");
        if (strcmp(attr->attr.name, "my_mode") == 0){
                if (my_bmode == 1) {
                        return sprintf(buf, "Normal-0 Uppercase-1 ROT13-2 AES-3 -> [%d]\n", my_mode);
                }
                else
                        return sprintf(buf, "Normal-0 Uppercase-1 ROT13-2 -> [%d]\n", my_mode);
        } else if (strcmp(attr->attr.name, "my_bmode") == 0){
                return sprintf(buf, "%d\n", my_bmode);
        } else if (strcmp(attr->attr.name, "my_bsize") == 0 && my_bmode == 1){
                return sprintf(buf, "%ld\n", my_bsize);
        } else if (strcmp(attr->attr.name, "my_passkey") == 0){
                return sprintf(buf, "%s\n", my_passkey);
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
        /*
         * Mode 0: Text in user input format 
         * Mode 1: Converting user input to uppercase
         * Mode 2: ROT13 encryption/decryption
         * Mode 3: AES encryption/decryption
         */
        int retval;
        mutex_lock(&sys_mutex);
        if (strcmp(attr->attr.name, "my_mode") == 0) {
                if (count > 2) {
                        goto outofbounds;
                }
                sscanf(buf, "%d\n", &my_mode);
                if (my_bmode == 1 && my_mode == 3) {
                        my_bsize = AES_BLOCK_SIZE;
                } else if (my_mode < 3 && my_mode >= 0) {
                } else {
                        my_mode = 0;
                        goto outofbounds;
                }
        } else if (strcmp(attr->attr.name, "my_bmode") == 0) {                
                if (count > 2) {
                        goto outofbounds;
                }

                sscanf(buf, "%d\n", &my_bmode);
                if (my_bmode != 0 && my_bmode != 1) {
                        pr_err("error: block mode reset\n");
                        my_bmode = 0;
                        goto outofbounds;
                }
                if (my_bmode == 0 && my_mode > 2)
                        my_mode = 0;
        } else if (strcmp(attr->attr.name, "my_bsize") == 0) {
                if (my_bmode == 1) {
                        if (count > 7) {
                                goto outofbounds;
                        }

                        if (my_mode != 3)
                                sscanf(buf, "%ld\n", &my_bsize);
                } else {
                        goto outofbounds;
                }
        } else if (strcmp(attr->attr.name, "my_passkey") == 0) {
                if(count != (MY_KEY_SIZE + 1))
                        goto outofbounds;        
                sscanf(buf, "%s", my_passkey);
        }
        retval = count;
        goto out;

outofbounds:
        retval = -EINVAL;
out:
        mutex_unlock(&sys_mutex);
        return retval;
}

static struct kobj_attribute my_attr_mode =  __ATTR(my_mode, 0660,
                                                        sysfs_show, sysfs_store);
static struct kobj_attribute my_attr_bmode = __ATTR(my_bmode, 0660,
                                                        sysfs_show, sysfs_store);
static struct kobj_attribute my_attr_bsize = __ATTR(my_bsize, 0660,
                                                        sysfs_show, sysfs_store);
static struct kobj_attribute my_attr_passkey = __ATTR(my_passkey, 0660,
                                                        sysfs_show, sysfs_store);

static struct attribute *attrs[] = {
        &my_attr_mode.attr,
        &my_attr_bmode.attr,
        &my_attr_bsize.attr,
        &my_attr_passkey.attr,
        NULL,
};

static struct attribute_group attr_group = {
    .attrs = attrs,
};

static int my_aes_init(struct my_dev *dev)
{       
        if (crypto_has_cipher("aes-generic", 0, 0))
                dev->my_ctx.tfm = crypto_alloc_cipher("aes-generic", 0, 0);
        else
                return -1;
        if (IS_ERR(dev->my_ctx.tfm)){
                printk(KERN_ERR 
                        "Failed to allocate tranformation aes-generic for : %ld\n",
                        PTR_ERR(dev->my_ctx.tfm));
                return PTR_ERR(dev->my_ctx.tfm);
        }

        //my_ctx->key = kmalloc(16 * sizeof(char *), GFP_KERNEL);
        memcpy(dev->my_ctx.key, MY_KEY, MY_KEY_SIZE);
        if (crypto_cipher_setkey(dev->my_ctx.tfm, dev->my_ctx.key, MY_KEY_SIZE))
                return -1;
        
        return 0;
}

static void my_aes_exit(struct my_dev *dev)
{
        crypto_free_cipher(dev->my_ctx.tfm);
        dev->my_ctx.tfm = NULL;
        kfree(dev->my_ctx.key);
}

static struct file_operations my_fops =
{
        .owner          = THIS_MODULE,
        .read           = my_read,
        .write          = my_write,
        .open           = my_open,
        .release        = my_release,
};

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
        int i, j, my_rdbmode, my_rdmode, retval;
        size_t numblocks, my_rdbsize;
        u8 *dec;
        struct my_dev *dev = (struct my_dev *) filp->private_data;
        ssize_t len = min_t(ssize_t, dev->size - *offset, size);
        char* rd_passkey = kzalloc(MY_KEY_SIZE * sizeof(char *), GFP_KERNEL);
        if(!rd_passkey){
                retval = -ENOMEM;
                goto memfault;
        }
        
        mutex_lock(&sys_mutex);
        my_rdbsize = my_bsize;
        my_rdbmode = my_bmode;
        my_rdmode = my_mode;
        memcpy(rd_passkey, my_passkey, MY_KEY_SIZE);
        mutex_unlock(&sys_mutex);

        if (mutex_lock_interruptible(&dev->mutex))
                return -ERESTARTSYS;
        
        if (len <= 0) {
                retval = 0;
                goto out;
        }
        /* read data from my_data->buffer to user buffer */
        if (my_rdbmode == 1){
                if (my_rdmode == 3 && strcmp(rd_passkey, MY_KEY) == 0)
                        dec = kzalloc(my_rdbsize * sizeof(char *), GFP_KERNEL);
                dev->buf = kzalloc(my_rdbsize * sizeof(char *), GFP_KERNEL);
                
                if (my_rdbsize == 0) {
                        pr_err( "error: block size not set");
                        retval = -EINVAL;
                        goto fault;
                }

                if (len % my_rdbsize) {
                        numblocks = (len / my_rdbsize) + 1;
                } else {    
                        numblocks = len / my_rdbsize;
                }
                for (i = 0; i < numblocks ; i++){
                        memset(dev->buf, 0, my_rdbsize * sizeof(char *));
                        for (j = 0; j < my_rdbsize; j++) {
                                *(dev->buf + j) = *(dev->data + *offset + (i * my_rdbsize) + j);
                        }

                        if (my_rdmode == 3 && strcmp(rd_passkey, MY_KEY) == 0) {
                                crypto_cipher_decrypt_one(dev->my_ctx.tfm, dec, dev->buf);
                                for (j = 0; j < my_rdbsize; j++) {
                                        *(dev->buf + j) = *(dec + j);
                                }
                        }

                        if (copy_to_user(user_buffer + (i * my_rdbsize), dev->buf, my_rdbsize)) {
                                retval = -EFAULT;
                                goto fault;
                        }
                }
        } else {
                if (copy_to_user(user_buffer, dev->data + *offset, len)) {
                        retval = -EFAULT;
                        goto fault;
                }
        }
        retval = len;
        *offset += len;
        mutex_unlock(&dev->mutex);
fault:
        if (my_rdbmode == 1)
                kfree(dev->buf);
out:
        mutex_unlock(&dev->mutex);
memfault:
        return retval;
}

void my_blockwrite(struct file *filp, int mode, int bsize, u8 *enc, char *passkey)
{       
        int j;
        struct my_dev *dev = (struct my_dev *) filp->private_data;

        if (mode == 0) {
        } else if (mode == 1) {
                /*
                 * offset of difference between 'A' and 'a' is added
                 * data write with no conversion if character not lowercase alphabet
                 */
                for (j = 0; j < bsize; j++) {
                        if ((*(dev->buf + j) >= 'a') && 
                                                (*(dev->buf + j) <= 'z'))
                                *(dev->buf + j) += ('A' - 'a');
                }
        } else if (mode == 2) {
                for (j = 0; j < bsize; j++) {
                        /*
                         * in blockmode, ROT13 has no checks,
                         * user needs to enter correct data
                         */
                        if (strcmp(dev->buf + j, "n") > 0)
                                *(dev->buf + j) -= ('n' - 'a');
                        else
                                *(dev->buf + j) += ('n' - 'a');
                }
        } else if (mode == 3 && strcmp(passkey, MY_KEY) == 0) {
                crypto_cipher_encrypt_one(dev->my_ctx.tfm, enc, dev->buf);
                for (j = 0; j < bsize; j++) {
                        *(dev->buf + j) = *(enc + j);
                }
        }
}

int my_bytewrite(char *buf, int mode, size_t size)
{       
        int i, retval = 0;
        if (mode == 0) {
        } else if (mode == 1) {
                for (i = 0; i < (size - 1); i++)
                        if ((*(buf + i) >= 'a') && 
                                        (*(buf + i) <= 'z'))
                                *(buf + i) += ('A' - 'a');
        } else if (mode == 2) {
                /*
                 * in bytemode, ROT13 returns error in case of
                 * non-lowercase alphabet
                 */
                for (i = 0; i < (size - 1); i++) {
                        if ((*(buf + i) < 'a') || 
                                        (*(buf + i) > 'z')) {
                                retval = -EINVAL;
                                goto out;
                        }
                        if (strcmp(buf + i, "n") > 0)
                                *(buf + i) -= ('n' - 'a');
                        else
                                *(buf + i) += ('n' - 'a');
                }
        }
out:
        return retval;
}

static ssize_t my_write(struct file *filp,
                const char __user *user_buffer, size_t size, loff_t *offset)
{
        int i, j, arrsize, my_wrbmode, my_wrmode;
        size_t numblocks, my_wrbsize;
        u8 *enc;
        struct my_dev *dev = (struct my_dev *) filp->private_data;
        ssize_t retval = -ENOMEM;
        char* wr_passkey = kzalloc(MY_KEY_SIZE * sizeof(char *), GFP_KERNEL);
        memcpy(wr_passkey, my_passkey, MY_KEY_SIZE);

        size = min_t(size_t, PAGE_SIZE * 32, size);

        mutex_lock(&sys_mutex);
        my_wrbsize = my_bsize;
        my_wrbmode = my_bmode;
        my_wrmode = my_mode;
        mutex_unlock(&sys_mutex);

        if (mutex_lock_interruptible(&dev->mutex))
                return -ERESTARTSYS;

        
        if (my_wrbmode == 1) {
                arrsize = my_wrbsize;
                if (my_wrbsize == 0) {
                        pr_err( "error: block size not set");
                        retval = -EINVAL;
                        goto out;
                }
                if (my_wrmode == 3) {
                        enc = kzalloc(my_wrbsize * sizeof(char *), GFP_KERNEL);
                        if (!enc) {
                                retval = -ENOMEM;
                                goto out;
                        }
                }
        } else {
                arrsize = size;
        }
        dev->buf = kzalloc(arrsize * sizeof(char *), GFP_KERNEL);
        if (!dev->buf) {
                retval = -ENOMEM;
                goto r_enc;
        }

        if (size <= 0){
                mutex_unlock(&dev->mutex);
                retval = 0;
                goto r_buf;
        }
        if (!dev->data) {
                if (my_wrbmode == 1){
                        if (size % my_wrbsize) {
                                numblocks = ((size / my_wrbsize) + 1);
                                arrsize = numblocks * my_wrbsize;
                        } else {    
                                numblocks = size / my_wrbsize;
                                arrsize = size;
                        }
                }
                else {
                        arrsize = size;
                }
                dev->data = kmalloc(arrsize * sizeof(char *), GFP_KERNEL);
                if (!dev->data)
                        goto r_buf;
                memset(dev->data, 0, arrsize * sizeof(char *));
        }
        if (my_wrbmode == 1){
                for (i = 0; i < numblocks ; i++){
                        memset(dev->buf, 0, my_wrbsize * sizeof(char *));
                        if (copy_from_user(dev->buf, user_buffer + (i * my_wrbsize), my_wrbsize)){
                                retval = -EFAULT;
                                goto r_buf;
                        }
                        my_blockwrite(filp, my_wrmode, my_wrbsize, enc, wr_passkey);
                        for (j = 0; j < my_wrbsize; j++){
                                if (i == (numblocks - 1) && j == ((size % my_wrbsize) - 1) && my_wrmode < 3)
                                        *(dev->data + (i * my_wrbsize) + j) = '\n';
                                else
                                        *(dev->data + (i * my_wrbsize) + j)= *(dev->buf + j);
                                /*TODO use memcpy instead of byte copying*/
                        }
                }
        }
        else {
                if (copy_from_user(dev->buf, user_buffer, size)){
                        retval = -EFAULT;
                        goto r_buf;
                }
                retval = my_bytewrite(dev->buf, my_wrmode, size);
                if (retval) {
                        retval = -EINVAL;
                        goto r_buf;
                }
                for (i = 0; i < size; i++){
                        if (i == size - 1 && my_wrmode != 4){
                                *(dev->data + i) = '\n';
                        } else {
                                *(dev->data + i) = *(dev->buf + i);
                        }
                }
        }
        
        *offset += size;
        dev->size += size;
        retval = size;
r_buf:
        kfree(dev->buf);
r_enc:
        if (my_wrmode == 3) {
                kfree(enc);
        }
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
                if (my_setup_cdev(&my_devs[i], i) < 0)
                        goto r_class;
                my_aes_init(&my_devs[i]);
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
        my_passkey = kmalloc(100 * sizeof(char *), GFP_KERNEL);

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
        kfree(my_passkey);
        return -1;
}

void __exit my_driver_exit(void)
{       
        int i;
        dev_t devno = MKDEV(mydriver_major, mydriver_minor);    /* Get rid of our char dev entries. */

        kfree(my_passkey);
        kobject_put(kobj_ref);
        sysfs_remove_group(kernel_kobj, &attr_group);
        device_destroy(dev_class, devno);
        class_destroy(dev_class);
        if (my_devs) {
                for (i = 0; i < my_nr_devs; i++) {
                        my_trim(my_devs + i);
                        cdev_del(&my_devs[i].cdev);
                        my_aes_exit(&my_devs[i]);                }
                kfree(my_devs);
        }    /* cleanup_module is never called if registering failed. */
        unregister_chrdev_region(devno, my_nr_devs);
        printk(KERN_INFO "my_driver: cleanup success\n");
}

module_init(my_driver_init);
module_exit(my_driver_exit);