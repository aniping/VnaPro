#include "network/SocketOwner.h"

namespace vna_server {
namespace network {
SocketOwner::SocketOwner(Socket value) : value_(value)
{
}

SocketOwner::~SocketOwner()
{
    reset();
}

Socket SocketOwner::get() const
{
    return value_;
}

void SocketOwner::reset(Socket value)
{
    if (value_ != kInvalidSocket) {
        CloseSocket(value_);
    }
    value_ = value;
}
}  // namespace network
}  // namespace vna_server
