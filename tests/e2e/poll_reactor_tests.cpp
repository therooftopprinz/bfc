#include <gtest/gtest.h>
#include <bfc/poll_reactor.hpp>
#include <bfc/socket.hpp>
#include <atomic>
#include <thread>
#include <netinet/tcp.h>

using namespace bfc;

using r_cb_t = std::function<void()>;
using reactor_t = bfc::poll_reactor<r_cb_t>;

struct counters_t
{
    uint64_t server_read = 0;
    uint64_t server_write = 0;
    uint64_t client_read = 0;
    uint64_t client_write = 0;
};

constexpr uint64_t N = 100000;

template <typename T=std::chrono::microseconds>
static uint64_t now()
{
    return std::chrono::duration_cast<T>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
}

TEST(poll_reactor, non_reactive_st)
{
    bfc::socket acceptor(create_tcp4());
    bfc::socket client(create_tcp4());
    bfc::socket server;

    acceptor.set_sock_opt(SOL_SOCKET , SO_REUSEADDR, 1);
    ASSERT_NE(-1, acceptor.bind(ip4_port_to_sockaddr(localhost4, 12345)));
    ASSERT_NE(-1, acceptor.listen());

    ASSERT_NE(-1, client.connect(ip4_port_to_sockaddr(localhost4, 12345)));
    printf("client: connected!\n");

    server = std::move(acceptor.accept(nullptr, nullptr));
    printf("server: accepted!\n");

    uint64_t total_latency = 0;
    uint64_t lo_latency = std::numeric_limits<uint64_t>::max();
    uint64_t hi_latency = std::numeric_limits<uint64_t>::min();

    auto t_start = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    for (uint64_t i=0; i < N; i++)
    {
        {
            uint64_t b = now();
            buffer_view wb((std::byte*) &b, sizeof(b));
            ASSERT_NE(-1, client.send(wb, 0));
        }
        {
            uint64_t b;
            buffer_view rb((std::byte*) &b, sizeof(b));
            auto lat = uint64_t((now()-b));
            if (lat > hi_latency) hi_latency = lat;
            if (lat < lo_latency) lo_latency = lat;
            total_latency += lat;
            ASSERT_NE(-1, server.recv(rb, 0));
        }
    }
    auto t_end = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();

    auto t_diff = (t_end - t_start);
    auto tput = double(N) * 1000 * 1000 * 1000 / t_diff;
    tput /= 1000000;
    auto ave_lat = double(total_latency)/N;
    printf("tput_meghz: %lf latency_us: %lf hi_latency_us: %zu lo_latency_us: %zu\n", tput, ave_lat, hi_latency, lo_latency);
}

TEST(poll_reactor, non_reactive_mt)
{
    bfc::socket acceptor(create_tcp4());
    bfc::socket client(create_tcp4());
    bfc::socket server;

    acceptor.set_sock_opt(SOL_SOCKET , SO_REUSEADDR, 1);
    ASSERT_NE(-1, acceptor.bind(ip4_port_to_sockaddr(localhost4, 12345)));
    ASSERT_NE(-1, acceptor.listen());

    ASSERT_NE(-1, client.connect(ip4_port_to_sockaddr(localhost4, 12345)));
    printf("client: connected!\n");

    server = std::move(acceptor.accept(nullptr, nullptr));
    printf("server: accepted!\n");

    std::atomic_bool start = false;
    std::thread sender = std::thread([&](){
        start = true;
        for (uint64_t i=0; i < N; i++)
        {
            uint64_t b = now();
            buffer_view wb((std::byte*) &b, sizeof(b));
            ASSERT_NE(-1, client.send(wb, 0));
        }
    });

    while(!start);

    uint64_t total_latency = 0;
    uint64_t lo_latency = std::numeric_limits<uint64_t>::max();
    uint64_t hi_latency = std::numeric_limits<uint64_t>::min();

    auto t_start = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    for (uint64_t i=0; i < N; i++)
    {
        uint64_t b;
        buffer_view rb((std::byte*) &b, sizeof(b));
        ASSERT_NE(-1, server.recv(rb, 0));
        auto lat = uint64_t((now()-b));
        if (lat > hi_latency) hi_latency = lat;
        if (lat < lo_latency) lo_latency = lat;
        total_latency += lat;
    }
    auto t_end = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();

    sender.join();

    auto t_diff = (t_end - t_start);
    auto tput = double(N) * 1000 * 1000 * 1000 / t_diff;
    tput /= 1000000;
    auto ave_lat = double(total_latency)/N;
    printf("tput_meghz: %lf latency_us: %lf hi_latency_us: %zu lo_latency_us: %zu\n", tput, ave_lat, hi_latency, lo_latency);
}

TEST(poll_reactor, reactive_read)
{
    reactor_t reactor;
    bfc::socket acceptor(create_tcp4());
    bfc::socket client(create_tcp4());
    bfc::socket server;

    acceptor.set_sock_opt(SOL_SOCKET , SO_REUSEADDR, 1);
    ASSERT_NE(-1, acceptor.bind(ip4_port_to_sockaddr(localhost4, 12345)));
    ASSERT_NE(-1, acceptor.listen());

    ASSERT_NE(-1, client.connect(ip4_port_to_sockaddr(localhost4, 12345)));
    printf("client: connected!\n");

    server = std::move(acceptor.accept(nullptr, nullptr));
    printf("server: accepted!\n");

    std::atomic_bool start = false;
    std::thread sender = std::thread([&](){
        start = true;
        for (uint64_t i=0; i < N; i++)
        {
            uint64_t b = now();
            buffer_view wb((std::byte*) &b, sizeof(b));
            ASSERT_NE(-1, client.send(wb, 0));
        }
    });

    uint64_t total_latency = 0;
    uint64_t lo_latency = std::numeric_limits<uint64_t>::max();
    uint64_t hi_latency = std::numeric_limits<uint64_t>::min();

    uint64_t rcx = 0;
    ASSERT_NE(-1, reactor.add_read_rdy(server.fd(), [&](){
            uint64_t b;
            buffer_view rb((std::byte*) &b, sizeof(b));
            ASSERT_NE(-1, server.recv(rb, 0));

            auto lat = uint64_t((now()-b));
            if (lat > hi_latency) hi_latency = lat;
            if (lat < lo_latency) lo_latency = lat;
            total_latency += lat;

            if (rcx >= (N-1))
            {
                reactor.stop();
            }
            rcx++;
        }));

    while(!start);
    auto t_start = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    reactor.run();
    auto t_end = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();

    sender.join();

    auto t_diff = (t_end - t_start);
    auto tput = double(N) * 1000 * 1000 * 1000 / t_diff;
    tput /= 1000000;
    auto ave_lat = double(total_latency)/N;
    printf("tput_meghz: %lf latency_us: %lf hi_latency_us: %zu lo_latency_us: %zu\n", tput, ave_lat, hi_latency, lo_latency);
}

TEST(poll_reactor, reactive_write)
{
    reactor_t reactor;
    bfc::socket acceptor(create_tcp4());
    bfc::socket client(create_tcp4());
    bfc::socket server;

    acceptor.set_sock_opt(SOL_SOCKET , SO_REUSEADDR, 1);
    ASSERT_NE(-1, acceptor.bind(ip4_port_to_sockaddr(localhost4, 12345)));
    ASSERT_NE(-1, acceptor.listen());

    ASSERT_NE(-1, client.connect(ip4_port_to_sockaddr(localhost4, 12345)));
    printf("client: connected!\n");

    server = std::move(acceptor.accept(nullptr, nullptr));
    printf("server: accepted!\n");

    std::thread receiver = std::thread([&](){
        for (uint64_t i=0; i < N; i++)
        {
            uint64_t b = i;
            buffer_view wb((std::byte*) &b, sizeof(b));
            ASSERT_NE(-1, server.recv(wb, 0));
        }
        reactor.stop();
    });

    uint64_t i=0;
    r_cb_t on_client_write_rdy = [&reactor, &client, &i](){
            uint64_t b = i;
            buffer_view wb((std::byte*) &b, sizeof(b));
            ASSERT_NE(-1, client.send(wb, 0));
            i++;
            if (i >= N)
            {
                return;
            }
            ASSERT_NE(false, reactor.req_write(client.fd()));
        };

    reactor.add_write_rdy(client.fd(), on_client_write_rdy);
    reactor.req_write(client.fd());

    auto t_start = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    reactor.run();
    auto t_end = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();

    receiver.join();

    auto t_diff = (t_end - t_start);
    auto tput = double(N) * 1000 * 1000 * 1000 / t_diff;
    tput /= 1000000;

    printf("tput: %lf\n", tput);
}

TEST(poll_reactor, reactive)
{
    reactor_t reactor;
    bfc::socket acceptor(create_tcp4());
    bfc::socket client(create_tcp4());
    bfc::socket server;

    counters_t ctrs;

    acceptor.set_sock_opt(SOL_SOCKET , SO_REUSEADDR, 1);
    ASSERT_NE(-1, acceptor.bind(ip4_port_to_sockaddr(localhost4, 12345)));
    ASSERT_NE(-1, acceptor.listen());

    r_cb_t on_server_read_rdy = [&reactor, &server, &ctrs](){
            uint64_t b;
            buffer_view rb((std::byte*) &b, sizeof(b));
            ASSERT_NE(-1, server.recv(rb, 0));
            ctrs.server_read++;
            if (ctrs.server_read >= N)
            {
                reactor.stop();
            }
        };

    r_cb_t on_client_write_rdy = [&reactor, &client, &ctrs](){
            uint64_t b = ctrs.client_write;
            buffer_view wb((std::byte*) &b, sizeof(b));
            ASSERT_NE(-1, client.send(wb, 0));
            ctrs.client_write++;
            if (ctrs.client_write >= N)
            {
                return;
            }

            ASSERT_NE(-1, reactor.req_write(client.fd()));
        };

    ASSERT_NE(-1, client.connect(ip4_port_to_sockaddr(localhost4, 12345)));
    printf("client: connected!\n");
    server = std::move(acceptor.accept(nullptr, nullptr));
    printf("server: accepted!\n");
    ASSERT_NE(-1, server.fd());

    ASSERT_NE(false, reactor.add_read_rdy(server.fd(), on_server_read_rdy));

    reactor.add_write_rdy(client.fd(), on_client_write_rdy);
    reactor.req_write(client.fd());

    auto t_start = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    reactor.run();
    auto t_end = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();

    auto t_diff = (t_end - t_start);
    auto tput = double(N) * 1000 * 1000 * 1000 / t_diff;
    tput /= 1000000;

    printf("counters.send: %zu\n", ctrs.client_write);
    printf("counters.recv: %zu\n", ctrs.server_read);
    printf("tput: %lf\n", tput);
}
