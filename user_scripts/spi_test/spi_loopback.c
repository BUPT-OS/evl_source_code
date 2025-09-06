#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h> 
#include <linux/spi/spidev.h>
#include <sys/ioctl.h>
#include <errno.h>

int main() {
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
    uint8_t tx[5] = { 'h', 'e', 'l', 'l', 'o' };
    uint8_t rx[5] = {0};

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

    // 打印接收到的数据
    printf("Received: ");
    for (int i = 0; i < sizeof(rx); i++) {
        printf("%02x ", rx[i]);
    }
    printf("\n");

    close(fd);
    return 0;
}
