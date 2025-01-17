// Copyright (c) 2021 Klemens D. Morgenstern
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
#ifndef BOOST_REQUESTS_POOL_HPP
#define BOOST_REQUESTS_POOL_HPP

#include <boost/requests/connection.hpp>
#include <boost/asem/st.hpp>
#include <list>
#include <boost/blank.hpp>
#include <boost/core/empty_value.hpp>

namespace boost {
namespace requests {

namespace detail {

template<typename T>
struct endpoint_hash;

template<typename Protocol>
struct endpoint_hash<asio::ip::basic_endpoint<Protocol>>
{
  std::size_t operator()(const asio::ip::basic_endpoint<Protocol> & be) const
  {
    const auto a = be.address();
    std::size_t res{be.port()};
    hash_combine(res,
                 a.is_v6()
                  ? hash_value(a.to_v6().to_bytes())
                  : hash_value(a.to_v4().to_bytes()));
    return res;
  }
};


template<typename Protocol>
struct endpoint_hash<asio::local::basic_endpoint<Protocol>>
{
  std::size_t operator()(const asio::local::basic_endpoint<Protocol> & be) const
  {
    return hash_range(be.data(), be.data() + be.size());
  }
};


template<bool>
struct ssl_base
{
protected:
  ssl_base () = default;

  template<typename Connection, typename Executor>
  std::shared_ptr<Connection> make_connection_(Executor exec)
  {
    return std::make_shared<Connection>(std::move(exec));
  }

  template<typename Connection, typename Allocator, typename Executor>
  std::shared_ptr<Connection> allocate_connection_(Allocator alloc, Executor exec)
  {
    return std::allocate_shared<Connection>(std::move(alloc), std::move(exec));
  }

};

template<>
struct ssl_base<true>
{
  asio::ssl::context & ssl_context() const
  {
    return context_;
  }
protected:
  ssl_base(asio::ssl::context & context) : context_(context) {}

  template<typename Connection, typename Executor>
  std::shared_ptr<Connection> make_connection_(Executor exec)
  {
    return std::make_shared<Connection>(std::move(exec), context_);
  }

  template<typename Connection, typename Allocator, typename Executor>
  std::shared_ptr<Connection> allocate_connection_(Allocator alloc, Executor exec)
  {
    return std::allocate_shared<Connection>(std::move(alloc), std::move(exec), context_);
  }
private:
  asio::ssl::context & context_;
};

}

template<typename Stream>
struct basic_connection_pool : detail::ssl_base<detail::has_ssl_v<Stream>>
{
    /// The type of the executor associated with the object.
    typedef typename Stream::executor_type executor_type;

    /// The type of the underlying connection.
    typedef basic_connection<Stream> connection_type;

    /// Rebinds the socket type to another executor.
    template<typename Executor1>
    struct rebind_executor
    {
        /// The socket type when rebound to the specified executor.
        typedef basic_connection_pool<typename Stream::template rebind_executor<Executor1>::other> other;
    };

    /// Get the executor
    executor_type get_executor() noexcept
    {
        return mutex_.get_executor();
    }

    /// The protocol-type of the lowest layer.
    using protocol_type = typename connection_type::protocol_type;

    /// The endpoint of the lowest lowest layer.
    using endpoint_type = typename connection_type::endpoint_type;

    /// The reolver_type of the lower layer.
    using resolver_type = typename protocol_type::resolver::template rebind_executor<executor_type>::other;

    /// Construct a stream.
    /**
     * @param exec The executor or execution_context.
     *
     * Everything else will be default constructed
     */
    template<typename Exec, typename = std::enable_if_t<!detail::has_ssl_v<Stream>, Exec>>
    explicit basic_connection_pool(Exec && exec,
                                   std::size_t limit = BOOST_REQUESTS_DEFAULT_POOL_SIZE)
        : mutex_(std::forward<Exec>(exec)), limit_(limit) {}

    /// Construct a stream.
    /**
     * @param exec The executor or execution_context.
     *
     * Everything else will be default constructed
     */
    template<typename Exec, typename = std::enable_if_t<detail::has_ssl_v<Stream>, Exec>>
    explicit basic_connection_pool(Exec && exec,
                                   asio::ssl::context & ctx,
                                   std::size_t limit = BOOST_REQUESTS_DEFAULT_POOL_SIZE)
        : detail::ssl_base<true>(ctx), mutex_(std::forward<Exec>(exec)), limit_(limit) {}

    /// Move constructor
    basic_connection_pool(basic_connection_pool && ) = default;

    /// rebind constructor.
    template<typename Exec>
    basic_connection_pool(basic_connection_pool<Exec> && lhs)
        : detail::ssl_base<detail::has_ssl_v<Stream>>(lhs),
          mutex_(std::move(lhs.mutex_)),
          host_(std::move(lhs.host_)),
          endpoints_(std::move(lhs.endpoints_)),
          limit_(lhs.limit_),
          conns_(std::move(lhs.conns_))
    {}

    void lookup(urls::authority_view av)
    {
      boost::system::error_code ec;
      lookup(av, ec);
      if (ec)
        urls::detail::throw_system_error(ec);
    }
    void lookup(urls::authority_view sv, system::error_code & ec);

    template<BOOST_ASIO_COMPLETION_TOKEN_FOR(void (boost::system::error_code)) CompletionToken
                  BOOST_ASIO_DEFAULT_COMPLETION_TOKEN_TYPE(executor_type)>
    BOOST_ASIO_INITFN_AUTO_RESULT_TYPE(CompletionToken,
                                       void (boost::system::error_code))
    async_lookup(urls::authority_view av,
                 CompletionToken && completion_token BOOST_ASIO_DEFAULT_COMPLETION_TOKEN(executor_type));


    std::size_t limit() const {return limit_;}
    std::size_t active() const {return conns_.size();}

    using request_type = request_settings;

    std::shared_ptr<connection_type> get_connection(error_code & ec);
    std::shared_ptr<connection_type> get_connection()
    {
      boost::system::error_code ec;
      auto res = get_connection(ec);
      if (ec)
        throw_exception(system::system_error(ec, "get_connection"));
      return res;
    }

    template<BOOST_ASIO_COMPLETION_TOKEN_FOR(void (system::error_code, std::shared_ptr<connection_type>)) CompletionToken>
    BOOST_ASIO_INITFN_AUTO_RESULT_TYPE(CompletionToken, void (system::error_code, std::shared_ptr<connection_type>))
      async_get_connection(CompletionToken && completion_token BOOST_ASIO_DEFAULT_COMPLETION_TOKEN(executor_type));

    template<typename RequestBody>
    auto ropen(beast::http::verb method,
               urls::url_view path,
               RequestBody && body,
               request_settings req,
               system::error_code & ec) -> typename connection_type::stream
    {
      auto conn = get_connection(ec);
      if (!ec && conn == nullptr)
        BOOST_REQUESTS_ASSIGN_EC(ec, asio::error::not_found);
      if (ec)
        return typename connection_type::stream{get_executor(), nullptr};

      assert(conn != nullptr);
      return conn->ropen(method, path, std::forward<RequestBody>(body), std::move(req), ec);
    }

    template<typename RequestBody>
    auto ropen(beast::http::verb method,
               urls::url_view path,
               RequestBody && body,
               request_settings req) -> typename connection_type::stream
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
               system::error_code & ec) -> stream
    {
      auto conn = get_connection(ec);
      if (!ec && conn == nullptr)
        BOOST_REQUESTS_ASSIGN_EC(ec, asio::error::not_found);
      if (ec)
        return typename connection_type::stream{get_executor(), nullptr};

      assert(conn != nullptr);
      return conn->ropen(req, opt, jar, ec);
    }

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
                                            typename connection_type::stream))
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

    using stream = typename connection_type::stream;
  private:
    detail::basic_mutex<executor_type> mutex_;
    std::string host_;
    std::vector<endpoint_type> endpoints_;
    std::size_t limit_;
    std::size_t connecting_{0u};

    boost::unordered_multimap<endpoint_type,
                              std::shared_ptr<connection_type>,
                              detail::endpoint_hash<endpoint_type>> conns_;

    struct async_lookup_op;
    struct async_get_connection_op;

    template<typename>
    struct async_ropen_op;

    template<typename>
    struct async_ropen_op_1;
};

template<typename Executor = asio::any_io_executor>
using basic_http_connection_pool  = basic_connection_pool<asio::basic_stream_socket<asio::ip::tcp, Executor>>;

template<typename Executor = asio::any_io_executor>
using basic_https_connection_pool = basic_connection_pool<asio::ssl::stream<asio::basic_stream_socket<asio::ip::tcp, Executor>>>;


using http_connection_pool  = basic_http_connection_pool<>;
using https_connection_pool = basic_https_connection_pool<>;


#if !defined(BOOST_REQUESTS_HEADER_ONLY)
extern template struct basic_connection_pool<asio::ip::tcp::socket>;
extern template struct basic_connection_pool<asio::ssl::stream<asio::ip::tcp::socket>>;
#endif

}
}

#include <boost/requests/impl/connection_pool.hpp>

#endif //BOOST_REQUESTS_POOL_HPP
