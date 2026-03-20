#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class FrontendIdentifierInterner {
   public:
    size_t intern(std::string_view text) {
        std::string owned(text);
        auto existing = m_ids.find(owned);
        if (existing != m_ids.end()) {
            return existing->second;
        }

        const size_t id = m_values.size();
        m_values.push_back(std::move(owned));
        m_ids.emplace(m_values.back(), id);
        return id;
    }

    const std::string& value(size_t id) const { return m_values.at(id); }
    size_t size() const { return m_values.size(); }

   private:
    std::unordered_map<std::string, size_t> m_ids;
    std::vector<std::string> m_values;
};
