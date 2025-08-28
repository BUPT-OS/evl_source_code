#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>

// 这里定义与驱动中一致的 ioctl 命令
// 假设驱动里是 _IO('M', 1)
// #define MY_IOCTL_CMD _IO('M', 1)
#define USER_DMA_IOCTL_IB_TEST _IOR('M', 1, int)  

int main(int argc, char *argv[])
{
    const char *dev = "/dev/user_dma";
    int fd;
    int value;

    fd = open(dev, O_RDWR);
    if (fd < 0) {
        perror("open");
        return EXIT_FAILURE;
    }

    printf("Opened device %s successfully.\n", dev);

    // ioctl
    int ret = ioctl(fd, USER_DMA_IOCTL_IB_TEST,&value);
    if (ret < 0) {
        perror("ioctl");
        close(fd);
        return EXIT_FAILURE;
    }
    printf("ioctl executed success\n");
    printf("Got value from kernel: %d\n", value);

    close(fd);
    return 0;
}
