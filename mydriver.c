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
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Akshat");

#define MY_KEY_SIZE     16

static int my_mode = 0;
static int my_bmode = 0;
static size_t my_bsize = 0;
static int mydriver_major = 0;
static int mydriver_minor = 0;
static int my_nr_devs = 2;
static u8 *tmp;


static DEFINE_MUTEX(sys_mutex);

static struct class *dev_class;
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
};

// struct my_aes_ctx {
//         struct crypto_cipher *fallback;
//         struct aes_key enc_key;
//         struct aes_key dec_key;
// };

static struct my_dev *my_devs;

static int __init my_driver_init(void);
static void __exit my_driver_exit(void);

static int my_aes_init(struct my_dev *dev);
// static int my_aes_init(struct crypto_tfm *tfm);
// static void my_aes_exit(struct crypto_tfm *tfm);
// static int my_aes_setkey(struct crypto_tfm *tfm, const u8 *key,
//                                 unsigned int keylen);
// static void my_aes_encrypt(struct crypto_tfm *tfm, u8 *dst, const u8 *src);
// static void my_aes_decrypt(struct crypto_tfm *tfm, u8 *dst, const u8 *src);

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
        memcpy(dev->my_ctx.key, "abcdefghabcdefgh", MY_KEY_SIZE);
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

// static int my_aes_init(struct crypto_tfm *tfm)
// {
//         const char *alg = crypto_tfm_alg_name(tfm);
//         struct crypto_cipher *fallback;
//         struct my_aes_ctx = crypto_tfm_ctx(tfm);

//         fallback = crypto_alloc_cipher(alg, 0, CRYPTO_ALG_NEED_FALLBACK);
//         if (IS_ERR(fallback)) {
//                 printk(KERN_ERR 
//                         "Failed to allocate tranformation for '%s': %ld\n",
//                         alg, PTR_ERR(fallback));
//                 return PTR_ERR(fallback);
//         }
        
//         crypto_cipher_set_flags(fallback, 
//                                 crypto_cipher_get_flags((struct crypto_cipher *)
//                                                                 tfm));
        
//         ctx->fallback = fallback;
        
//         return 0;
// }

// static void my_aes_exit(struct crypto_tfm *tfm)
// {
//         struct my_aes_ctx *ctx = crypto_tfm_ctx(tfm);

//         if (ctx->fallback) {
//                 crypto_free_cipher(ctx->fallback);
//                 ctx->fallback = NULL;
//         }
// }

// static int my_aes_setkey(struct crypto_tfm *tfm, const u8 *key,
//                                 unsigned int keylen)
// {
//         int ret;
//         struct my_aes_ctx *ctx = crypto_tfm_ctx(tfm);

//         preempt_disable();
//         pagefault_disable();
//         enable_kernel_vsx();
//         ret = aes_my_set_encrypt_key(key, keylen * 8, &ctx->enc_key);
//         ret |= aes_my_set_decrypt_key(key, keylen * 8, &ctx->dec_key);
//         disable_kernel_vsx();
//         pagefault_enable();
//         preempt_enable();

//         ret |= crypto_cipher_setkey(ctx->fallback, key, keylen);

//         return ret ? -EINVAL : 0;
// }

// static void my_aes_encrypt(struct crypto_tfm *tfm, u8 *dst, const u8 *src)
// {
//         struct my_aes_ctx *ctx = crypto_tfm_ctx(tfm);

//         if (!crypto_simd_usable()) {
//                 crypto_cipher_encrypt_one(ctx->fallback, dst, src);
//         } else {
//                 preempt_disable();
//                 pagefault_disable();
//                 enable_kernel_vsx();
//                 aes_my_encrypt(src, dst, &ctx->enc_key);
//                 disable_kernel_vsx();
//                 pagefault_enable();
//                 preempt_enable();
//         }
// }

// static void my_aes_decrypt(struct crypto_tfm *tfm, u8 *dst, const u8 *src)
// {
//         struct my_aes_ctx *ctx = crypto_tfm_ctx(tfm);

//         if(!crypto_simd_usable()) {
//                 crypto_cipher_decrypt_one(ctx->fallback, dst, src);
//         } else {
//                 preempt_disable();
//                 pagefault_disable();
//                 enable_kernel_vsx();
//                 aes_my_decrypt(src, dst, &ctx->dec_key);
//                 disable_kernel_vsx();
//                 pagefault_enable();
//                 preempt_enable();
//         }
// }

// struct crypto_alg my_aes_alg = {
//         .cra_name = "aes",
//         .cra_driver_name = "mydriver",
//         .cra_module = THIS_MODULE,
//         .cra_priority = 1000,
//         .cra_type = NULL,
//         .cra_flags = CRYPTO_ALG_TYPE_CIPHER | CRYPTO_ALG_NEED_FALLBACK,
//         .cra_alignmask = 0,
//         .cra_blocksize = AES_BLOCK_SIZE,
//         .cra_ctxsize = sizeof(struct p8_aes_ctx),
//         .cra_init = p8_aes_init,
//         .cra_exit = p8_aes_exit,
//         .cra_cipher = {
//                        .cia_min_keysize = AES_MIN_KEY_SIZE,
//                        .cia_max_keysize = AES_MAX_KEY_SIZE,
//                        .cia_setkey = p8_aes_setkey,
//                        .cia_encrypt = p8_aes_encrypt,
//                        .cia_decrypt = p8_aes_decrypt,
//         },
// };

static ssize_t sysfs_show(struct kobject *kobj, 
                struct kobj_attribute *attr, char *buf)
{       
        //printk(KERN_INFO "Sysfs - Read!\n");
        if (strcmp(attr->attr.name, "my_mode") == 0){
                return sprintf(buf, "%d\n", my_mode);
        } else if (strcmp(attr->attr.name, "my_bmode") == 0){
                return sprintf(buf, "%d\n", my_bmode);
        } else if (strcmp(attr->attr.name, "my_bsize") == 0 && my_bmode == 1){
                return sprintf(buf, "%ld\n", my_bsize);
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
                // if (my_mode >= 3){
                //         my_bsize = crypto_cipher_blocksize(my_ctx->tfm);
                //         my_bmode = 1;
                // }
                sscanf(buf, "%d\n", &my_mode);
        } else if (strcmp(attr->attr.name, "my_bmode") == 0) {
                // if (my_mode >= 3)
                //         ;
                // else
                sscanf(buf, "%d\n", &my_bmode);
        } else if (strcmp(attr->attr.name, "my_bsize") == 0 && my_bmode == 1) {
                // if (my_mode >= 3)
                //         ;
                // else
                sscanf(buf, "%ld\n", &my_bsize);
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
        int i, j, my_rdbmode, my_rdmode;
        size_t numblocks, my_rdbsize;
        u8 *dec;
        struct my_dev *dev = (struct my_dev *) filp->private_data;
        ssize_t len = min_t(ssize_t, dev->size - *offset, size);
        
        if (mutex_lock_interruptible(&dev->mutex))
                return -ERESTARTSYS;
        my_rdbsize = my_bsize;
        my_rdbmode = my_bmode;
        my_rdmode = my_mode;
        if (my_rdmode == 4) {
                my_rdbsize = crypto_cipher_blocksize(dev->my_ctx.tfm);
                dec = kmalloc(my_rdbsize * sizeof(char *), GFP_KERNEL);
                memset(dec, 0, my_rdbsize * sizeof(char *));
        }
        if (my_rdbmode == 1){
                tmp = kmalloc(my_rdbsize * sizeof(char *), GFP_KERNEL);
                memset(tmp, 0, my_rdbsize * sizeof(char *));
        }
        if (len <= 0) {
                mutex_unlock(&dev->mutex);
                return 0;
        }
        /* read data from my_data->buffer to user buffer */
        if (my_rdbmode == 1){
                if (len % my_rdbsize) {
                        numblocks = (len / my_rdbsize) + 1;
                } else {    
                        numblocks = len / my_rdbsize;
                }
                for (i = 0; i < numblocks ; i++){
                        memset(tmp, 0, my_rdbsize * sizeof(char *));
                        for (j = 0; j < my_rdbsize; j++) {
                                if (i == (numblocks - 1) && j == ((len % my_rdbsize) - 1) && my_rdmode < 3 )
                                        break;
                                *(tmp + j) = *(dev->data + *offset + (i * my_rdbsize) + j);
                                printk(KERN_INFO "data in device %x\n", *(dev->data + *offset + (i * my_rdbsize) + j));
                        }
                        if (my_rdmode == 4) {
                                crypto_cipher_decrypt_one(dev->my_ctx.tfm, dec, tmp);
                                for (j = 0; j < my_rdbsize; j++) {
                                        printk(KERN_INFO "data from device %x\n", *(tmp + j));
                                        *(tmp + j) = *(dec + j);
                                        printk(KERN_INFO "decoded data %x\n", *(dec + j));
                                }
                        }
                        if (i == (numblocks - 1) && my_rdmode < 3) {
                                if (copy_to_user(user_buffer + (i * my_rdbsize), tmp, (len % my_rdbsize) - 1))
                                        return -EFAULT;                        
                        } else {
                                if (copy_to_user(user_buffer + (i * my_rdbsize), tmp, my_rdbsize))
                                        return -EFAULT;
                        }
                }
        } else {
                if (copy_to_user(user_buffer, dev->data + *offset, len))
                        return -EFAULT;
        }
        if (my_rdbmode == 1)
                kfree(tmp);
        *offset += len;
        mutex_unlock(&dev->mutex);
        return len;
}
static ssize_t my_write(struct file *filp,
                const char __user *user_buffer, size_t size, loff_t *offset)
{
        int i, j, arrsize, my_wrbmode, my_wrmode;
        size_t numblocks, my_wrbsize;
        u8 *enc;
        struct my_dev *dev = (struct my_dev *) filp->private_data;
        //ssize_t len = min_t(ssize_t, 4096 - (dev->size - *offset), size);
        ssize_t retval = -ENOMEM;

        if (mutex_lock_interruptible(&dev->mutex))
                return -ERESTARTSYS;
        my_wrbsize = my_bsize;
        my_wrbmode = my_bmode;
        my_wrmode = my_mode;
        if (my_wrmode == 3) {
                my_wrbsize = crypto_cipher_blocksize(dev->my_ctx.tfm);
                enc = kmalloc(my_wrbsize * sizeof(char *), GFP_KERNEL);
                memset(enc, 0, my_wrbsize * sizeof(char *));
        }
        if (my_wrbmode == 1) {
                tmp = kmalloc(my_wrbsize * sizeof(char *), GFP_KERNEL);
                memset(tmp, 0, my_wrbsize * sizeof(char *));

        } else {
                tmp = kmalloc(size * sizeof(char *), GFP_KERNEL);
                memset(tmp, 0, my_wrbsize * sizeof(char *));
        }
        if (size <= 0){
                mutex_unlock(&dev->mutex);
                return 0;
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
                        goto out;
                memset(dev->data, 0, arrsize * sizeof(char *));
        }
        if (my_wrbmode == 1){
                for (i = 0; i < numblocks ; i++){
                        memset(tmp, 0, my_wrbsize * sizeof(char *));
                        if (copy_from_user(tmp, user_buffer + (i * my_wrbsize), my_wrbsize)){
                                retval = -EFAULT;
                                return retval;
                        }
                        if (my_wrmode == 0) {
                        } else if (my_wrmode == 1) {
                                for (j = 0; j < my_wrbsize; j++)
                                        *(tmp + j) += ('A' - 'a');
                        } else if (my_wrmode == 2) {
                                for (j = 0; j < my_wrbsize; j++) {
                                        if (strcmp(tmp + j, "m") > 0)
                                                *(tmp + j) -= ('n' - 'a');
                                        else
                                                *(tmp + j) += ('n' - 'a');
                                }
                        } else if (my_wrmode == 3) {
                                
                                crypto_cipher_encrypt_one(dev->my_ctx.tfm, enc, tmp);
                                for (j = 0; j < my_wrbsize; j++) {
                                        printk(KERN_INFO "data from user buffer %x\n", *(tmp + j));
                                        *(tmp + j) = *(enc + j);
                                        printk(KERN_INFO "encoded data %x\n", *(enc + j));
                                }
                        } /*else if (my_wrmode == 4) {
                                crypto_cipher_decrypt_one(dev->my_ctx.tfm, enc, tmp);
                                for (j = 0; j < my_wrbsize; j++)
                                        *(tmp + j) = *(enc + j);
                        }*/
                        for (j = 0; j < my_wrbsize; j++){
                                if (i == (numblocks - 1) && j == ((size % my_wrbsize) - 1) && my_wrmode != 3)
                                        break;
                                *(dev->data + (i * my_wrbsize) + j)= *(tmp + j);        /*TODO use memcpy instead of byte copying*/
                                printk(KERN_INFO "data written into device %x\n", *(dev->data + (i * my_wrbsize) + j));
                        }
                }
        }
        else {
                if (copy_from_user(tmp, user_buffer, size)){
                        retval = -EFAULT;
                        return retval;
                }
                if (my_wrmode == 0) {
                } else if (my_wrmode == 1) {
                        for (i = 0; i < (size - 1); i++)
                                *(tmp + i) += ('A' - 'a');
                } else if (my_wrmode == 2) {
                        for (i = 0; i < (size - 1); i++) {
                                if (strcmp(tmp + i, "m") > 0)
                                        *(tmp + i) -= ('n' - 'a');
                                else
                                        *(tmp + i) += ('n' - 'a');
                        }
                }
                for (i = 0; i < (size - 1); i++){
                        *(dev->data + i) = *(tmp + i);
                }
        }
        if (my_wrmode >= 3) {
                kfree(enc);
        }
        kfree(tmp);
        *offset += size;
        dev->size += size;
        retval = size;
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

        kfree(tmp);
        kobject_put(kobj_ref);
        sysfs_remove_group(kernel_kobj, &attr_group);
        device_destroy(dev_class, devno);
        class_destroy(dev_class);
        if (my_devs) {
                for (i = 0; i < my_nr_devs; i++) {
                        my_trim(my_devs + i);
                        cdev_del(&my_devs[i].cdev);
                        my_aes_exit(&my_devs[i]);
                }
                kfree(my_devs);
        }    /* cleanup_module is never called if registering failed. */
        unregister_chrdev_region(devno, my_nr_devs);
        printk(KERN_INFO "my_driver: cleanup success\n");
}

module_init(my_driver_init);
module_exit(my_driver_exit);