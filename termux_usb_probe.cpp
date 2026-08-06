#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <fcntl.h>
#include <linux/usbdevice_fs.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <libusb.h>

static bool ParseFd(int &fd)
{
    const char *value = std::getenv("TERMUX_USB_FD");
    if (!value || !*value)
    {
        std::fprintf(stderr, "TERMUX_USB_FD is not set\n");
        return false;
    }

    errno = 0;
    char *end = nullptr;
    long parsed = std::strtol(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' || parsed < 0 || parsed > INT_MAX)
    {
        std::fprintf(stderr, "Invalid TERMUX_USB_FD: %s\n", value);
        return false;
    }

    fd = static_cast<int>(parsed);
    return true;
}

static int FindCdcDataInterface(const libusb_config_descriptor *config,
                                unsigned char &inEndpoint,
                                unsigned char &outEndpoint)
{
    int selected = -1;

    std::printf("Descriptor configuration value: %u\n", config->bConfigurationValue);
    std::printf("Descriptor interface slots: %u\n", config->bNumInterfaces);

    for (int i = 0; i < config->bNumInterfaces; ++i)
    {
        const libusb_interface &iface = config->interface[i];
        for (int j = 0; j < iface.num_altsetting; ++j)
        {
            const libusb_interface_descriptor &alt = iface.altsetting[j];
            std::printf(
                "slot=%d alt_slot=%d bInterfaceNumber=%u bAlternateSetting=%u class=0x%02x endpoints=%u\n",
                i,
                j,
                alt.bInterfaceNumber,
                alt.bAlternateSetting,
                alt.bInterfaceClass,
                alt.bNumEndpoints);

            unsigned char candidateIn = 0;
            unsigned char candidateOut = 0;

            for (int k = 0; k < alt.bNumEndpoints; ++k)
            {
                const libusb_endpoint_descriptor &endpoint = alt.endpoint[k];
                std::printf(
                    "  endpoint[%d]=0x%02x attributes=0x%02x maxPacket=%u\n",
                    k,
                    endpoint.bEndpointAddress,
                    endpoint.bmAttributes,
                    endpoint.wMaxPacketSize);

                if (endpoint.bEndpointAddress & LIBUSB_ENDPOINT_IN)
                    candidateIn = endpoint.bEndpointAddress;
                else
                    candidateOut = endpoint.bEndpointAddress;
            }

            if (selected < 0 &&
                alt.bInterfaceClass == 0x0A &&
                alt.bNumEndpoints == 2 &&
                candidateIn != 0 &&
                candidateOut != 0)
            {
                selected = alt.bInterfaceNumber;
                inEndpoint = candidateIn;
                outEndpoint = candidateOut;
            }
        }
    }

    return selected;
}

static void PrintFdState(int fd)
{
    int flags = fcntl(fd, F_GETFL);
    if (flags < 0)
    {
        std::printf("fcntl(F_GETFL) failed: errno=%d (%s)\n", errno, std::strerror(errno));
        return;
    }

    const char *mode = "UNKNOWN";
    switch (flags & O_ACCMODE)
    {
        case O_RDONLY: mode = "O_RDONLY"; break;
        case O_WRONLY: mode = "O_WRONLY"; break;
        case O_RDWR: mode = "O_RDWR"; break;
    }

    std::printf("FD access mode: %s, flags=0x%x\n", mode, flags);

    unsigned int capabilities = 0;
    errno = 0;
    int result = ioctl(fd, USBDEVFS_GET_CAPABILITIES, &capabilities);
    std::printf("USBDEVFS_GET_CAPABILITIES=%d errno=%d (%s) caps=0x%x\n",
                result,
                errno,
                std::strerror(errno),
                capabilities);
}

static void ProbeRawClaim(int fd, int interfaceNumber)
{
    int iface = interfaceNumber;
    errno = 0;
    int result = ioctl(fd, USBDEVFS_CLAIMINTERFACE, &iface);
    int savedErrno = errno;

    std::printf("raw_claim_interface_%d=%d errno=%d (%s)\n",
                interfaceNumber,
                result,
                savedErrno,
                std::strerror(savedErrno));

    if (result == 0)
    {
        errno = 0;
        int releaseResult = ioctl(fd, USBDEVFS_RELEASEINTERFACE, &iface);
        std::printf("raw_release_interface_%d=%d errno=%d (%s)\n",
                    interfaceNumber,
                    releaseResult,
                    errno,
                    std::strerror(errno));
    }
}

static int ProbeHandshake(libusb_device_handle *handle,
                          unsigned char inEndpoint,
                          unsigned char outEndpoint)
{
    unsigned char request[4] = {'O', 'D', 'I', 'N'};
    int transferred = 0;

    int result = libusb_bulk_transfer(handle,
                                      outEndpoint,
                                      request,
                                      sizeof(request),
                                      &transferred,
                                      1000);
    std::printf("bulk_ODIN=%d (%s), transferred=%d\n",
                result,
                libusb_error_name(result),
                transferred);

    if (result != LIBUSB_SUCCESS)
        return result;

    unsigned char response[8] = {0};
    transferred = 0;
    result = libusb_bulk_transfer(handle,
                                  inEndpoint,
                                  response,
                                  7,
                                  &transferred,
                                  1000);

    std::printf("bulk_LOKE=%d (%s), transferred=%d, response=",
                result,
                libusb_error_name(result),
                transferred);

    for (int i = 0; i < transferred; ++i)
        std::printf("%02x", response[i]);

    std::printf(" text=\"%.*s\"\n", transferred, response);
    return result;
}

int main()
{
    int fd = -1;
    if (!ParseFd(fd))
        return 2;

    PrintFdState(fd);
    ProbeRawClaim(fd, 0);
    ProbeRawClaim(fd, 1);

    libusb_context *context = nullptr;
    libusb_device_handle *handle = nullptr;

    libusb_init_option option;
    option.option = LIBUSB_OPTION_NO_DEVICE_DISCOVERY;
    option.value.ival = 1;

    int result = libusb_init_context(&context, &option, 1);
    std::printf("libusb_init_context=%d (%s)\n", result, libusb_error_name(result));
    if (result != LIBUSB_SUCCESS)
        return 3;

    result = libusb_wrap_sys_device(context, static_cast<intptr_t>(fd), &handle);
    std::printf("libusb_wrap_sys_device=%d (%s)\n", result, libusb_error_name(result));
    if (result != LIBUSB_SUCCESS)
    {
        libusb_exit(context);
        return 4;
    }

    int configuration = -1;
    result = libusb_get_configuration(handle, &configuration);
    std::printf("libusb_get_configuration=%d (%s), value=%d\n",
                result,
                libusb_error_name(result),
                configuration);

    libusb_device *device = libusb_get_device(handle);
    libusb_config_descriptor *config = nullptr;
    result = libusb_get_active_config_descriptor(device, &config);
    std::printf("libusb_get_active_config_descriptor=%d (%s)\n",
                result,
                libusb_error_name(result));

    if (result != LIBUSB_SUCCESS || !config)
    {
        result = libusb_get_config_descriptor(device, 0, &config);
        std::printf("libusb_get_config_descriptor(0)=%d (%s)\n",
                    result,
                    libusb_error_name(result));
    }

    if (result != LIBUSB_SUCCESS || !config)
    {
        libusb_close(handle);
        libusb_exit(context);
        return 5;
    }

    unsigned char inEndpoint = 0;
    unsigned char outEndpoint = 0;
    int interfaceNumber = FindCdcDataInterface(config, inEndpoint, outEndpoint);
    libusb_free_config_descriptor(config);

    std::printf("Selected CDC interface=%d in=0x%02x out=0x%02x\n",
                interfaceNumber,
                inEndpoint,
                outEndpoint);

    if (interfaceNumber < 0)
    {
        libusb_close(handle);
        libusb_exit(context);
        return 6;
    }

    result = libusb_claim_interface(handle, interfaceNumber);
    std::printf("libusb_claim_interface=%d (%s)\n", result, libusb_error_name(result));

    int handshakeResult = ProbeHandshake(handle, inEndpoint, outEndpoint);

    if (result == LIBUSB_SUCCESS)
        libusb_release_interface(handle, interfaceNumber);

    libusb_close(handle);
    libusb_exit(context);

    return handshakeResult == LIBUSB_SUCCESS ? 0 : 7;
}
