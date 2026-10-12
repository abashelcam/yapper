#include <exception>
#include <iostream>

#include "app.hpp"

int main()
{
  // Flush every line, so `docker logs` shows output right away
  std::cout << std::unitbuf;

  try
  {
    yapper::ServerConfig config = yapper::LoadServerConfig("server.json");
    yapper::Server       server(config);

    std::cout << config.name << " listening on port " << config.port << " (TCP and UDP)\n";
    server.Run();
  }
  catch (const std::exception &e)
  {
    std::cerr << "fatal: " << e.what() << "\n";
    return 1;
  }

  return 0;
}
