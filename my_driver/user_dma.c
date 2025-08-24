#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/completion.h>
#include <linux/kernel.h>

#define DEVICE_NAME "user_dma"
static dev_t dev_num;
static struct cdev user_dma_cdev;
static struct class *user_dma_class;

static struct dma_chan *dma_chan;

struct dma_transfer {
    dma_addr_t src;
    dma_addr_t dst;
    size_t len;
};

// 完成中断回调
static void dma_complete_func(void *completion)
{
    complete(completion);
}

static long user_dma_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct dma_transfer xfer;
    struct dma_async_tx_descriptor *tx;
    dma_cookie_t cookie;
    struct completion cmp;

    if (copy_from_user(&xfer, (void __user *)arg, sizeof(xfer)))
        return -EFAULT;

    init_completion(&cmp);

    tx = dmaengine_prep_dma_memcpy(dma_chan, xfer.dst, xfer.src, xfer.len, DMA_PREP_INTERRUPT);
    if (!tx)
        return -EIO;

    tx->callback = dma_complete_func;
    tx->callback_param = &cmp;

    cookie = dmaengine_submit(tx);
    dma_async_issue_pending(dma_chan);

    // 等待完成
    wait_for_completion(&cmp);
    return 0;
}

static int user_dma_open(struct inode *inode, struct file *file)
{
    pr_info("user_dma opened\n");
    return 0;
}

static int user_dma_release(struct inode *inode, struct file *file)
{
    pr_info("user_dma released\n");
    return 0;
}

static const struct file_operations user_dma_fops = {
    .owner          = THIS_MODULE,
    .open           = user_dma_open,
    .release        = user_dma_release,
    .unlocked_ioctl = user_dma_ioctl,
};

static int __init user_dma_init(void)
{
    int ret;

    // 申请DMA通道
    dma_chan = dma_request_chan(NULL, "memcpy");
    if (IS_ERR(dma_chan)) {
        pr_err("Failed to request DMA channel\n");
        return PTR_ERR(dma_chan);
    }

    // 注册字符设备
    ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        pr_err("Failed to alloc chrdev\n");
        dma_release_channel(dma_chan);
        return ret;
    }

    cdev_init(&user_dma_cdev, &user_dma_fops);
    cdev_add(&user_dma_cdev, dev_num, 1);

    user_dma_class = class_create(DEVICE_NAME);
    device_create(user_dma_class, NULL, dev_num, NULL, DEVICE_NAME);

    pr_info("user_dma module loaded\n");
    return 0;
}

static void __exit user_dma_exit(void)
{
    device_destroy(user_dma_class, dev_num);
    class_destroy(user_dma_class);
    cdev_del(&user_dma_cdev);
    unregister_chrdev_region(dev_num, 1);

    dma_release_channel(dma_chan);
    pr_info("user_dma module unloaded\n");
}

module_init(user_dma_init);
module_exit(user_dma_exit);

MODULE_LICENSE("GPL");
