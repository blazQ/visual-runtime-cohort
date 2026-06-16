#include "visual_runtime_module.h"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <thread>

static volatile bool running = true;
static void on_signal(int) { running = false; }

int main() {
  std::signal(SIGINT, on_signal);
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  VisualRuntimeModule runtime =
      VisualRuntimeModule::open(VISUAL_RUNTIME_LIB_PATH);
  if (!runtime)
    return 1;

  using clock = std::chrono::steady_clock;
  auto last = clock::now();

  while (running) {
    if (runtime.reloadIfChanged())
      std::printf("[host] reloaded\n");

    auto now = clock::now();
    float dt = std::chrono::duration<float>(now - last).count();
    last = now;

    runtime.tick(dt);

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }

  std::printf("\n[host] exiting\n");
}
