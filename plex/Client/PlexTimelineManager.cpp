#include "PlexTimelineManager.h"
#include "UrlOptions.h"
#include "log.h"
#include "Application.h"
#include "PlexApplication.h"
#include "Client/PlexMediaServerClient.h"
#include "GUIWindowManager.h"
#include "PlayList.h"
#include "plex/Remote/PlexRemoteSubscriberManager.h"
#include "utils/StringUtils.h"
#include <boost/lexical_cast.hpp>
#include "video/VideoInfoTag.h"
#include "music/tags/MusicInfoTag.h"
#include "settings/AdvancedSettings.h"

CPlexTimelineManager::CPlexTimelineManager()
{
  m_currentItems[MUSIC] = CFileItemPtr();
  m_currentItems[PHOTO] = CFileItemPtr();
  m_currentItems[VIDEO] = CFileItemPtr();
}

////////////////////////////////////////////////////////////////////////////////////////
std::string CPlexTimelineManager::StateToString(MediaState state)
{
  CStdString strstate;
  switch (state) {
    case MEDIA_STATE_STOPPED:
      strstate = "stopped";
      break;
    case MEDIA_STATE_BUFFERING:
      strstate = "buffering";
      break;
    case MEDIA_STATE_PLAYING:
      strstate = "playing";
      break;
    case MEDIA_STATE_PAUSED:
      strstate = "paused";
      break;
  }
  return strstate;
}

uint64_t CPlexTimelineManager::GetItemDuration(CFileItemPtr item)
{
  if (item->HasProperty("duration"))
    return item->GetProperty("duration").asInteger();
  else if (item->HasVideoInfoTag())
    return item->GetVideoInfoTag()->m_duration * 1000;
  else if (item->HasMusicInfoTag())
    return item->GetMusicInfoTag()->GetDuration() * 1000;

  return 0;
}

CUrlOptions CPlexTimelineManager::GetCurrentTimeline(MediaType type, bool forServer)
{
  CUrlOptions options;

  options.AddOption("state", StateToString(m_currentStates[type]));
  options.AddOption("type", MediaTypeToString(type));

  CFileItemPtr item = m_currentItems[type];
  if (item)
  {
    options.AddOption("time", item->GetProperty("viewOffset").asString());
    options.AddOption("machineidentifier", item->GetProperty("plexserver").asString());
    options.AddOption("ratingKey", item->GetProperty("ratingKey").asString());
    options.AddOption("key", item->GetProperty("unprocessed_key").asString());
    options.AddOption("containerKey", item->GetProperty("containerKey").asString());

    if (item->HasProperty("guid"))
      options.AddOption("guid", item->GetProperty("guid").asString());

    if (item->HasProperty("url"))
      options.AddOption("url", item->GetProperty("url").asString());

    if (GetItemDuration(item) > 0)
      options.AddOption("duration", boost::lexical_cast<std::string>(GetItemDuration(item)));
  }
  else
  {
    options.AddOption("time", 0);
  }

  if (!forServer)
  {
    int player = g_application.IsPlayingAudio() ? PLAYLIST_MUSIC : PLAYLIST_VIDEO;
    int playlistLen = g_playlistPlayer.GetPlaylist(player).size();
    int playlistPos = g_playlistPlayer.GetCurrentSong();

    /* determine what things are controllable */
    std::vector<std::string> controllable;

    if (playlistLen > 0)
    {
      if (playlistPos > 0)
        controllable.push_back("skipPrevious");
      if (playlistLen > (playlistPos + 1))
        controllable.push_back("skipNext");

      controllable.push_back("shuffle");
      controllable.push_back("repeat");
    }
    if (type == MUSIC || type == VIDEO)
    {
      controllable.push_back("volume");
      controllable.push_back("stepBack");
      controllable.push_back("stepForward");
      if (type == VIDEO)
      {
        controllable.push_back("subtitleStream");
        controllable.push_back("audioStream");
      }
    }

    if (controllable.size() > 0)
      options.AddOption("controllable", StringUtils::Join(controllable, ","));

    if (g_application.IsPlaying())
    {
      options.AddOption("volume", g_application.GetVolume());

      if (g_playlistPlayer.IsShuffled(player))
        options.AddOption("shuffle", 1);
      else
        options.AddOption("shuffle", 0);

      if (g_playlistPlayer.GetRepeat(player) == PLAYLIST::REPEAT_ONE)
        options.AddOption("repeat", 1);
      else if (g_playlistPlayer.GetRepeat(player) == PLAYLIST::REPEAT_ALL)
        options.AddOption("repeat", 2);
      else
        options.AddOption("repeat", 0);

      if (type == VIDEO && g_application.IsPlayingVideo())
      {
        int subid = g_application.m_pPlayer->GetSubtitleVisible() ? g_application.m_pPlayer->GetSubtitlePlexID() : -1;
        options.AddOption("subtitleStreamID", subid);
        options.AddOption("audioStreamID", g_application.m_pPlayer->GetAudioStreamPlexID());
      }

      std::string location = "navigation";
      int currentWindow = g_windowManager.GetActiveWindow();
      if (currentWindow == WINDOW_FULLSCREEN_VIDEO)
        location = "fullScreenVideo";
      else if (currentWindow == WINDOW_SLIDESHOW)
        location = "fullScreenPhoto";
      else if (currentWindow == WINDOW_NOW_PLAYING || currentWindow == WINDOW_VISUALISATION)
        location = "fullScreenMusic";

      options.AddOption("location", location);
    }
  }

  return options;
}

void CPlexTimelineManager::ReportProgress(CFileItemPtr currentItem, CPlexTimelineManager::MediaState state, uint64_t currentPosition)
{
  MediaType type = GetMediaType(currentItem);
  if (type == UNKNOWN)
  {
    CLog::Log(LOGDEBUG, "PlexTimelineManager::ReportProgress unknown item %s", currentItem->GetPath().c_str());
    return;
  }

  bool stateChange = false;

  if (currentItem != m_currentItems[type])
  {
    m_currentItems[type] = currentItem;
    stateChange = true;
  }

  if (state != m_currentStates[type])
  {
    m_currentStates[type] = state;
    stateChange = true;
  }

  if (currentItem)
    currentItem->SetProperty("viewOffset", currentPosition);

  if ((m_pollEvent.getNumWaits() > 0 || g_plexApplication.remoteSubscriberManager->hasSubscribers()) &&
      (stateChange || m_subTimer.elapsed() > 1))
  {
    CLog::Log(LOGDEBUG, "CPlexTimelineManager::ReportProgress updating subscribers.");
    m_pollEvent.Set();

    m_subTimer.restart();
  }

  if (stateChange || m_serverTimer.elapsed() > 10)
  {
    CLog::Log(LOGDEBUG, "CPlexTimelineManager::ReportProgress updating server");
    g_plexApplication.mediaServerClient->SendServerTimeline(m_currentItems[type], GetCurrentTimeline(type));
    m_serverTimer.restart();
  }

  /* Mark progress for the item */
  float percentage = 0.0;
  if (GetItemDuration(currentItem) > 0)
  {
    percentage = (float)(((float)currentPosition/(float)GetItemDuration(currentItem)) * 100.0);
    CLog::Log(LOGDEBUG, "CPlexTimelineManager::ReportProgress %lld / %lld = %f", currentPosition, GetItemDuration(currentItem), percentage);
  }

  if (currentItem->GetOverlayImageID() == CGUIListItem::ICON_OVERLAY_UNWATCHED &&
      currentPosition >= 5)
  {
    currentItem->SetOverlayImage(CGUIListItem::ICON_OVERLAY_IN_PROGRESS);
  }

  CLog::Log(LOGDEBUG, "CPlexTimelineManager::ReportProgress percentage is %f", percentage);
  if ((currentItem->GetOverlayImageID() == CGUIListItem::ICON_OVERLAY_IN_PROGRESS ||
      currentItem->GetOverlayImageID() == CGUIListItem::ICON_OVERLAY_UNWATCHED) &&
      percentage > g_advancedSettings.m_videoPlayCountMinimumPercent)
  {
    currentItem->MarkAsWatched();
  }

  /* if we are stopping, we need to reset our currentItem */
  if (state == MEDIA_STATE_STOPPED)
    m_currentItems[type].reset();

  m_pollEvent.Reset();
}

CPlexTimelineManager::MediaType CPlexTimelineManager::GetMediaType(CFileItemPtr item)
{
  EPlexDirectoryType plexType = item->GetPlexDirectoryType();

  switch(plexType)
  {
    case PLEX_DIR_TYPE_TRACK:
      return MUSIC;
    case PLEX_DIR_TYPE_VIDEO:
    case PLEX_DIR_TYPE_EPISODE:
    case PLEX_DIR_TYPE_CLIP:
    case PLEX_DIR_TYPE_MOVIE:
      return VIDEO;
    case PLEX_DIR_TYPE_IMAGE:
      return PHOTO;
    default:
      return UNKNOWN;
  }
  return UNKNOWN;
}

std::string CPlexTimelineManager::MediaTypeToString(MediaType type)
{
  switch(type)
  {
    case MUSIC:
      return "music";
    case PHOTO:
      return "photo";
    case VIDEO:
      return "video";
    default:
      return "unknown";
  }
}

CPlexTimelineManager::MediaType CPlexTimelineManager::GetMediaType(const CStdString &typestr)
{
  if (typestr == "music")
    return MUSIC;
  if (typestr == "photo")
    return PHOTO;
  if (typestr == "video")
    return VIDEO;
  return UNKNOWN;
}

std::vector<CUrlOptions> CPlexTimelineManager::WaitForTimeline()
{
  if (m_pollEvent.Wait())
  {
    return GetCurrentTimeLines();
  }

  return std::vector<CUrlOptions>();
}

std::vector<CUrlOptions> CPlexTimelineManager::GetCurrentTimeLines()
{
  std::vector<CUrlOptions> array;

  if (m_currentItems[MUSIC])
    array.push_back(GetCurrentTimeline(MUSIC, false));
  if (m_currentItems[PHOTO])
    array.push_back(GetCurrentTimeline(PHOTO, false));
  if (m_currentItems[VIDEO])
    array.push_back(GetCurrentTimeline(VIDEO, false));

  return array;
}

