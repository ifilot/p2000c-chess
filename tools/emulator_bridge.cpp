// SPDX-License-Identifier: GPL-3.0-only
// Persistent emulator for the ZMATCH RAM mailbox. Disks are copied by Python.
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include "core/p2000c_machine.h"

int main(int argc, char **argv) {
  try {
    if (argc != 5) throw std::runtime_error("expected IPL HD0 HD1 ZMATCH.map");
    std::map<std::string, unsigned> symbols;
    std::ifstream mapfile(argv[4]);
    std::regex pattern("^(_[A-Za-z0-9_]+) +[=] [$]([0-9A-F]+).*");
    std::string line;
    while (std::getline(mapfile, line)) {
      std::smatch found;
      if (std::regex_match(line, found, pattern))
        symbols[found[1]] = std::stoul(found[2], nullptr, 16);
    }
    auto address = [&](const char *name) { return symbols.at(name); };
    p2000c::P2000cMachine machine;
    machine.set_storage_delays_enabled(false);
    std::string error;
    if (!machine.load_ipl_rom(argv[1], &error) ||
        !machine.mount_hard_disk(0, argv[2], &error) ||
        !machine.mount_hard_disk(1, argv[3], &error)) throw std::runtime_error(error);
    auto wait_text = [&](const std::string &text) {
      auto deadline = machine.cycles() + 200000000ULL;
      while (machine.cycles() < deadline) {
        const auto &screen = machine.terminal().screen();
        std::string flat(screen.begin(), screen.end());
        if (flat.find(text) != std::string::npos) return;
        machine.run_for(10000);
      }
      throw std::runtime_error("boot timeout waiting for " + text);
    };
    wait_text("A>");
    for (unsigned char ch : std::string("F:ZMATCH\r")) machine.queue_key(ch);
    wait_text("ENGINE READY");
    auto byte = [&](const char *name) { return machine.read_memory(address(name)); };
    auto word = [&](const char *name) {
      auto a = address(name);
      return machine.read_memory(a) | (machine.read_memory(a + 1) << 8);
    };
    auto write_word = [&](const char *name, unsigned value) {
      machine.write_memory(address(name), value & 255);
      machine.write_memory(address(name) + 1, (value >> 8) & 255);
    };
    while (std::getline(std::cin, line)) {
      std::istringstream input(line);
      unsigned command, level, seed, from, to, flags, key;
      if (!(input >> command)) throw std::runtime_error("bad command");
      if (command == 1) {
        if (!(input >> level >> seed)) throw std::runtime_error("bad init");
        machine.write_memory(address("_cpu_level"), level);
        write_word("_cpu_seed", seed);
      } else if (command == 2 && (input >> level >> seed)) {
        machine.write_memory(address("_cpu_level"), level);
        write_word("_cpu_seed", seed);
      } else if (command == 3) {
        if (!(input >> from >> to >> flags >> key)) throw std::runtime_error("bad move");
        auto a = address("_bridge_move");
        machine.write_memory(a, from); machine.write_memory(a + 1, to);
        machine.write_memory(a + 2, flags); machine.write_memory(a + 3, key);
      }
      auto started = machine.cycles();
      machine.write_memory(address("_bridge_cmd"), command);
      while (byte("_bridge_cmd")) {
        if (machine.cycles() - started > 100000000ULL)
          throw std::runtime_error("engine command exceeded 25 emulated seconds");
        machine.run_for(2000);
      }
      std::string fen;
      for (unsigned i = 0; i < 100; ++i) {
        auto ch = machine.read_memory(address("_bridge_fen") + i);
        if (!ch) break;
        fen += static_cast<char>(ch);
      }
      std::cout << "{\"ok\":" << static_cast<unsigned>(byte("_bridge_ok"))
                << ",\"clock_available\":" << static_cast<unsigned>(byte("_clock_available"))
                << ",\"cpu_seed\":" << word("_cpu_seed")
                << ",\"fen\":\"" << fen << "\",\"nodes\":" << word("_cpu_nodes")
                << ",\"depth\":" << static_cast<unsigned>(byte("_cpu_depth"))
                << ",\"emulated_seconds\":" << std::setprecision(8)
                << (machine.cycles() - started) / 4000000.0 << ",\"move\":[";
      for (unsigned i = 0; i < 4; ++i)
        std::cout << (i ? "," : "") << static_cast<unsigned>(machine.read_memory(address("_bridge_move") + i));
      std::cout << "],\"legal\":[";
      if (command == 4) {
        auto count = word("_bridge_count");
        if (count > 220) throw std::runtime_error("too many root moves");
        for (unsigned i = 0; i < count; ++i) {
          std::cout << (i ? ",[" : "[");
          for (unsigned j = 0; j < 4; ++j)
            std::cout << (j ? "," : "") << static_cast<unsigned>(machine.read_memory(address("_moves") + i * 4 + j));
          std::cout << "]";
        }
      }
      std::cout << "]}" << std::endl;
    }
  } catch (const std::exception &exception) {
    std::cerr << exception.what() << std::endl;
    return 1;
  }
}
