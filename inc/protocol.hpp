#pragma once

#include <vector>
#include <exception>
#include <streambuf>
#include <deque>
#include <map>

#include <cstring>

#include <iomanip>
#include <crc.hpp>

namespace robomaster
{

constexpr static uint8_t PKG_START_BYTE = 0x55;
constexpr static uint8_t CRC_HEADER_INIT = 119;
constexpr static uint16_t CRC_PACKAGE_INIT = 13970;

class id_tracker
{
public:
    id_tracker() {}

    unsigned int get_count_for_id(uint16_t id)
    {
        const auto it = _seq_id_counts.find(id);

        if (it == _seq_id_counts.end())
        {
            _seq_id_counts[id] = 0;
            return 0;
        }
        else
        {
            const auto current_seq_id = it->second;
            it->second += 1;
            return current_seq_id;
        }
    }

    static id_tracker& get_instance()
    {
        static id_tracker instance;
        return instance;
    }

private:
    std::map<uint16_t, unsigned int> _seq_id_counts;

};

class package
{
public:
    package()
        : is_ack{false}
        , need_ack{false}
        , seq_id{0}
        , sender{0}
        , receiver{0}
        , cmd_set{0}
        , cmd_id{0}
        , data{}
    {}

    package(uint8_t sender, uint8_t receiver, uint8_t cmd_set, uint8_t cmd_id, bool is_ack, bool need_ack)
        : is_ack{is_ack}
        , need_ack{need_ack}
        , seq_id{0}
        , sender{sender}
        , receiver{receiver}
        , cmd_set{cmd_set}
        , cmd_id{cmd_id}
    {}

    package(uint8_t sender, uint8_t receiver, uint8_t cmd_set, uint8_t cmd_id, bool is_ack, bool need_ack, const std::deque<uint8_t>& data)
        : is_ack{is_ack}
        , need_ack{need_ack}
        , seq_id{0}
        , sender{sender}
        , receiver{receiver}
        , cmd_set{cmd_set}
        , cmd_id{cmd_id}
        , data{data}
    {}

    template <typename T>
    void put(const T var)
    {
        auto bytes = reinterpret_cast<const uint8_t*>(&var);
        data.insert(data.end(), bytes, bytes + sizeof(T));
    }

    void discard(size_t n)
    {
        data.erase(data.begin(), data.begin() + n);
    }

    template <typename T>
    void get(T& var)
    {
        std::vector<uint8_t> d{data.begin(), data.begin() + sizeof(T)};
        var = *reinterpret_cast<T*>(d.data());
        discard(sizeof(T));
    }

    void write_to(std::ostream& out)
    {
        const auto pkg_size = data.size() + 13;
        std::vector<uint8_t> serial_data{};
        serial_data.reserve(pkg_size);

        serial_data.resize(3);
        serial_data[0] = PKG_START_BYTE;
        serial_data[1] = static_cast<uint8_t>(pkg_size);
        serial_data[2] = (static_cast<uint8_t>(pkg_size >> 8) & 0x3) | 0x4;
        const auto crc_header = crc::crc(CRC_HEADER_INIT, serial_data);
        serial_data.resize(11);
        serial_data[3] = crc_header;
        serial_data[4] = sender;
        serial_data[5] = receiver;
        const auto cmd = (static_cast<uint16_t>(cmd_set) << 8) | static_cast<uint16_t>(cmd_id);
        const auto seq_id = id_tracker::get_instance().get_count_for_id(cmd);
        serial_data[6] = static_cast<uint8_t>(seq_id);
        serial_data[7] = static_cast<uint8_t>(seq_id >> 8);
        serial_data[8] = (is_ack << 7) | (need_ack << 5);
        serial_data[9] = cmd_set;
        serial_data[10] = cmd_id;
        serial_data.insert(serial_data.end(), data.begin(), data.end());

        const auto crc_package = crc::crc(CRC_PACKAGE_INIT, serial_data);
        serial_data.resize(pkg_size);
        serial_data[pkg_size - 2] = static_cast<uint8_t>(crc_package);
        serial_data[pkg_size - 1] = static_cast<uint8_t>(crc_package >> 8);

        out.write(reinterpret_cast<char*>(serial_data.data()), serial_data.size());
    }

    void read_from(std::istream& in)
    {
        while (true)
        {
            while (in.get() != PKG_START_BYTE);

            uint8_t length_lsb = in.get();
            uint8_t length_msb = in.get();

            if (!(length_msb & 0x04))
            {
                continue;
            }

            uint8_t crc_header_parsed = in.get();
            std::vector<uint8_t> parsed_data{};
            parsed_data.resize(3);
            parsed_data[0] = PKG_START_BYTE;
            parsed_data[1] = length_lsb;
            parsed_data[2] = length_msb;

            if (crc_header_parsed != crc::crc(CRC_HEADER_INIT, parsed_data))
            {
                continue;
            }

            const auto length_parsed = length_lsb | ((length_msb & 0x03) << 8);
            parsed_data.resize(length_parsed - 2); // don't consider checksum
            parsed_data[3] = crc_header_parsed;

            in.read(reinterpret_cast<char*>(&parsed_data[4]), length_parsed - 4 - 2); // don't parse header and checksum

            uint16_t crc_package_parsed;
            in.read(reinterpret_cast<char*>(&crc_package_parsed), 2);

            // Finished parsing, verify package checksum
            if (crc_package_parsed != crc::crc(CRC_PACKAGE_INIT, parsed_data))
            {
                continue;
            }

            // now move into package data
            sender = parsed_data[4];
            receiver = parsed_data[5];
            seq_id = *(reinterpret_cast<uint16_t*>(&parsed_data[6]));
            is_ack = parsed_data[8] & (1 << 7);
            need_ack = parsed_data[8] & (1 << 5);
            cmd_set = parsed_data[9];
            cmd_id = parsed_data[10];
            data.assign(parsed_data.begin() + 11, parsed_data.end());

            return;
        }
    }

    bool is_ack;
    bool need_ack;
    uint16_t seq_id;
    uint8_t sender;
    uint8_t receiver;
    uint8_t cmd_set;
    uint8_t cmd_id;

    std::deque<uint8_t> data;
};

template <typename T>
package& operator<<(package& pkg, const T& var)
{
    pkg.put(var);
    return pkg;
}

template <typename T>
package& operator>>(package& pkg, T& var)
{
    pkg.get(var);
    return pkg;
}

std::ostream& operator<<(std::ostream& out, package& p)
{
    out << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(p.sender) << ">" << std::hex << std::setw(2) << static_cast<int>(p.receiver);
    out << " [" << std::hex << std::setfill('0') << std::setw(4) << static_cast<int>((p.cmd_set << 8) | p.cmd_id) << "]";
    out << (p.is_ack ? "+" : " ");
    out << (p.need_ack ? "!" : " ");
    out << " l=" << std::dec << std::setfill(' ') << std::setw(4) << static_cast<int>(p.data.size());

    if (!p.data.empty())
    {
        out << " d=";

        for (const auto& val : p.data)
        {
            std::cout << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(val) << " ";
        }
    }

    return out;
}

}

