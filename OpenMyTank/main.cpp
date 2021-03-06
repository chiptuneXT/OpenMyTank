
//-----------------------------------------------------------------------------

#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers

#include "tanki_online.h"
#include "flash.h"
#include "gui.h"
#include "clipboard_manager.h"
#include "hooks.h"
#include "screenshot.h"
#include "web_browser.h"
#include "account.h"
#include "clicker_thread.h"
#include "autoupdating.h"

#include <windows.h>
#include <Windowsx.h>
#include <tchar.h>
#include <atlbase.h>
#include <atlwin.h>
#include <mmsystem.h>

#include <vector>

//-----------------------------------------------------------------------------

HINSTANCE Instance;  // global
HWND MainWindow;     // global

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
bool CreateAndShowMainWindow();
void OnButtonClicked(Gui::Button* button);

bool IsFullScreen = false;
bool IsMenuVisible = true;

std::auto_ptr<Gui::Button> GetUrlButton;
std::auto_ptr<Gui::Button> OpenUrlButton;

std::vector<Gui::Button*>  ServerButtons;
static Gui::Button* PrevButton = NULL;

std::auto_ptr<Gui::Button> LeftPlayButton;
std::auto_ptr<Gui::Button> RightPlayButton;
std::auto_ptr<Gui::Button> FullScreenButton;

std::auto_ptr<Gui::ClickerWindow> LeftClickerWindow;
std::auto_ptr<Gui::ClickerWindow> RightClickerWindow;

std::auto_ptr<Gui::AboutWindow> AboutWindow;
std::auto_ptr<Flash::PlayerWindow> FlashPlayer;

Gui::WindowTitle* WindowTitle = NULL;

std::auto_ptr<TO::UrlManager> BattleUrl;

std::auto_ptr<ClipboardManager> Clipboard;

std::auto_ptr<KeyboardHook>  KeyboardHooker;
std::auto_ptr<MouseHook>  MouseHooker;

std::auto_ptr<ScreenshoterThread>  Screenshot;

std::auto_ptr<ClickerThread> Clicker;
volatile bool BattleIsStarted = false;

//-----------------------------------------------------------------------------

RECT GetRectForFlash()
{
  RECT rect;
  ::GetClientRect(MainWindow, &rect);

  //rect.left  += 1;
  rect.right -= 1/* + 1*/;

  if (IsMenuVisible)
  {
    rect.top += Gui::GetMenuHeight();
  }
  rect.bottom -= 1/* + 1*/;

  return rect;
}

//-----------------------------------------------------------------------------

void UpdateWidgetPositions()
{
  const RECT rect = GetRectForFlash();

  POINT point = {rect.right - (5 - 1) - FullScreenButton->GetSize().cx, 5};
  FullScreenButton->Move(point);
  point.x -= (RightPlayButton->GetSize().cx + 5);
  RightPlayButton->Move(point);
  point.x -= (LeftPlayButton->GetSize().cx + 2);
  LeftPlayButton->Move(point);

  const int serverButtons = ServerButtons.size();
  const int availableLeft = OpenUrlButton->GetPoint().x + OpenUrlButton->GetSize().cx + 5;
  const int buttonOffset = 2;
  const int availableWidth = point.x - (availableLeft) - (5 - buttonOffset);
  const int buttonWidth = availableWidth / serverButtons - buttonOffset;
  const int buttonHeight = 20;
  const int increasedButtons = availableWidth - ((buttonWidth + buttonOffset) * serverButtons);
  int x = availableLeft;
  for (int i = 0; i < serverButtons; ++i)
  {
    Gui::Button*const button = ServerButtons[i];
    const int thisButtonWidth = (i + increasedButtons >= serverButtons) ? (buttonWidth + 1) : (buttonWidth);
    button->SetRect(MakeRECT(MakePOINT(x, 5), MakeSIZE(thisButtonWidth, buttonHeight)));
    x += (thisButtonWidth + buttonOffset);
  }

  if (FlashPlayer.get() != NULL)
  {
    FlashPlayer->MoveWindow(rect);
  }

  if (AboutWindow.get() != NULL)
  {
    AboutWindow->UpdateSize();
  }

  if (LeftClickerWindow.get() != NULL)
  {
    const RECT rc = TO::GetLeftClickerWindowRect(FlashPlayer->GetSize());
    ::MoveWindow(LeftClickerWindow->GetHwnd(), rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, TRUE);
  }
  if (RightClickerWindow.get() != NULL)
  {
    const RECT rc = TO::GetRightClickerWindowRect(FlashPlayer->GetSize());
    ::MoveWindow(RightClickerWindow->GetHwnd(), rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, TRUE);
  }

  ::RedrawWindow(MainWindow, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
}

//-----------------------------------------------------------------------------

POINT GetMinSizeOfMainWindow()
{
  static POINT MinSizeOfMainWindow = {0, 0};

  if (MinSizeOfMainWindow.x == 0)
  {
    RECT rc = {0};
    ::AdjustWindowRectEx(&rc,
                         Gui::GetMainWindowGwlStyle(),  //  ::GetWindowLong(MainWindow, GWL_STYLE),
                         FALSE,                    //(BOOL)::GetMenu(MainWindow),
                         0                         //::GetWindowLong(MainWindow, GWL_EXSTYLE)
                        );

    MinSizeOfMainWindow.x = TO::MinimumPlayerSize().cx + (-rc.left) + rc.right;
    MinSizeOfMainWindow.y = TO::MinimumPlayerSize().cy + (-rc.top) + rc.bottom + Gui::GetMenuHeight();
  }

  return MinSizeOfMainWindow;
}

//-----------------------------------------------------------------------------

#pragma comment(lib, "Urlmon.lib")

static void CorrectMyUserAgent()
{
  DWORD dw;
  char buf[1024] = {'\0'};
  ::UrlMkGetSessionOption(URLMON_OPTION_USERAGENT, buf, sizeof(buf)/sizeof(buf[0]), &dw, 0);

  char*const str = std::strstr(buf, "MSIE ");
  if (str != NULL)
  {
    str[5] = '8';
    ::UrlMkSetSessionOption(URLMON_OPTION_USERAGENT, buf, dw, 0);
  }
}

//-----------------------------------------------------------------------------

int APIENTRY _tWinMain(HINSTANCE instance,
                       HINSTANCE /*hPrevInstance*/,
                       LPTSTR    commandLine,
                       int       /*nCmdShow*/)
{
  Instance = instance;

  ForAutoupdate();

  BattleUrl.reset(new TO::UrlManager(commandLine));
  WindowTitle = new Gui::WindowTitle(commandLine);

  ::CoInitialize(NULL);
  ::AtlAxWinInit();

  if (!CreateAndShowMainWindow())
  {
    return FALSE;
  }

  KeyboardHooker.reset(new KeyboardHook());

  CorrectMyUserAgent();

  MSG msg;

  while (::GetMessage(&msg, NULL, 0, 0))
  {
    ::TranslateMessage(&msg);
    ::DispatchMessage(&msg);
  }

  for (std::vector<Gui::Button*>::const_iterator it = ServerButtons.begin();
       it != ServerButtons.end();
       ++it)
  {
    delete *it;
  }

  ::UnregisterClass(Gui::GetWindowClassName(), Instance);

  ::CoUninitialize();

  KeyboardHooker.reset();
  MouseHooker.reset();
  Screenshot.reset();

  delete WindowTitle;

  return (int)msg.wParam;
}

//-----------------------------------------------------------------------------

bool CreateAndShowMainWindow()
{
  WNDCLASSEX wcex;
  wcex.cbSize = sizeof(WNDCLASSEX);

  wcex.style		= CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
  wcex.lpfnWndProc	= WndProc;
  wcex.cbClsExtra	= 0;
  wcex.cbWndExtra	= 0;
  wcex.hInstance	= Instance;
  wcex.hIcon		= ::LoadIcon(Instance, MAKEINTRESOURCE(IDI_FAVICON));
  wcex.hCursor		= ::LoadCursor(0, IDC_ARROW);
  wcex.hbrBackground	= (HBRUSH)::CreateSolidBrush(0x0D220C);
  wcex.lpszMenuName	= NULL;
  wcex.lpszClassName	= Gui::GetWindowClassName();
  wcex.hIconSm		= wcex.hIcon;

  if (::RegisterClassEx(&wcex) == 0)
  {
    return false;
  }

  RECT rect = GetSettings()->MainWindowRect;
  if ((rect.left == 0 && rect.right == 0) || rect.right < 0 || rect.bottom < 0) // check negativ values for fix bug
  {
    CONST POINT windowSize = GetMinSizeOfMainWindow();
    rect.left = (::GetSystemMetrics(SM_CXSCREEN) - windowSize.x) / 2;
    rect.top = (::GetSystemMetrics(SM_CYSCREEN) - windowSize.y) / 2;
    rect.right = rect.left + windowSize.x;
    rect.bottom = rect.top + windowSize.y;
  }
  MainWindow = ::CreateWindow(Gui::GetWindowClassName(),
                              WindowTitle->Get(),
                              Gui::GetMainWindowGwlStyle(),
                              rect.left,
                              rect.top,
                              rect.right - rect.left/* + 1*/,
                              rect.bottom - rect.top/* + 1*/,
                              NULL,
                              NULL,
                              Instance,
                              NULL);

  if (MainWindow == NULL)
  {
    return false;
  }

  ::ShowWindow(MainWindow, (GetSettings()->WindowState & 0x1) ? (SW_SHOWMAXIMIZED) : (SW_SHOW));
  ::UpdateWindow(MainWindow);
  ::SetForegroundWindow(MainWindow);

  if (GetSettings()->WindowState & 0x2)
  {
    FullScreenButton->Click();
  }

  return true;
}

//-----------------------------------------------------------------------------

void LoadUrl(const tstring& newUrl = tstring())
{
  Clicker.reset();
  LeftClickerWindow.reset();
  RightClickerWindow.reset();

  LeftPlayButton->SetChecked(false);
  RightPlayButton->SetChecked(false);

  OpenUrlButton->SetEnabled(true);

  ::SendMessage(MainWindow, WM_ACTIVATE, (WPARAM)WA_CLICKACTIVE, (LPARAM)MainWindow);

  if (newUrl.empty())
  {
    FlashPlayer.reset();

    if (AboutWindow.get() == NULL)
    {
      AboutWindow.reset(new Gui::AboutWindow(MainWindow));
    }

    GetUrlButton->SetEnabled(false);
    PrevButton->SetChecked(false);
    PrevButton = NULL;

    ::SetWindowText(MainWindow, WindowTitle->SetActiveServer());
  }
  else
  {
    AboutWindow.reset();

    try
    {
      FlashPlayer.reset();  // without it c-tor of new will be runned before d-tor last
      FlashPlayer.reset(new Flash::PlayerWindow(MainWindow, newUrl.c_str()));
      GetUrlButton->SetEnabled(true);
      LeftPlayButton->SetEnabled(true);
      RightPlayButton->SetEnabled(true);
    }
    catch (const TCHAR* text)
    {
      ::MessageBox(MainWindow, text, ProgramTitle, MB_OK | MB_ICONERROR);
    }

    ::SetWindowText(MainWindow, WindowTitle->SetActiveServer(TO::UrlManager::ExtractServerNumber(newUrl)));
  }

  UpdateWidgetPositions();
}

//-----------------------------------------------------------------------------

void SaveScreenshot()
{
  if (FlashPlayer.get() != NULL)
  {
    const HWND hwnd = FlashPlayer->GetFlashHwnd();
    if (hwnd != NULL)
    {
      if (Screenshot.get() == NULL)
      {
        Screenshot.reset(new ScreenshoterThread(hwnd,
            GetSettings()->ScreenshotPath.c_str(),
            GetSettings()->ScreenshotDateFormat.c_str(),
            GetSettings()->ScreenshotTimeFormat.c_str(),
            GetSettings()->ScreenshotFormat.c_str(),
            GetSettings()->JpegQuality,
            GetSettings()->BeepOnScreenshot,
            MainWindow));
        Screenshot->Start();
      }
    }
  }
}

//-----------------------------------------------------------------------------

void ToggleMenuVisible()
{
  if (FlashPlayer.get() != NULL)
  {
    IsMenuVisible = !IsMenuVisible;
    UpdateWidgetPositions();
  }
}

//-----------------------------------------------------------------------------

void ToggleFullScreenMode()
{
  const RECT& windowRect = GetSettings()->MainWindowRect;
  if (IsFullScreen) // from FullScreen:
  {
    MouseHooker.reset();

    GetSettings()->WindowState &= ~0x2;
    LONG style = GetWindowLong(MainWindow, GWL_STYLE);
    style &= ~WS_POPUP;
    style |= WS_OVERLAPPEDWINDOW;
    ::SetWindowLong(MainWindow, GWL_STYLE, style);

    if ((GetSettings()->WindowState & 0x1) != 0)
    {
      ::SetWindowPos(MainWindow, NULL,
        0, 0,
        ::GetSystemMetrics(SM_CXFULLSCREEN), ::GetSystemMetrics(SM_CYCAPTION) + ::GetSystemMetrics(SM_CYFULLSCREEN),
        SWP_NOZORDER | SWP_FRAMECHANGED);
    }
    else
    {
      ::SetWindowPos(MainWindow, NULL,
        windowRect.left, windowRect.top,
        windowRect.right - windowRect.left, windowRect.bottom - windowRect.top,
        SWP_NOZORDER | SWP_FRAMECHANGED);
    }
  }
  else // to FullScreen:
  {
    MouseHooker.reset(new MouseHook());

    GetSettings()->WindowState |= 0x2;
    LONG style = GetWindowLong(MainWindow, GWL_STYLE);
    style &= ~WS_OVERLAPPEDWINDOW;
    style |= WS_POPUP;
    ::SetWindowLong(MainWindow, GWL_STYLE, style);

    if ((GetSettings()->WindowState & 0x1) == 0)
    {
      GetSettings()->SaveCurrentWindowRect(MainWindow);
    }
    ::SetWindowPos(MainWindow, NULL,
      0, 0,
      ::GetSystemMetrics(SM_CXSCREEN), ::GetSystemMetrics(SM_CYSCREEN),
      SWP_NOZORDER | SWP_FRAMECHANGED);
  }
  IsFullScreen = !IsFullScreen;
  UpdateWidgetPositions();

  FullScreenButton->SetChecked(IsFullScreen);
}

//-----------------------------------------------------------------------------

void OnButtonClicked(Gui::Button* button)
{
  if (button == FullScreenButton.get())
  {
    ToggleFullScreenMode();
  }
  else if (button == LeftPlayButton.get())
  {
    if (FlashPlayer.get() != NULL)
    {
      LeftClickerWindow.reset();
      if (button->GetChecked())
      {
        if (FlashPlayer->IsBattleHappens())
        {
          button->SetChecked(false);
          return;
        }
        LeftClickerWindow.reset(new Gui::ClickerWindow(FlashPlayer->GetFlashHwnd(),
                                                       TO::GetLeftClickerWindowRect(FlashPlayer->GetSize()),
                                                       LeftPlayButton.get()));
        if (Clicker.get() == NULL)
        {
          Clicker.reset(new ClickerThread(MainWindow));
          Clicker->Start();
        }
      }
      Clicker->SetLeftClicker(button->GetChecked());
      if (!Clicker->IsBusy())
      {
        Clicker.reset();
      }
    }
  }
  else if (button == RightPlayButton.get())
  {
    if (FlashPlayer.get() != NULL)
    {
      RightClickerWindow.reset();
      if (button->GetChecked())
      {
        if (FlashPlayer->IsBattleHappens())
        {
          button->SetChecked(false);
          return;
        }
        RightClickerWindow.reset(new Gui::ClickerWindow(FlashPlayer->GetFlashHwnd(),
                                                        TO::GetRightClickerWindowRect(FlashPlayer->GetSize()),
                                                        RightPlayButton.get()));
        if (Clicker.get() == NULL)
        {
          Clicker.reset(new ClickerThread(MainWindow));
          Clicker->Start();
        }
      }
      Clicker->SetRightClicker(button->GetChecked());
      if (!Clicker->IsBusy())
      {
        Clicker.reset();
      }
    }
  }
  else if (button == GetUrlButton.get())
  {
    if (FlashPlayer.get() != NULL)
    {
      const tstring url = FlashPlayer->GetBattleUrl();
      if (!url.empty())
      {
        Clipboard->Write(url);
      }
    }
  }
  else if (button == OpenUrlButton.get())
  {
    const tstring url = BattleUrl->ToMyUrl(Clipboard->Read());
    if (BattleUrl->IsValidUrl(url))
    {
      const unsigned serverNumber = BattleUrl->ExtractServerNumber(url);
      if (serverNumber < TO::GetNumberOfServers())
      {
        if (PrevButton != NULL)
        {
          PrevButton->SetChecked(false);
        }

        PrevButton = ServerButtons[serverNumber - 1];
        PrevButton->SetChecked(true);

        LoadUrl(url);
      }
    }
  }
  else
  {
    if (button != PrevButton)
    {
      if (PrevButton != NULL)
      {
        PrevButton->SetChecked(false);
      }
      button->SetChecked(true);
      PrevButton = button;
    }

    const int serverNumber = button->GetId();
    LoadUrl(BattleUrl->GetServerLink(serverNumber));
  }
}

//-----------------------------------------------------------------------------

void CreateServerButtons()
{
  if (TO::GetNumberOfServers() > ServerButtons.size())
  {
    const LONG buttonHeight = 20;
    const POINT pt = {0, 0};
    const SIZE size = {0, buttonHeight};
    for (unsigned i = ServerButtons.size(); i < TO::GetNumberOfServers(); ++i)
    {
      TCHAR caption[8];
      ::wsprintf(caption, TEXT("%d"), i + 1);
      ServerButtons.push_back(new Gui::Button(MainWindow, MakeRECT(pt, size), caption, i + 1, false));
    }
  }
  else if (TO::GetNumberOfServers() < ServerButtons.size())
  {
    do
    {
      delete ServerButtons.back();
      ServerButtons.pop_back();
    }
    while (TO::GetNumberOfServers() < ServerButtons.size());
  }
  else
  {
    return;
  }

  UpdateWidgetPositions();
}

//-----------------------------------------------------------------------------

bool CreateGuiControls(const HWND parentWindow)
{
  const LONG buttonHeight = 20;
  const POINT pt = {0, 0};
  const SIZE size = {0, buttonHeight};

  GetUrlButton.reset(new Gui::Button(parentWindow, MakeRECT(MakePOINT(5, 5), size), GetString(IDS_GET_URL_BUTTON).c_str(), 1000));
  GetUrlButton->SetEnabled(false);

  OpenUrlButton.reset(new Gui::Button(parentWindow,
                                      MakeRECT(MakePOINT(5 + GetUrlButton->GetSize().cx + 2, 5), size),
                                      GetString(IDS_OPEN_URL_BUTTON).c_str(),
                                      1001));
  OpenUrlButton->SetEnabled(Flash::IsAccountExists());

  LeftPlayButton.reset(new Gui::Button(parentWindow, MakeRECT(pt, size), GetString(IDS_LEFT_CLICKER_BUTTON).c_str(), 1003, true));
  LeftPlayButton->SetEnabled(false);
  RightPlayButton.reset(new Gui::Button(parentWindow, MakeRECT(pt, size), GetString(IDS_RIGHT_CLICKER_BUTTON).c_str(), 1004, true));
  RightPlayButton->SetEnabled(false);
  FullScreenButton.reset(new Gui::Button(parentWindow, MakeRECT(pt, MakeSIZE(20, buttonHeight)), TEXT("+"), 1005, true));

  CreateServerButtons();

  AboutWindow.reset(new Gui::AboutWindow(parentWindow));

  UpdateWidgetPositions();

  return true;
}

//-----------------------------------------------------------------------------

void ShowAboutBattleStarted()
{
  if (::GetForegroundWindow() != MainWindow)
  {
    ::PlaySound(MAKEINTRESOURCE(IDR_BEEPWAVE), NULL, SND_RESOURCE | SND_ASYNC | SND_LOOP);
    ::MessageBox(MainWindow, GetString(IDS_BATTLE_IS_LOADED).c_str(), ProgramTitle, MB_OK | MB_ICONINFORMATION | MB_SYSTEMMODAL | MB_TOPMOST);
    ::PlaySound(NULL, NULL, 0);
  }
}

//-----------------------------------------------------------------------------

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  switch (message)
  {
  case WM_COMMAND:
    {
      OnButtonClicked(reinterpret_cast<Gui::Button*>(lParam));
      return 0;
    }
  case WM_GETMINMAXINFO:
    {
      MINMAXINFO*const info = (MINMAXINFO*)(lParam);
      info->ptMinTrackSize = GetMinSizeOfMainWindow();
      return 0;
    }
  case WM_SIZE:
    {
      UpdateWidgetPositions();
      //return 0;
    }
  case WM_MOVE:
    {
      if (GetSettings()->WindowState == 0)
      {
        GetSettings()->SaveCurrentWindowRect(MainWindow);
      }
      return 0;
    }
  case WM_SYSCOMMAND:
    {
      switch (wParam & 0xFFF0)  // in low 4 bits receives extended information
      {
      case SC_MAXIMIZE:
        GetSettings()->WindowState |= 0x1;
        break;
      case SC_RESTORE:
        GetSettings()->WindowState &= ~0x1;
        break;
      }
      return ::DefWindowProc(hwnd, message, wParam, lParam);
    }
  case WM_CHANGECBCHAIN:
    {
      return Clipboard->Do_WM_CHANGECBCHAIN(hwnd, message, wParam, lParam);
    }
  case WM_DRAWCLIPBOARD:
    {
      const tstring myUrl(Clipboard->Read());
      const tstring publicUrl(BattleUrl->ToPublicUrl(myUrl));
      if (myUrl != publicUrl)
      {
        Clipboard->Write(publicUrl);
      }
      return Clipboard->Do_WM_DRAWCLIPBOARD(hwnd, message, wParam, lParam);
    }
  case Thread::WM_THREADFINISH:
    {
      if (reinterpret_cast<ClickerThread *>(wParam) == Clicker.get())
      {
        LeftPlayButton->SetChecked(false);
        RightPlayButton->SetChecked(false);
        LeftClickerWindow.reset();
        RightClickerWindow.reset();
        Clicker.reset();
        if (BattleIsStarted)
        {
          BattleIsStarted = false;
          ShowAboutBattleStarted();
        }
      }
      else
      {
        Screenshot.reset();
      }
      return 0;
    }
  case BrowserEvent::WM_BROWSEREVENT:
    {
      std::auto_ptr<tstring> url((tstring*)(lParam));
      if (DISPID_NAVIGATEERROR == wParam
          || url->compare(TEXT("http://") + GetSettings()->Account + TEXT(".tankionline.com/")) == 0) // FIXME: Wtf?
      {
        LoadUrl();
      }
      return 0;
    }
  case WM_CREATE:
    {
      MainWindow = hwnd;
      if (!CreateGuiControls(MainWindow))
      {
        return -1;
      }
      Clipboard.reset(new ClipboardManager(MainWindow));
      Clipboard->HookChanges();
      return 0;
    }
  case WM_DESTROY:
    {
      GetSettings()->Save(); // For save window position
      Clipboard.reset();
      ::PostQuitMessage(0);
      return 0;
    }
  }
  return ::DefWindowProc(hwnd, message, wParam, lParam);
}

//-----------------------------------------------------------------------------
