//
// Copyright (c) 2022 Klemens Morgenstern (klemens.morgenstern@gmx.net)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BOOST_REQUESTS_REQUEST_HPP
#define BOOST_REQUESTS_REQUEST_HPP

#include <boost/requests/http.hpp>
#include <boost/requests/service.hpp>
#include <boost/requests/session.hpp>

namespace boost
{
namespace requests
{

template<typename RequestBody, typename Allocator = std::allocator<char>>
auto request(beast::http::verb method,
             urls::url_view path,
             RequestBody && body,
             beast::http::basic_fields<Allocator> req,
             system::error_code & ec) -> response;

template<typename RequestBody, typename Allocator = std::allocator<char>>
auto request(beast::http::verb method,
             urls::url_view path,
             RequestBody && body,
             beast::http::basic_fields<Allocator> req)
    -> response
{
  boost::system::error_code ec;
  auto res = request(method, path, std::forward<RequestBody>(body), std::move(req), ec);
  if (ec)
    throw_exception(system::system_error(ec, "request"));
  return res;
}

template<typename RequestBody, typename Allocator = std::allocator<char>>
auto request(beast::http::verb method,
             core::string_view path,
             RequestBody && body,
             beast::http::basic_fields<Allocator> req,
             system::error_code & ec) -> response
{
  auto url = urls::parse_uri(path);
  if (url.has_error())
  {
    ec = url.error();
    return response{req.get_allocator()};
  }
  else
    return request(method, url.value(), std::forward<RequestBody>(body), req, ec);
}

template<typename RequestBody, typename Allocator = std::allocator<char>>
auto request(beast::http::verb method,
             core::string_view path,
             RequestBody && body,
             beast::http::basic_fields<Allocator> req)
    -> response
{
  boost::system::error_code ec;
  auto res = request(method, path, std::forward<RequestBody>(body), std::move(req), ec);
  if (ec)
    throw_exception(system::system_error(ec, "request"));
  return res;
}


template<typename RequestBody,
          typename Allocator = std::allocator<char>,
          BOOST_ASIO_COMPLETION_TOKEN_FOR(void (boost::system::error_code,
                                               response)) CompletionToken>
BOOST_ASIO_INITFN_AUTO_RESULT_TYPE(CompletionToken,
                                   void (boost::system::error_code,
                                        response))
async_request(beast::http::verb method,
              urls::url_view path,
              RequestBody && body,
              beast::http::basic_fields<Allocator> req,
              CompletionToken && completion_token);

template<typename RequestBody,
          typename Allocator = std::allocator<char>,
          BOOST_ASIO_COMPLETION_TOKEN_FOR(void (boost::system::error_code,
                                               response)) CompletionToken>
BOOST_ASIO_INITFN_AUTO_RESULT_TYPE(CompletionToken,
                                   void (boost::system::error_code,
                                        response))
async_request(beast::http::verb method,
              core::string_view path,
              RequestBody && body,
              beast::http::basic_fields<Allocator> req,
              CompletionToken && completion_token);

template<typename Allocator = std::allocator<char>>
auto download(urls::url_view path,
              beast::http::basic_fields<Allocator> req,
              const filesystem::path & download_path,
              system::error_code & ec) -> response;

template<typename Allocator= std::allocator<char> >
auto download(urls::url_view path,
              beast::http::basic_fields<Allocator> req,
              const filesystem::path & download_path) -> response
{
  boost::system::error_code ec;
  auto res = download(path, std::move(req), download_path, ec);
  if (ec)
    throw_exception(system::system_error(ec, "download"));
  return res;
}

template<typename Allocator = std::allocator<char>>
auto download(core::string_view path,
              beast::http::basic_fields<Allocator> req,
              const filesystem::path & download_path,
              system::error_code & ec) -> response
{
  auto url = urls::parse_uri(path);
  if (url.has_error())
  {
    ec = url.error();
    return response{req.get_allocator()};
  }
  else
    return download(url.value(), req, download_path, ec);
}

template<typename Allocator= std::allocator<char> >
auto download(core::string_view path,
              beast::http::basic_fields<Allocator> req,
              const filesystem::path & download_path) -> response
{
  boost::system::error_code ec;
  auto res = download(path, std::move(req), download_path, ec);
  if (ec)
    throw_exception(system::system_error(ec, "download"));
  return res;
}



template<typename Allocator = std::allocator<char>,
          BOOST_ASIO_COMPLETION_TOKEN_FOR(void (boost::system::error_code,
                                               response)) CompletionToken>
BOOST_ASIO_INITFN_AUTO_RESULT_TYPE(CompletionToken,
                                   void (boost::system::error_code,
                                        response))
async_download(urls::url_view path,
               beast::http::basic_fields<Allocator> req,
               filesystem::path download_path,
               CompletionToken && completion_token);

template<typename Allocator = std::allocator<char>,
          BOOST_ASIO_COMPLETION_TOKEN_FOR(void (boost::system::error_code,
                                               response)) CompletionToken>
BOOST_ASIO_INITFN_AUTO_RESULT_TYPE(CompletionToken,
                                   void (boost::system::error_code,
                                        response))
async_download(core::string_view path,
               beast::http::basic_fields<Allocator> req,
               filesystem::path download_path,
               CompletionToken && completion_token);

#define request_type http::fields
#define basic_request beast::http::basic_fields
#define executor_type boost::asio::system_executor
#define target_view urls::url_view
#include <boost/requests/detail/alias.def>
#undef target_view


#define target_view core::string_view
#include <boost/requests/detail/alias.def>
#undef target_view

#undef executor_type
#undef basic_request
#undef request_type

}
}

#include <boost/requests/impl/request.hpp>

#endif // BOOST_REQUESTS_REQUEST_HPP
