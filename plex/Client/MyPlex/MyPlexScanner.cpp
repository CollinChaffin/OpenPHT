//
//  MyPlexScanner.cpp
//  Plex
//
//  Created by Tobias Hieta <tobias@plexapp.com> on 2013-04-12.
//  Copyright 2013 Plex Inc. All rights reserved.
//

#include "MyPlexScanner.h"
#include "Client/PlexServerManager.h"
#include "XBMCTinyXML.h"
#include "FileSystem/PlexFile.h"
#include "FileSystem/PlexDirectory.h"
#include "utils/StringUtils.h"
#include "PlexApplication.h"
#include "GUISettings.h"

#define DEFAULT_PORT 32400

CMyPlexManager::EMyPlexError CMyPlexScanner::DoScan()
{
  CPlexServerPtr myplex = g_plexApplication.serverManager->FindByUUID("myplex");
  CURL url = myplex->BuildPlexURL("pms/resources");
  url.SetOption("includeHttps", "1");

  XFILE::CPlexDirectory dir;
  CFileItemList list;

  if (!dir.GetDirectory(url.Get(), list))
  {
    CLog::Log(LOGERROR, "CMyPlexScanner::DoScan not authorized from myPlex");
    if (dir.IsTokenInvalid())
      return CMyPlexManager::ERROR_INVALID_AUTH_TOKEN;
    else if (dir.GetHTTPResponseCode() == 401)
      return CMyPlexManager::ERROR_WRONG_CREDS;
    return CMyPlexManager::ERROR_NETWORK;
  }

  PlexServerList serverList;
  for (int i = 0; i < list.Size(); i ++)
  {
    CFileItemPtr serverItem = list.Get(i);
    if (serverItem && serverItem->GetPlexDirectoryType() == PLEX_DIR_TYPE_DEVICE)
    {
      bool synced = serverItem->GetProperty("synced").asBoolean();

      if (synced && g_guiSettings.GetBool("myplex.hidecloudsync"))
      {
        CLog::Log(LOGDEBUG, "CMyPlexScanner::DoScan hiding cloudsync server");
        continue;
      }

      CStdString uuid = serverItem->GetProperty("clientIdentifier").asString();
      CStdString name = serverItem->GetProperty("name").asString();
      CStdString token = serverItem->GetProperty("accessToken").asString();
      bool owned = serverItem->GetProperty("owned").asBoolean();
      bool home = serverItem->GetProperty("home").asBoolean(false);

      if (uuid.empty() || name.empty())
        continue;

      CPlexServerPtr server = CPlexServerPtr(new CPlexServer(uuid, name, owned));

      if (serverItem->HasProperty("sourceTitle"))
        server->SetOwner(serverItem->GetProperty("sourceTitle").asString());
      server->SetHome(home);

      BOOST_FOREACH(const CFileItemPtr& connection, serverItem->m_connections)
      {
        CStdString uri = connection->GetProperty("uri").asString();
        CURL connectionurl(uri);

        CPlexConnectionPtr conn = CPlexConnectionPtr(new CPlexConnection(
          CPlexConnection::CONNECTION_MYPLEX,
          connectionurl.GetHostName(),
          connectionurl.GetPort(),
          connectionurl.GetProtocol(),
          token));

        server->AddConnection(conn);

        if (connectionurl.GetProtocol() == "https")
        {
          CPlexConnectionPtr plainConn = CPlexConnectionPtr(new CPlexConnection(
            CPlexConnection::CONNECTION_MYPLEX,
            connectionurl.GetHostName(),
            connectionurl.GetPort(),
            "http",
            token));
          server->AddConnection(plainConn);
        }
      }

      serverList.push_back(server);
    }
  }
  
  BOOST_FOREACH(const CPlexServerPtr& server, serverList)
  {
    CLog::Log(LOGDEBUG, "CMyPlexScanner::DoScan server found: %s (%s) (owned: %s, home: %s)", server->GetName().c_str(), server->GetUUID().c_str(), server->GetOwned() ? "YES" : "NO", server->GetHome() ? "YES" : "NO");
    std::vector<CPlexConnectionPtr> connections;
    server->GetConnections(connections);
    BOOST_FOREACH(const CPlexConnectionPtr& conn, connections)
      CLog::Log(LOGDEBUG, "CMyPlexScanner::DoScan              - %s (isLocal: %s)", conn->GetAddress().Get().c_str(), conn->IsLocal() ? "YES" : "NO");
  }

  g_plexApplication.serverManager->UpdateFromConnectionType(serverList, CPlexConnection::CONNECTION_MYPLEX);

  return CMyPlexManager::ERROR_NOERROR;
}
