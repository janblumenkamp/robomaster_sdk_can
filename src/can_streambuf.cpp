#pragma once

#include <string>
#include <exception>
#include <streambuf>
#include <stdio.h>
#include <cstring>
#include <unistd.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <poll.h>

#include <linux/can.h>
#include <linux/can/raw.h>

class can_streambuf : public std::basic_streambuf<char>
{
public:
    can_streambuf(const std::string iface_name, const unsigned int can_id)
        : _can_id{can_id}
    {
        if ((_pf.fd = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0)
        {
            throw std::system_error(EFAULT, std::generic_category());
        }

        _pf.events = POLLIN;

        struct can_filter rfilter;
        rfilter.can_id = _can_id;
        rfilter.can_mask = CAN_EFF_MASK;
        setsockopt(_pf.fd, SOL_CAN_RAW, CAN_RAW_FILTER, &rfilter, sizeof(rfilter));

        struct ifreq ifr {};
        strncpy(ifr.ifr_name, iface_name.c_str(), iface_name.size());

        ifr.ifr_ifindex = if_nametoindex(ifr.ifr_name);

        if (!ifr.ifr_ifindex)
        {
            throw std::system_error(EFAULT, std::generic_category());
        }

        _addr.can_family = AF_CAN;
        _addr.can_ifindex = ifr.ifr_ifindex;

        if (bind(_pf.fd, (struct sockaddr*)&_addr, sizeof(_addr)) < 0)
        {
            throw std::system_error(EFAULT, std::generic_category());
        }

        // stream init
        setg(inbuf, inbuf, inbuf);
        setp(outbuf, outbuf + 7);
    }

    ~can_streambuf()
    {
        close(_pf.fd);
    }

private:
    struct sockaddr_can _addr {};
    struct pollfd _pf;

    char inbuf[8];
    char outbuf[8];

    canid_t _can_id;

    int sync()
    {
        struct can_frame frame;
        frame.can_id = _can_id;
        frame.can_dlc = pptr() - pbase();

        if (frame.can_dlc > 0)
        {
            std::memmove(frame.data, outbuf, frame.can_dlc);

            if (write(_pf.fd, &frame, CAN_MTU) != CAN_MTU)
            {
                return -1;
            }

            pbump(pbase() - pptr());
        }

        return 0;
    }

    int_type overflow(int_type ch)
    {
        *pptr() = ch;
        pbump(1);

        return (sync() == -1 ? std::char_traits<char>::eof() : ch);
    }

    int_type underflow()
    {
        struct can_frame rx_frame;
        const auto nbytes = read(_pf.fd, &rx_frame, sizeof(struct can_frame));

        if (nbytes <= 0)
        {
            perror("read");
            return std::char_traits<char>::eof();
        }

        std::memmove(inbuf, rx_frame.data, rx_frame.can_dlc);

        setg(inbuf, inbuf, inbuf + rx_frame.can_dlc);
        return std::char_traits<char>::to_int_type(*gptr());
    }

};
