#include <cassert>
#include <chrono>
#include <functional>
#include <iostream>
#include <thread>
#include "../src/Adapters/Network/BoundedHttpsTransport.hpp"

using SmokerController::Adapters::Network::BoundedHttpsTransport;

void waitFor(const std::function<bool()>& predicate) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(1);
    while(!predicate() && std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(predicate());
}
bool start(BoundedHttpsTransport& transport) { return transport.start("certificate",12,"private-key",12,443); }

int main() {
    {
        BoundedHttpsTransport transport;
        assert(WebSpy::tasks==0 && WebSpy::tls_live==0);
        WebSpy::fail_semaphore=true; assert(!start(transport)); WebSpy::fail_semaphore=false;
        WebSpy::fail_task=true; assert(!start(transport)); WebSpy::fail_task=false;
        fail_httpd_start=true; assert(!start(transport)); fail_httpd_start=false;
        waitFor([] { return WebSpy::tasks==0; });
        assert(transport.server()==nullptr);
        assert(start(transport));
        assert(httpd_settings.max_open_sockets==2 && httpd_settings.server_port==443);
        auto handle=transport.server();
        auto* server=static_cast<ServerSpy*>(handle);

        // The absolute bound is armed before entering a blocked TLS handshake.
        WebSpy::handshake_hook=[](int fd) {
            WebSpy::now_us+=3000001;
            waitFor([fd] { return WebSpy::shutdown_count(fd)>0; });
        };
        assert(open_session(handle,10)==ESP_FAIL);
        WebSpy::handshake_hook={};
        assert(WebSpy::close_count(10)==1 && WebSpy::tls_live==0);
        assert(std::string(reinterpret_cast<const char*>(WebSpy::tls_config.servercert_buf))=="certificate");
        assert(std::string(reinterpret_cast<const char*>(WebSpy::tls_config.serverkey_buf))=="private-key");

        WebSpy::fail_tls_init=true;
        assert(open_session(handle,11)==ESP_FAIL); WebSpy::fail_tls_init=false;
        assert(WebSpy::close_count(11)==1 && WebSpy::tls_live==0);
        WebSpy::fail_handshake=true;
        assert(open_session(handle,12)==ESP_FAIL); WebSpy::fail_handshake=false;
        assert(WebSpy::close_count(12)==1 && WebSpy::tls_live==0);

        // HTTPD uses the same receive override for headers and request bodies.
        // Successful partial reads must never extend the absolute deadline.
        WebSpy::now_us=0;
        assert(open_session(handle,20)==ESP_OK);
        char byte{};
        for(int64_t now : {1000000,2000000,2999999}) {
            WebSpy::now_us=now;
            assert(server->sessions.at(20).recv(handle,20,&byte,1,0)==1);
            assert(byte == 'x');
        }
        WebSpy::now_us=3000000;
        assert(server->sessions.at(20).recv(handle,20,&byte,1,0)==HTTPD_SOCK_ERR_TIMEOUT);
        waitFor([] { return WebSpy::shutdown_count(20)>0; });
        close_session(handle,20);
        assert(WebSpy::close_count(20)==1);

        // A recycled slot must relinquish its previous established ownership
        // before any allocation or handshake failure on the next connection.
        WebSpy::now_us=0;
        WebSpy::fail_tls_init=true;
        assert(open_session(handle,24)==ESP_FAIL); WebSpy::fail_tls_init=false;
        assert(WebSpy::close_count(24)==1 && WebSpy::tls_live==0);
        WebSpy::fail_handshake=true;
        assert(open_session(handle,25)==ESP_FAIL); WebSpy::fail_handshake=false;
        assert(WebSpy::close_count(25)==1 && WebSpy::tls_live==0);
        assert(open_session(handle,26)==ESP_OK);
        close_session(handle,26);
        assert(WebSpy::close_count(26)==1 && WebSpy::tls_live==0);

        WebSpy::now_us=0;
        assert(open_session(handle,21)==ESP_OK);
        WebSpy::read_hook=[](int fd) {
            WebSpy::now_us=3000001;
            waitFor([fd] { return WebSpy::shutdown_count(fd)>0; });
        };
        assert(server->sessions.at(21).recv(handle,21,&byte,1,0)==HTTPD_SOCK_ERR_TIMEOUT);
        WebSpy::read_hook={};
        close_session(handle,21);

        // Exercise the actual installed I/O hooks, including binary content and
        // short writes. Response spies alone do not prove TLS forwarding.
        WebSpy::now_us=0;
        assert(open_session(handle,22)==ESP_OK);
        const std::string payload("reply\0bytes",11);
        const auto send=server->sessions.at(22).send;
        assert(send(handle,22,payload.data(),payload.size(),0)==static_cast<int>(payload.size()));
        assert(WebSpy::write_fd==22 && WebSpy::written_bytes==payload);
        WebSpy::write_hook=[](int,const char*,size_t) { return 3; };
        assert(send(handle,22,payload.data(),payload.size(),0)==3);
        WebSpy::write_hook=[](int,const char*,size_t) { return -1; };
        assert(send(handle,22,payload.data(),payload.size(),0)==HTTPD_SOCK_ERR_FAIL);
        WebSpy::write_hook={};
        WebSpy::read_bytes=std::string("request\0body",12);
        char input[32]{};
        assert(server->sessions.at(22).recv(handle,22,input,sizeof(input),0)==12);
        assert(std::string(input,12)==WebSpy::read_bytes);
        WebSpy::read_bytes="x";
        WebSpy::now_us=3000000;
        const auto writes_before_expiry=WebSpy::writes;
        assert(send(handle,22,payload.data(),payload.size(),0)==HTTPD_SOCK_ERR_TIMEOUT);
        assert(WebSpy::writes==writes_before_expiry);
        close_session(handle,22);

        WebSpy::now_us=0;
        assert(open_session(handle,23)==ESP_OK);
        WebSpy::write_hook=[](int fd,const char*,size_t) {
            WebSpy::now_us=3000001;
            waitFor([fd] { return WebSpy::shutdown_count(fd)>0; });
            return -1;
        };
        assert(server->sessions.at(23).send(handle,23,payload.data(),payload.size(),0)==HTTPD_SOCK_ERR_TIMEOUT);
        WebSpy::write_hook={};
        close_session(handle,23);

        // Closing an old connection invalidates its watchdog entry before the
        // descriptor can be reused; the replacement receives its own deadline.
        WebSpy::now_us=0;
        assert(open_session(handle,30)==ESP_OK);
        WebSpy::now_us=1000000;
        close_session(handle,30);
        assert(open_session(handle,30)==ESP_OK);
        WebSpy::now_us=3000001;
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        assert(WebSpy::shutdown_count(30)==0);
        WebSpy::now_us=4000000;
        waitFor([] { return WebSpy::shutdown_count(30)>0; });
        close_session(handle,30);
        assert(WebSpy::close_count(30)==2);

        WebSpy::now_us=5000000;
        assert(open_session(handle,40)==ESP_OK);
        assert(open_session(handle,41)==ESP_OK);
        assert(open_session(handle,42)==ESP_FAIL);
        assert(WebSpy::tls_live==2 && WebSpy::close_count(42)==1);
        // Destructor must wake active I/O, close each TLS session once, and join
        // the watchdog before releasing its owner and session array.
    }
    waitFor([] { return WebSpy::tasks==0; });
    assert(WebSpy::tls_live==0);
    assert(WebSpy::close_count(40)==1 && WebSpy::close_count(41)==1);
    assert(WebSpy::shutdown_count(40)>0 && WebSpy::shutdown_count(41)>0);
    const auto prior=WebSpy::shutdown_count(40);
    WebSpy::now_us+=10000000;
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    assert(WebSpy::shutdown_count(40)==prior);
    std::cout << "HTTPS transport deadline, cleanup and descriptor ownership checks passed\n";
}
