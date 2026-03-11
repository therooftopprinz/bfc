#ifndef __BFC_SOCKET_HPP__
#define __BFC_SOCKET_HPP__

#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <inttypes.h>

#include <memory>
#include <string>
#include <stdexcept>
#include <cstring>

#include <bfc/buffer.hpp>

namespace bfc
{

inline constexpr uint32_t to_ip(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    uint32_t rv = (uint32_t(a) << 24);
    rv |= (uint32_t(b) << 16);
    rv |= (uint32_t(c) << 8);
    rv |= (uint32_t(d) << 0);
    return rv;
}

constexpr uint32_t localhost4 = to_ip(127,0,0,1);

inline sockaddr_in ip4_port_to_sockaddr(uint32_t ip, uint16_t port)
{
    sockaddr_in addr;
    memset((char *)&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(ip);
    addr.sin_port = htons(port);
    return addr;
}

inline sockaddr_in ip4_port_to_sockaddr(std::string ip, uint16_t port)
{
    sockaddr_in addr;
    memset((char *)&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (1 != inet_pton(AF_INET, ip.c_str(), &addr.sin_addr))
        addr.sin_family = 0;
    return addr;
}

inline sockaddr_in6 ip6_port_to_sockaddr(std::string ip, uint16_t port)
{
    sockaddr_in6 addr;
    memset((char *)&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(port);
    if (1 != inet_pton(AF_INET6, ip.c_str(), &addr.sin6_addr))
        addr.sin6_family = 0;
    return addr;
}

inline std::string sockaddr_to_string(sockaddr *addr)
{
    char buffer[64];
    char out[72];
    in_port_t port = 0;
    if (addr->sa_family == AF_INET)
    {
        sockaddr_in* addr4 = (sockaddr_in*) addr;
        port = ntohs(addr4->sin_port);
        inet_ntop(AF_INET, &addr4->sin_addr, buffer, sizeof(buffer));
    }
    else if (addr->sa_family == AF_INET6)
    {
        sockaddr_in6* addr6 = (sockaddr_in6*) addr;
        port = ntohs(addr6->sin6_port);
        inet_ntop(AF_INET6, &addr6->sin6_addr, buffer, sizeof(buffer));
    }
    else
    {
        strncpy(buffer, "unknown_family", sizeof(buffer));
    }

    snprintf(out, sizeof(out), "%s:%" PRIu16, buffer, port);
    return out;
}

inline int create_tcp4()
{
    return ::socket(AF_INET, SOCK_STREAM, 0);
}

inline int create_tcp6()
{
    return ::socket(AF_INET6, SOCK_STREAM, 0);
}

inline int create_udp4()
{
    return ::socket(AF_INET, SOCK_DGRAM, 0);
}

inline int create_udp6()
{
    return ::socket(AF_INET6, SOCK_DGRAM, 0);
}

class socket
{
public:
    socket()
        : m_fd(-1)
    {}

    socket(int p_fd)
        : m_fd(p_fd)
    {}

    ~socket()
    {
        if (-1 != m_fd)
        {
            close (m_fd);
        }
    }

    socket(const socket&) = delete;
    socket(const socket&&) = delete;

    socket(socket&& p_other)
        : m_fd(-1)
    {
        if (-1 != p_other.m_fd)
        {
            m_fd = p_other.m_fd;
            p_other.m_fd = -1;
        }
    }

    socket& operator=(const socket&& p_other) = delete;
    socket& operator=(socket&& p_other)
    {
        if (-1 != p_other.m_fd)
        {
            if (-1 != m_fd)
            {
                close(m_fd);
            }

            m_fd = p_other.m_fd;
            p_other.m_fd = -1;
        }
        return *this;
    }

    template <typename T>
    int bind(const T& addr)
    {
        return bind((const sockaddr*) &addr, sizeof(addr));
    }

    int bind(const sockaddr *p_addr, socklen_t p_size)
    {
        return ::bind(m_fd, p_addr, p_size);
    }

    template <typename T>
    ssize_t send(const T& p_data, int p_flags, const sockaddr* p_to, socklen_t p_to_size)
    {
        return sendto(m_fd, p_data.data(), p_data.size(), p_flags, p_to, p_to_size);
    }

    template <typename T>
    ssize_t send(const T& p_data, int p_flags = 0)
    {
        return ::send(m_fd, p_data.data(), p_data.size(), p_flags);
    }

    template <typename T>
    ssize_t recv(T&& p_data, int p_flags, sockaddr* p_addr, socklen_t* p_addr_sz)
    {
        return recvfrom(m_fd, p_data.data(), p_data.size(), p_flags, p_addr, p_addr_sz);
    }

    template <typename T>
    ssize_t recv(T&& p_data, int p_flags)
    {
        return ::recv(m_fd, p_data.data(), p_data.size(), p_flags);
    }

    int set_sock_opt(int p_level, int p_name, const void *p_value, socklen_t p_len)
    {
        return setsockopt(m_fd, p_level, p_name, p_value, p_len);
    }

    template <typename T>
    int set_sock_opt(int p_level, int p_name, T t)
    {
        return setsockopt(m_fd, p_level, p_name, (const void *) &t, sizeof(t));
    }

    int listen(int p_size=10)
    {
        return ::listen(m_fd, p_size);
    }

    socket accept(sockaddr* p_addr, socklen_t* p_addr_sz)
    {
        return socket(::accept(m_fd, p_addr, p_addr_sz));
    }

    template <typename T>
    socket accept(T& addr)
    {
        socklen_t sz = sizeof(addr);
        return socket(accept((sockaddr*) &addr, &sz));
    }

    int connect(const sockaddr* p_addr, socklen_t p_addr_sz)
    {
        return ::connect(m_fd, p_addr, p_addr_sz);
    }

    template <typename T>
    int connect(const T& addr)
    {
        return connect((const sockaddr*) &addr, sizeof(addr));
    }

    int fd() const
    {
        return m_fd;
    }

protected:
    int m_fd = -1;
};


} // namespace bfc

#endif // __BFC_SOCKET_HPP__
