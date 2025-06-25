#pragma once

#include <deque>
#include <unordered_map>
#include <string>
#include <expected>
#include <optional>

namespace VCLG {

    struct RenamingScope {
        std::unordered_map<std::string, std::string> names;
    };

    class RenamingScopeManager {
    public:
        RenamingScopeManager();
        ~RenamingScopeManager();

        void PushScope();

        void PopScope();

        std::string GetNewName(const std::string& name) const;
        bool PushNewName(const std::string& name, const std::string& newName);

        bool IsCurrentScopeGlobal() const;

    private:
        /**
         * @brief The scope stack. It can't contain less than one scope wich represent the global scope.
         */
        std::deque<RenamingScope> scopes;
    };
}