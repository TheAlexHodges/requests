// Copyright (c) 2021 Klemens D. Morgenstern
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
#ifndef BOOST_REQUESTS_CONNECTION_HPP
#define BOOST_REQUESTS_CONNECTION_HPP

#include <boost/asem/guarded.hpp>
#include <boost/asem/st.hpp>
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/experimental/parallel_group.hpp>
#include <boost/asio/ip/basic_resolver.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/prepend.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/write.hpp>
#include <boost/beast/websocket/stream.hpp>
#include <boost/requests/body_traits.hpp>
#include <boost/requests/detail/ssl.hpp>
#include <boost/requests/detail/tracker.hpp>
#include <boost/requests/fields/keep_alive.hpp>
#include <boost/requests/redirect.hpp>
#include <boost/requests/request_options.hpp>
#include <boost/requests/request_settings.hpp>
#include <boost/requests/response.hpp>
#include <boost/requests/stream.hpp>
#include <boost/smart_ptr/allocate_unique.hpp>
#include <boost/url/url_view.hpp>

namespace boost {
namespace requests {

template<typename Stream>
struct basic_connection : private detail::stream_base
{
    /// The type of the next layer.
    typedef typename std::remove_reference<Stream>::type next_layer_type;

    /// The type of the executor associated with the object.
    typedef typename next_layer_type::executor_type executor_type;

    /// The type of the executor associated with the object.
    typedef typename next_layer_type::lowest_layer_type lowest_layer_type;

    /// Rebinds the socket type to another executor.
    template<typename Executor1>
    struct rebind_executor
    {
        /// The socket type when rebound to the specified executor.
        typedef basic_connection<typename next_layer_type::template rebind_executor<Executor1>::other> other;
    };

    /// Get the executor
    executor_type get_executor() noexcept
    {
        return next_layer_.get_executor();
    }
    /// Get the underlying stream
    const next_layer_type &next_layer() const noexcept
    {
        return next_layer_;
    }

    /// Get the underlying stream
    next_layer_type &next_layer() noexcept
    {
        return next_layer_;
    }

    /// The protocol-type of the lowest layer.
    using protocol_type = typename beast::lowest_layer_type<next_layer_type>::protocol_type;

    /// The endpoint of the lowest lowest layer.
    using endpoint_type = typename protocol_type::endpoint;

    /// Construct a stream.
    /**
     * @param args The arguments to be passed to initialise the underlying stream.
     *
     * Everything else will be default constructed
     */
    template<typename ... Args>
    explicit basic_connection(Args && ... args) : next_layer_(std::forward<Args>(args)...) {}

    basic_connection(basic_connection && ) noexcept = default;

    template<typename Other>
    basic_connection(basic_connection<Other> && lhs)
          : next_layer_(std::move(lhs.next_layer_))
          , read_mtx_(std::move(lhs.read_mtx_))
          , write_mtx_(std::move(lhs.write_mtx_))
          , host_(std::move(lhs.host_))
          , buffer_(std::move(lhs.buffer_))
          , ongoing_requests_(std::move(lhs.ongoing_requests_.load()))
          , keep_alive_set_(std::move(lhs.keep_alive_set_))
          , endpoint_(std::move(lhs.endpoint_))
    {}

    void connect(endpoint_type ep)
    {
      boost::system::error_code ec;
      connect(ep, ec);
      if (ec)
        urls::detail::throw_system_error(ec);
    }

    void connect(endpoint_type ep,
                 system::error_code & ec);

    template<BOOST_ASIO_COMPLETION_TOKEN_FOR(void (boost::system::error_code)) CompletionToken
                  BOOST_ASIO_DEFAULT_COMPLETION_TOKEN_TYPE(executor_type)>
    BOOST_ASIO_INITFN_AUTO_RESULT_TYPE(CompletionToken,
                                       void (boost::system::error_code))
    async_connect(endpoint_type ep,
                  CompletionToken && completion_token BOOST_ASIO_DEFAULT_COMPLETION_TOKEN(executor_type));

    void close()
    {
      boost::system::error_code ec;
      close(ec);
      if (ec)
        urls::detail::throw_system_error(ec);
    }

    void close(system::error_code & ec);

    template<BOOST_ASIO_COMPLETION_TOKEN_FOR(void (boost::system::error_code)) CompletionToken
                 BOOST_ASIO_DEFAULT_COMPLETION_TOKEN_TYPE(executor_type)>
    BOOST_ASIO_INITFN_AUTO_RESULT_TYPE(CompletionToken,
                                       void (boost::system::error_code))
    async_close(CompletionToken && completion_token BOOST_ASIO_DEFAULT_COMPLETION_TOKEN(executor_type));

    bool is_open() const final
    {
      return beast::get_lowest_layer(next_layer_).is_open();
    }

    // Endpoint
    endpoint_type endpoint() const {return endpoint_;}

    // Timeout of the connection-alive
    std::chrono::system_clock::time_point timeout() const
    {
      return keep_alive_set_.timeout;
    }

    std::size_t working_requests() const { return ongoing_requests_; }

    // Reserve memory for the internal buffer.
    void reserve(std::size_t size)
    {
      buffer_.reserve(size);
    }

    void set_host(core::string_view sv)
    {
      boost::system::error_code ec;
      set_host(sv, ec);
      if (ec)
        urls::detail::throw_system_error(ec);
    }

    void set_host(core::string_view sv, system::error_code & ec);
    core::string_view host() const {return host_;}
    constexpr static redirect_mode supported_redirect_mode() {return redirect_mode::endpoint;}

    using request_type = request_settings;

    using stream = basic_stream<executor_type>;

    template<typename RequestBody>
    auto ropen(beast::http::verb method,
               urls::url_view path,
               RequestBody && body,
               request_settings req,
               system::error_code & ec) -> stream;

    template<typename RequestBody>
    auto ropen(beast::http::verb method,
               urls::url_view path,
               RequestBody && body,
               request_settings req) -> stream
    {
      boost::system::error_code ec;
      auto res = ropen(method, path, std::move(body), std::move(req), ec);
      if (ec)
        throw_exception(system::system_error(ec, "open"));
      return res;
    }

    template<typename RequestBody>
    auto ropen(http::request<RequestBody> & req,
               request_options opt,
               cookie_jar * jar,
               system::error_code & ec) -> stream;

    template<typename RequestBody>
    auto ropen(beast::http::verb method,
               urls::url_view path,
               http::request<RequestBody> & req,
               request_options opt,
               cookie_jar * jar) -> stream
    {
      boost::system::error_code ec;
      auto res = ropen(method, path, req, opt, jar, ec);
      if (ec)
        throw_exception(system::system_error(ec, "open"));
      return res;
    }

    template<typename RequestBody,
             typename CompletionToken
                  BOOST_ASIO_DEFAULT_COMPLETION_TOKEN_TYPE(executor_type)>
    BOOST_ASIO_INITFN_AUTO_RESULT_TYPE(CompletionToken,
                                       void (boost::system::error_code,
                                             typename basic_connection<Stream>::stream))
    async_ropen(beast::http::verb method,
                urls::url_view path,
                RequestBody && body,
                request_settings req,
                CompletionToken && completion_token BOOST_ASIO_DEFAULT_COMPLETION_TOKEN(executor_type));

    template<typename RequestBody,
              typename CompletionToken
                  BOOST_ASIO_DEFAULT_COMPLETION_TOKEN_TYPE(executor_type)>
    BOOST_ASIO_INITFN_AUTO_RESULT_TYPE(CompletionToken,
                                       void (boost::system::error_code,
                                            typename basic_connection<Stream>::stream))
    async_ropen(http::request<RequestBody> & req,
                request_options opt,
                cookie_jar * jar = nullptr,
                CompletionToken && completion_token BOOST_ASIO_DEFAULT_COMPLETION_TOKEN(executor_type));

    template<typename CompletionToken
             BOOST_ASIO_DEFAULT_COMPLETION_TOKEN_TYPE(executor_type)>
    BOOST_ASIO_INITFN_AUTO_RESULT_TYPE(CompletionToken,
                                       void (boost::system::error_code, typename basic_connection<Stream>::stream))
    async_ropen(http::request<http::empty_body> & req,
                request_options opt,
                cookie_jar * jar = nullptr,
                CompletionToken && completion_token BOOST_ASIO_DEFAULT_COMPLETION_TOKEN(executor_type));

    template<typename CompletionToken
             BOOST_ASIO_DEFAULT_COMPLETION_TOKEN_TYPE(executor_type)>
    BOOST_ASIO_INITFN_AUTO_RESULT_TYPE(CompletionToken,
                                       void (boost::system::error_code, typename basic_connection<Stream>::stream))
    async_ropen(http::request<http::file_body> & req,
                request_options opt,
                cookie_jar * jar = nullptr,
                CompletionToken && completion_token BOOST_ASIO_DEFAULT_COMPLETION_TOKEN(executor_type));

    template<typename CompletionToken
             BOOST_ASIO_DEFAULT_COMPLETION_TOKEN_TYPE(executor_type)>
    BOOST_ASIO_INITFN_AUTO_RESULT_TYPE(CompletionToken,
                                       void (boost::system::error_code, typename basic_connection<Stream>::stream))
    async_ropen(http::request<http::string_body> & req,
                request_options opt,
                cookie_jar * jar = nullptr,
                CompletionToken && completion_token BOOST_ASIO_DEFAULT_COMPLETION_TOKEN(executor_type));

    auto ropen(http::request<http::empty_body>  & req, request_options opt, cookie_jar * jar, system::error_code & ec) -> stream;
    auto ropen(http::request<http::file_body>   & req, request_options opt, cookie_jar * jar, system::error_code & ec) -> stream;
    auto ropen(http::request<http::string_body> & req, request_options opt, cookie_jar * jar, system::error_code & ec) -> stream;

  private:
    template<typename >
    friend struct basic_connection;

    template<typename Body>
    void write_impl(http::request<Body> & req,
                    asem::lock_guard<detail::basic_mutex<executor_type>> & read_lock,
                    system::error_code & ec);

    Stream next_layer_;
    detail::basic_mutex<executor_type>
            read_mtx_{next_layer_.get_executor()},
            write_mtx_{next_layer_.get_executor()};

    std::string host_;
    beast::flat_buffer buffer_;
    std::atomic<std::size_t> ongoing_requests_{0u};
    keep_alive keep_alive_set_;
    endpoint_type endpoint_;

    struct async_close_op;
    struct async_connect_op;

    template<typename RequestBody>
    struct async_ropen_op;

    struct async_ropen_file_op;
    struct async_ropen_string_op;
    struct async_ropen_empty_op;

    template<typename Body> async_ropen_op<Body> pick_ropen_op(Body * );
    async_ropen_file_op   pick_ropen_op(http::file_body   *);
    async_ropen_string_op pick_ropen_op(http::string_body *);
    async_ropen_empty_op  pick_ropen_op(http::empty_body  *);

    std::size_t do_read_some_(beast::http::basic_parser<false> & parser) final;
    std::size_t do_read_some_(beast::http::basic_parser<false> & parser, system::error_code & ec) final;
    void do_async_read_some_(beast::http::basic_parser<false> & parser, detail::co_token_t<void(system::error_code, std::size_t)>) final;

    void do_close_(system::error_code & ec) final;
    void do_async_close_(detail::co_token_t<void(system::error_code)>) final;

    keep_alive & get_keep_alive_set_() final
    {
      return keep_alive_set_;
    };
};

template<typename Executor = asio::any_io_executor>
using basic_http_connection  = basic_connection<asio::basic_stream_socket<asio::ip::tcp, Executor>>;

template<typename Executor = asio::any_io_executor>
using basic_https_connection = basic_connection<asio::ssl::stream<asio::basic_stream_socket<asio::ip::tcp, Executor>>>;


using http_connection  = basic_http_connection<>;
using https_connection = basic_https_connection<>;

#if !defined(BOOST_REQUESTS_HEADER_ONLY)
extern template struct basic_connection<asio::ip::tcp::socket>;
extern template struct basic_connection<asio::ssl::stream<asio::ip::tcp::socket>>;
#endif

}
}

#include <boost/requests/impl/connection.hpp>

#endif //BOOST_REQUESTS_CONNECTION_HPP
