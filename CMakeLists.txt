cmake_minimum_required(VERSION 3.20)

project(boost_requests)

add_library(boost_requests INTERFACE)
add_library(Boost::requests ALIAS boost_requests)

target_include_directories(boost_requests INTERFACE include)
target_link_libraries(boost_requests
    INTERFACE
      Boost::boost
)

include_directories(include libs/url/include)
