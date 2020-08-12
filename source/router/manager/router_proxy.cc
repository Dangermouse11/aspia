//
// Aspia Project
// Copyright (C) 2020 Dmitry Chapyshev <dmitry@aspia.ru>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
//

#include "router/manager/router_proxy.h"

#include "base/logging.h"
#include "base/task_runner.h"
#include "router/manager/router.h"

namespace router {

class RouterProxy::Impl : public std::enable_shared_from_this<Impl>
{
public:
    Impl(std::shared_ptr<base::TaskRunner> io_task_runner, std::unique_ptr<Router> router);
    ~Impl();

    void connectToRouter(const std::u16string& address, uint16_t port);
    void disconnectFromRouter();
    void refreshHostList();
    void disconnectHost(uint64_t host_id);
    void refreshRelayList();
    void refreshUserList();
    void addUser(const proto::User& user);
    void modifyUser(const proto::User& user);
    void deleteUser(int64_t entry_id);

private:
    std::shared_ptr<base::TaskRunner> io_task_runner_;
    std::unique_ptr<Router> router_;

    DISALLOW_COPY_AND_ASSIGN(Impl);
};

RouterProxy::Impl::Impl(std::shared_ptr<base::TaskRunner> io_task_runner,
                        std::unique_ptr<Router> router)
    : io_task_runner_(std::move(io_task_runner)),
      router_(std::move(router))
{
    DCHECK(io_task_runner_ && router_);
}

RouterProxy::Impl::~Impl()
{
    DCHECK(!router_);
}

void RouterProxy::Impl::connectToRouter(const std::u16string& address, uint16_t port)
{
    if (!io_task_runner_->belongsToCurrentThread())
    {
        io_task_runner_->postTask(
            std::bind(&Impl::connectToRouter, shared_from_this(), address, port));
        return;
    }

    if (router_)
        router_->connectToRouter(address, port);
}

void RouterProxy::Impl::disconnectFromRouter()
{
    if (!io_task_runner_->belongsToCurrentThread())
    {
        io_task_runner_->postTask(
            std::bind(&Impl::disconnectFromRouter, shared_from_this()));
        return;
    }

    router_.reset();
}

void RouterProxy::Impl::refreshHostList()
{
    if (!io_task_runner_->belongsToCurrentThread())
    {
        io_task_runner_->postTask(std::bind(&Impl::refreshHostList, shared_from_this()));
        return;
    }

    if (router_)
        router_->refreshHostList();
}

void RouterProxy::Impl::disconnectHost(uint64_t host_id)
{
    if (!io_task_runner_->belongsToCurrentThread())
    {
        io_task_runner_->postTask(std::bind(&Impl::disconnectHost, shared_from_this(), host_id));
        return;
    }

    if (router_)
        router_->disconnectHost(host_id);
}

void RouterProxy::Impl::refreshRelayList()
{
    if (!io_task_runner_->belongsToCurrentThread())
    {
        io_task_runner_->postTask(std::bind(&Impl::refreshRelayList, shared_from_this()));
        return;
    }

    if (router_)
        router_->refreshRelayList();
}

void RouterProxy::Impl::refreshUserList()
{
    if (!io_task_runner_->belongsToCurrentThread())
    {
        io_task_runner_->postTask(std::bind(&Impl::refreshUserList, shared_from_this()));
        return;
    }

    if (router_)
        router_->refreshUserList();
}

void RouterProxy::Impl::addUser(const proto::User& user)
{
    if (!io_task_runner_->belongsToCurrentThread())
    {
        io_task_runner_->postTask(std::bind(&Impl::addUser, shared_from_this(), user));
        return;
    }

    if (router_)
        router_->addUser(user);
}

void RouterProxy::Impl::modifyUser(const proto::User& user)
{
    if (!io_task_runner_->belongsToCurrentThread())
    {
        io_task_runner_->postTask(std::bind(&Impl::modifyUser, shared_from_this(), user));
        return;
    }

    if (router_)
        router_->modifyUser(user);
}

void RouterProxy::Impl::deleteUser(int64_t entry_id)
{
    if (!io_task_runner_->belongsToCurrentThread())
    {
        io_task_runner_->postTask(std::bind(&Impl::deleteUser, shared_from_this(), entry_id));
        return;
    }

    if (router_)
        router_->deleteUser(entry_id);
}

RouterProxy::RouterProxy(std::shared_ptr<base::TaskRunner> io_task_runner,
                         std::unique_ptr<Router> router)
    : impl_(std::make_shared<Impl>(std::move(io_task_runner), std::move(router)))
{
    // Nothing
}

RouterProxy::~RouterProxy()
{
    impl_->disconnectFromRouter();
}

void RouterProxy::connectToRouter(const std::u16string& address, uint16_t port)
{
    impl_->connectToRouter(address, port);
}

void RouterProxy::disconnectFromRouter()
{
    impl_->disconnectFromRouter();
}

void RouterProxy::refreshHostList()
{
    impl_->refreshHostList();
}

void RouterProxy::disconnectHost(uint64_t host_id)
{
    impl_->disconnectHost(host_id);
}

void RouterProxy::refreshRelayList()
{
    impl_->refreshRelayList();
}

void RouterProxy::refreshUserList()
{
    impl_->refreshUserList();
}

void RouterProxy::addUser(const proto::User& user)
{
    impl_->addUser(user);
}

void RouterProxy::modifyUser(const proto::User& user)
{
    impl_->modifyUser(user);
}

void RouterProxy::deleteUser(int64_t entry_id)
{
    impl_->deleteUser(entry_id);
}

} // namespace router
