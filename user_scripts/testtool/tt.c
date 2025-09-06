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

enum Command {
    CMD_UNKNOWN,
    CMD_MEMCPY_IB_TEST,
    CMD_SPILOOP_IB_TEST,
    CMD_SPILOOP_OOB_TEST
};

enum Command parse_command(const char *arg) {
    if (strcmp(arg, "memib") == 0) return CMD_MEMCPY_IB_TEST;
    if (strcmp(arg, "spiib") == 0) return CMD_SPILOOP_IB_TEST;
    if (strcmp(arg, "spioob") == 0) return CMD_SPILOOP_OOB_TEST;
    return CMD_UNKNOWN;
}

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

    switch  (parse_command(argv[1])) { 
        case CMD_MEMCPY_IB_TEST: {
            int ret = ioctl(fd, USER_DMA_IOCTL_IB_TEST,&value);
            if (ret < 0) {
                perror("ioctl");
                close(fd);
                return EXIT_FAILURE;
            }
            printf("inband memcpy executed success\n");
            printf("Got value from kernel: %d\n", value);
            break;
        }
        case CMD_SPILOOP_IB_TEST: {
            break;
        }
            
        case CMD_SPILOOP_OOB_TEST: {
            break;
        }
            
        default:
            printf("Unknown command: %s\n", argv[1]);
    }

    close(fd);
    return 0;
}
