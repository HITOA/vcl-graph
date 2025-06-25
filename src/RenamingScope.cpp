#include "RenamingScope.hpp"


VCLG::RenamingScopeManager::RenamingScopeManager() {
    PushScope();
}

VCLG::RenamingScopeManager::~RenamingScopeManager() {
    PopScope();
}

void VCLG::RenamingScopeManager::PushScope() {
    scopes.emplace_front();
}

void VCLG::RenamingScopeManager::PopScope() {
    scopes.pop_front();
}

std::string VCLG::RenamingScopeManager::GetNewName(const std::string& name) const {
    for (const RenamingScope& scope : scopes) {
        if (scope.names.count(name))
            return scope.names.at(name);
    }
    return "";
}

bool VCLG::RenamingScopeManager::PushNewName(const std::string& name, const std::string& newName) {
    scopes[0].names.emplace(name, newName);
    return true;
}

bool VCLG::RenamingScopeManager::IsCurrentScopeGlobal() const {
    return scopes.size() == 1;
}
