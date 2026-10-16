#include <gtest/gtest.h>
#include <bfc/epoll_reactor.hpp>
#include <bfc/socket.hpp>
#include <atomic>
#include <chrono>
#include <thread>
#include <netinet/tcp.h>
#include <sys/eventfd.h>
#include <unistd.h>

// #undef ASSERT_NE
// #define ASSERT_NE(A, B) B

using namespace bfc;

using r_cb_t = std::function<void()>;
using reactor_t = bfc::epoll_reactor<r_cb_t>;

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

TEST(epoll_reactor, non_reactive_st)
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

    // server.set_sock_opt(IPPROTO_TCP, TCP_NODELAY, int(1));
    // client.set_sock_opt(IPPROTO_TCP, TCP_NODELAY, int(1));

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

TEST(epoll_reactor, non_reactive_mt)
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

    // server.set_sock_opt(IPPROTO_TCP, TCP_NODELAY, int(1));
    // client.set_sock_opt(IPPROTO_TCP, TCP_NODELAY, int(1));

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

TEST(epoll_reactor, reactive_read)
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

    // server.set_sock_opt(IPPROTO_TCP, TCP_NODELAY, int(1));
    // client.set_sock_opt(IPPROTO_TCP, TCP_NODELAY, int(1));

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

TEST(epoll_reactor, reactive_write)
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

    // server.set_sock_opt(IPPROTO_TCP, TCP_NODELAY, int(1));
    // client.set_sock_opt(IPPROTO_TCP, TCP_NODELAY, int(1));

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
                // printf("client: stop!\n");
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

TEST(epoll_reactor, reactive)
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
            // printf("server: read! v=%zu\n", b);
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
            // printf("client: write! v=%zu\n", ctrs.client_write);
            ctrs.client_write++;
            if (ctrs.client_write >= N)
            {
                // printf("client: stop!\n");
                return;
            }

            // printf("client: wreq!\n");
            ASSERT_NE(-1, reactor.req_write(client.fd()));
        };

    ASSERT_NE(-1, client.connect(ip4_port_to_sockaddr(localhost4, 12345)));
    printf("client: connected!\n");
    server = std::move(acceptor.accept(nullptr, nullptr));
    printf("server: accepted!\n");
    ASSERT_NE(-1, server.fd());

    // server.set_sock_opt(IPPROTO_TCP, TCP_NODELAY, int(1));
    // client.set_sock_opt(IPPROTO_TCP, TCP_NODELAY, int(1));

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

namespace {

constexpr uint16_t rem_test_port = 12347;

static bool wait_until(const std::atomic<bool>& flag, std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!flag.load(std::memory_order_acquire))
    {
        if (std::chrono::steady_clock::now() >= deadline)
        {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

static void connect_tcp_pair(bfc::socket& server, bfc::socket& client)
{
    bfc::socket acceptor(create_tcp4());
    acceptor.set_sock_opt(SOL_SOCKET, SO_REUSEADDR, 1);
    ASSERT_NE(-1, acceptor.bind(ip4_port_to_sockaddr(localhost4, rem_test_port)));
    ASSERT_NE(-1, acceptor.listen());
    ASSERT_NE(-1, client.connect(ip4_port_to_sockaddr(localhost4, rem_test_port)));
    server = std::move(acceptor.accept(nullptr, nullptr));
    ASSERT_NE(-1, server.fd());
    ASSERT_NE(-1, client.fd());
}

} // namespace

TEST(epoll_reactor, rem_read_rdy_invokes_done_cb_on_reactor_thread)
{
    reactor_t reactor;
    bfc::socket client(create_tcp4());
    bfc::socket server;
    connect_tcp_pair(server, client);

    std::atomic<bool> done_cb_called{false};
    std::thread::id reactor_thread_id;
    std::thread::id done_cb_thread_id;

    std::thread reactor_thread([&](){
        reactor_thread_id = std::this_thread::get_id();
        reactor.run();
    });

    ASSERT_TRUE(reactor.add_read_rdy(server.fd(), [](){}));
    ASSERT_TRUE(reactor.rem_read_rdy(server.fd(), [&](){
        done_cb_called.store(true, std::memory_order_release);
        done_cb_thread_id = std::this_thread::get_id();
        reactor.stop();
    }));

    ASSERT_TRUE(wait_until(done_cb_called, std::chrono::seconds(5)));
    reactor_thread.join();

    EXPECT_EQ(done_cb_thread_id, reactor_thread_id);
}

TEST(epoll_reactor, rem_read_rdy_suppresses_further_notifications)
{
    reactor_t reactor;
    bfc::socket client(create_tcp4());
    bfc::socket server;
    connect_tcp_pair(server, client);

    std::atomic<uint64_t> read_count{0};
    std::atomic<bool> removed{false};

    std::thread reactor_thread([&](){
        reactor.run();
    });

    ASSERT_TRUE(reactor.add_read_rdy(server.fd(), [&](){
        read_count.fetch_add(1, std::memory_order_relaxed);
    }));

    ASSERT_TRUE(reactor.rem_read_rdy(server.fd(), [&](){
        removed.store(true, std::memory_order_release);
    }));
    ASSERT_TRUE(wait_until(removed, std::chrono::seconds(5)));

    uint64_t payload = 42;
    buffer_view wb((std::byte*)&payload, sizeof(payload));
    ASSERT_NE(-1, client.send(wb, 0));

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    reactor.stop();
    reactor_thread.join();

    EXPECT_EQ(read_count.load(), 0u);
}

TEST(epoll_reactor, rem_write_rdy_invokes_done_cb_on_reactor_thread)
{
    reactor_t reactor;
    bfc::socket client(create_tcp4());
    bfc::socket server;
    connect_tcp_pair(server, client);

    std::atomic<bool> done_cb_called{false};
    std::thread::id reactor_thread_id;
    std::thread::id done_cb_thread_id;

    std::thread reactor_thread([&](){
        reactor_thread_id = std::this_thread::get_id();
        reactor.run();
    });

    ASSERT_TRUE(reactor.add_write_rdy(client.fd(), [](){}));
    ASSERT_TRUE(reactor.rem_write_rdy(client.fd(), [&](){
        done_cb_called.store(true, std::memory_order_release);
        done_cb_thread_id = std::this_thread::get_id();
        reactor.stop();
    }));

    ASSERT_TRUE(wait_until(done_cb_called, std::chrono::seconds(5)));
    reactor_thread.join();

    EXPECT_EQ(done_cb_thread_id, reactor_thread_id);
}

TEST(epoll_reactor, is_reactor_thread)
{
    reactor_t reactor;
    bfc::socket client(create_tcp4());
    bfc::socket server;
    connect_tcp_pair(server, client);

    EXPECT_FALSE(reactor.is_reactor_thread());

    std::atomic<bool> checked_in_callback{false};

    ASSERT_TRUE(reactor.add_read_rdy(server.fd(), [&](){
        EXPECT_TRUE(reactor.is_reactor_thread());
        checked_in_callback.store(true, std::memory_order_release);
        reactor.stop();
    }));

    std::thread reactor_thread([&](){
        reactor.run();
    });

    uint64_t payload = 1;
    buffer_view wb((std::byte*)&payload, sizeof(payload));
    ASSERT_NE(-1, client.send(wb, 0));

    ASSERT_TRUE(wait_until(checked_in_callback, std::chrono::seconds(5)));
    reactor_thread.join();

    EXPECT_FALSE(reactor.is_reactor_thread());
}

TEST(epoll_reactor, wake_up_cb_runs_on_reactor_thread)
{
    reactor_t reactor;

    std::atomic<bool> done_cb_called{false};
    std::thread::id reactor_thread_id;
    std::thread::id done_cb_thread_id;

    std::thread reactor_thread([&](){
        reactor_thread_id = std::this_thread::get_id();
        reactor.run();
    });

    reactor.wake_up([&](){
        EXPECT_TRUE(reactor.is_reactor_thread());
        done_cb_called.store(true, std::memory_order_release);
        done_cb_thread_id = std::this_thread::get_id();
        reactor.stop();
    });

    ASSERT_TRUE(wait_until(done_cb_called, std::chrono::seconds(5)));
    reactor_thread.join();

    EXPECT_EQ(done_cb_thread_id, reactor_thread_id);
}

TEST(epoll_reactor, sub_millisecond_timer_chain)
{
    reactor_t reactor;
    auto t0 = std::chrono::steady_clock::now();
    int fired = 0;
    for (int i = 0; i < 100; ++i)
    {
        reactor.get_timer().wait_us(250 * int64_t(i + 1), [&](){ ++fired; });
    }
    reactor.get_timer().wait_us(250 * 100 + 500, [&](){ reactor.stop(); });
    reactor.run();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0);
    EXPECT_EQ(fired, 100);
    // 25 ms of timers; generous bound for loaded machines
    EXPECT_LT(elapsed.count(), 80);
}

TEST(epoll_reactor, wait_ms_not_early)
{
    reactor_t reactor;
    auto t0 = std::chrono::steady_clock::now();
    reactor.get_timer().wait_ms(10, [&](){ reactor.stop(); });
    reactor.run();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0);
    EXPECT_GE(elapsed.count(), 10);
}

static int make_readable_eventfd()
{
    int fd = eventfd(0, EFD_NONBLOCK);
    uint64_t one = 1;
    auto res [[maybe_unused]] = write(fd, &one, sizeof(one));
    return fd;
}

// Two fds become readable in the same epoll batch and whichever callback runs
// first removes and closes the other. The removed fd's callback must not run
// afterwards: its owner may already be destroyed.
TEST(epoll_reactor, rem_read_rdy_from_callback_suppresses_pending_event)
{
    reactor_t reactor;
    int fds[2] = {make_readable_eventfd(), make_readable_eventfd()};
    ASSERT_GE(fds[0], 0);
    ASSERT_GE(fds[1], 0);
    bool removed[2] = {false, false};
    int calls_after_removal = 0;
    int calls = 0;

    for (int i = 0; i < 2; ++i)
    {
        ASSERT_TRUE(reactor.add_read_rdy(fds[i], [&, i](){
            if (removed[i])
            {
                ++calls_after_removal;
                return;
            }
            ++calls;
            int other = 1 - i;
            if (!removed[other])
            {
                reactor.rem_read_rdy(fds[other]);
                close(fds[other]);
                removed[other] = true;
            }
            uint64_t v;
            auto res [[maybe_unused]] = read(fds[i], &v, sizeof(v));
        }));
    }
    reactor.get_timer().wait_ms(20, [&](){ reactor.stop(); });
    reactor.run();

    EXPECT_EQ(calls, 1);
    EXPECT_EQ(calls_after_removal, 0);
    for (int i = 0; i < 2; ++i)
    {
        if (!removed[i])
        {
            reactor.rem_read_rdy(fds[i]);
            close(fds[i]);
        }
    }
}

// Removing an fd, closing it and registering a new fd that reuses the same
// number from one callback must leave the new registration armed.
TEST(epoll_reactor, rem_then_add_same_fd_number_stays_armed)
{
    reactor_t reactor;
    int trigger = make_readable_eventfd();
    int victim = eventfd(0, EFD_NONBLOCK);
    ASSERT_GE(trigger, 0);
    ASSERT_GE(victim, 0);
    ASSERT_TRUE(reactor.add_read_rdy(victim, [](){}));

    int replacement = -1;
    bool replacement_fired = false;
    ASSERT_TRUE(reactor.add_read_rdy(trigger, [&](){
        uint64_t v;
        auto res [[maybe_unused]] = read(trigger, &v, sizeof(v));
        if (replacement != -1)
        {
            return;
        }
        reactor.rem_read_rdy(victim);
        close(victim);
        replacement = eventfd(0, EFD_NONBLOCK);
        ASSERT_EQ(replacement, victim);
        ASSERT_TRUE(reactor.add_read_rdy(replacement, [&](){
            uint64_t r;
            if (read(replacement, &r, sizeof(r)) > 0)
            {
                replacement_fired = true;
                reactor.stop();
            }
        }));
        // Make the replacement readable only after the reactor has run its
        // deferred work for this iteration.
        reactor.get_timer().wait_ms(5, [&](){
            uint64_t one = 1;
            auto w [[maybe_unused]] = write(replacement, &one, sizeof(one));
        });
    }));
    reactor.get_timer().wait_ms(200, [&](){ reactor.stop(); });
    reactor.run();

    EXPECT_TRUE(replacement_fired);
    reactor.rem_read_rdy(trigger);
    close(trigger);
    if (replacement != -1)
    {
        reactor.rem_read_rdy(replacement);
        close(replacement);
    }
}

TEST(epoll_reactor, minimum_timer_step)
{
#if defined(BFC_EPOLL_PWAIT2)
    static_assert(reactor_t::minimum_timer_step() == 1);
#else
    static_assert(reactor_t::minimum_timer_step() == 1000);
#endif
}
