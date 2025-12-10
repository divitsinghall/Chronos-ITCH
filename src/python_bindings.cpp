/**
 * @file python_bindings.cpp
 * @brief pybind11 Python bindings for ITCH parser.
 *
 * Provides a Python module 'itch_handler' that parses PCAP files
 * and returns data as NumPy arrays for easy Pandas integration.
 *
 * DESIGN:
 * - PythonAccumulator collects data in C++ vectors (no Python callbacks)
 * - parse_file() returns a dict of NumPy arrays (zero-copy where possible)
 * - Reuses offset detection logic from main.cpp for PCAP header handling
 */

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <cstdint>
#include <string>
#include <vector>

#include <itch/messages.hpp>
#include <itch/parser.hpp>
#include <itch/pcap_reader.hpp>

namespace py = pybind11;

namespace {

// ============================================================================
// ITCH Message Type Validation (reused from main.cpp)
// ============================================================================

inline bool is_valid_itch_type(char c) {
  // Order messages
  if (c == 'A' || c == 'F' || c == 'E' || c == 'C' || c == 'X' || c == 'D' ||
      c == 'U')
    return true;
  // Trade messages
  if (c == 'P' || c == 'Q' || c == 'B')
    return true;
  // System/stock messages
  if (c == 'S' || c == 'R' || c == 'H' || c == 'Y' || c == 'L')
    return true;
  // Net order imbalance
  if (c == 'I' || c == 'N')
    return true;
  // MWCB and IPO
  if (c == 'V' || c == 'W' || c == 'K')
    return true;
  return false;
}

// ============================================================================
// PCAP Offset Detection (reused from main.cpp)
// ============================================================================

inline size_t find_itch_offset(const char *data, size_t len) {
  // Common header configurations
  constexpr size_t OFFSETS[] = {
      42, // Standard UDP
      46, // With VLAN tag
      62, // Standard + MoldUDP header
      64, // Standard + MoldUDP + length prefix
      66, // VLAN + MoldUDP header
      68, // VLAN + MoldUDP + length prefix
  };

  // Try each known offset
  for (size_t offset : OFFSETS) {
    if (offset < len) {
      char msg_type = data[offset];
      if (is_valid_itch_type(msg_type)) {
        if (len >= offset + 3) {
          uint16_t stock_locate = static_cast<uint16_t>(
              (static_cast<uint8_t>(data[offset + 1]) << 8) |
              static_cast<uint8_t>(data[offset + 2]));
          if (stock_locate > 0 && stock_locate < 10000) {
            return offset;
          }
        }
        return offset;
      }
    }
  }

  // Fallback: Search first 100 bytes
  constexpr size_t SEARCH_LIMIT = 100;
  size_t search_end = (len < SEARCH_LIMIT) ? len : SEARCH_LIMIT;

  for (size_t offset = 0; offset < search_end; ++offset) {
    char msg_type = data[offset];
    if (is_valid_itch_type(msg_type)) {
      if (len >= offset + 3) {
        uint16_t stock_locate = static_cast<uint16_t>(
            (static_cast<uint8_t>(data[offset + 1]) << 8) |
            static_cast<uint8_t>(data[offset + 2]));
        if (stock_locate > 0 && stock_locate < 10000) {
          return offset;
        }
      }
    }
  }

  return 42; // Last resort
}

// ============================================================================
// Python Accumulator - Collects data in C++ vectors
// ============================================================================

/**
 * @brief Accumulates parsed ITCH messages into vectors.
 *
 * This is much faster than calling back into Python for each message.
 * After parsing, vectors are converted to NumPy arrays.
 */
class PythonAccumulator : public itch::DefaultVisitor {
public:
  // AddOrder data
  std::vector<uint64_t> add_order_refs;
  std::vector<uint64_t> add_timestamps;
  std::vector<uint16_t> add_stock_locates;
  std::vector<uint32_t> add_shares;
  std::vector<uint32_t> add_prices;
  std::vector<char> add_sides;

  // OrderExecuted data
  std::vector<uint64_t> exec_order_refs;
  std::vector<uint64_t> exec_timestamps;
  std::vector<uint16_t> exec_stock_locates;
  std::vector<uint32_t> exec_shares;
  std::vector<uint64_t> exec_match_numbers;

  void on_add_order(const itch::AddOrder &msg) {
    add_order_refs.push_back(static_cast<uint64_t>(msg.order_ref));
    add_timestamps.push_back(msg.timestamp.nanoseconds());
    add_stock_locates.push_back(static_cast<uint16_t>(msg.stock_locate));
    add_shares.push_back(static_cast<uint32_t>(msg.shares));
    add_prices.push_back(static_cast<uint32_t>(msg.price));
    add_sides.push_back(msg.side);
  }

  void on_order_executed(const itch::OrderExecuted &msg) {
    exec_order_refs.push_back(static_cast<uint64_t>(msg.order_ref));
    exec_timestamps.push_back(msg.timestamp.nanoseconds());
    exec_stock_locates.push_back(static_cast<uint16_t>(msg.stock_locate));
    exec_shares.push_back(static_cast<uint32_t>(msg.executed_shares));
    exec_match_numbers.push_back(static_cast<uint64_t>(msg.match_number));
  }

  /**
   * @brief Convert accumulated AddOrder data to Python dict of NumPy arrays.
   */
  py::dict get_add_orders() const {
    py::dict result;

    // Convert vectors to NumPy arrays
    result["order_ref"] =
        py::array_t<uint64_t>(add_order_refs.size(), add_order_refs.data());
    result["timestamp"] =
        py::array_t<uint64_t>(add_timestamps.size(), add_timestamps.data());
    result["stock_locate"] = py::array_t<uint16_t>(add_stock_locates.size(),
                                                   add_stock_locates.data());
    result["shares"] =
        py::array_t<uint32_t>(add_shares.size(), add_shares.data());
    result["price"] =
        py::array_t<uint32_t>(add_prices.size(), add_prices.data());

    // Side needs special handling (char array)
    py::array_t<char> sides(add_sides.size());
    auto sides_buf = sides.mutable_unchecked<1>();
    for (size_t i = 0; i < add_sides.size(); ++i) {
      sides_buf(i) = add_sides[i];
    }
    result["side"] = sides;

    return result;
  }

  /**
   * @brief Convert accumulated OrderExecuted data to Python dict of NumPy
   * arrays.
   */
  py::dict get_order_executed() const {
    py::dict result;

    result["order_ref"] =
        py::array_t<uint64_t>(exec_order_refs.size(), exec_order_refs.data());
    result["timestamp"] =
        py::array_t<uint64_t>(exec_timestamps.size(), exec_timestamps.data());
    result["stock_locate"] = py::array_t<uint16_t>(exec_stock_locates.size(),
                                                   exec_stock_locates.data());
    result["executed_shares"] =
        py::array_t<uint32_t>(exec_shares.size(), exec_shares.data());
    result["match_number"] = py::array_t<uint64_t>(exec_match_numbers.size(),
                                                   exec_match_numbers.data());

    return result;
  }
};

// ============================================================================
// Main Parse Function
// ============================================================================

/**
 * @brief Parse a PCAP file and return ITCH data as NumPy arrays.
 *
 * @param filename Path to PCAP file
 * @return dict with 'add_orders' and 'order_executed' sub-dicts,
 *         each containing NumPy arrays for each field.
 */
py::dict parse_file(const std::string &filename) {
  itch::PcapReader reader(filename.c_str());

  if (!reader.is_open()) {
    throw std::runtime_error("Failed to open PCAP file: " + filename);
  }

  itch::Parser parser;
  PythonAccumulator accumulator;

  // Process all packets
  size_t packet_count =
      reader.for_each_packet([&](const char *data, size_t len) {
        size_t offset = find_itch_offset(data, len);
        if (offset < len) {
          const char *itch_data = data + offset;
          size_t itch_len = len - offset;
          (void)parser.parse_buffer(itch_data, itch_len, accumulator);
        }
      });

  // Build result dictionary
  py::dict result;
  result["add_orders"] = accumulator.get_add_orders();
  result["order_executed"] = accumulator.get_order_executed();
  result["packet_count"] = packet_count;
  result["file_size"] = reader.file_size();

  return result;
}

/**
 * @brief Get version information.
 */
std::string version() { return "1.0.0"; }

} // anonymous namespace

// ============================================================================
// Python Module Definition
// ============================================================================

PYBIND11_MODULE(itch_handler, m) {
  m.doc() = R"pbdoc(
        ITCH 5.0 Parser Python Bindings
        --------------------------------

        High-performance parser for NASDAQ TotalView-ITCH 5.0 protocol.
        Parses PCAP files and returns data as NumPy arrays.

        Example:
            import itch_handler
            data = itch_handler.parse_file("market_data.pcap")
            add_orders = data['add_orders']
            print(f"Parsed {len(add_orders['order_ref'])} add orders")
    )pbdoc";

  m.def("parse_file", &parse_file, py::arg("filename"),
        R"pbdoc(
            Parse a PCAP file containing ITCH 5.0 messages.

            Args:
                filename: Path to the PCAP file.

            Returns:
                dict with keys:
                    - 'add_orders': dict of NumPy arrays (order_ref, timestamp, 
                                    stock_locate, shares, price, side)
                    - 'order_executed': dict of NumPy arrays (order_ref, timestamp,
                                        stock_locate, executed_shares, match_number)
                    - 'packet_count': Number of packets processed
                    - 'file_size': Size of PCAP file in bytes
        )pbdoc");

  m.def("version", &version, "Get library version string");

  m.attr("__version__") = "1.0.0";
}
