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
#include <linux/of.h>
#include <linux/ioctl.h>

#define DEVICE_NAME  "user_dma"
#define BUF_LEN      4096

#define USER_DMA_IOCTL_IB_TEST _IOR('M', 1, int)  

//char device
static dev_t dev_num;
static struct cdev user_dma_cdev;
static struct class *user_dma_class;
//dma channel
static struct dma_chan *dma_chan;
dma_cap_mask_t mask;
//transfer info
struct transfer_config {
    //bus addr that dmac can use
    dma_addr_t dma_buf_src;
    dma_addr_t dma_buf_des;
    //virt addr that driver can use
    void      *buf_src;
    void      *buf_des;
};
static struct transfer_config trans_config;
//
struct completion cmp;
// 完成中断回调
static void dma_complete_func(void *completion)
{
    complete(completion);
    pr_info("\ndma complete callback is called\n");
    return;
}

static void fill_source_buffer(void)
{
    int num = 0;
    unsigned char *src_buf = trans_config.buf_src;
    for(int i=0;i<BUF_LEN;i++)
    {
        src_buf[i] = (unsigned char)(0xff & num);
    }
    return;
}
//return true if des and src buffer are the same
static bool check_dest_buffer(void)
{
    unsigned char *src_buf = trans_config.buf_src;
    unsigned char *des_buf = trans_config.buf_des;
    for(int i = 0 ;i<BUF_LEN;i++)
    {
        if(src_buf[i]!=des_buf[i])
        {
            return false;
        }
    }
    return true;
}
static long user_dma_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{

    // struct dma_transfer xfer;
    struct dma_async_tx_descriptor *tx;
    dma_cookie_t cookie;
    int output;
    
    switch (cmd)
    {
        case USER_DMA_IOCTL_IB_TEST:            
            fill_source_buffer();//fill pattern in source buffer
            init_completion(&cmp);
            //prepare
            tx = dmaengine_prep_dma_memcpy(dma_chan, 
                trans_config.dma_buf_src , 
                trans_config.dma_buf_des ,
                BUF_LEN, 
                DMA_CTRL_ACK | DMA_PREP_INTERRUPT
            );
            tx->callback       = dma_complete_func;
            tx->callback_param = &cmp;
            pr_info("\ndma prepare comp\n");
            //submit
            cookie = dmaengine_submit(tx);
            pr_info("\ndma submit comp\n");
            //issue pending 
            dma_async_issue_pending(dma_chan);
            pr_info("\ndma issue pending comp\n");
            //wait for dma to comp
            pr_info("\nwait for dma to comp\n");
            wait_for_completion(&cmp);
            pr_info("\nwait finish\n");
            //check the pattern in dest buffer
            output = (check_dest_buffer())?(0):(1);
            if (copy_to_user((int __user *)arg, &output, sizeof(output)))
                return -EFAULT;
            break;
        default:
            return -ENOTTY;
        return 0;
    }

    

    // tx = dmaengine_prep_dma_memcpy(dma_chan, xfer.dst, xfer.src, xfer.len, DMA_PREP_INTERRUPT);
    // if (!tx)
    //     return -EIO;

    // tx->callback = dma_complete_func;
    // tx->callback_param = &cmp;

    // cookie = dmaengine_submit(tx);
    // dma_async_issue_pending(dma_chan);

    // // 等待完成
    // wait_for_completion(&cmp);
    return 0;
}

static int user_dma_open(struct inode *inode, struct file *file)
{
    pr_info("\nuser_dma opened\n");
    //print the channel info here
    if (!dma_chan) 
    {
        pr_err("\nDMA channel not initialized\n");
        return -ENODEV;
    }
    pr_info("\nDMA channel info: name=%s id=%d\n",
        dma_chan_name(dma_chan),
        dma_chan->chan_id);
    
    //use dma_alloc_coherent to allocate memory that dmac can reach
        //better use the dev of the dmac to get buffer
        //when using the dev of this driver,error will occur
    trans_config.buf_src = dma_alloc_coherent(dma_chan->device->dev, BUF_LEN, &(trans_config.dma_buf_src), GFP_KERNEL);
    trans_config.buf_des = dma_alloc_coherent(dma_chan->device->dev, BUF_LEN, &(trans_config.dma_buf_des), GFP_KERNEL);
    if(!trans_config.buf_src && !trans_config.buf_des)
    {
        pr_err("\nDMAC reachable buffer not initialized\n");
        return -ENODEV;
    }
    pr_info("\nallocate DMAC reachable buffer success\n");
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
//filter dmac by device name,target for dw-axi-dmac
static bool my_dma_filter(struct dma_chan *chan, void *param)
{
    if (!chan || !chan->device)
        return false;

    struct device *dev = chan->device->dev;
    const char *target_name = param;

    if (!dev || !dev->of_node)
        return false;

    const char *node_name = of_node_full_name(dev->of_node);
    if (!node_name) {
        return false;
    }        

    return strcmp(node_name, target_name) == 0;
}
static int __init user_dma_init(void)
{
    int ret;

    dma_cap_zero(mask);
    dma_cap_set(DMA_MEMCPY, mask);
    
    pr_info("start to request for dma channel\n");
    // request for dma channels
    dma_chan = dma_request_channel(mask, my_dma_filter, "dma-controller@16050000");
    pr_info("end to request for dma channel\n");

    if (IS_ERR(dma_chan)) {
        pr_err("Failed to request DMA channel\n");
        return PTR_ERR(dma_chan);
    } else {
        if(dma_chan && dma_chan->device && dma_chan->device->dev) {
            pr_info("Got channel: %s\n", dma_chan_name(dma_chan));
        } else {
            pr_info("Got channel, but device not ready yet\n");
        }      
    }
    // register char device
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
    //release dma buffer
    dma_free_coherent(dma_chan->device->dev, BUF_LEN, trans_config.buf_src, trans_config.dma_buf_src);
    dma_free_coherent(dma_chan->device->dev, BUF_LEN, trans_config.buf_des, trans_config.dma_buf_des);

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
