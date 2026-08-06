#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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

static int FindCdcDataInterface(const libusb_config_descriptor *config)
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

            for (int k = 0; k < alt.bNumEndpoints; ++k)
            {
                std::printf(
                    "  endpoint[%d]=0x%02x attributes=0x%02x maxPacket=%u\n",
                    k,
                    alt.endpoint[k].bEndpointAddress,
                    alt.endpoint[k].bmAttributes,
                    alt.endpoint[k].wMaxPacketSize);
            }

            if (selected < 0 && alt.bInterfaceClass == 0x0A && alt.bNumEndpoints == 2)
                selected = alt.bInterfaceNumber;
        }
    }

    return selected;
}

int main()
{
    int fd = -1;
    if (!ParseFd(fd))
        return 2;

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
    std::printf("libusb_get_configuration=%d (%s), value=%d\n", result, libusb_error_name(result), configuration);

    libusb_device *device = libusb_get_device(handle);
    libusb_config_descriptor *config = nullptr;
    result = libusb_get_active_config_descriptor(device, &config);
    std::printf("libusb_get_active_config_descriptor=%d (%s)\n", result, libusb_error_name(result));

    if (result != LIBUSB_SUCCESS || !config)
    {
        result = libusb_get_config_descriptor(device, 0, &config);
        std::printf("libusb_get_config_descriptor(0)=%d (%s)\n", result, libusb_error_name(result));
    }

    if (result != LIBUSB_SUCCESS || !config)
    {
        libusb_close(handle);
        libusb_exit(context);
        return 5;
    }

    int interfaceNumber = FindCdcDataInterface(config);
    libusb_free_config_descriptor(config);

    std::printf("Selected CDC interface=%d\n", interfaceNumber);
    if (interfaceNumber < 0)
    {
        libusb_close(handle);
        libusb_exit(context);
        return 6;
    }

    result = libusb_claim_interface(handle, interfaceNumber);
    std::printf("claim_before_reconfigure=%d (%s)\n", result, libusb_error_name(result));

    if (result == LIBUSB_SUCCESS)
    {
        libusb_release_interface(handle, interfaceNumber);
        libusb_close(handle);
        libusb_exit(context);
        return 0;
    }

    if (configuration <= 0)
        configuration = 1;

    result = libusb_set_configuration(handle, configuration);
    std::printf("libusb_set_configuration(%d)=%d (%s)\n", configuration, result, libusb_error_name(result));

    int configurationAfter = -1;
    int getAfter = libusb_get_configuration(handle, &configurationAfter);
    std::printf("configuration_after=%d (%s), value=%d\n", getAfter, libusb_error_name(getAfter), configurationAfter);

    result = libusb_claim_interface(handle, interfaceNumber);
    std::printf("claim_after_reconfigure=%d (%s)\n", result, libusb_error_name(result));

    if (result == LIBUSB_SUCCESS)
        libusb_release_interface(handle, interfaceNumber);

    libusb_close(handle);
    libusb_exit(context);

    return result == LIBUSB_SUCCESS ? 0 : 7;
}
