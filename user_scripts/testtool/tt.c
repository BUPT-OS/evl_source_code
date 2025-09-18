#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>
#include <linux/spi/spidev.h>
#include <stdbool.h>
#include <stdint.h> 

// 这里定义与驱动中一致的 ioctl 命令
// 假设驱动里是 _IO('M', 1)
// #define MY_IOCTL_CMD _IO('M', 1)

enum Command {
    CMD_UNKNOWN,

    CMD_DW_MEMCPY_IB_TEST,//USER_DMA: test inband memcpy by dw-axi-dmac
    CMD_SPILOOP_TEST,     //PL08:     test dma by PL08 driver，using spi loopback
    CMD_SPI_USERDMA_IB,   //USER_DMA: test spi loopback,using duplex dma in user_dma
    CMD_DAC_CY_IB,         //USER_DMA: test dac tx,using dma in user_dma
    CMD_DAC_CY_OOB,         //USER_DMA: test dac tx,using dma in user_dma
    CMD_DAC_SG_IB,
    CMD_DAC_SG_OOB
};

struct Cmd_element {
    const char * cmd_input;
    enum Command cmd;
    int (*cmd_callback)(void);
    const char * cmd_desc;
};

//ioctls
#define USER_DMA_IOCTL_MEM_CPY_IB       _IOR('M', 1, int)  
#define USER_DMA_IOCTL_SPI_TXRX_IB      _IOR('M', 2, int)//test inband spi dma loopback
#define USER_DMA_IOCTL_DACcy_TX_IB      _IOR('M', 3, int)//test inband dac dma-tx
#define USER_DMA_IOCTL_DACcy_TX_OOB     _IOR('M', 4, int)//test oob dac dma-tx
#define USER_DMA_IOCTL_DACsg_TX_IB      _IOR('M', 5, int)//test inband dac dma sg tx
#define USER_DMA_IOCTL_DACsg_TX_OOB     _IOR('M', 6, int)//test oob dac dma sg tx

int do_dw_memcpy_ib_test(void)
{
    const char *dev = "/dev/user_dma";
    int fd;
    int value;

    fd = open(dev, O_RDWR);
    if (fd < 0) {
        perror("open");
        return EXIT_FAILURE;
    }
    int ret = ioctl(fd, USER_DMA_IOCTL_MEM_CPY_IB,&value);
    if (ret < 0) {
        perror("ioctl");
        close(fd);
        return EXIT_FAILURE;
    }
    printf("inband memcpy executed finish\n");
    if(value==0) {
        printf("inband memcpy success!\n");
    } else {
        printf("inband memcpy error!\n");
    }
    close(fd);
    return 0;
}
    
int do_spi_loop_test(void)
{
    bool test_success = true;

    //open char device
    const char *device = "/dev/spidev1.0";
    int fd = open(device, O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    // setting mode,bit width,frequency
    uint8_t mode = SPI_MODE_0;
    if (ioctl(fd, SPI_IOC_WR_MODE, &mode) == -1) {
        perror("SPI_IOC_WR_MODE");
        close(fd);
        return 1;
    }

    uint8_t bits = 8;
    if (ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits) == -1) {
        perror("SPI_IOC_WR_BITS_PER_WORD");
        close(fd);
        return 1;
    }

    uint32_t speed = 399193; // 399 kHz
    if (ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) == -1) {
        perror("SPI_IOC_WR_MAX_SPEED_HZ");
        close(fd);
        return 1;
    }

    // prepare data
    uint8_t tx[1024] = {0};
    uint8_t rx[1024] = {0};
    int num  = 0;
    for(int i=0;i<1024;i++)
    {
        tx[i] = (unsigned char)(0xff & num);
        num++;
    }
    memset(rx,0,1024);

    struct spi_ioc_transfer tr = {
        .tx_buf = (unsigned long)tx,
        .rx_buf = (unsigned long)rx,
        .len = sizeof(tx),
        .speed_hz = speed,
        .bits_per_word = bits,
        .delay_usecs = 0,
    };

    // trigger send and recv
    if (ioctl(fd, SPI_IOC_MESSAGE(1), &tr) < 1) {
        perror("SPI_IOC_MESSAGE");
        close(fd);
        return 1;
    }

    // ckeck data
    for(int i=0;i<1024;i++)
    {   
        if(tx[i]!=rx[i])
        {
            test_success = false;
            break;
        }
    }

    if(test_success)
    {
        printf("spi loopback success!\n");
    }
    else
    {
        printf("spi loopback error!\n");
    }

    close(fd);
    return 0;
}

int do_spi_userdma_ib_test(void)
{
    const char *dev = "/dev/user_dma";
    int fd;
    int value;

    fd = open(dev, O_RDWR);
    if (fd < 0) {
        perror("open");
        return EXIT_FAILURE;
    }
    int ret = ioctl(fd, USER_DMA_IOCTL_SPI_TXRX_IB,&value);
    if (ret < 0) {
        perror("ioctl");
        close(fd);
        return EXIT_FAILURE;
    }
    printf("inband spi loopback by user_dma executed finish\n");
    if(value==0) {
        printf("inband spi loopback success!\n");
    } else {
        printf("inband spi loopback error!\n");
    }
    close(fd);
    return 0;
}

int do_dac_dma_tx_ib(void)
{
    const char *dev = "/dev/user_dma";
    int fd;
    int value;

    fd = open(dev, O_RDWR);
    if (fd < 0) {
        perror("open");
        return EXIT_FAILURE;
    }
    int ret = ioctl(fd, USER_DMA_IOCTL_DACcy_TX_IB,&value);
    if (ret < 0) {
        perror("ioctl");
        close(fd);
        return EXIT_FAILURE;
    }
    printf("inband dac dma-tx by user_dma executed finish\n");
    if(value==0) {
        printf("inband dac dma-tx success!\n");
    } else {
        printf("inband dac dma-tx error!\n");
    }
    close(fd);
    return 0;
}

int do_dac_dma_tx_oob(void)
{
    const char *dev = "/dev/user_dma";
    int fd;
    int value;

    fd = open(dev, O_RDWR);
    if (fd < 0) {
        perror("open");
        return EXIT_FAILURE;
    }
    int ret = ioctl(fd, USER_DMA_IOCTL_DACcy_TX_OOB,&value);
    if (ret < 0) {
        perror("ioctl");
        close(fd);
        return EXIT_FAILURE;
    }
    printf("oob dac dma-tx by user_dma executed finish\n");
    if(value==0) {
        printf("oob dac dma-tx success!\n");
    } else {
        printf("oob dac dma-tx error!\n");
    }
    close(fd);
    return 0;
}

int do_dac_dma_sg_ib(void)
{
    const char *dev = "/dev/user_dma";
    int fd;
    int value;

    fd = open(dev, O_RDWR);
    if (fd < 0) {
        perror("open");
        return EXIT_FAILURE;
    }
    int ret = ioctl(fd, USER_DMA_IOCTL_DACsg_TX_IB,&value);
    if (ret < 0) {
        perror("ioctl");
        close(fd);
        return EXIT_FAILURE;
    }
    printf("ib dac dma-sg by user_dma executed finish\n");
    if(value==0) {
        printf("ib dac dma-sg success!\n");
    } else {
        printf("ib dac dma-sg error!\n");
    }
    close(fd);
    return 0;
}

int do_dac_dma_sg_oob(void)
{
    const char *dev = "/dev/user_dma";
    int fd;
    int value;

    fd = open(dev, O_RDWR);
    if (fd < 0) {
        perror("open");
        return EXIT_FAILURE;
    }
    int ret = ioctl(fd, USER_DMA_IOCTL_DACsg_TX_OOB,&value);
    if (ret < 0) {
        perror("ioctl");
        close(fd);
        return EXIT_FAILURE;
    }
    printf("oob dac dma-sg by user_dma executed finish\n");
    if(value==0) {
        printf("oob dac dma-sg success!\n");
    } else {
        printf("oob dac dma-sg error!\n");
    }
    close(fd);
    return 0;
}


const struct Cmd_element CMD_TABLE[] ={
    {"dw_mem_ib"      , CMD_DW_MEMCPY_IB_TEST, do_dw_memcpy_ib_test   , "test inband memcpy by dw-dmac\n"},
    {"spi_driver_loop", CMD_SPILOOP_TEST     , do_spi_loop_test       , "test inband spi loopback using pl022-spi driver\n"},
    {"spi_userdma_ib" , CMD_SPI_USERDMA_IB   , do_spi_userdma_ib_test , "test inband spi loopback using user_dma\n"},
    {"dac_cy_ib"      , CMD_DAC_CY_IB        , do_dac_dma_tx_ib       , "test inband cyclic mem2dev dma using dw-dmac and dac device\n"},
    {"dac_cy_oob"     , CMD_DAC_CY_OOB       , do_dac_dma_tx_oob      , "test oob    cyclic mem2dev dma using dw-dmac and dac device\n"},
    {"dac_sg_ib"      , CMD_DAC_SG_IB        , do_dac_dma_sg_ib       , "test inband sg     mem2dev dma using dw-dmac and dac device\n"},
    {"dac_sg_oob"     , CMD_DAC_SG_OOB       , do_dac_dma_sg_oob      , "test oob    sg     mem2dev dma using dw-dmac and dac device\n"}
};

void print_help(void)
{
    printf("|%-20s| %s","INSTRUCTIONS","DESCRIBES\n");
    for(int i=0;i<sizeof(CMD_TABLE)/sizeof(CMD_TABLE[0]);i++) {
            printf("|%-20s| %s",CMD_TABLE[i].cmd_input,CMD_TABLE[i].cmd_desc);
    }
    return;
}

enum Command parse_command(const char *arg) {

    for(int i=0;i<sizeof(CMD_TABLE)/sizeof(CMD_TABLE[0]);i++) {
        if (strcmp(arg,CMD_TABLE[i].cmd_input) == 0) return CMD_TABLE[i].cmd;
    }
    return CMD_UNKNOWN;
}

int main(int argc, char *argv[])
{
    int ret = 0;
    enum Command input_cmd = parse_command(argv[1]);
    if(input_cmd == CMD_UNKNOWN){
        print_help();
        return ret;
    }

    for(int i=0;i<sizeof(CMD_TABLE)/sizeof(CMD_TABLE[0]);i++)
    {
        if(input_cmd == CMD_TABLE[i].cmd) {
            ret = CMD_TABLE[i].cmd_callback();
            break;
        }
    }

    return ret;
}
