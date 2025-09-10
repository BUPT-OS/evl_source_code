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
    CMD_DW_MEMCPY_IB_TEST,//test inband memcpy by dw-axi-dmac
    CMD_SPILOOP_TEST  //test dma by PL08 using spi loopback
};

enum Command parse_command(const char *arg) {
    if (strcmp(arg, "dw_mem_ib") == 0) return CMD_DW_MEMCPY_IB_TEST;
    if (strcmp(arg, "spi") == 0) return CMD_SPILOOP_TEST;
    return CMD_UNKNOWN;
}

#define USER_DMA_IOCTL_IB_TEST       _IOR('M', 1, int)  

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
    int ret = ioctl(fd, USER_DMA_IOCTL_IB_TEST,&value);
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
int main(int argc, char *argv[])
{
    int ret = 0;

    switch  (parse_command(argv[1])) { 
        case CMD_DW_MEMCPY_IB_TEST: {
            ret = do_dw_memcpy_ib_test();
            break;            
        }
        case CMD_SPILOOP_TEST: {
            ret = do_spi_loop_test();
            break;
        }
        default:
            printf("Unknown command: %s\n", argv[1]);
    }

    return ret;
}
