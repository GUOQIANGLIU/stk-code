//
//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2013 Glenn De Jonghe
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License
//  as published by the Free Software Foundation; either version 3
//  of the License, or (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.


#include "online/servers_manager.hpp"

#include <string>
#include <irrString.h>
#include <assert.h>
#include "config/user_config.hpp"
#include "utils/translation.hpp"
#include "utils/time.hpp"

#define SERVER_REFRESH_INTERVAL 5.0f

namespace Online{

    static Synchronised<ServersManager*> manager_singleton(NULL);

    ServersManager* ServersManager::acquire()
    {
        manager_singleton.lock();
        ServersManager * manager = manager_singleton.getData();
        if (manager == NULL)
        {
            manager_singleton.unlock();
            manager = new ServersManager();
            manager_singleton.setAtomic(manager);
            manager_singleton.lock();
        }
        return manager;
    }

    void ServersManager::release()
    {
        manager_singleton.unlock();
    }

    void ServersManager::deallocate()
    {
        manager_singleton.lock();
        ServersManager* manager = manager_singleton.getData();
        delete manager;
        manager = NULL;
        manager_singleton.unlock();
    }   // deallocate

    // ============================================================================
    ServersManager::ServersManager(){
        m_servers = new PtrVector<Server>;
        m_info_message = "";
        m_last_load_time = 0.0f;
        m_joined_server = NULL;
    }

    ServersManager::~ServersManager(){
        m_servers->clearAndDeleteAll();
        delete m_joined_server;
    }

    // ============================================================================
    ServersManager::RefreshRequest * ServersManager::refreshRequest()
    {
        RefreshRequest * request = NULL;
        if(Time::getRealTime() - m_last_load_time > SERVER_REFRESH_INTERVAL)
        {
            request = new RefreshRequest();
            request->setURL((std::string)UserConfigParams::m_server_multiplayer + "client-user.php");
            request->setParameter("action",std::string("get_server_list"));
            HTTPManager::get()->addRequest(request);
        }
        return request;
    }

    void ServersManager::refresh(const RefreshRequest * input)
    {
        if (input->isSuccess())
        {
            const XMLNode * servers_xml = input->getResult()->getNode("servers");
            m_servers->clearAndDeleteAll();
            for (unsigned int i = 0; i < servers_xml->getNumNodes(); i++)
            {
                m_servers->push_back(new Server(*servers_xml->getNode(i)));
            }
            m_last_load_time = Time::getRealTime();
        }
        m_info_message = input->getInfo();
        //FIXME error message
    }

    void ServersManager::RefreshRequest::callback()
    {
        ServersManager::acquire()->refresh(this);
        ServersManager::release();
    }

    // ============================================================================
    Server * ServersManager::getQuickPlay()
    {
        if(m_servers->size() > 0)
            return m_servers->get(0);
        return NULL;
    }
} // namespace Online
