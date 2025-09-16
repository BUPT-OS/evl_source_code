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
#include <linux/of_address.h>
#include <linux/clk.h>
#include <linux/reset.h> 
#include <linux/err.h>   
#include <linux/ioctl.h>
#include <linux/jiffies.h> 
#include <linux/amba/pl022.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <sound/dmaengine_pcm.h>
#include <linux/pm_runtime.h>


extern unsigned long loops_per_jiffy;
//mocking PL022 spi driver

#define GEN_MASK_BITS(val, mask, sb) \
 (((val)<<(sb)) & (mask))

/*
 * Macros to access SSP Registers with their offsets
 */
#define SSP_CR0(r)	(r + 0x000)
#define SSP_CR1(r)	(r + 0x004)
#define SSP_DR(r)	(r + 0x008)
#define SSP_SR(r)	(r + 0x00C)
#define SSP_CPSR(r)	(r + 0x010)
#define SSP_IMSC(r)	(r + 0x014)
#define SSP_ICR(r)	(r + 0x020)
#define SSP_DMACR(r)	(r + 0x024)

//spi control:masks and flag

//cr0
#define SSP_CR0_MASK_DSS	(0x0FUL << 0)
#define SSP_CR0_MASK_FRF	(0x3UL << 4)
#define SSP_CR0_MASK_SPO	(0x1UL << 6)
#define SSP_CR0_MASK_SPH	(0x1UL << 7)
#define SSP_CR0_MASK_SCR	(0xFFUL << 8)

//cr1
#define SSP_CR1_MASK_LBM	(0x1UL << 0)
#define SSP_CR1_MASK_SSE	(0x1UL << 1)
#define SSP_CR1_MASK_MS		(0x1UL << 2)
#define SSP_CR1_MASK_SOD	(0x1UL << 3)

#define SSP_DISABLED			(0)
#define SSP_ENABLED			(1)
#define DRIVE_TX		     0
#define DO_NOT_DRIVE_TX	  	1

//dmacr
#define SSP_DMACR_MASK_RXDMAE		(0x1UL << 0)/* Receive DMA Enable bit */
#define SSP_DMACR_MASK_TXDMAE		(0x1UL << 1)/* Transmit DMA Enable bit */

#define SSP_DMA_ENABLED			(1)

//sr
#define SSP_SR_MASK_RNE		(0x1UL << 2) /* Receive FIFO not empty */
#define SSP_SR_MASK_BSY		(0x1UL << 4) /* Busy Flag */

//cpsr 
#define SSP_CPSR_MASK_CPSDVSR	(0xFFUL << 0)

#define SSP_DEFAULT_PRESCALE 0x2
#define SSP_DEFAULT_CLKRATE 0x3d

//imsc
#define DEFAULT_SSP_REG_IMSC  0x0UL

#define DEFAULT_SSP_REG_CR0 ( \
	GEN_MASK_BITS(SSP_DATA_BITS_8, SSP_CR0_MASK_DSS, 0)	| \
	GEN_MASK_BITS(SSP_INTERFACE_MOTOROLA_SPI, SSP_CR0_MASK_FRF, 4) | \
	GEN_MASK_BITS(SSP_CLK_POL_IDLE_LOW, SSP_CR0_MASK_SPO, 6) | \
	GEN_MASK_BITS(SSP_CLK_FIRST_EDGE, SSP_CR0_MASK_SPH, 7) | \
	GEN_MASK_BITS(SSP_DEFAULT_CLKRATE, SSP_CR0_MASK_SCR, 8) \
)

#define DEFAULT_SSP_REG_CR1 ( \
	GEN_MASK_BITS(LOOPBACK_DISABLED, SSP_CR1_MASK_LBM, 0) | \
	GEN_MASK_BITS(SSP_DISABLED, SSP_CR1_MASK_SSE, 1) | \
	GEN_MASK_BITS(SSP_MASTER, SSP_CR1_MASK_MS, 2) | \
	GEN_MASK_BITS(DO_NOT_DRIVE_TX, SSP_CR1_MASK_SOD, 3) \
)

#define DEFAULT_SSP_REG_DMACR (\
	GEN_MASK_BITS(SSP_DMA_ENABLED, SSP_DMACR_MASK_RXDMAE, 0) | \
	GEN_MASK_BITS(SSP_DMA_ENABLED, SSP_DMACR_MASK_TXDMAE, 1) \
)

#define DEFAULT_SSP_REG_CPSR ( \
	GEN_MASK_BITS(SSP_DEFAULT_PRESCALE, SSP_CPSR_MASK_CPSDVSR, 0) \
)

#define DISABLE_ALL_INTERRUPTS DEFAULT_SSP_REG_IMSC
#define CLEAR_ALL_INTERRUPTS  0x3

//pl022 functions 
static void flush_spi_fifo(void __iomem *spi_base_addr_virt)
{
	unsigned long limit = loops_per_jiffy << 1;

	do {
		while (readw(SSP_SR(spi_base_addr_virt)) & SSP_SR_MASK_RNE)
			readw(SSP_DR(spi_base_addr_virt));
	} while ((readw(SSP_SR(spi_base_addr_virt)) & SSP_SR_MASK_BSY) && limit--);

	return;
}
static void spi_enable(void __iomem *spi_base_addr_virt)
{
    //enable spi
    writew((readw(SSP_CR1(spi_base_addr_virt)) | SSP_CR1_MASK_SSE),
    SSP_CR1(spi_base_addr_virt));
}
static void spi_dma_config(void __iomem *spi_base_addr_virt)
{
    //1-the DMA burstsize should equal the FIFO trigger levels
    //2-the addr width should be the same
    //3-config fifo trigger level:the pl022 on vf2 will trigger interrupt when fifo is not empty
    
    writew(DEFAULT_SSP_REG_CR0, SSP_CR0(spi_base_addr_virt));
    writew(DEFAULT_SSP_REG_CR1, SSP_CR1(spi_base_addr_virt));
    writew(DEFAULT_SSP_REG_DMACR, SSP_DMACR(spi_base_addr_virt));//enable fifo for dma
    writew(DEFAULT_SSP_REG_CPSR, SSP_CPSR(spi_base_addr_virt));
    writew(DISABLE_ALL_INTERRUPTS, SSP_IMSC(spi_base_addr_virt));//Disable interrupts in DMA mode, IRQ from DMA controller
	writew(CLEAR_ALL_INTERRUPTS, SSP_ICR(spi_base_addr_virt));

    flush_spi_fifo(spi_base_addr_virt);
    pr_info("\nspi fifo flushed\n");
}

static void print_spi_regs(void __iomem *spi_base_addr_virt)
{
    u32 read_cr0;
	u16 read_cr1, read_dmacr, read_sr, read_cpsr ,read_imsc;

    read_cr0   = readw(SSP_CR0(spi_base_addr_virt));
	read_cr1   = readw(SSP_CR1(spi_base_addr_virt));
	read_dmacr = readw(SSP_DMACR(spi_base_addr_virt));
	read_sr    = readw(SSP_SR(spi_base_addr_virt));
	read_cpsr  = readw(SSP_CPSR(spi_base_addr_virt));
	read_imsc  = readw(SSP_IMSC(spi_base_addr_virt));

    pr_info("SPI CR0=0x%x\n",read_cr0);

    pr_info("SPI CR1=0x%x, SPI enable=%d\n",
        read_cr1,
        !!(read_cr1 & SSP_CR1_MASK_SSE));

    pr_info("SPI SR=0x%x\n",read_sr);

	pr_info("SPI CPSR=0x%x\n",read_cpsr);

	pr_info("SPI IMSC=0x%x\n",read_imsc);

    pr_info("SPI DMACR=0x%x, SPI DMA rx dmaenable=%d, SPI DMA tx dmaenable=%d\n",
        read_dmacr,
        !!(read_dmacr & SSP_DMACR_MASK_RXDMAE),
        !!(read_dmacr & SSP_DMACR_MASK_TXDMAE));
    return;
}

//end of PL022 content


//start of pwmdac

#define JH7110_PWMDAC_WDATA				0x00
#define JH7110_PWMDAC_CTRL				0x04
#define JH7110_PWMDAC_ENABLE			BIT(0)
#define JH7110_PWMDAC_SHIFT			BIT(1)
#define JH7110_PWMDAC_DUTY_CYCLE_SHIFT		2
#define JH7110_PWMDAC_DUTY_CYCLE_MASK		GENMASK(3, 2)
#define JH7110_PWMDAC_CNT_N_SHIFT		4
#define JH7110_PWMDAC_CNT_N_MASK		GENMASK(12, 4)
#define JH7110_PWMDAC_DATA_CHANGE		BIT(13)
#define JH7110_PWMDAC_DATA_MODE			BIT(14)
#define JH7110_PWMDAC_DATA_SHIFT_SHIFT		15
#define JH7110_PWMDAC_DATA_SHIFT_MASK		GENMASK(17, 15)

#define PWMDAC_CLKRATE   12288000

enum JH7110_PWMDAC_SHIFT_VAL {
	PWMDAC_SHIFT_8 = 0,
	PWMDAC_SHIFT_10,
};

enum JH7110_PWMDAC_DUTY_CYCLE_VAL {
	PWMDAC_CYCLE_LEFT = 0,
	PWMDAC_CYCLE_RIGHT,
	PWMDAC_CYCLE_CENTER,
};

enum JH7110_PWMDAC_CNT_N_VAL {
	PWMDAC_SAMPLE_CNT_1 = 1,
	PWMDAC_SAMPLE_CNT_2,
	PWMDAC_SAMPLE_CNT_3,
	PWMDAC_SAMPLE_CNT_512 = 512, /* max */
};

enum JH7110_PWMDAC_DATA_CHANGE_VAL {
	NO_CHANGE = 0,
	CHANGE,
};

enum JH7110_PWMDAC_DATA_MODE_VAL {
	UNSIGNED_DATA = 0,
	INVERTER_DATA_MSB,
};

enum JH7110_PWMDAC_DATA_SHIFT_VAL {
	PWMDAC_DATA_LEFT_SHIFT_BIT_0 = 0,
	PWMDAC_DATA_LEFT_SHIFT_BIT_1,
	PWMDAC_DATA_LEFT_SHIFT_BIT_2,
	PWMDAC_DATA_LEFT_SHIFT_BIT_3,
	PWMDAC_DATA_LEFT_SHIFT_BIT_4,
	PWMDAC_DATA_LEFT_SHIFT_BIT_5,
	PWMDAC_DATA_LEFT_SHIFT_BIT_6,
	PWMDAC_DATA_LEFT_SHIFT_BIT_7,
};

struct jh7110_pwmdac_cfg {
	enum JH7110_PWMDAC_SHIFT_VAL shift;
	enum JH7110_PWMDAC_DUTY_CYCLE_VAL duty_cycle;
	u16 cnt_n;
	enum JH7110_PWMDAC_DATA_CHANGE_VAL data_change;
	enum JH7110_PWMDAC_DATA_MODE_VAL data_mode;
	enum JH7110_PWMDAC_DATA_SHIFT_VAL data_shift;
};


struct jh7110_pwmdac_dev {
	void __iomem *base;
	resource_size_t	mapbase;
	struct jh7110_pwmdac_cfg cfg;

	struct clk_bulk_data clks[2];
	struct reset_control *rst_apb;
	struct device *dev;
	struct snd_dmaengine_dai_dma_data play_dma_data;
	u32 saved_ctrl;
};


static void jh7110_pwmdac_dump_cfg(struct jh7110_pwmdac_dev *pwmdac)
{
    struct jh7110_pwmdac_cfg *cfg = &pwmdac->cfg;

    if (!pwmdac || !pwmdac->dev)
        return;

    dev_info(pwmdac->dev, "----- JH7110 PWMDAC CFG DUMP -----\n");
    dev_info(pwmdac->dev, "shift       : %d\n", cfg->shift);
    dev_info(pwmdac->dev, "duty_cycle  : %d\n", cfg->duty_cycle);
    dev_info(pwmdac->dev, "cnt_n       : %u\n", cfg->cnt_n);
    dev_info(pwmdac->dev, "data_change : %d\n", cfg->data_change);
    dev_info(pwmdac->dev, "data_mode   : %d\n", cfg->data_mode);
    dev_info(pwmdac->dev, "data_shift  : %d\n", cfg->data_shift);
    dev_info(pwmdac->dev, "----------------------------------\n");
}
static inline void jh7110_pwmdac_write_reg(void __iomem *io_base, int reg, u32 val)
{
	writel(val, io_base + reg);
}

static inline u32 jh7110_pwmdac_read_reg(void __iomem *io_base, int reg)
{
	return readl(io_base + reg);
}

static void dump_pwmdac_regs(void __iomem *base_addr_virt)
{
	u32 read_ctrl;
	read_ctrl = jh7110_pwmdac_read_reg(base_addr_virt, JH7110_PWMDAC_CTRL);
	pr_info("ctrl value=0x%x\n",read_ctrl);
	return;
}

static void jh7110_pwmdac_init_params(struct jh7110_pwmdac_dev *dev)
{
	dev->cfg.shift = PWMDAC_SHIFT_8;
	dev->cfg.duty_cycle = PWMDAC_CYCLE_CENTER;
	dev->cfg.cnt_n = PWMDAC_SAMPLE_CNT_1;
	dev->cfg.data_change = NO_CHANGE;
	dev->cfg.data_mode = INVERTER_DATA_MSB;
	dev->cfg.data_shift = PWMDAC_DATA_LEFT_SHIFT_BIT_0;

	dev->play_dma_data.addr = dev->mapbase + JH7110_PWMDAC_WDATA;
	dev->play_dma_data.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	dev->play_dma_data.fifo_size = 1;
	dev->play_dma_data.maxburst = 16;
}

static int jh7110_pwmdac_crg_enable(struct jh7110_pwmdac_dev *dev, bool enable)
{
	int ret;

	if (enable) {
		ret = clk_bulk_prepare_enable(ARRAY_SIZE(dev->clks), dev->clks);
		if (ret)
			return dev_err_probe(dev->dev, ret,
					     "failed to enable pwmdac clocks\n");

		ret = reset_control_deassert(dev->rst_apb);
		if (ret) {
			dev_err(dev->dev, "failed to deassert pwmdac apb reset\n");
			goto err_rst_apb;
		}
	} else {
		clk_bulk_disable_unprepare(ARRAY_SIZE(dev->clks), dev->clks);
	}

	return 0;

err_rst_apb:
	clk_bulk_disable_unprepare(ARRAY_SIZE(dev->clks), dev->clks);

	return ret;
}
static int jh7110_pwmdac_runtime_resume(struct device *dev)
{
	struct jh7110_pwmdac_dev *pwmdac = dev_get_drvdata(dev);

	return jh7110_pwmdac_crg_enable(pwmdac, true);
}

static int jh7110_pwmdac_probe(struct platform_device *pdev)
{
	struct jh7110_pwmdac_dev *dev;
	struct resource *res;
	int ret;

	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	dev->base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(dev->base))
		return PTR_ERR(dev->base);

	dev->mapbase = res->start;

	dev->clks[0].id = "apb";
	dev->clks[1].id = "core";

	ret = devm_clk_bulk_get(&pdev->dev, ARRAY_SIZE(dev->clks), dev->clks);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to get pwmdac clocks\n");

	dev->rst_apb = devm_reset_control_get_exclusive(&pdev->dev, NULL);
	if (IS_ERR(dev->rst_apb))
		return dev_err_probe(&pdev->dev, PTR_ERR(dev->rst_apb),
				     "failed to get pwmdac apb reset\n");

	jh7110_pwmdac_init_params(dev);

	dev->dev = &pdev->dev;
	dev_set_drvdata(&pdev->dev, dev);

	dev_info(&pdev->dev,"use this pwmdac to test dmac\n");
	pm_runtime_enable(dev->dev);
	if (!pm_runtime_enabled(&pdev->dev)) {
		ret = jh7110_pwmdac_runtime_resume(&pdev->dev);
		if (ret)
			goto err_pm_disable;
	}

	return 0;

err_pm_disable:
	pm_runtime_disable(&pdev->dev);

	return ret;
}


static void jh7110_pwmdac_set_enable(struct jh7110_pwmdac_dev *dev, bool enable)
{
	u32 value;

	value = jh7110_pwmdac_read_reg(dev->base, JH7110_PWMDAC_CTRL);
	if (enable)
		value |= JH7110_PWMDAC_ENABLE;
	else
		value &= ~JH7110_PWMDAC_ENABLE;

	jh7110_pwmdac_write_reg(dev->base, JH7110_PWMDAC_CTRL, value);
}

static void jh7110_pwmdac_set_shift(struct jh7110_pwmdac_dev *dev)
{
	u32 value;

	value = jh7110_pwmdac_read_reg(dev->base, JH7110_PWMDAC_CTRL);
	if (dev->cfg.shift == PWMDAC_SHIFT_8)
		value &= ~JH7110_PWMDAC_SHIFT;
	else if (dev->cfg.shift == PWMDAC_SHIFT_10)
		value |= JH7110_PWMDAC_SHIFT;

	jh7110_pwmdac_write_reg(dev->base, JH7110_PWMDAC_CTRL, value);
}

static void jh7110_pwmdac_set_duty_cycle(struct jh7110_pwmdac_dev *dev)
{
	u32 value;

	value = jh7110_pwmdac_read_reg(dev->base, JH7110_PWMDAC_CTRL);
	value &= ~JH7110_PWMDAC_DUTY_CYCLE_MASK;
	value |= (dev->cfg.duty_cycle & 0x3) << JH7110_PWMDAC_DUTY_CYCLE_SHIFT;

	jh7110_pwmdac_write_reg(dev->base, JH7110_PWMDAC_CTRL, value);
}

static void jh7110_pwmdac_set_cnt_n(struct jh7110_pwmdac_dev *dev)
{
	u32 value;

	value = jh7110_pwmdac_read_reg(dev->base, JH7110_PWMDAC_CTRL);
	value &= ~JH7110_PWMDAC_CNT_N_MASK;
	value |= ((dev->cfg.cnt_n - 1) & 0x1ff) << JH7110_PWMDAC_CNT_N_SHIFT;

	jh7110_pwmdac_write_reg(dev->base, JH7110_PWMDAC_CTRL, value);
}

static void jh7110_pwmdac_set_data_change(struct jh7110_pwmdac_dev *dev)
{
	u32 value;

	value = jh7110_pwmdac_read_reg(dev->base, JH7110_PWMDAC_CTRL);
	if (dev->cfg.data_change == NO_CHANGE)
		value &= ~JH7110_PWMDAC_DATA_CHANGE;
	else if (dev->cfg.data_change == CHANGE)
		value |= JH7110_PWMDAC_DATA_CHANGE;

	jh7110_pwmdac_write_reg(dev->base, JH7110_PWMDAC_CTRL, value);
}

static void jh7110_pwmdac_set_data_mode(struct jh7110_pwmdac_dev *dev)
{
	u32 value;

	value = jh7110_pwmdac_read_reg(dev->base, JH7110_PWMDAC_CTRL);
	if (dev->cfg.data_mode == UNSIGNED_DATA)
		value &= ~JH7110_PWMDAC_DATA_MODE;
	else if (dev->cfg.data_mode == INVERTER_DATA_MSB)
		value |= JH7110_PWMDAC_DATA_MODE;

	jh7110_pwmdac_write_reg(dev->base, JH7110_PWMDAC_CTRL, value);
}

static void jh7110_pwmdac_set_data_shift(struct jh7110_pwmdac_dev *dev)
{
	u32 value;

	value = jh7110_pwmdac_read_reg(dev->base, JH7110_PWMDAC_CTRL);
	value &= ~JH7110_PWMDAC_DATA_SHIFT_MASK;
	value |= (dev->cfg.data_shift & 0x7) << JH7110_PWMDAC_DATA_SHIFT_SHIFT;

	jh7110_pwmdac_write_reg(dev->base, JH7110_PWMDAC_CTRL, value);
}
static void jh7110_pwmdac_set(struct jh7110_pwmdac_dev *dev)
{
	jh7110_pwmdac_set_shift(dev);
	jh7110_pwmdac_set_duty_cycle(dev);
	jh7110_pwmdac_set_cnt_n(dev);
	jh7110_pwmdac_set_enable(dev, true);

	jh7110_pwmdac_set_data_change(dev);
	jh7110_pwmdac_set_data_mode(dev);
	jh7110_pwmdac_set_data_shift(dev);
}

static void jh7110_pwmdac_stop(struct jh7110_pwmdac_dev *dev)
{
	jh7110_pwmdac_set_enable(dev, false);
}

static void mocking_jh7110_pwmdac_hw_params(struct jh7110_pwmdac_dev *dev)
{
    int ret;
    jh7110_pwmdac_set(dev);
    ret = clk_set_rate(dev->clks[1].clk, PWMDAC_CLKRATE+64);
    if (ret)
		pr_err("failed to set rate %u for core clock\n",PWMDAC_CLKRATE+64);
    return;
}

//end of pwmdac

#define DEVICE_NAME  "user_dma"
#define BUF_LEN      4096
#define DW_AXI_DMAC_NAME "dma-controller@16050000" 
#define PL08_DMAC_NAME   "dma-controller@16008000"

#define SPI_PATH         "/soc/spi@10060000"
#define PWMDAC_PATH      "/soc/pwmdac@100b0000"

#define PWMDAC_ADDR      0x100b0000

//USER_DMA_IOCTL_<device>_<direction:CPY_TX_RX>_<IB/OOB>
#define USER_DMA_IOCTL_MEM_CPY_IB    _IOR('M', 1, int)//test inband memcpy by dw-axi-dmac
#define USER_DMA_IOCTL_SPI_TXRX_IB   _IOR('M', 2, int)//test inband spi dma loopback
#define USER_DMA_IOCTL_DAC_TX_IB     _IOR('M', 3, int)//test inband dac dma-tx
#define USER_DMA_IOCTL_DAC_TX_OOB   _IOR('M', 4, int)//test oob dac dma-tx
struct transfer_config {
    //bus addr that dmac can use
    dma_addr_t dma_buf_src;
    dma_addr_t dma_buf_des;
    //virt addr that driver can use
    void      *buf_src;
    void      *buf_des;
};

struct my_cb_param {
    const char *log;             
    struct completion done;       // 用于同步的 completion
};

struct spi_ctrl {
    void __iomem *virt_addr;
    dma_addr_t phy_addr;   /* phy addr for dmac */
    struct clk *clk;
    struct reset_control *rst;
};

//transfer info
// static struct transfer_config trans_config;
//char device
static dev_t dev_num;
static struct cdev user_dma_cdev;
static struct class *user_dma_class;
//dma channel
// static struct dma_chan *dw_dma_chan_tx;//memcpy test
// static struct dma_chan *dw_dma_chan_dev;//memcpy to dev test
// static struct dma_chan *pl_dma_chan_tx;//mem2dev
// static struct dma_chan *pl_dma_chan_rx;//dev2mem
//complete
// struct completion cmp_tx;
// struct completion cmp_rx;

struct jh7110_pwmdac_dev *pwmdac;
struct platform_device *pdev_dac;

//spi
static struct spi_ctrl spi;

//callbacks
static void cb_ib(void *param)
{
    struct my_cb_param *params = param;
    complete(&params->done);
    pr_info("%s\n",params->log);
    return;
}

static void cb_oob(void *param)
{
    if(param == NULL)
        pr_info("oob param is NULL\n");
    struct my_cb_param *params = param;
    complete(&params->done);
    if(running_oob()) {
        pr_info("%s\n",params->log);
        pr_info("running oob\n");
    }        
    return;
}

static void fill_source_buffer(struct transfer_config *trans_config)
{
    int num = 0;
    unsigned char *src_buf = trans_config->buf_src;
    for(int i=0;i<BUF_LEN;i++)
    {
        src_buf[i] = (unsigned char)(0xff & num);
        num++;
    }
    return;
}
//return true if des and src buffer are the same
static bool check_dest_buffer(struct transfer_config *trans_config)
{
    unsigned char *src_buf = trans_config->buf_src;
    unsigned char *des_buf = trans_config->buf_des;
    for(int i = 0 ;i<BUF_LEN;i++)
    {
        if(src_buf[i]!=des_buf[i])
        {
            return false;
        }
    }
    return true;
}

static bool fetch_spi_addr(void)
{
    struct device_node *np;
    struct resource res;

    /* 1. 找到 SPI 节点，名字要和 DTS 一致 */
    np = of_find_node_by_path(SPI_PATH);
    if (!np) {
        pr_err("cannot find spi0 node\n");
        return false;
    }

    /* 2. 获取 resource */
    if (of_address_to_resource(np, 0, &res)) {
        pr_err("failed to get spi resource\n");
        of_node_put(np);
        return false;
    }

    /* 3. 物理基地址 */
    spi.phy_addr = res.start;

    /* 4. CPU 访问寄存器（可选） */
    spi.virt_addr = ioremap(res.start, resource_size(&res));
    if (!spi.virt_addr) {
        pr_err("user_dma: ioremap failed\n");
        of_node_put(np);
        return false;
    }
    pr_info("user_dma: spi phys=%pad virt=%p\n",
            &spi.phy_addr, spi.virt_addr);
    
    /* 通过 node 获取 clk */
    spi.clk = of_clk_get(np, 0); // 索引 0, 对应 clocks 属性的第一个
    if (IS_ERR(spi.clk)) {
        pr_err("failed to get clk\n");
        return PTR_ERR(spi.clk);
    }
    clk_prepare_enable(spi.clk);
    /* 通过 node 获取 reset */
    spi.rst = of_reset_control_get(np, NULL); // NULL = 默认reset
    if (IS_ERR(spi.rst)) {
        pr_err("failed to get reset\n");
        return PTR_ERR(spi.rst);
    }
    reset_control_deassert(spi.rst);   

    of_node_put(np);
    return true;
}
static bool enable_dac(void)
{
    int ret;
    ret = clk_bulk_prepare_enable(ARRAY_SIZE(pwmdac->clks), pwmdac->clks);
    if (ret) {
        pr_err("clk enable error\n");
        return false;
    }
    ret = reset_control_deassert(pwmdac->rst_apb);
    if (ret) {
        pr_err("failed to deassert pwmdac apb reset\n");
        return false;
    }
    pr_info("pwmdac clocks enabled, reset released\n");
    return true;
}
static void disable_dac(void)
{
    reset_control_assert(pwmdac->rst_apb);

    clk_bulk_disable_unprepare(ARRAY_SIZE(pwmdac->clks), pwmdac->clks);

    pr_info("pwmdac clocks disabled, reset asserted\n");
}
static bool fetch_dac_addr(void)
{   
    struct device_node *np;
    int ret;
    /* 1. 找到 dac 节点，名字要和 DTS 一致 */
    np = of_find_node_by_path(PWMDAC_PATH);
    if (!np) {
        pr_err("cannot find pwmdac node\n");
        return false;
    }
    pdev_dac = of_find_device_by_node(np);
    if (!pdev_dac) {
        pr_err("cannot find platform_device for DAC\n");
        return false;
    }

    ret = jh7110_pwmdac_probe(pdev_dac);
    if(ret) {
        pr_err("probe fails\n");
        return false;
    }
    pwmdac = dev_get_drvdata(&pdev_dac->dev);
    jh7110_pwmdac_dump_cfg(pwmdac);
    pr_info( "dac base (virt addr) : %p\n", pwmdac->base);
    pr_info( "dac mapbase (phys)   : %pa\n", &pwmdac->mapbase);
    //enable the device
    if(!enable_dac()){
        return false;
    }
    
    of_node_put(np);
    dump_pwmdac_regs(pwmdac->base);
    return true;
}

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


static bool allocate_trans_config(struct transfer_config *trans_config,struct device *dev)
{
    trans_config->buf_src = dma_alloc_coherent(dev, BUF_LEN, &(trans_config->dma_buf_src), GFP_KERNEL);
    trans_config->buf_des = dma_alloc_coherent(dev, BUF_LEN, &(trans_config->dma_buf_des), GFP_KERNEL);
    if(!trans_config->buf_src && !trans_config->buf_des)
    {
        pr_err("allocate DMAC reachable buffer error\n");
        return false;
    }
    pr_info("allocate DMAC reachable buffer success\n");
    return true;
}

static void release_trans_config(struct transfer_config *trans_config,struct device *dev)
{
    dma_free_coherent(dev, BUF_LEN, trans_config->buf_src, trans_config->dma_buf_src);
    dma_free_coherent(dev, BUF_LEN, trans_config->buf_des, trans_config->dma_buf_des);
    return;
}

//do_<dmac name>_<device>_<cp/cy/sg>_<ib/oob>_test
static int do_dw_mem_cp_ib_test(void)
{
    int ret = 0;
    struct transfer_config trans_config;
    struct my_cb_param params;
    
    dma_cap_mask_t mask;
    struct dma_chan *dchan;
    struct dma_async_tx_descriptor *tx;
    dma_cookie_t cookie_tx;

    //get channel
    dma_cap_zero(mask);
    dma_cap_set(DMA_MEMCPY, mask);
    dchan = dma_request_channel(mask, my_dma_filter, DW_AXI_DMAC_NAME);
    if (IS_ERR(dchan)) {
        pr_err("Failed to request DMA channel\n");
        ret = 1;
        goto chan_exit;
    } else {
        if(dchan && dchan->device && dchan->device->dev)
            pr_info("Got dw memcpy channel success: %s\n", dma_chan_name(dchan));            
    }
    //allocate dma reachable buffer
    if(!allocate_trans_config(&trans_config,dchan->device->dev)) {
        ret = 1;
        goto trans_config_exit;
    }
    fill_source_buffer(&trans_config);
    memset(trans_config.buf_des,0,BUF_LEN);

    init_completion(&params.done);
    params.log = "MEM:inband memcpy by dw-axi-dmac cb is called";

    //prepare
    tx = dmaengine_prep_dma_memcpy(dchan, 
        trans_config.dma_buf_src , 
        trans_config.dma_buf_des ,
        BUF_LEN, 
        DMA_CTRL_ACK | DMA_PREP_INTERRUPT
    );
    tx->callback       = cb_ib;
    tx->callback_param = &params;
    pr_info("dma prepare success\n");
    //submit
    cookie_tx = dmaengine_submit(tx);
    pr_info("dma submit success\n");
    //issue pending 
    dma_async_issue_pending(dchan);
    pr_info("dma issue pending success\n");
    //wait for dma to comp
    pr_info("wait for dma to comp\n");
    wait_for_completion(&params.done);
    pr_info("wait finish\n");
    //check the pattern in dest buffer
    ret = (check_dest_buffer(&trans_config))?(0):(1);

    //release trans_config
    release_trans_config(&trans_config,dchan->device->dev);
trans_config_exit:
    dma_release_channel(dchan);
chan_exit:
    return ret;
}

static int do_pl08_spi_sg_ib_test(void)
{
    int ret = 0;
    struct transfer_config trans_config;
    struct my_cb_param params;

    dma_cap_mask_t mask;
    struct dma_chan *dchan_tx;
    struct dma_chan *dchan_rx;

    struct dma_slave_config chan_tx_config = {0};
    struct dma_slave_config chan_rx_config = {0};
    struct scatterlist sg_tx[1], sg_rx[1];
    struct dma_async_tx_descriptor *rx;
    struct dma_async_tx_descriptor *tx;
    dma_cookie_t cookie_rx;
    dma_cookie_t cookie_tx;
    int xfer_len = 8;

    //get channel
    dma_cap_zero(mask);
    dma_cap_set(DMA_SLAVE, mask);
    dchan_tx = dma_request_channel(mask, my_dma_filter, PL08_DMAC_NAME);
    dchan_rx = dma_request_channel(mask, my_dma_filter, PL08_DMAC_NAME);
    if(IS_ERR(dchan_tx) || IS_ERR(dchan_rx)) {
        pr_err("Failed to request PL08 DMA channel\n");
        ret = 1;
        goto chan_exit;
        // return false;
    } else {
        if(dchan_tx && dchan_rx && dchan_tx->device && dchan_tx->device->dev) {
            pr_info("Got PL08 tx channel: %s\n", dma_chan_name(dchan_tx));
            pr_info("Got PL08 rx channel: %s\n", dma_chan_name(dchan_rx));
        }
    }
    //allocate dma reachable buffer
    if(!allocate_trans_config(&trans_config,dchan_tx->device->dev)) {
        ret = 1;
        goto trans_config_exit;
    }
    fill_source_buffer(&trans_config);
    memset(trans_config.buf_des,0,BUF_LEN);
    spi_dma_config(spi.virt_addr);
    print_spi_regs(spi.virt_addr);
    pr_info("\nPL022 spi config and enable sucess\n");
    // ini params
    init_completion(&params.done);
    params.log = "SPI:inband spi-rx dma callback by PL08-dmac is called";

    //config dma channel
    //tx
    chan_tx_config.direction      = DMA_MEM_TO_DEV;
    chan_tx_config.dst_addr       = SSP_DR(spi.phy_addr);
    chan_tx_config.dst_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE;//
    chan_tx_config.dst_maxburst   = 1;//
    chan_tx_config.device_fc      = false;

    dmaengine_slave_config(dchan_tx, &chan_tx_config);
    //rx
    chan_rx_config.direction      = DMA_DEV_TO_MEM;
    chan_rx_config.src_addr       = SSP_DR(spi.phy_addr);
    chan_rx_config.src_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE;//
    chan_rx_config.src_maxburst   = 1;//
    chan_rx_config.device_fc      = false;

    dmaengine_slave_config(dchan_rx, &chan_rx_config);
    pr_info("dma slave config success\n");
    //init scatter list
    sg_init_table(sg_tx, 1);
    sg_init_table(sg_rx, 1);
    sg_init_one(&sg_tx[0], trans_config.buf_src, xfer_len);        
    sg_init_one(&sg_rx[0], trans_config.buf_des, xfer_len);

    sg_dma_address(&sg_tx[0]) = trans_config.dma_buf_src;
    sg_dma_len(&sg_tx[0])     = xfer_len;

    sg_dma_address(&sg_rx[0]) = trans_config.dma_buf_des;
    sg_dma_len(&sg_rx[0])     = xfer_len;

    //prepare dma desc
    tx = dmaengine_prep_slave_sg(dchan_tx,
        sg_tx, 
        1, 
        DMA_MEM_TO_DEV, 
        DMA_PREP_INTERRUPT | DMA_CTRL_ACK
    );
    rx = dmaengine_prep_slave_sg(dchan_rx,
        sg_rx, 
        1, 
        DMA_DEV_TO_MEM, 
        DMA_PREP_INTERRUPT | DMA_CTRL_ACK
    );
    rx->callback       = cb_ib;
    rx->callback_param = &params;
    pr_info("dma prepare success\n");
    //submit
    cookie_rx = dmaengine_submit(rx);
    cookie_tx = dmaengine_submit(tx);
    pr_info("TX cookie=%u RX cookie=%u\n", cookie_tx, cookie_rx);
    //issue pending 
    dma_async_issue_pending(dchan_rx);
    dma_async_issue_pending(dchan_tx);
    spi_enable(spi.virt_addr);
    print_spi_regs(spi.virt_addr);
    pr_info("dma issue pending success\n");
    pr_info("wait for dma to comp\n");

    if (!wait_for_completion_timeout(&params.done, msecs_to_jiffies(5000))) {
        pr_err("rx DMA timeout error!\n");
        dmaengine_terminate_sync(dchan_tx);
        dmaengine_terminate_sync(dchan_rx);
        pr_info("pl dma channels terminated\n");
        ret = 1;
    } else {
        pr_info("wait success\n");
    }
    //release trans_config
    release_trans_config(&trans_config,dchan_tx->device->dev);
trans_config_exit:
    dma_release_channel(dchan_tx);
    dma_release_channel(dchan_rx);
chan_exit:
    return ret;
}

static int do_dw_dac_cy_ib_test(void)
{
    int ret = 0;
    struct transfer_config trans_config;
    struct my_cb_param params;

    dma_cap_mask_t mask;
    struct dma_chan *dchan;
    struct dma_slave_config chan_tx_config = {0};
    struct dma_async_tx_descriptor *tx;
    dma_cookie_t cookie_tx;
    //get channel 
    dma_cap_zero(mask);
    dma_cap_set(DMA_SLAVE, mask);
    dchan = dma_request_chan(&pdev_dac->dev, "tx");
    if (IS_ERR(dchan)) {
        pr_err("Failed to request DMA channel\n");
        ret = 1;
        goto chan_exit;
    } else {
        if(dchan && dchan->device && dchan->device->dev)
            pr_info("Got dw m2d channel success: %s\n", dma_chan_name(dchan));            
    }
    //allocate dma reachable buffer
    if(!allocate_trans_config(&trans_config,dchan->device->dev)) {
        ret = 1;
        goto trans_config_exit;
    }
    fill_source_buffer(&trans_config);
    memset(trans_config.buf_des,0,BUF_LEN);

    init_completion(&params.done);
    params.log = "DAC:inband m2d dma by dw-axi-dmac cb is called";

    //mocking jh7110_pwmdac_hw_params
    mocking_jh7110_pwmdac_hw_params(pwmdac);
    //slave config
    memset(&chan_tx_config, 0, sizeof(chan_tx_config));
    chan_tx_config.direction      = DMA_MEM_TO_DEV;
    chan_tx_config.dst_addr       = PWMDAC_ADDR;
    chan_tx_config.dst_addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES;//
    chan_tx_config.dst_maxburst   = 16;//
    chan_tx_config.device_fc      = false;
    dmaengine_slave_config(dchan, &chan_tx_config);
    
    //prepare
    tx = dmaengine_prep_dma_cyclic(dchan, 
        trans_config.dma_buf_src, 
        BUF_LEN,
        BUF_LEN, 
        DMA_MEM_TO_DEV,
        DMA_CTRL_ACK | DMA_PREP_INTERRUPT
    );
    tx->callback       = cb_ib;
    tx->callback_param = &params;
    pr_info("dma prepare sucess\n");
    //submit
    cookie_tx = dmaengine_submit(tx);
    pr_info("TX cookie=%u\n", cookie_tx);

    dma_async_issue_pending(dchan);
    enable_dac();
    // jh7110_pwmdac_set(pwmdac);
    dump_pwmdac_regs(pwmdac->base);
    if (!wait_for_completion_timeout(&params.done, msecs_to_jiffies(5000))) {
        pr_err("tx DMA timeout error!\n");
        jh7110_pwmdac_stop(pwmdac);
        dump_pwmdac_regs(pwmdac->base);
        pr_info("pl dma channels terminated\n");
    } else {
        jh7110_pwmdac_stop(pwmdac);
        pr_info("wait finish\n");
    }
    dmaengine_pause(dchan);
    msleep(1);
    disable_dac();
    dmaengine_terminate_sync(dchan);
    //release trans_config
    release_trans_config(&trans_config,dchan->device->dev);
trans_config_exit:
    dma_release_channel(dchan);
chan_exit:
    return ret;
}

static int do_dw_dac_cy_oob_test(void)
{
    int ret = 0;
    struct transfer_config trans_config;
    struct my_cb_param params;

    dma_cap_mask_t mask;
    struct dma_chan *dchan;
    struct dma_slave_config chan_tx_config = {0};
    struct dma_async_tx_descriptor *tx;
    dma_cookie_t cookie_tx;

    //get channel 
    dma_cap_zero(mask);
    dma_cap_set(DMA_SLAVE, mask);
    dchan = dma_request_chan(&pdev_dac->dev, "tx");
    if (IS_ERR(dchan)) {
        pr_err("Failed to request DMA channel\n");
        ret = 1;
        goto chan_exit;
    } else {
        if(dchan && dchan->device && dchan->device->dev)
            pr_info("Got dw m2d channel success: %s\n", dma_chan_name(dchan));            
    }
    //allocate dma reachable buffer
    if(!allocate_trans_config(&trans_config,dchan->device->dev)) {
        ret = 1;
        goto trans_config_exit;
    }

    fill_source_buffer(&trans_config);
    memset(trans_config.buf_des,0,BUF_LEN);

    init_completion(&params.done);
    params.log = "DAC:oob m2d dma by dw-axi-dmac cb is called";

    //mocking jh7110_pwmdac_hw_params
    mocking_jh7110_pwmdac_hw_params(pwmdac);

    //slave config
    memset(&chan_tx_config, 0, sizeof(chan_tx_config));
    chan_tx_config.direction      = DMA_MEM_TO_DEV;
    chan_tx_config.dst_addr       = PWMDAC_ADDR;
    chan_tx_config.dst_addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES;//
    chan_tx_config.dst_maxburst   = 16;//
    chan_tx_config.device_fc      = false;
    dmaengine_slave_config(dchan, &chan_tx_config);
    
    //prepare
    tx = dmaengine_prep_dma_cyclic(dchan, 
        trans_config.dma_buf_src, 
        BUF_LEN,
        BUF_LEN, 
        DMA_MEM_TO_DEV,
        DMA_CTRL_ACK | DMA_PREP_INTERRUPT | DMA_OOB_INTERRUPT
    );
    tx->callback       = cb_oob;
    tx->callback_param = &params;
    //submit
    cookie_tx = dmaengine_submit(tx);
    pr_info("TX cookie=%u\n", cookie_tx);

    dma_async_issue_pending(dchan);
    enable_dac();
    jh7110_pwmdac_set(pwmdac);
    dump_pwmdac_regs(pwmdac->base);
    if (!wait_for_completion_timeout(&params.done, msecs_to_jiffies(5000))) {
        pr_err("tx DMA timeout error!\n");
        jh7110_pwmdac_stop(pwmdac);
        dump_pwmdac_regs(pwmdac->base);
        pr_info("pl dma channels terminated\n");
    } else {
        jh7110_pwmdac_stop(pwmdac);
        pr_info("wait finish\n");
    }
    dmaengine_pause(dchan);
    msleep(1);
    disable_dac();
    dmaengine_terminate_sync(dchan);
    //release trans_config
    release_trans_config(&trans_config,dchan->device->dev);
trans_config_exit:
    dma_release_channel(dchan);
chan_exit:
    return ret;
}

static long user_dma_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    int output;
    switch (cmd)
    {
        case USER_DMA_IOCTL_MEM_CPY_IB:
            output = do_dw_mem_cp_ib_test();
            if (copy_to_user((int __user *)arg, &output, sizeof(output)))
                return -EFAULT;
            break;
        case USER_DMA_IOCTL_SPI_TXRX_IB:{
            output = do_pl08_spi_sg_ib_test();
            if (copy_to_user((int __user *)arg, &output, sizeof(output)))
                return -EFAULT;
            break;
        }
        case USER_DMA_IOCTL_DAC_TX_IB:{
            output = do_dw_dac_cy_ib_test();
            if (copy_to_user((int __user *)arg, &output, sizeof(output)))
                return -EFAULT;
            break;
        }
        case USER_DMA_IOCTL_DAC_TX_OOB:{
            output = do_dw_dac_cy_oob_test();
            if (copy_to_user((int __user *)arg, &output, sizeof(output)))
                return -EFAULT;
            break;
        }
        default:
            return -ENOTTY;
        return 0;
    }
    return 0;
}

static int user_dma_open(struct inode *inode, struct file *file)
{
    pr_info("user_dma open success\n");    

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

// static bool prep_dma_chans(void) 
// {
    
//     dma_cap_zero(mask);
//     dma_cap_set(DMA_SLAVE, mask);
//     // dw_dma_chan_dev= dma_request_channel(mask, my_dma_filter, DW_AXI_DMAC_NAME);
//     dw_dma_chan_dev = dma_request_chan(&pdev_dac->dev, "tx");
//     //PL08 dmac channels
//     dma_cap_zero(mask);
//     dma_cap_set(DMA_SLAVE, mask);
//     pl_dma_chan_tx = dma_request_channel(mask, my_dma_filter, PL08_DMAC_NAME);
//     pl_dma_chan_rx = dma_request_channel(mask, my_dma_filter, PL08_DMAC_NAME);
    
//     pr_info("end to request for dma channel\n");
//     //check
//     if (IS_ERR(dw_dma_chan_tx) || IS_ERR(dw_dma_chan_dev)) {
//         pr_err("Failed to request DMA channel\n");
//         return false;
//     } else {
//         if(dw_dma_chan_tx && dw_dma_chan_dev && dw_dma_chan_tx->device && dw_dma_chan_tx->device->dev)
//             pr_info("Got dw tx channel: %s\n", dma_chan_name(dw_dma_chan_tx));
//             pr_info("Got dw mem2dev channel: %s\n", dma_chan_name(dw_dma_chan_dev));
//     }
//     if(IS_ERR(pl_dma_chan_tx) || IS_ERR(pl_dma_chan_rx)) {
//         pr_err("Failed to request PL08 DMA channel\n");
//         return false;
//     } else {
//         if(pl_dma_chan_tx && pl_dma_chan_rx && pl_dma_chan_tx->device && pl_dma_chan_tx->device->dev) {
//             pr_info("Got PL08 tx channel: %s\n", dma_chan_name(pl_dma_chan_tx));
//             pr_info("Got PL08 rx channel: %s\n", dma_chan_name(pl_dma_chan_rx));
//         }
//     }
//     return true;
// }

// static void release_dma_chans(void)
// {
//     //release dma buffer
//     dma_free_coherent(dw_dma_chan_tx->device->dev, BUF_LEN, trans_config.buf_src, trans_config.dma_buf_src);
//     dma_free_coherent(dw_dma_chan_tx->device->dev, BUF_LEN, trans_config.buf_des, trans_config.dma_buf_des);

//     dma_release_channel(dw_dma_chan_tx);
//     dma_release_channel(dw_dma_chan_dev);
//     return;
// }

static int __init user_dma_init(void)
{
    int ret;

    if(!fetch_spi_addr()) {
        pr_err("fetch spi error\n");
    } else {
        pr_info("spi success\n");
    }

    if(!fetch_dac_addr()) {
        pr_err("fetch dac error\n");
    } else {
        pr_info("dac success\n");
    }

    // register char device
    ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        pr_err("Failed to alloc chrdev\n");
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
    // release_dma_chans();

    device_destroy(user_dma_class, dev_num);
    class_destroy(user_dma_class);

    cdev_del(&user_dma_cdev);
    unregister_chrdev_region(dev_num, 1);

    pr_info("user_dma module unloaded\n");
}

module_init(user_dma_init);
module_exit(user_dma_exit);

MODULE_LICENSE("GPL");
