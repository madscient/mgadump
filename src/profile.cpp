#include "profile.hpp"

#include "nlohmann/json.hpp"
#include <fstream>
#include <sstream>
#include <cstdio>
#include <iomanip>

using json = nlohmann::json;

// hex文字列 or 数値 → uint32_t
static uint32_t parseUint32(const json& j, const std::string& fieldName) {
    if (j.is_string()) {
        try { return static_cast<uint32_t>(std::stoul(j.get<std::string>(), nullptr, 0)); }
        catch (...) { throw std::runtime_error("Invalid value for '" + fieldName + "': " + j.get<std::string>()); }
    }
    if (j.is_number_unsigned()) return j.get<uint32_t>();
    if (j.is_number_integer())  return static_cast<uint32_t>(j.get<int64_t>());
    throw std::runtime_error("Field '" + fieldName + "' must be a string or integer");
}

static const json& require(const json& obj, const std::string& key, const std::string& ctx) {
    if (!obj.contains(key))
        throw std::runtime_error("Missing required field '" + key + "' in " + ctx);
    return obj[key];
}

static std::string hex32(uint32_t v) {
    std::ostringstream o; o << "0x" << std::hex << v; return o.str();
}

std::vector<CartProfile> loadProfiles(const std::string& jsonPath) {
    std::ifstream ifs(jsonPath);
    if (!ifs.is_open())
        throw std::runtime_error(
            "Cannot open profile file: " + jsonPath +
            "\n  Hint: place mga_profiles.json in the current directory"
            " or specify the path with --profiles");

    json root;
    try { ifs >> root; }
    catch (const json::parse_error& e) {
        throw std::runtime_error("JSON parse error in " + jsonPath + ":\n  " + e.what());
    }

    if (!root.contains("profiles") || !root["profiles"].is_array())
        throw std::runtime_error(jsonPath + ": top-level key \"profiles\" (array) is required");

    std::vector<CartProfile> result;

    for (size_t pi = 0; pi < root["profiles"].size(); ++pi) {
        const json& jp  = root["profiles"][pi];
        const std::string ctx = "profiles[" + std::to_string(pi) + "]";

        CartProfile profile;

        const json& jname = require(jp, "name", ctx);
        if (!jname.is_string() || jname.get<std::string>().empty())
            throw std::runtime_error(ctx + ".name must be a non-empty string");
        profile.name = jname.get<std::string>();

        const std::string pctx = "profile \"" + profile.name + "\"";

        if (jp.contains("description") && jp["description"].is_string())
            profile.description = jp["description"].get<std::string>();

        profile.totalSize = parseUint32(require(jp, "total_size", pctx), "total_size");
        if (profile.totalSize == 0)
            throw std::runtime_error(pctx + ": total_size must be > 0");

        const json& jbanks = require(jp, "banks", pctx);
        if (!jbanks.is_array() || jbanks.empty())
            throw std::runtime_error(pctx + ": \"banks\" must be a non-empty array");

        for (size_t bi = 0; bi < jbanks.size(); ++bi) {
            const json& jb   = jbanks[bi];
            const std::string bctx = pctx + " banks[" + std::to_string(bi) + "]";

            BankRegion bank;

            if (jb.contains("switches")) {
                const json& jsws = jb["switches"];
                if (!jsws.is_array())
                    throw std::runtime_error(bctx + ".switches must be an array");
                for (size_t si = 0; si < jsws.size(); ++si) {
                    const json& jsw = jsws[si];
                    const std::string sctx = bctx + " switches[" + std::to_string(si) + "]";
                    BankSwitch sw;
                    sw.regAddr = static_cast<uint16_t>(parseUint32(require(jsw, "reg_addr", sctx), "reg_addr"));
                    uint32_t val = parseUint32(require(jsw, "value", sctx), "value");
                    if (val > 0xFF) throw std::runtime_error(sctx + ".value must be 0x00-0xFF");
                    sw.value = static_cast<uint8_t>(val);
                    bank.switches.push_back(sw);
                }
            }

            uint32_t sa = parseUint32(require(jb, "slot_addr", bctx), "slot_addr");
            if (sa > 0xFFFF) throw std::runtime_error(bctx + ".slot_addr must be 0x0000-0xFFFF");
            bank.slotAddr = static_cast<uint16_t>(sa);

            uint32_t len = parseUint32(require(jb, "length", bctx), "length");
            if (len == 0 || len > 0x10000) throw std::runtime_error(bctx + ".length must be 1-0x10000");
            bank.length = static_cast<uint16_t>(len);

            bank.outputOffset = parseUint32(require(jb, "output_offset", bctx), "output_offset");

            if (bank.outputOffset + bank.length > profile.totalSize)
                throw std::runtime_error(
                    bctx + ": output_offset(" + hex32(bank.outputOffset) +
                    ") + length(" + hex32(bank.length) +
                    ") exceeds total_size(" + hex32(profile.totalSize) + ")");

            profile.banks.push_back(std::move(bank));
        }

        result.push_back(std::move(profile));
    }

    return result;
}

const CartProfile& findProfile(const std::vector<CartProfile>& profiles,
                               const std::string& name) {
    for (auto& p : profiles)
        if (p.name == name) return p;

    std::ostringstream oss;
    oss << "Profile not found: \"" << name << "\"\n"
        << "Available profiles:\n";
    for (auto& p : profiles)
        oss << "  " << p.name << "\n";
    oss << "Use --list-profiles to see details.";
    throw std::runtime_error(oss.str());
}

void printProfiles(const std::vector<CartProfile>& profiles) {
    printf("Available profiles:\n\n");
    printf("  %-20s  %-8s  %s\n", "NAME", "SIZE", "DESCRIPTION");
    printf("  %-20s  %-8s  %s\n", "--------------------", "--------",
           "-----------------------------------------------");
    for (auto& p : profiles)
        printf("  %-20s  %4u KB  %s\n",
               p.name.c_str(), p.totalSize / 1024, p.description.c_str());
    printf("\n");
}
