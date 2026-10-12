// app.hpp first: asio needs winsock2.h before windows.h
#include "app.hpp"

#include <Windows.h>

#include <cstdlib>
#include <exception>
#include <stdexcept>
#include <string>

// No arguments opens the GUI. For testing: yapper_client.exe host port username [password]
static int Run()
{
  if (__argc >= 4)
  {
    const int port = std::atoi(__argv[2]);
    if (port < 1 || port > 65535)
    {
      throw std::invalid_argument("usage: yapper_client.exe host port username [password]");
    }

    yapper::Client client(false, __argv[1], static_cast<std::uint16_t>(port), __argv[3], __argc >= 5 ? __argv[4] : "");
    return client.Run();
  }

  yapper::Client client(true);
  return client.Run();
}

static int RunWithHandling()
{
  try
  {
    return Run();
  }
  catch (const std::exception &e)
  {
    MessageBoxA(nullptr, e.what(), "Yapper", MB_OK | MB_ICONERROR);
  }
  catch (...)
  {
    MessageBoxA(nullptr, "Unknown error occurred", "Yapper", MB_OK | MB_ICONERROR);
  }

  return EXIT_FAILURE;
}

int WINAPI WinMain(HINSTANCE,
                   HINSTANCE,
                   PSTR,
                   int)
{
  return RunWithHandling();
}
